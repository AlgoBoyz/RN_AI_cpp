/**
 * hidpp_timing — DWT-based microsecond clock + single-slot deferred response
 * queue for the G302 HID++ protocol layer.
 *
 * Rationale (project-stm32-usbd-transmit-thread-context):
 *   USBD_LL_Transmit() called from inside the OTG_FS RX IRQ silently drops
 *   on the host side. HID++ responses must therefore be scheduled from the
 *   EP0 SET_REPORT callback (IRQ context) and dispatched from the main loop.
 *
 *   HID++ is strictly request-response with G HUB sending serially, so a
 *   single in-flight response slot is sufficient — no ring buffer needed.
 *
 * Reuses libraries/sweet_platform/platform.[ch] for DWT init (TRCENA +
 * CYCCNTENA). 96 MHz SYSCLK -> 96 cycles per microsecond.
 */
#ifndef HIDPP_TIMING_H
#define HIDPP_TIMING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "usbd_def.h"

#define HIDPP_MAX_REPLY_LEN 20U

void     hidpp_timing_init(void);
uint32_t hidpp_micros(void);

/* IRQ context: schedule a reply to be sent after `delay_us` microseconds.
 * If a previous reply is still pending it is overwritten (request-response
 * model — last writer wins; in practice never overlaps). */
void hidpp_schedule_reply(const uint8_t *buf, uint8_t len, uint32_t delay_us);

/* Main loop: check the deadline and Transmit when due. Safe to call every
 * iteration; cheap when nothing pending. */
void hidpp_poll_tx(USBD_HandleTypeDef *pdev);

/* Returns a jittered delay in microseconds, uniformly distributed in
 * [base_us - span_us/2, base_us + span_us/2]. Backed by a small LCG —
 * not cryptographically random, just enough to break "fingerprint-perfect
 * constant delay" detection.
 *
 * Real-device delay distributions we calibrate against (from
 * HIDPP_PROTOCOL.md §9/§10/§11 / §14):
 *   SetSensorDPI    : 4.81 ms σ=0.02 ms  -> base 4810, span 80
 *   SetReportRate   : 4.70 ms σ=0.23 ms  -> base 4700, span 800
 *   RGB changed     : 84.4 ms σ=1.0  ms  -> base 84400, span 3500
 *   RGB skip        :  4.6 ms σ~0        -> base 4600, span 200
 *   Metadata (Ping/GetFeature/GetName)
 *                   : ~1-3 ms (no precise capture yet)
 *                                        -> base 1500, span 800 (conservative)
 *   Error replies   : ~1 ms              -> base 1000, span 400
 *
 * Uniform-distribution σ ≈ span / (2·√3). Spans above are picked to match
 * real σ within ±25 % — exact enough that KS-test on the distribution
 * passes, loose enough that we are not artificially "too perfect". */
uint32_t hidpp_jittered_delay(uint32_t base_us, uint32_t span_us);

/* Named delay presets — use these in handlers rather than raw constants
 * so timing tuning happens in one place. */
#define HIDPP_DELAY_DPI_SET()      hidpp_jittered_delay(4810U,   80U)
#define HIDPP_DELAY_RATE_SET()     hidpp_jittered_delay(4700U,  800U)
#define HIDPP_DELAY_RGB_CHANGED()  hidpp_jittered_delay(84400U, 3500U)
#define HIDPP_DELAY_RGB_SKIP()     hidpp_jittered_delay(4600U,  200U)
#define HIDPP_DELAY_METADATA()     hidpp_jittered_delay(1500U,  800U)
#define HIDPP_DELAY_ERROR()        hidpp_jittered_delay(1000U,  400U)

/* Diag counters (extern so main.c can mirror into the SRAM diag block). */
extern volatile uint32_t hidpp_reply_cnt;
extern volatile uint32_t hidpp_tx_cnt;
extern volatile uint32_t hidpp_tx_busy_cnt;

#ifdef __cplusplus
}
#endif
#endif
