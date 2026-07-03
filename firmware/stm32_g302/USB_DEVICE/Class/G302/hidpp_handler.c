#include "hidpp_handler.h"
#include "hidpp_features.h"
#include "hidpp_rgb.h"
#include "hidpp_timing.h"
#include "usbd_g302.h"
#include <string.h>

volatile uint32_t hidpp_unknown_feat_cnt = 0;
volatile uint32_t hidpp_unknown_func_cnt = 0;
volatile uint32_t hidpp_err_reply_cnt    = 0;

/* Feature index -> handler. Index space matches g_hidpp_feat_fid in
 * hidpp_features.c (17 slots, matches real G302 FeatureSet). Unfilled
 * slots fall through to the unknown-feat error path. */

static const hidpp_feature_fn g_feature_table[HIDPP_FEAT_COUNT] = {
    [0x00] = hidpp_feat_root,
    [0x01] = hidpp_feat_feature_set,
    [0x03] = hidpp_feat_device_fw_version,
    [0x04] = hidpp_feat_device_name,
    [0x05] = hidpp_feat_device_friendly_name,
    [0x0d] = hidpp_feat_adjustable_dpi,
    [0x0e] = hidpp_feat_report_rate,
    [0x0f] = hidpp_feat_onboard_profiles,
    [0x10] = hidpp_feat_mouse_button_spy,
    /* 0x02, 0x06..0x0c — stubbed in P3; default to unknown-feat. */
};

void hidpp_handler_init(void)
{
    hidpp_timing_init();
}

void hidpp_handler_poll_tx(USBD_HandleTypeDef *pdev)
{
    hidpp_poll_tx(pdev);
}

static uint8_t rid_len(uint8_t rid)
{
    return (rid == 0x11U) ? 20U : 7U;
}

void hidpp_send_response(const hidpp_req_t *req,
                         const uint8_t *payload, uint8_t payload_len,
                         uint32_t delay_us)
{
    uint8_t buf[20] = {0};

    /* HID++ 2.0 response sizing: the response uses Short (RID 0x10, 3B
     * payload) only when payload fits; anything larger MUST upgrade to
     * Long (RID 0x11, 16B payload). G HUB consistently sends GetFwInfo
     * and GetName as Short requests but expects Long responses — the
     * real G302 dump confirms this (LITE §5.1/§5.2 show "10 ... → 11 ...").
     * If the original request was already Long, keep it Long. */
    uint8_t rid   = (payload_len > 3U || req->rid == 0x11U) ? 0x11U : 0x10U;
    uint8_t total = rid_len(rid);

    buf[0] = rid;
    buf[1] = req->dev;
    buf[2] = req->feat_idx;
    buf[3] = (uint8_t)((req->func << 4) | (req->sw_id & 0x0FU));

    uint8_t cap = (uint8_t)(total - HIDPP_HDR_LEN);
    if (payload && payload_len) {
        uint8_t n = payload_len > cap ? cap : payload_len;
        memcpy(&buf[HIDPP_HDR_LEN], payload, n);
    }
    hidpp_schedule_reply(buf, total, delay_us);
}

void hidpp_send_error(const hidpp_req_t *req, uint8_t err_code,
                      uint32_t delay_us)
{
    /* HID++ 2.0 error frame: short (RID 0x10) regardless of request size,
     * with byte[2] = 0xFF marker. */
    uint8_t buf[7] = {0};
    buf[0] = 0x10U;
    buf[1] = req->dev;
    buf[2] = HIDPP_ERROR_FEAT_IDX;
    buf[3] = req->feat_idx;
    buf[4] = (uint8_t)((req->func << 4) | (req->sw_id & 0x0FU));
    buf[5] = err_code;
    buf[6] = 0;
    hidpp_err_reply_cnt++;
    hidpp_schedule_reply(buf, sizeof(buf), delay_us);
}

void hidpp_handler_on_request(const uint8_t *data, uint16_t len)
{
    if (len < HIDPP_HDR_LEN) return;
    uint8_t rid = data[0];
    if (rid != 0x10U && rid != 0x11U) return;

    hidpp_req_t req = {
        .rid       = rid,
        .dev       = data[1],
        .feat_idx  = data[2],
        .func      = (uint8_t)(data[3] >> 4),
        .sw_id     = (uint8_t)(data[3] & 0x0FU),
        .params    = &data[HIDPP_HDR_LEN],
        .param_len = (uint8_t)(len - HIDPP_HDR_LEN),
    };

    /* Legacy subID 0x05 RGB channel rides on the same byte slot as the
     * 2.0 DeviceFriendlyName feature (also 0x05). They are disambiguated
     * by func: RGB always uses func=5 (G HUB pattern, LITE §5.7);
     * DeviceFriendlyName uses func=0/1. Route RGB first so adding a
     * DeviceFriendlyName handler later cannot shadow RGB writes. */
    if (req.feat_idx == 0x05U && req.func == 0x5U) {
        hidpp_rgb_dispatch(&req);
        return;
    }

    if (req.feat_idx < HIDPP_FEAT_COUNT && g_feature_table[req.feat_idx]) {
        g_feature_table[req.feat_idx](&req);
        return;
    }

    hidpp_unknown_feat_cnt++;
    hidpp_send_error(&req, HIDPP2_ERR_INVALID_FEATURE_INDEX, HIDPP_DELAY_ERROR());
}
