/**
 * hidpp_features — concrete handler implementations.
 *
 * P0 set: Root (0x0000), FeatureSet (0x0001), DeviceFwVersion (0x0003),
 * DeviceName (0x0005, stored at feat_idx 0x04 in the real G302's
 * FeatureSet ordering — see g_feature_index_map below).
 *
 * Mapping authority: HIDPP_PROTOCOL_LITE.md §4 — captures the real G302's
 * FeatureSet enumeration result so G HUB's FeatureSet probe (feat 0x01
 * func 1) sees the byte-for-byte same FID at each index.
 */
#include "hidpp_features.h"
#include "hidpp_handler.h"
#include "hidpp_timing.h"
#include <string.h>

/* FeatureSet enumeration table — slots match the real G302 dump
 * (HIDPP_PROTOCOL_LITE.md §4). Slots 0x06..0x0c are placeholders
 * we have not captured yet; they return FID 0x0000 (= "Root") which
 * G HUB tolerates as "feature absent" when paired with flags=0.
 * P3-1 will revisit these once we capture their byte-level behavior. */
const uint16_t g_hidpp_feat_fid[HIDPP_FEAT_COUNT] = {
    [0x00] = FID_ROOT,
    [0x01] = FID_FEATURE_SET,
    [0x02] = FID_DEVICE_INFO,
    [0x03] = FID_DEVICE_FW_VERSION,
    [0x04] = FID_DEVICE_NAME,
    [0x05] = 0x0013U,            /* DeviceFriendlyName */
    [0x06] = 0x1801U,            /* placeholder for unidentified 0x18xx */
    [0x07] = 0x1802U,
    [0x08] = 0x1803U,
    [0x09] = 0x1804U,
    [0x0a] = 0x1805U,
    [0x0b] = 0x1e00U,            /* placeholder for unidentified 0x1exx */
    [0x0c] = 0x1e01U,
    [0x0d] = FID_ADJUSTABLE_DPI,
    [0x0e] = FID_REPORT_RATE,
    [0x0f] = FID_ONBOARD_PROFILES,
    [0x10] = FID_MOUSE_BUTTON_SPY,
};

/* ── Root (feat 0x00) ──────────────────────────────────────────────────── */

/* Root.GetFeature(fid) — func=0
 *   Request payload : [fid_hi][fid_lo]
 *   Response payload: [feat_idx][type=0][version=0]
 *
 * Root.Ping(payload) — func=1
 *   Request payload : [0][0][echo_byte]
 *   Response payload: [proto_major=4][proto_minor=2][echo_byte]
 *
 * Reachable from both sw_id=0xF (Logitech FW probe) and 0xD (G HUB);
 * since hidpp_send_response echoes sw_id from req, both paths work. */
void hidpp_feat_root(const hidpp_req_t *req)
{
    switch (req->func) {
    case 0x0: { /* GetFeature */
        if (req->param_len < 2) {
            hidpp_send_error(req, HIDPP2_ERR_INVALID_ARGUMENT, HIDPP_DELAY_ERROR());
            return;
        }
        uint16_t fid = (uint16_t)((req->params[0] << 8) | req->params[1]);
        uint8_t  resp[3] = {0, 0, 0};
        for (uint8_t i = 0; i < HIDPP_FEAT_COUNT; i++) {
            if (g_hidpp_feat_fid[i] == fid) { resp[0] = i; break; }
        }
        hidpp_send_response(req, resp, sizeof(resp), HIDPP_DELAY_METADATA());
        return;
    }
    case 0x1: { /* Ping */
        uint8_t echo = req->param_len >= 3 ? req->params[2] : 0;
        uint8_t resp[3] = { 0x04, 0x02, echo };
        hidpp_send_response(req, resp, sizeof(resp), HIDPP_DELAY_METADATA());
        return;
    }
    default:
        hidpp_send_error(req, HIDPP2_ERR_INVALID_FUNCTION_ID, HIDPP_DELAY_ERROR());
        return;
    }
}

/* ── FeatureSet (feat 0x01) ────────────────────────────────────────────── */

/* GetCount       (func=0) -> [count=17]
 * GetFeatureID(i)(func=1) -> [fid_hi][fid_lo][flags=0]                 */
void hidpp_feat_feature_set(const hidpp_req_t *req)
{
    switch (req->func) {
    case 0x0: {
        uint8_t resp[1] = { HIDPP_FEAT_COUNT };
        hidpp_send_response(req, resp, sizeof(resp), HIDPP_DELAY_METADATA());
        return;
    }
    case 0x1: {
        uint8_t idx = req->param_len >= 1 ? req->params[0] : 0xFF;
        if (idx >= HIDPP_FEAT_COUNT) {
            hidpp_send_error(req, HIDPP2_ERR_OUT_OF_RANGE, HIDPP_DELAY_ERROR());
            return;
        }
        uint16_t fid = g_hidpp_feat_fid[idx];
        uint8_t  resp[3] = {
            (uint8_t)(fid >> 8),
            (uint8_t)(fid & 0xFFU),
            0x00,
        };
        hidpp_send_response(req, resp, sizeof(resp), HIDPP_DELAY_METADATA());
        return;
    }
    default:
        hidpp_send_error(req, HIDPP2_ERR_INVALID_FUNCTION_ID, HIDPP_DELAY_ERROR());
        return;
    }
}

/* ── DeviceFwVersion (feat 0x03) ───────────────────────────────────────── */

/* GetEntityCount   (func=0) -> [4]
 * GetFwInfo(idx)   (func=1) -> [type][n0][n1][n2][ver_hi][ver_lo]
 *                              [build_hi][build_lo][...zero-padded]
 *
 * 4 entities verbatim from the real G302 dump (LITE §5.1). MainFW ver
 * 0x9100 matches USB descriptor bcdDevice; G HUB cross-checks this. */
static const uint8_t k_fw_entities[4][11] = {
    /* type  name3 (3B)            ver_hi ver_lo build_hi build_lo +pad(2) */
    { 0x00, 'U','\x20','\x20',    0x91, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00 },
    { 0x01, 'B','O','T',          0x14, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00 },
    { 0x02, 'H','W','\x20',       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x04, 'P','I','X',          0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00 },
};

void hidpp_feat_device_fw_version(const hidpp_req_t *req)
{
    switch (req->func) {
    case 0x0: { /* GetEntityCount */
        uint8_t resp[1] = { 4 };
        hidpp_send_response(req, resp, sizeof(resp), HIDPP_DELAY_METADATA());
        return;
    }
    case 0x1: { /* GetFwInfo(idx) */
        uint8_t idx = req->param_len >= 1 ? req->params[0] : 0xFF;
        if (idx >= 4) {
            hidpp_send_error(req, HIDPP2_ERR_OUT_OF_RANGE, HIDPP_DELAY_ERROR());
            return;
        }
        hidpp_send_response(req, k_fw_entities[idx],
                            sizeof(k_fw_entities[idx]), HIDPP_DELAY_METADATA());
        return;
    }
    default:
        hidpp_send_error(req, HIDPP2_ERR_INVALID_FUNCTION_ID, HIDPP_DELAY_ERROR());
        return;
    }
}

/* ── DeviceName (feat 0x04) ────────────────────────────────────────────── */

/* GetNameLength (func=0) -> [0x13]              (19 bytes incl. padding)
 * GetName(off)  (func=1) -> 16B ASCII block starting at offset `off`
 *
 * Real G302 string is "G302 Daedalime" — the typo "Daedalime" (instead
 * of "Daedalus") MUST be preserved byte-for-byte; G HUB matches the
 * literal product string returned over HID++ against its device DB. */
static const char k_device_name[19] =
    { 'G','3','0','2',' ','D','a','e','d','a','l','i','m','e',
      0x00, 0x00, 0x00, 0x00, 0x00 };

void hidpp_feat_device_name(const hidpp_req_t *req)
{
    switch (req->func) {
    case 0x0: {
        uint8_t resp[1] = { (uint8_t)sizeof(k_device_name) };
        hidpp_send_response(req, resp, sizeof(resp), HIDPP_DELAY_METADATA());
        return;
    }
    case 0x1: {
        uint8_t off = req->param_len >= 1 ? req->params[0] : 0;
        if (off >= sizeof(k_device_name)) {
            hidpp_send_error(req, HIDPP2_ERR_OUT_OF_RANGE, HIDPP_DELAY_ERROR());
            return;
        }
        uint8_t block[16] = {0};
        uint8_t avail = (uint8_t)(sizeof(k_device_name) - off);
        uint8_t n = avail > 16 ? 16 : avail;
        memcpy(block, &k_device_name[off], n);
        hidpp_send_response(req, block, sizeof(block), HIDPP_DELAY_METADATA());
        return;
    }
    default:
        hidpp_send_error(req, HIDPP2_ERR_INVALID_FUNCTION_ID, HIDPP_DELAY_ERROR());
        return;
    }
}

/* ── DeviceFriendlyName (feat 0x05) ────────────────────────────────────── */

/* feat 0x05 is shared between two roles on the real G302:
 *   - HID++ 4.2 DeviceFriendlyName (func 0/1/2 — name length / read / default)
 *   - Legacy subID 0x05 RGB write (func=5) — dispatched earlier in
 *     hidpp_handler.c before this handler runs.
 *
 * G HUB probes the non-RGB funcs to validate the feature is "alive"
 * before unlocking the LIGHTSYNC UI write path. Returning
 * INVALID_FEATURE_INDEX from these probes was short-circuiting RGB
 * writes entirely (RGB UI clickable but no SET_REPORT ever sent).
 * Reusing the DeviceName payload — real device returns the same
 * string for both feat 0x04 and feat 0x05 reads. */
void hidpp_feat_device_friendly_name(const hidpp_req_t *req)
{
    switch (req->func) {
    case 0x0: { /* GetFriendlyNameLen */
        uint8_t resp[1] = { (uint8_t)sizeof(k_device_name) };
        hidpp_send_response(req, resp, sizeof(resp), HIDPP_DELAY_METADATA());
        return;
    }
    case 0x1:    /* GetFriendlyName(off) */
    case 0x2: {  /* GetDefaultFriendlyName(off) */
        uint8_t off = req->param_len >= 1 ? req->params[0] : 0;
        if (off >= sizeof(k_device_name)) {
            hidpp_send_error(req, HIDPP2_ERR_OUT_OF_RANGE, HIDPP_DELAY_ERROR());
            return;
        }
        uint8_t block[16] = {0};
        uint8_t avail = (uint8_t)(sizeof(k_device_name) - off);
        uint8_t n = avail > 16 ? 16 : avail;
        memcpy(block, &k_device_name[off], n);
        hidpp_send_response(req, block, sizeof(block), HIDPP_DELAY_METADATA());
        return;
    }
    /* func=5 is intercepted by hidpp_handler.c and routed to
     * hidpp_rgb_dispatch — never reaches here. */
    default: {
        /* Real device silently ACKs unknown funcs with zero payload —
         * an error reply makes G HUB mark the whole feat as broken. */
        uint8_t zero[16] = {0};
        hidpp_send_response(req, zero, sizeof(zero), HIDPP_DELAY_METADATA());
        return;
    }
    }
}

/* ── ReportRate (feat 0x0e) ────────────────────────────────────────────── */

/* G302 real device supports 1000 Hz + 125 Hz (bitmask 0x81). USB
 * bInterval=1 already locks the actual rate to 1 ms; SetReportRate is
 * an ACK-only state mutation — we just remember it for GetReportRate
 * read-back. G HUB sends SetReportRate(1) before every DPI change, so
 * stalling this call would block the DPI slider (LITE §5.4). */

static volatile uint8_t g_report_rate_ms = 0x01U; /* default 1000 Hz */

void hidpp_feat_report_rate(const hidpp_req_t *req)
{
    switch (req->func) {
    case 0x0: { /* GetReportRateList */
        uint8_t resp[1] = { 0x81U };
        hidpp_send_response(req, resp, sizeof(resp), HIDPP_DELAY_METADATA());
        return;
    }
    case 0x1: { /* GetReportRate */
        uint8_t resp[1] = { g_report_rate_ms };
        hidpp_send_response(req, resp, sizeof(resp), HIDPP_DELAY_METADATA());
        return;
    }
    case 0x2: { /* SetReportRate — echo + state */
        if (req->param_len < 1) {
            hidpp_send_error(req, HIDPP2_ERR_INVALID_ARGUMENT, HIDPP_DELAY_ERROR());
            return;
        }
        uint8_t target = req->params[0];
        /* Accept any 1..8 ms; G HUB only sets 1 or 8. */
        if (target == 0 || target > 8U) {
            hidpp_send_error(req, HIDPP2_ERR_OUT_OF_RANGE, HIDPP_DELAY_ERROR());
            return;
        }
        g_report_rate_ms = target;
        uint8_t resp[1] = { target };
        /* LITE §6: real-device delay 4.70 ± 0.23 ms. */
        hidpp_send_response(req, resp, sizeof(resp), HIDPP_DELAY_RATE_SET());
        return;
    }
    default:
        hidpp_send_error(req, HIDPP2_ERR_INVALID_FUNCTION_ID, HIDPP_DELAY_ERROR());
        return;
    }
}

/* ── AdjustableDPI (feat 0x0d) ─────────────────────────────────────────── */

/* Single PixArt sensor; range-step DPI table [200, step=50, 8000] —
 * encoded as [0x00C8, 0xE032, 0x1F40] terminated by 0x0000.
 * Default / current DPI = 0x0640 = 1600. SetSensorDPI echoes the full
 * Long frame back unchanged (LITE §5.3). */

#define DPI_DEFAULT_HI 0x06U
#define DPI_DEFAULT_LO 0x40U

static volatile uint8_t g_dpi_hi = DPI_DEFAULT_HI;
static volatile uint8_t g_dpi_lo = DPI_DEFAULT_LO;

void hidpp_feat_adjustable_dpi(const hidpp_req_t *req)
{
    switch (req->func) {
    case 0x0: { /* GetSensorCount */
        uint8_t resp[1] = { 1 };
        hidpp_send_response(req, resp, sizeof(resp), HIDPP_DELAY_METADATA());
        return;
    }
    case 0x1: { /* GetSensorDPIList(page) */
        /* [sensor_idx][0x00C8][0xE032][0x1F40][0x0000 terminator]
         *   sensor    200      step50   8000   end                    */
        uint8_t resp[11] = {
            0x00,                       /* sensor index */
            0x00, 0xC8,                 /* 200 (start)    */
            0xE0, 0x32,                 /* step encoding: 0xE0|step=50 */
            0x1F, 0x40,                 /* 8000 (end)     */
            0x00, 0x00,                 /* terminator     */
            0x00, 0x00,
        };
        hidpp_send_response(req, resp, sizeof(resp), HIDPP_DELAY_METADATA());
        return;
    }
    case 0x2: { /* GetSensorDPI */
        uint8_t resp[5] = {
            0x00,
            g_dpi_hi, g_dpi_lo,
            DPI_DEFAULT_HI, DPI_DEFAULT_LO,
        };
        hidpp_send_response(req, resp, sizeof(resp), HIDPP_DELAY_METADATA());
        return;
    }
    case 0x3: { /* SetSensorDPI — echo 20B Long with the new DPI */
        if (req->param_len < 3) {
            hidpp_send_error(req, HIDPP2_ERR_INVALID_ARGUMENT, HIDPP_DELAY_ERROR());
            return;
        }
        g_dpi_hi = req->params[1];
        g_dpi_lo = req->params[2];
        /* Echo params: [sensor][hi][lo] — pad the rest to 16B Long. */
        uint8_t resp[16] = {0};
        resp[0] = req->params[0];
        resp[1] = g_dpi_hi;
        resp[2] = g_dpi_lo;
        /* LITE §6: SetSensorDPI 4.81 ± 0.02 ms (σ small enough that DWT
         * noise alone suffices — no extra jitter). */
        hidpp_send_response(req, resp, sizeof(resp), HIDPP_DELAY_DPI_SET());
        return;
    }
    default:
        hidpp_send_error(req, HIDPP2_ERR_INVALID_FUNCTION_ID, HIDPP_DELAY_ERROR());
        return;
    }
}

/* ── OnboardProfiles (feat 0x0f) ───────────────────────────────────────── */

/* GetProfileDirectory payload — verbatim from LITE §5.5:
 *   01 01 01 01 01  06  10 01  00 0a  01  00...
 *   capabilities    num profile_size  default_dpi=10  default_profile=1
 *                       272 BE          (idx into bank0 DPI table)
 *
 * Default mode = 0x01 (onboard) matching real device factory default. */

static volatile uint8_t g_onboard_mode = 0x01U;

static const uint8_t k_profile_directory[12] = {
    0x01, 0x01, 0x01, 0x01, 0x01,
    0x06,                /* num_profiles = 6 */
    0x10, 0x01,          /* profile_size = 0x0110 BE = 272 */
    0x00, 0x0A,          /* default_dpi_idx = 10 */
    0x01,                /* default_profile = 1 */
    0x00,
};

/* Flash image — 2 banks × 16 pages × 16 bytes. Bank 0 page 0 holds the
 * default DPI table (LITE §5.5):
 *   80 01 00 02  a4 01  48 03  3c 06  78 0c  00 00  ff ff
 * All other pages are unprogrammed flash, which the real device returns
 * as 0xFF. We serve the whole image from a static const array since the
 * real STM32 flash layout is irrelevant — G HUB only walks it via
 * MemoryRead16B. Non-DPI page bodies are filled to 0xFF at runtime
 * (lazy init in hidpp_feat_onboard_profiles). */
#define FLASH_BANKS  2U
#define FLASH_PAGES  16U
#define FLASH_BLOCK  16U

static uint8_t g_flash_image[FLASH_BANKS][FLASH_PAGES][FLASH_BLOCK];
static bool    g_flash_initialized = false;

static const uint8_t k_flash_bank0_page0[FLASH_BLOCK] = {
    0x80, 0x01, 0x00, 0x02,
    0xA4, 0x01, 0x48, 0x03,
    0x3C, 0x06, 0x78, 0x0C,
    0x00, 0x00, 0xFF, 0xFF,
};

static void flash_image_init(void)
{
    if (g_flash_initialized) return;
    memset(g_flash_image, 0xFF, sizeof(g_flash_image));
    memcpy(g_flash_image[0][0], k_flash_bank0_page0, FLASH_BLOCK);
    g_flash_initialized = true;
}

void hidpp_feat_onboard_profiles(const hidpp_req_t *req)
{
    switch (req->func) {
    case 0x0: { /* GetProfileDirectory */
        hidpp_send_response(req, k_profile_directory,
                            sizeof(k_profile_directory), HIDPP_DELAY_METADATA());
        return;
    }
    case 0x1: { /* SetMode(mode) — must echo and update state */
        if (req->param_len < 1) {
            hidpp_send_error(req, HIDPP2_ERR_INVALID_ARGUMENT, HIDPP_DELAY_ERROR());
            return;
        }
        uint8_t mode = req->params[0];
        if (mode != 0x01U && mode != 0x02U) {
            hidpp_send_error(req, HIDPP2_ERR_OUT_OF_RANGE, HIDPP_DELAY_ERROR());
            return;
        }
        g_onboard_mode = mode;
        uint8_t resp[1] = { mode };
        hidpp_send_response(req, resp, sizeof(resp), HIDPP_DELAY_METADATA());
        return;
    }
    case 0x2: { /* GetMode */
        uint8_t resp[1] = { g_onboard_mode };
        hidpp_send_response(req, resp, sizeof(resp), HIDPP_DELAY_METADATA());
        return;
    }
    case 0x4: { /* GetActiveProfile -> 00 01 */
        uint8_t resp[2] = { 0x00, 0x01 };
        hidpp_send_response(req, resp, sizeof(resp), HIDPP_DELAY_METADATA());
        return;
    }
    case 0x5: { /* MemoryRead16B(bank, ?, ?, page, ?, ?, ?) */
        /* Request layout (Long): [bank][0x01][0x00][page_offset][...]
         * bank ∈ {0,1}; page_offset increments by 0x10 across pages. */
        if (req->param_len < 4) {
            hidpp_send_error(req, HIDPP2_ERR_INVALID_ARGUMENT, HIDPP_DELAY_ERROR());
            return;
        }
        flash_image_init();
        uint8_t bank = req->params[0];
        uint8_t page = (uint8_t)(req->params[3] >> 4); /* 0x00,0x10,0x20.. -> 0..15 */
        if (bank >= FLASH_BANKS || page >= FLASH_PAGES) {
            /* Out-of-range read — return 0xFF block like real device. */
            uint8_t empty[FLASH_BLOCK];
            memset(empty, 0xFF, sizeof(empty));
            hidpp_send_response(req, empty, sizeof(empty), HIDPP_DELAY_METADATA());
            return;
        }
        hidpp_send_response(req, g_flash_image[bank][page], FLASH_BLOCK, HIDPP_DELAY_METADATA());
        return;
    }
    case 0xB: { /* GetFlashStatus -> 0x03 (ready) */
        uint8_t resp[1] = { 0x03 };
        hidpp_send_response(req, resp, sizeof(resp), HIDPP_DELAY_METADATA());
        return;
    }
    default:
        hidpp_send_error(req, HIDPP2_ERR_INVALID_FUNCTION_ID, HIDPP_DELAY_ERROR());
        return;
    }
}

/* ── MouseButtonSpy (feat 0x10) ────────────────────────────────────────── */

/* G302 captures (UsbCap/RGB.pcapng) show this feature responds to G HUB
 * enum (10 ff ff 10 .. 06 .. probes) and is the channel the real device
 * uses to push button events as `11 ff 10 00 00 01 00 00...` (down) and
 * `11 ff 10 00 00 00 00 00...` (up). LITE §5.6 captures three responses
 * the real device gives to G HUB's read probes:
 *
 *   func=0 GetButtonInfo    -> 01 01 00 40 00...
 *   func=1 GetButtonReport  -> 01 01 00 80 00...
 *   func=3 (unknown read)   -> ff ff ff ff ff ff... (16x 0xFF)
 *
 * Returning these byte-for-byte unblocks any G HUB UI gating on the
 * feature being "live" rather than absent. Func=4 (and any others we
 * have not captured) return INVALID_FUNCTION so G HUB knows the feature
 * exists but a particular call is unsupported — a different signal from
 * INVALID_FEATURE_INDEX. */
void hidpp_feat_mouse_button_spy(const hidpp_req_t *req)
{
    switch (req->func) {
    case 0x0: { /* GetButtonInfo */
        uint8_t resp[5] = { 0x01, 0x01, 0x00, 0x40, 0x00 };
        hidpp_send_response(req, resp, sizeof(resp), HIDPP_DELAY_METADATA());
        return;
    }
    case 0x1: { /* GetButtonReport */
        uint8_t resp[5] = { 0x01, 0x01, 0x00, 0x80, 0x00 };
        hidpp_send_response(req, resp, sizeof(resp), HIDPP_DELAY_METADATA());
        return;
    }
    case 0x3: { /* Unknown read — real device returns 16x 0xFF */
        uint8_t resp[16];
        memset(resp, 0xFF, sizeof(resp));
        hidpp_send_response(req, resp, sizeof(resp), HIDPP_DELAY_METADATA());
        return;
    }
    default:
        hidpp_send_error(req, HIDPP2_ERR_INVALID_FUNCTION_ID, HIDPP_DELAY_ERROR());
        return;
    }
}
