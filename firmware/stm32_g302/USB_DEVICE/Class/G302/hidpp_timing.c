#include "hidpp_timing.h"
#include "usbd_g302.h"
#include "platform.h"
#include <string.h>

volatile uint32_t hidpp_reply_cnt   = 0;
volatile uint32_t hidpp_tx_cnt      = 0;
volatile uint32_t hidpp_tx_busy_cnt = 0;

static struct {
    uint8_t  buf[HIDPP_MAX_REPLY_LEN];
    uint8_t  len;
    uint32_t deadline_us;
    volatile bool pending;
} g_slot;

void hidpp_timing_init(void)
{
    platform_delay_init();
    g_slot.pending = false;
}

uint32_t hidpp_micros(void)
{
    return DWT_CYCCNT / 96U;
}

/* LCG jitter — same Numerical Recipes constants used in hidpp_rgb.c.
 * Returns a value in [base - span/2, base + span/2]. Span clamped at
 * 2·base to avoid the half-window exceeding the base (we never want a
 * delay near zero). */
uint32_t hidpp_jittered_delay(uint32_t base_us, uint32_t span_us)
{
    static uint32_t s_rng = 0x12345678U;
    s_rng = s_rng * 1103515245U + 12345U;
    if (span_us == 0U) return base_us;
    if (span_us > 2U * base_us) span_us = 2U * base_us;
    uint32_t offset = s_rng % span_us;        /* 0 .. span-1   */
    uint32_t signed_offset = offset;          /* keep unsigned for arithmetic */
    /* Centre around base: subtract half the span. */
    return base_us + signed_offset - (span_us / 2U);
}

void hidpp_schedule_reply(const uint8_t *buf, uint8_t len, uint32_t delay_us)
{
    if (len == 0 || len > HIDPP_MAX_REPLY_LEN) return;
    /* IRQ context — single-writer/single-reader; clear pending first to
     * avoid main loop racing on a half-written buffer. */
    g_slot.pending = false;
    memcpy(g_slot.buf, buf, len);
    g_slot.len         = len;
    g_slot.deadline_us = hidpp_micros() + delay_us;
    hidpp_reply_cnt++;
    g_slot.pending     = true;
}

void hidpp_poll_tx(USBD_HandleTypeDef *pdev)
{
    if (!g_slot.pending) return;

    /* DWT_CYCCNT wraps every ~44.7 s at 96 MHz so hidpp_micros() wraps
     * every ~44.7 s as well. Use signed delta for wrap safety. */
    int32_t delta = (int32_t)(hidpp_micros() - g_slot.deadline_us);
    if (delta < 0) return;

    uint8_t rc = USBD_G302_SendHidpp(pdev, g_slot.buf, g_slot.len);
    if (rc == USBD_OK) {
        g_slot.pending = false;
        hidpp_tx_cnt++;
    } else {
        /* EP busy — retry next loop iteration. */
        hidpp_tx_busy_cnt++;
    }
}
