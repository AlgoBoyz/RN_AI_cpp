/**
 * USBD_G302 — Logitech G302 Daedalus Apex clone, 2-interface HID class.
 *
 * Mirrors the real device captured in cap10.pcapng (VID 0x046D / PID 0xC07F,
 * bcdDevice 0x9100). Layout:
 *
 *   Interface 0 — Boot Mouse              EP 0x81 IN,  8 B, no Report ID
 *   Interface 1 — Multimedia + HID++      EP 0x82 IN, 20 B
 *
 * Mouse report (IF0, 8 B): {btn_lo, btn_hi, x_lo, x_hi, y_lo, y_hi, wheel, pan}
 *
 * IF1 report IDs in/out:
 *   0x01  Keyboard 8 B (IN)
 *   0x03  Consumer 5 B (IN)
 *   0x04  SysCtl   1 B (IN)
 *   0x10  HID++ Short 7 B (IN + OUT via EP0 SET_REPORT)
 *   0x11  HID++ Long 20 B (IN + OUT via EP0 SET_REPORT)
 *
 * HID++ host requests arrive on EP0 (SET_REPORT) — the class layer dispatches
 * them to USBD_G302_HidppReceive(). Application sends responses back via
 * USBD_G302_SendHidpp().
 */
#ifndef USBD_G302_H
#define USBD_G302_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_ioreq.h"

/* Interface numbers */
#define G302_ITF_MOUSE        0U
#define G302_ITF_MM_HIDPP     1U
#define G302_ITF_COUNT        2U

/* Endpoint addresses */
#define G302_EP_MOUSE_IN      0x81U
#define G302_EP_MM_IN         0x82U

/* Max packet sizes — match real device */
#define G302_EP_MOUSE_SIZE    8U
#define G302_EP_MM_SIZE       20U

/* Polling interval (FS, ms) — G302 polls 1 ms on both interrupt IN endpoints */
#define G302_BINTERVAL        0x01U

/* HID class descriptor types */
#define G302_HID_DESC_TYPE         0x21U
#define G302_HID_REPORT_DESC_TYPE  0x22U

/* HID class requests */
#define G302_HID_REQ_GET_REPORT    0x01U
#define G302_HID_REQ_GET_IDLE      0x02U
#define G302_HID_REQ_GET_PROTOCOL  0x03U
#define G302_HID_REQ_SET_REPORT    0x09U
#define G302_HID_REQ_SET_IDLE      0x0AU
#define G302_HID_REQ_SET_PROTOCOL  0x0BU

/* HID++ report IDs and lengths (payload includes the Report ID byte). */
#define G302_RID_HIDPP_SHORT       0x10U
#define G302_RID_HIDPP_LONG        0x11U
#define G302_HIDPP_SHORT_LEN       7U
#define G302_HIDPP_LONG_LEN        20U

typedef enum {
    G302_STATE_IDLE = 0,
    G302_STATE_BUSY,
} USBD_G302_StateTypeDef;

typedef struct {
    USBD_G302_StateTypeDef mouse_state;
    USBD_G302_StateTypeDef mm_state;        /* IF1 EP 0x82 IN */
    uint8_t protocol[G302_ITF_COUNT];
    uint8_t idle_state[G302_ITF_COUNT];
    uint8_t alt_setting[G302_ITF_COUNT];

    /* SET_REPORT EP0 staging buffer — large enough for HID++ Long (20 B). */
    uint8_t ep0_out_buf[G302_HIDPP_LONG_LEN];
    uint16_t ep0_out_len;
    uint8_t  ep0_out_rid;     /* report id parsed from setup wValue low byte */
} USBD_G302_HandleTypeDef;

extern USBD_ClassTypeDef USBD_G302;
#define USBD_G302_CLASS &USBD_G302

/* Send mouse report on EP 0x81 IN. report points at 8-byte payload (no RID). */
uint8_t USBD_G302_SendMouseReport(USBD_HandleTypeDef *pdev,
                                  uint8_t *report, uint16_t len);

/* Send HID++ (or any IF1) report on EP 0x82 IN. report[0] is the report ID. */
uint8_t USBD_G302_SendHidpp(USBD_HandleTypeDef *pdev,
                            uint8_t *report, uint16_t len);

/* Host → device HID++ packet callback — weak, override in application.
 * Fires after the EP0 SET_REPORT control transfer has completed. data[0]
 * is the Report ID; len is the full packet length (7 for short, 20 for long).
 * Runs in the OUT-data-stage callback context (IRQ-driven); buffer your
 * work and respond from main loop with USBD_G302_SendHidpp(). */
void USBD_G302_HidppReceive(const uint8_t *data, uint16_t len);

/* SOF-sync: DataIn ISR sets →1, main loop clears →0 after consuming one frame.
 * Keeps USB submits aligned to host IN token cadence. */
extern volatile uint8_t g302_mouse_ep_ready;

#ifdef __cplusplus
}
#endif
#endif
