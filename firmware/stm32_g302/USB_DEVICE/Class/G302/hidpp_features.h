/**
 * hidpp_features — per-feature handlers (forward declarations).
 *
 * Each handler is registered in g_feature_table (hidpp_handler.c) and is
 * called with a parsed hidpp_req_t. Handlers schedule responses via
 * hidpp_send_response / hidpp_send_error.
 */
#ifndef HIDPP_FEATURES_H
#define HIDPP_FEATURES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "hidpp_handler.h"

/* Feature ID constants (FIDs) — from HIDPP_PROTOCOL_LITE.md §4. */
#define FID_ROOT              0x0000U
#define FID_FEATURE_SET       0x0001U
#define FID_DEVICE_INFO       0x0002U
#define FID_DEVICE_FW_VERSION 0x0003U
#define FID_DEVICE_NAME       0x0005U
#define FID_RESET             0x0020U
#define FID_BATTERY_LEVEL     0x1000U /* placeholder, unconfirmed */
#define FID_ADJUSTABLE_DPI    0x2201U
#define FID_REPORT_RATE       0x8060U
#define FID_ONBOARD_PROFILES  0x8100U
#define FID_MOUSE_BUTTON_SPY  0x8110U

/* Per-feature handlers (P0 + P1). */
void hidpp_feat_root(const hidpp_req_t *req);
void hidpp_feat_feature_set(const hidpp_req_t *req);
void hidpp_feat_device_fw_version(const hidpp_req_t *req);
void hidpp_feat_device_name(const hidpp_req_t *req);
void hidpp_feat_device_friendly_name(const hidpp_req_t *req); /* feat 0x05 non-RGB funcs */
void hidpp_feat_adjustable_dpi(const hidpp_req_t *req);    /* P1-2 */
void hidpp_feat_report_rate(const hidpp_req_t *req);       /* P1-1 */
void hidpp_feat_onboard_profiles(const hidpp_req_t *req);  /* P1-3 */
void hidpp_feat_mouse_button_spy(const hidpp_req_t *req);  /* P3 stub */

/* FeatureSet enumeration table — 17 entries matching the real G302.
 * Index = feat_idx (slot in FeatureSet); value = 16-bit FID returned by
 * Root.GetFeature / FeatureSet.GetFeatureID. 0x0000 in slot >0 means
 * "unknown — stubbed" (placeholder for slots 0x06..0x0c we have not
 * captured yet). */
#define HIDPP_FEAT_COUNT 17U
extern const uint16_t g_hidpp_feat_fid[HIDPP_FEAT_COUNT];

#ifdef __cplusplus
}
#endif
#endif
