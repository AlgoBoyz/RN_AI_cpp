/**
 * Stm_g302 — STM32F411CEU6 + ST HAL + USB Device Library
 *
 * Logitech G302 Daedalus Apex clone (VID 0x046D / PID 0xC07F). Two-interface
 * HID: IF0 Boot Mouse (8-byte report, no Report ID), IF1 Multimedia + HID++.
 *
 * Vendor input channel: HID++ packets arrive on EP0 SET_REPORT — see
 * USBD_G302_HidppReceive() below. Mouse motion injection comes from a
 * 10-byte UART frame at 115200 baud on USART1:
 *
 *   [SOF=0xAA] [OP=0x01] [dx_lo dx_hi] [dy_lo dy_hi] [buttons] [wheel] [hwheel] [XOR]
 *
 * The parsed (dx, dy, buttons, wheel, hwheel) is injected into the IF0
 * 8-byte mouse report (pan = hwheel).
 *
 * Boot chain: Reset_Handler -> SystemInit -> main
 *   HAL_Init -> SystemClock_Config (HSE 25MHz -> 96MHz, USB 48MHz)
 *   -> MX_GPIO_Init -> MX_USB_DEVICE_Init -> idle in WFI
 */

#include "main.h"
#include "usb_device.h"
#include "usbd_g302.h"
#include "hidpp_handler.h"
#include "hidpp_rgb.h"
#include "hidpp_timing.h"
#include <stdbool.h>
#include <string.h>

extern USBD_HandleTypeDef hUsbDeviceFS;

/* ── UART status byte bit definitions ────────────────────────────── */
#define STATUS_UART_FRAME_OK   (1U << 0)  /* XOR checksum valid */
#define STATUS_BUF_PUSHED      (1U << 1)  /* frame pushed to ring buffer */
#define STATUS_BUF_DROPPED     (1U << 2)  /* ring buffer full, frame dropped */
#define STATUS_USB_SEND_OK     (1U << 3)  /* USB HID report sent */
#define STATUS_USB_SEND_BUSY   (1U << 4)  /* USB EP busy, will retry */
#define STATUS_USB_SEND_FAIL   (1U << 5)  /* USB send failed */
#define STATUS_XOR_ERROR       (1U << 6)  /* XOR mismatch on received frame */
#define STATUS_UART_OVERRUN    (1U << 7)  /* UART error occurred */

/* IF0 mouse report — 8 bytes, NO Report ID:
 *   { btn_lo, btn_hi, x_lo, x_hi, y_lo, y_hi, wheel_i8, pan_i8 } */
typedef struct __attribute__((packed)) {
    uint16_t button;
    int16_t  x;
    int16_t  y;
    int8_t   wheel;
    int8_t   pan;
} g302_mouse_report_t;

_Static_assert(sizeof(g302_mouse_report_t) == 8, "G302 mouse report must be 8 bytes");

/* ── Diagnostic snapshot at fixed SRAM 0x20004000 (read via mdw) ────────── */
typedef struct __attribute__((packed, aligned(4))) {
    uint32_t magic;                  /* +0x00  0xD146C302 — G302 marker */
    uint32_t hidpp_recv_cnt;         /* +0x04 */
    uint32_t hidpp_last_len;         /* +0x08 */
    uint32_t hidpp_move_cnt;         /* +0x0C */
    uint32_t mouse_send_call_cnt;    /* +0x10 */
    uint32_t mouse_send_ok_cnt;      /* +0x14 */
    uint32_t mouse_send_busy_cnt;    /* +0x18 */
    uint32_t mouse_send_fail_cnt;    /* +0x1C */
    int32_t  last_dx;                /* +0x20 */
    int32_t  last_dy;                /* +0x24 */
    uint32_t dev_state_at_send;      /* +0x28 */
    uint32_t hidpp_reply_cnt;        /* +0x2C — schedule_reply count */
    uint8_t  last_hidpp[32];         /* +0x30..+0x4F  most recent HID++ packet */
    uint32_t hidpp_tx_cnt;           /* +0x50 — successful Transmit */
    uint32_t hidpp_tx_busy_cnt;      /* +0x54 — EP busy retries */
    uint32_t hidpp_unknown_feat_cnt; /* +0x58 */
    uint32_t hidpp_err_reply_cnt;    /* +0x5C */
    /* RGB mirror (P2-3). */
    uint32_t rgb_set_cnt;            /* +0x60 — changed-frame ACKs scheduled */
    uint32_t rgb_skip_cnt;           /* +0x64 — memcmp-hit skip ACKs */
    uint8_t  rgb_zone0_bright;       /* +0x68 */
    uint8_t  rgb_zone0_valid;        /* +0x69 */
    uint16_t rgb_zone0_period_ms;    /* +0x6A */
    uint8_t  rgb_zone1_bright;       /* +0x6C */
    uint8_t  rgb_zone1_valid;        /* +0x6D */
    uint16_t rgb_zone1_period_ms;    /* +0x6E */
    uint32_t uart_frame_ok_cnt;      /* +0x70 */
    uint32_t uart_frame_err_cnt;     /* +0x74 */
    uint32_t uart_rx_byte_cnt;       /* +0x78 */
    uint32_t uart_overrun_cnt;       /* +0x7C */
    uint8_t  uart_status;            /* +0x80 — last status byte sent to client */
} diag_t;

_Static_assert(sizeof(diag_t) <= 256, "diag_t grew, check layout");
volatile diag_t *const DIAG = (volatile diag_t *)0x20004000U;

/* IRQ → main hand-off: ring buffer decouples UART/HID++ producers from
 * the main-loop consumer.  ISR pushes frames, main loop pops one per
 * iteration and only advances the tail on USBD_OK — USB BUSY results
 * in a retry, not a silent drop. */
#define MOUSE_BUF_SIZE 8

typedef struct {
    int16_t  dx;
    int16_t  dy;
    uint8_t  buttons;
    int8_t   wheel;
    int8_t   hwheel;
    uint8_t  status;   /* reception-stage bits set by ISR */
} mouse_frame_t;

static volatile mouse_frame_t g_frame_buf[MOUSE_BUF_SIZE];
static volatile uint8_t g_buf_head = 0;  /* ISR writes here */
static volatile uint8_t g_buf_tail = 0;  /* main loop reads here */
static volatile uint8_t g_buf_cnt  = 0;  /* frames buffered */

/* UART receive globals */
UART_HandleTypeDef huart1;
static uint8_t uart_rx_byte;
static uint8_t uart_state;
static uint8_t uart_frame[10];
static uint8_t uart_frame_idx;
static uint32_t last_uart_rx_us;
static volatile uint8_t g_err_status;       /* pending error status to send from main loop */
static uint8_t g_frame_status_sent;         /* suppress duplicate status on USB BUSY retries */

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);

static uint8_t mouse_send_dxy(int16_t dx, int16_t dy, uint8_t buttons, int8_t wheel, int8_t hwheel)
{
    g302_mouse_report_t r = {
        .button = buttons,
        .x      = dx,
        .y      = dy,
        .wheel  = wheel,
        .pan    = hwheel,
    };
    DIAG->mouse_send_call_cnt++;
    DIAG->last_dx = dx;
    DIAG->last_dy = dy;
    DIAG->dev_state_at_send = (uint32_t)hUsbDeviceFS.dev_state;

    uint8_t rc = USBD_G302_SendMouseReport(&hUsbDeviceFS, (uint8_t *)&r, sizeof(r));
    if (rc == USBD_OK)         DIAG->mouse_send_ok_cnt++;
    else if (rc == USBD_BUSY)  DIAG->mouse_send_busy_cnt++;
    else                       DIAG->mouse_send_fail_cnt++;
    return rc;
}

/* Send a 3-byte response frame [0xBB][status][XOR] over USART1.
 * Blocking with 2ms timeout. Called from main loop only. */
static void uart_send_status(uint8_t status)
{
    uint8_t buf[3];
    buf[0] = 0xBB;
    buf[1] = status;
    buf[2] = 0xBB ^ status;
    HAL_UART_Transmit(&huart1, buf, 3, 2);
    DIAG->uart_status = status;
}

/* HID++ host→device packet — arrives in EP0 OUT data-stage callback context
 * (IRQ-driven). Parse minimal "move mouse" opcode and defer the actual
 * USB transmit to the main loop. */
void USBD_G302_HidppReceive(const uint8_t *data, uint16_t len)
{
    DIAG->hidpp_recv_cnt++;
    DIAG->hidpp_last_len = len;
    uint16_t copy_n = len < sizeof(DIAG->last_hidpp) ? len : sizeof(DIAG->last_hidpp);
    for (uint16_t i = 0; i < copy_n; i++) DIAG->last_hidpp[i] = data[i];

    /* Short HID++ frame: [0x10][device_idx][feature][p0..p3]
     * Private mouse-move opcode: feature == 0xFE, params = int16 dx, int16 dy.
     * Anything else is real HID++ — hand off to the protocol layer. */
    if (len >= 7 && data[0] == 0x10 && data[2] == 0xFE) {
        if (g_buf_cnt < MOUSE_BUF_SIZE) {
            uint8_t h = g_buf_head;
            g_frame_buf[h].dx      = (int16_t)(data[3] | (data[4] << 8));
            g_frame_buf[h].dy      = (int16_t)(data[5] | (data[6] << 8));
            g_frame_buf[h].buttons = 0;
            g_frame_buf[h].wheel   = 0;
            g_frame_buf[h].hwheel  = 0;
            g_frame_buf[h].status  = STATUS_BUF_PUSHED;
            g_buf_head = (h + 1) % MOUSE_BUF_SIZE;
            g_buf_cnt++;
        }
        DIAG->hidpp_move_cnt++;
        return;
    }

    hidpp_handler_on_request(data, len);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) return;

    uint8_t b = uart_rx_byte;
    DIAG->uart_rx_byte_cnt++;
    last_uart_rx_us = hidpp_micros();

    if (uart_state == 0) {
        if (b == 0xAA) {
            uart_frame[0] = b;
            uart_frame_idx = 1;
            uart_state = 1;
        }
    } else {
        uart_frame[uart_frame_idx++] = b;
        if (uart_frame_idx >= 10) {
            uint8_t csum = 0;
            for (uint8_t i = 0; i < 9; i++) csum ^= uart_frame[i];
            if (csum == uart_frame[9] && uart_frame[1] == 0x01) {
                if (g_buf_cnt < MOUSE_BUF_SIZE) {
                    uint8_t h = g_buf_head;
                    g_frame_buf[h].dx      = (int16_t)(uart_frame[2] | (uart_frame[3] << 8));
                    g_frame_buf[h].dy      = (int16_t)(uart_frame[4] | (uart_frame[5] << 8));
                    g_frame_buf[h].buttons = uart_frame[6];
                    g_frame_buf[h].wheel   = (int8_t)uart_frame[7];
                    g_frame_buf[h].hwheel  = (int8_t)uart_frame[8];
                    g_frame_buf[h].status  = STATUS_UART_FRAME_OK | STATUS_BUF_PUSHED;
                    g_buf_head = (h + 1) % MOUSE_BUF_SIZE;
                    g_buf_cnt++;
                } else {
                    g_err_status = STATUS_UART_FRAME_OK | STATUS_BUF_DROPPED;
                }
                DIAG->uart_frame_ok_cnt++;
            } else {
                g_err_status = STATUS_XOR_ERROR;
                DIAG->uart_frame_err_cnt++;
            }
            uart_state = 0;
        }
    }

    HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        DIAG->uart_overrun_cnt++;
        g_err_status |= STATUS_UART_OVERRUN;
        HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1);
    }
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init();
    HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1);

    for (uint8_t *p = (uint8_t *)DIAG;
         p < (uint8_t *)DIAG + sizeof(*DIAG); p++) {
        *p = 0;
    }
    DIAG->magic = 0xD146C302U;

    MX_USB_DEVICE_Init();
    hidpp_handler_init();

    while (1) {
        if (g302_mouse_ep_ready && g_buf_cnt > 0) {
            g302_mouse_ep_ready = 0;
            uint8_t t = g_buf_tail;
            mouse_frame_t f = g_frame_buf[t];
            uint8_t rc = mouse_send_dxy(f.dx, f.dy, f.buttons, f.wheel, f.hwheel);

            /* Send status once per frame (suppress duplicates on USB BUSY retries) */
            if (!g_frame_status_sent) {
                uint8_t s = f.status;                     /* bits 0-2, 6-7 set by ISR */
                if (rc == USBD_OK)        s |= STATUS_USB_SEND_OK;
                else if (rc == USBD_BUSY) s |= STATUS_USB_SEND_BUSY;
                else                      s |= STATUS_USB_SEND_FAIL;
                uart_send_status(s);
                g_frame_status_sent = 1;
            }

            if (rc == USBD_OK) {
                g_buf_tail = (t + 1) % MOUSE_BUF_SIZE;
                __disable_irq();
                g_buf_cnt--;
                __enable_irq();
                g_frame_status_sent = 0;  /* ready for next frame */
            } else {
                g302_mouse_ep_ready = 1;  /* retry next iteration */
            }
        }

        /* Flush pending error status (XOR error, overrun, buffer drop — no associated frame) */
        if (g_err_status) {
            uart_send_status(g_err_status);
            g_err_status = 0;
        }

        hidpp_handler_poll_tx(&hUsbDeviceFS);

        /* Mirror HID++ counters into diag block for non-intrusive readout. */
        DIAG->hidpp_reply_cnt         = hidpp_reply_cnt;
        DIAG->hidpp_tx_cnt            = hidpp_tx_cnt;
        DIAG->hidpp_tx_busy_cnt       = hidpp_tx_busy_cnt;
        DIAG->hidpp_unknown_feat_cnt  = hidpp_unknown_feat_cnt;
        DIAG->hidpp_err_reply_cnt     = hidpp_err_reply_cnt;
        DIAG->rgb_set_cnt             = hidpp_rgb_set_cnt;
        DIAG->rgb_skip_cnt            = hidpp_rgb_skip_cnt;
        DIAG->rgb_zone0_bright        = g_hidpp_rgb_zones[0].bright;
        DIAG->rgb_zone0_valid         = g_hidpp_rgb_zones[0].valid ? 1U : 0U;
        DIAG->rgb_zone0_period_ms     = g_hidpp_rgb_zones[0].period_ms;
        DIAG->rgb_zone1_bright        = g_hidpp_rgb_zones[1].bright;
        DIAG->rgb_zone1_valid         = g_hidpp_rgb_zones[1].valid ? 1U : 0U;
        DIAG->rgb_zone1_period_ms     = g_hidpp_rgb_zones[1].period_ms;

        /* ── UART activity LED: 2 Hz blink while receiving data ────────── */
        uint32_t now_us = hidpp_micros();
        if (now_us - last_uart_rx_us < 500000U) {
            uint32_t phase = (now_us / 250000U) & 1U;
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13,
                              phase ? GPIO_PIN_RESET : GPIO_PIN_SET);
        } else {
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);  /* off */
        }

    }
}

/* HSE 25 MHz BlackPill: PLLM=25 PLLN=192 PLLP=2 PLLQ=4 -> SYSCLK=96, USB=48. */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 25;
    osc.PLL.PLLN = 192;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 4;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                  | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_3) != HAL_OK) Error_Handler();
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();   /* USB OTG FS on PA11/PA12 — pin AF setup in HAL_PCD_MspInit */
    __HAL_RCC_GPIOC_CLK_ENABLE();   /* PC13 onboard LED */

    /* PC13 onboard LED — active-low (sink), open-drain */
    gpio.Pin   = GPIO_PIN_13;
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);  /* off */
}

static void MX_USART1_UART_Init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK) Error_Handler();
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) { }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) { (void)file; (void)line; }
#endif
