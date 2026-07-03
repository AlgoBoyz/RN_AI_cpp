/**
 * hidpp_rgb — Legacy subID 0x05 dispatcher for the G302 blue LED.
 *
 * G302 carries RGB on the pre-2.0 Logitech channel (`subID=0x05`,
 * `func=5`) instead of a feature-table feature. Frames look like a
 * normal Long HID++ packet but byte[2] is a subID, not a FeatureSet
 * index — see HIDPP_PROTOCOL_LITE.md §5.7. We expose just the entry
 * point; hidpp_handler.c intercepts the frame before feature dispatch.
 */
#ifndef HIDPP_RGB_H
#define HIDPP_RGB_H

#ifdef __cplusplus
extern "C" {
#endif

#include "hidpp_handler.h"

#define HIDPP_RGB_ZONES 2U

typedef struct {
    uint8_t  bright;
    uint8_t  sub_mode;
    uint8_t  mode_flag;
    uint16_t period_ms;
    bool     valid;
} hidpp_rgb_zone_t;

void hidpp_rgb_dispatch(const hidpp_req_t *req);

/* Snapshots for diag mirroring (read from main loop). */
extern volatile hidpp_rgb_zone_t g_hidpp_rgb_zones[HIDPP_RGB_ZONES];
extern volatile uint32_t         hidpp_rgb_set_cnt;
extern volatile uint32_t         hidpp_rgb_skip_cnt;

#ifdef __cplusplus
}
#endif
#endif
