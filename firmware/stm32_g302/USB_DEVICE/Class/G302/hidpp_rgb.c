/**
 * hidpp_rgb — implementation of the legacy subID 0x05 RGB channel.
 *
 * State: two independent zones (logo / DPI indicator). Each stores
 * brightness + period_ms + mode flag; reconstructed from incoming
 * Set frames at byte offsets per HIDPP_PROTOCOL_LITE.md §5.7.
 *
 * Timing (LITE §6) — all delays come from hidpp_timing.h presets
 * (HIDPP_DELAY_RGB_CHANGED / HIDPP_DELAY_RGB_SKIP) so the jitter LCG
 * and base/σ values live in one place across the whole HID++ surface.
 *
 * Echo payload: zero-filled 16B Long body. Real device returns the
 * subID/feat/func header echo plus 16 zero bytes (LITE §5.7).
 */
#include "hidpp_rgb.h"
#include "hidpp_handler.h"
#include "hidpp_timing.h"
#include <string.h>

volatile hidpp_rgb_zone_t g_hidpp_rgb_zones[HIDPP_RGB_ZONES] = {0};
volatile uint32_t         hidpp_rgb_set_cnt  = 0;
volatile uint32_t         hidpp_rgb_skip_cnt = 0;

/* Echo response: same RID as request (always Long for subID 0x05 RGB),
 * 16 bytes of zero payload. Routed through hidpp_send_response so the
 * header/sw_id echo logic stays single-sourced. */
static void rgb_send_ack(const hidpp_req_t *req, uint32_t delay_us)
{
    uint8_t zero[16] = {0};
    hidpp_send_response(req, zero, sizeof(zero), delay_us);
}

void hidpp_rgb_dispatch(const hidpp_req_t *req)
{
    /* Frame layout (after the 4-byte HID++ header is stripped):
     *   params[0] = zone (0/1)
     *   params[1] = sub_mode
     *   params[2] = mode_flag (0x80)
     *   params[3] = reserved
     *   params[4] = blue brightness
     *   params[5..6] = period_ms BE  (NOT [7..8] — LITE doc had the
     *                  wrong offset; confirmed against real-device
     *                  UsbCap/RGB.pcapng frame 12552 carrying 0x1388).
     * Need at least 7 payload bytes through period_lo. */
    if (req->param_len < 7U) {
        hidpp_send_error(req, HIDPP2_ERR_INVALID_ARGUMENT, 200U);
        return;
    }

    uint8_t zone = req->params[0];
    if (zone >= HIDPP_RGB_ZONES) {
        /* Real device ACKs unknown zones silently — but G HUB never sends
         * them, so an OUT_OF_RANGE here is informative for our diag. */
        hidpp_send_error(req, HIDPP2_ERR_OUT_OF_RANGE, 200U);
        return;
    }

    hidpp_rgb_zone_t next = {
        .sub_mode  = req->params[1],
        .mode_flag = req->params[2],
        .bright    = req->params[4],
        .period_ms = (uint16_t)((req->params[5] << 8) | req->params[6]),
        .valid     = true,
    };

    hidpp_rgb_zone_t prev = g_hidpp_rgb_zones[zone];
    bool changed = !prev.valid
                 || prev.bright    != next.bright
                 || prev.sub_mode  != next.sub_mode
                 || prev.mode_flag != next.mode_flag
                 || prev.period_ms != next.period_ms;

    g_hidpp_rgb_zones[zone] = next;

    if (changed) {
        hidpp_rgb_set_cnt++;
        rgb_send_ack(req, HIDPP_DELAY_RGB_CHANGED());
    } else {
        hidpp_rgb_skip_cnt++;
        rgb_send_ack(req, HIDPP_DELAY_RGB_SKIP());
    }
}
