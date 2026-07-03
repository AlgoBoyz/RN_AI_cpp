/**
 * hidpp_handler — HID++ 2.0/4.2 request dispatcher.
 *
 * Frame layout (sent over EP0 SET_REPORT, received in IRQ context):
 *
 *   [rid][dev][feat_idx][func<<4 | sw_id][params...]
 *     │    │      │             │             │
 *     │    │      │             │             └── 0..3 (Short) / 0..16 (Long)
 *     │    │      │             ├── sw_id: 0xD = G HUB, 0xF = Logitech FW;
 *     │    │      │             │   must echo back unchanged.
 *     │    │      │             └── func: feature-local function number
 *     │    │      └── slot index in FeatureSet (0x00..0x10 for the real G302).
 *     │    └── device index — single-device 0x00; G HUB initial probe uses 0xFF.
 *     └── 0x10 Short (7 B) / 0x11 Long (20 B).
 *
 * Error response (HID++ 2.0): byte[2] becomes 0xFF, byte[3] = original
 * feat_idx, byte[4] = original (func<<4 | sw_id), byte[5] = err_code.
 */
#ifndef HIDPP_HANDLER_H
#define HIDPP_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "usbd_def.h"

/* HID++ 2.0 error codes. */
#define HIDPP2_ERR_UNKNOWN                0x01U
#define HIDPP2_ERR_INVALID_ARGUMENT       0x02U
#define HIDPP2_ERR_OUT_OF_RANGE           0x03U
#define HIDPP2_ERR_HW_ERROR               0x04U
#define HIDPP2_ERR_LOGITECH_INTERNAL      0x05U
#define HIDPP2_ERR_INVALID_FEATURE_INDEX  0x06U
#define HIDPP2_ERR_INVALID_FUNCTION_ID    0x07U
#define HIDPP2_ERR_BUSY                   0x08U
#define HIDPP2_ERR_UNSUPPORTED            0x09U

/* Reserved feature-index value used in error frames (byte[2] = 0xFF). */
#define HIDPP_ERROR_FEAT_IDX              0xFFU

#define HIDPP_HDR_LEN                     4U   /* rid + dev + feat_idx + func|sw */

/* Parsed request handed to feature handlers. params/param_len cover the
 * bytes after the 4-byte header. */
typedef struct {
    uint8_t        rid;       /* 0x10 short / 0x11 long */
    uint8_t        dev;
    uint8_t        feat_idx;
    uint8_t        func;      /* upper nibble of byte[3] */
    uint8_t        sw_id;     /* lower nibble of byte[3] */
    const uint8_t *params;
    uint8_t        param_len;
} hidpp_req_t;

/* Feature handler signature. Free to call hidpp_send_response / _send_error. */
typedef void (*hidpp_feature_fn)(const hidpp_req_t *req);

void hidpp_handler_init(void);

/* Called from IRQ (EP0 SET_REPORT OUT data-stage). */
void hidpp_handler_on_request(const uint8_t *data, uint16_t len);

/* Called from main loop. Thin wrapper around hidpp_poll_tx. */
void hidpp_handler_poll_tx(USBD_HandleTypeDef *pdev);

/* Schedule a normal response. payload is what follows the 4-byte header.
 * If payload_len < (rid_len - 4) it is zero-padded; if greater it is
 * truncated. Use 0 for "no payload" (header only). */
void hidpp_send_response(const hidpp_req_t *req,
                         const uint8_t *payload, uint8_t payload_len,
                         uint32_t delay_us);

/* Schedule a HID++ 2.0 error frame for the given request. */
void hidpp_send_error(const hidpp_req_t *req, uint8_t err_code,
                      uint32_t delay_us);

/* Diag counters. */
extern volatile uint32_t hidpp_unknown_feat_cnt;
extern volatile uint32_t hidpp_unknown_func_cnt;
extern volatile uint32_t hidpp_err_reply_cnt;

#ifdef __cplusplus
}
#endif
#endif
