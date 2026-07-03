/**
 * USBD_G302 — Logitech G302 Daedalus Apex clone (2-interface HID).
 *
 * Built on ST's USB Device Library (non-composite mode — we own the whole
 * configuration descriptor). Mirrors cap10.pcapng byte-for-byte:
 *
 *   IF0  Boot Mouse                 EP 0x81 IN  wMaxPacket  8  bInterval 1
 *   IF1  Multimedia + HID++ vendor  EP 0x82 IN  wMaxPacket 20  bInterval 1
 *
 * HID++ (Logitech proprietary, report IDs 0x10/0x11) arrives host→device on
 * EP0 SET_REPORT — handled by EP0_RxReady. Device→host responses go out on
 * EP 0x82 IN via USBD_G302_SendHidpp().
 *
 * GET_REPORT is answered with a single zero byte per project-mouse-mover-
 * get-report-trap (defensive — ST's USBD path differs from TinyUSB DWC2 but
 * we keep the policy).
 */

#include <string.h>

#include "usbd_g302.h"
#include "usbd_ctlreq.h"
#include "usb_desc_g302.h"

/* Diagnostic counters — defined further down. */
extern volatile uint32_t g302_datain_mouse_cnt;
extern volatile uint32_t g302_datain_mm_cnt;

/* ── Forward declarations ─────────────────────────────────────────────────── */
static uint8_t USBD_G302_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_G302_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_G302_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t USBD_G302_EP0_RxReady(USBD_HandleTypeDef *pdev);
static uint8_t USBD_G302_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_G302_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t *USBD_G302_GetFSCfgDesc(uint16_t *length);
static uint8_t *USBD_G302_GetHSCfgDesc(uint16_t *length);
static uint8_t *USBD_G302_GetOtherSpeedCfgDesc(uint16_t *length);
static uint8_t *USBD_G302_GetDeviceQualifierDesc(uint16_t *length);

USBD_ClassTypeDef USBD_G302 = {
    USBD_G302_Init,
    USBD_G302_DeInit,
    USBD_G302_Setup,
    NULL,                       /* EP0_TxSent */
    USBD_G302_EP0_RxReady,      /* EP0_RxReady — completes SET_REPORT for HID++ */
    USBD_G302_DataIn,
    USBD_G302_DataOut,
    NULL,                       /* SOF */
    NULL,                       /* IsoINIncomplete */
    NULL,                       /* IsoOUTIncomplete */
    USBD_G302_GetHSCfgDesc,
    USBD_G302_GetFSCfgDesc,
    USBD_G302_GetOtherSpeedCfgDesc,
    USBD_G302_GetDeviceQualifierDesc,
};

/* ── Configuration descriptor ─────────────────────────────────────────────
 *
 *   bMaxPower : 150 = 300 mA / 2  — matches real Logitech G302
 *   bmAttributes : 0xA0 — bus-powered + remote wakeup (matches real)
 *   Total length : 9 + (9+9+7) + (9+9+7) = 59 bytes (0x3B)
 */
#define G302_CFG_TOTAL_LEN 59U

__ALIGN_BEGIN static uint8_t USBD_G302_CfgDesc[G302_CFG_TOTAL_LEN] __ALIGN_END = {
    /* Configuration descriptor (9) */
    0x09,                                /* bLength */
    USB_DESC_TYPE_CONFIGURATION,         /* bDescriptorType */
    LOBYTE(G302_CFG_TOTAL_LEN),          /* wTotalLength */
    HIBYTE(G302_CFG_TOTAL_LEN),
    G302_ITF_COUNT,                      /* bNumInterfaces */
    0x01,                                /* bConfigurationValue */
    0x00,                                /* iConfiguration */
    0xA0,                                /* bmAttributes (bus-powered + remote wakeup) */
    150,                                 /* bMaxPower 300 mA / 2 */

    /* ── Interface 0 — Boot Mouse ─────────────────────────────────────── */
    0x09, USB_DESC_TYPE_INTERFACE,
    G302_ITF_MOUSE, 0x00, 0x01,          /* bInterfaceNumber, alt, bNumEPs */
    0x03, 0x01, 0x02, 0x00,              /* HID, boot, mouse, iInterface */
    /* HID descriptor (9) */
    0x09, G302_HID_DESC_TYPE,
    0x11, 0x01,                          /* bcdHID 1.11 */
    0x00, 0x01,                          /* country, bNumDescriptors */
    G302_HID_REPORT_DESC_TYPE,
    LOBYTE(G302_HID_REPORT_DESC_IF0_LEN),
    HIBYTE(G302_HID_REPORT_DESC_IF0_LEN),
    /* Endpoint 0x81 IN (7) */
    0x07, USB_DESC_TYPE_ENDPOINT,
    G302_EP_MOUSE_IN, 0x03,              /* interrupt */
    LOBYTE(G302_EP_MOUSE_SIZE), HIBYTE(G302_EP_MOUSE_SIZE),
    G302_BINTERVAL,

    /* ── Interface 1 — Multimedia + HID++ vendor ──────────────────────── */
    0x09, USB_DESC_TYPE_INTERFACE,
    G302_ITF_MM_HIDPP, 0x00, 0x01,       /* one EP — IN only; HID++ OUT via EP0 */
    0x03, 0x00, 0x00, 0x00,              /* HID, no boot, no protocol */
    0x09, G302_HID_DESC_TYPE,
    0x11, 0x01,
    0x00, 0x01,
    G302_HID_REPORT_DESC_TYPE,
    LOBYTE(G302_HID_REPORT_DESC_IF1_LEN),
    HIBYTE(G302_HID_REPORT_DESC_IF1_LEN),
    0x07, USB_DESC_TYPE_ENDPOINT,
    G302_EP_MM_IN, 0x03,
    LOBYTE(G302_EP_MM_SIZE), HIBYTE(G302_EP_MM_SIZE),
    G302_BINTERVAL,
};

__ALIGN_BEGIN static uint8_t USBD_G302_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END = {
    USB_LEN_DEV_QUALIFIER_DESC,
    USB_DESC_TYPE_DEVICE_QUALIFIER,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00,
};

/* HID class descriptors per interface (synthesized for GET_DESCRIPTOR(HID)). */
static const uint8_t hid_desc_if0[9] = {
    0x09, G302_HID_DESC_TYPE, 0x11, 0x01, 0x00, 0x01, G302_HID_REPORT_DESC_TYPE,
    LOBYTE(G302_HID_REPORT_DESC_IF0_LEN),
    HIBYTE(G302_HID_REPORT_DESC_IF0_LEN),
};
static const uint8_t hid_desc_if1[9] = {
    0x09, G302_HID_DESC_TYPE, 0x11, 0x01, 0x00, 0x01, G302_HID_REPORT_DESC_TYPE,
    LOBYTE(G302_HID_REPORT_DESC_IF1_LEN),
    HIBYTE(G302_HID_REPORT_DESC_IF1_LEN),
};

/* ── Lifecycle ────────────────────────────────────────────────────────── */

static uint8_t USBD_G302_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx) {
    (void)cfgidx;
    USBD_G302_HandleTypeDef *h =
        (USBD_G302_HandleTypeDef *)USBD_malloc(sizeof(USBD_G302_HandleTypeDef));
    if (h == NULL) {
        pdev->pClassDataCmsit[pdev->classId] = NULL;
        return (uint8_t)USBD_EMEM;
    }
    pdev->pClassDataCmsit[pdev->classId] = (void *)h;
    pdev->pClassData = h;

    memset(h, 0, sizeof(*h));
    h->mouse_state = G302_STATE_IDLE;
    h->mm_state    = G302_STATE_IDLE;

    pdev->ep_in[G302_EP_MOUSE_IN & 0x0FU].bInterval = G302_BINTERVAL;
    pdev->ep_in[G302_EP_MM_IN    & 0x0FU].bInterval = G302_BINTERVAL;

    USBD_LL_OpenEP(pdev, G302_EP_MOUSE_IN, USBD_EP_TYPE_INTR, G302_EP_MOUSE_SIZE);
    USBD_LL_OpenEP(pdev, G302_EP_MM_IN,    USBD_EP_TYPE_INTR, G302_EP_MM_SIZE);
    pdev->ep_in[G302_EP_MOUSE_IN & 0x0FU].is_used = 1U;
    pdev->ep_in[G302_EP_MM_IN    & 0x0FU].is_used = 1U;

    return (uint8_t)USBD_OK;
}

static uint8_t USBD_G302_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx) {
    (void)cfgidx;
    USBD_LL_CloseEP(pdev, G302_EP_MOUSE_IN);
    USBD_LL_CloseEP(pdev, G302_EP_MM_IN);
    pdev->ep_in[G302_EP_MOUSE_IN & 0x0FU].is_used = 0U;
    pdev->ep_in[G302_EP_MM_IN    & 0x0FU].is_used = 0U;

    if (pdev->pClassDataCmsit[pdev->classId] != NULL) {
        USBD_free(pdev->pClassDataCmsit[pdev->classId]);
        pdev->pClassDataCmsit[pdev->classId] = NULL;
    }
    return (uint8_t)USBD_OK;
}

/* ── Setup ────────────────────────────────────────────────────────────── */

static const uint8_t *hid_desc_for_itf(uint8_t itf) {
    switch (itf) {
        case G302_ITF_MOUSE:    return hid_desc_if0;
        case G302_ITF_MM_HIDPP: return hid_desc_if1;
        default:                return NULL;
    }
}

static const uint8_t *report_desc_for_itf(uint8_t itf, uint16_t *len) {
    switch (itf) {
        case G302_ITF_MOUSE:
            *len = G302_HID_REPORT_DESC_IF0_LEN;
            return G302_HID_REPORT_DESC_IF0;
        case G302_ITF_MM_HIDPP:
            *len = G302_HID_REPORT_DESC_IF1_LEN;
            return G302_HID_REPORT_DESC_IF1;
        default:
            *len = 0;
            return NULL;
    }
}

static uint8_t USBD_G302_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req) {
    USBD_G302_HandleTypeDef *h =
        (USBD_G302_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
    if (h == NULL) return (uint8_t)USBD_FAIL;

    uint8_t itf = (uint8_t)(req->wIndex & 0xFFU);
    if (itf >= G302_ITF_COUNT) {
        USBD_CtlError(pdev, req);
        return (uint8_t)USBD_FAIL;
    }

    uint16_t status_info = 0U;
    USBD_StatusTypeDef ret = USBD_OK;

    switch (req->bmRequest & USB_REQ_TYPE_MASK) {
    case USB_REQ_TYPE_CLASS:
        switch (req->bRequest) {
        case G302_HID_REQ_SET_PROTOCOL:
            h->protocol[itf] = (uint8_t)req->wValue;
            break;
        case G302_HID_REQ_GET_PROTOCOL:
            USBD_CtlSendData(pdev, &h->protocol[itf], 1U);
            break;
        case G302_HID_REQ_SET_IDLE:
            h->idle_state[itf] = (uint8_t)(req->wValue >> 8);
            break;
        case G302_HID_REQ_GET_IDLE:
            USBD_CtlSendData(pdev, &h->idle_state[itf], 1U);
            break;
        case G302_HID_REQ_SET_REPORT: {
            /* wValue high byte = Report Type, low byte = Report ID.
             * HID++ Short(0x10)/Long(0x11) arrive here on IF1. Stage the
             * OUT data into ep0_out_buf; EP0_RxReady will dispatch. */
            uint16_t n = req->wLength;
            if (n > sizeof(h->ep0_out_buf)) n = sizeof(h->ep0_out_buf);
            h->ep0_out_len = n;
            h->ep0_out_rid = (uint8_t)(req->wValue & 0xFFU);
            USBD_CtlPrepareRx(pdev, h->ep0_out_buf, n);
            break;
        }
        case G302_HID_REQ_GET_REPORT: {
            /* Defensive: never return real data here.
             * See project-mouse-mover-get-report-trap. */
            static uint8_t zero = 0;
            uint16_t n = req->wLength ? 1U : 0U;
            USBD_CtlSendData(pdev, &zero, n);
            break;
        }
        default:
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
            break;
        }
        break;

    case USB_REQ_TYPE_STANDARD:
        switch (req->bRequest) {
        case USB_REQ_GET_STATUS:
            if (pdev->dev_state == USBD_STATE_CONFIGURED) {
                USBD_CtlSendData(pdev, (uint8_t *)&status_info, 2U);
            } else {
                USBD_CtlError(pdev, req);
                ret = USBD_FAIL;
            }
            break;

        case USB_REQ_GET_DESCRIPTOR: {
            uint8_t type = (uint8_t)(req->wValue >> 8);
            if (type == G302_HID_REPORT_DESC_TYPE) {
                uint16_t rlen = 0;
                const uint8_t *p = report_desc_for_itf(itf, &rlen);
                if (p == NULL) { USBD_CtlError(pdev, req); ret = USBD_FAIL; break; }
                uint16_t n = (req->wLength < rlen) ? req->wLength : rlen;
                USBD_CtlSendData(pdev, (uint8_t *)p, n);
            } else if (type == G302_HID_DESC_TYPE) {
                const uint8_t *p = hid_desc_for_itf(itf);
                if (p == NULL) { USBD_CtlError(pdev, req); ret = USBD_FAIL; break; }
                uint16_t n = (req->wLength < 9U) ? req->wLength : 9U;
                USBD_CtlSendData(pdev, (uint8_t *)p, n);
            } else {
                USBD_CtlError(pdev, req);
                ret = USBD_FAIL;
            }
            break;
        }

        case USB_REQ_GET_INTERFACE:
            if (pdev->dev_state == USBD_STATE_CONFIGURED) {
                USBD_CtlSendData(pdev, &h->alt_setting[itf], 1U);
            } else {
                USBD_CtlError(pdev, req);
                ret = USBD_FAIL;
            }
            break;

        case USB_REQ_SET_INTERFACE:
            if (pdev->dev_state == USBD_STATE_CONFIGURED) {
                h->alt_setting[itf] = (uint8_t)req->wValue;
            } else {
                USBD_CtlError(pdev, req);
                ret = USBD_FAIL;
            }
            break;

        case USB_REQ_CLEAR_FEATURE:
            break;

        default:
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
            break;
        }
        break;

    default:
        USBD_CtlError(pdev, req);
        ret = USBD_FAIL;
        break;
    }
    return (uint8_t)ret;
}

/* ── EP0 OUT data stage complete (SET_REPORT body arrived) ────────────── */

static uint8_t USBD_G302_EP0_RxReady(USBD_HandleTypeDef *pdev) {
    USBD_G302_HandleTypeDef *h =
        (USBD_G302_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
    if (h == NULL || h->ep0_out_len == 0) return (uint8_t)USBD_OK;

    /* Only forward HID++ packets — ignore other SET_REPORT (e.g. keyboard LEDs). */
    if (h->ep0_out_rid == G302_RID_HIDPP_SHORT ||
        h->ep0_out_rid == G302_RID_HIDPP_LONG) {
        USBD_G302_HidppReceive(h->ep0_out_buf, h->ep0_out_len);
    }
    h->ep0_out_len = 0;
    return (uint8_t)USBD_OK;
}

/* ── Data IN/OUT ──────────────────────────────────────────────────────── */

static uint8_t USBD_G302_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum) {
    USBD_G302_HandleTypeDef *h =
        (USBD_G302_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
    if (h == NULL) return (uint8_t)USBD_FAIL;

    switch (epnum & 0x0FU) {
    case (G302_EP_MOUSE_IN & 0x0FU):
        h->mouse_state = G302_STATE_IDLE;
        g302_mouse_ep_ready = 1;
        g302_datain_mouse_cnt++;
        break;
    case (G302_EP_MM_IN & 0x0FU):
        h->mm_state = G302_STATE_IDLE;
        g302_datain_mm_cnt++;
        break;
    default:
        break;
    }
    return (uint8_t)USBD_OK;
}

static uint8_t USBD_G302_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum) {
    (void)pdev; (void)epnum;
    /* No interrupt OUT endpoints — HID++ host→device travels via EP0. */
    return (uint8_t)USBD_OK;
}

__attribute__((weak)) void USBD_G302_HidppReceive(const uint8_t *data, uint16_t len) {
    (void)data; (void)len;
}

/* ── Public API ───────────────────────────────────────────────────────── */

/* Diagnostic counters (read via mdw at fixed SRAM offsets). */
volatile uint32_t g302_send_entry_cnt        = 0;
volatile uint32_t g302_send_pdev_null        = 0;
volatile uint32_t g302_send_h_null           = 0;
volatile uint32_t g302_send_devstate_bad     = 0;
volatile uint32_t g302_send_state_busy       = 0;
volatile uint32_t g302_send_transmit_called  = 0;
volatile uint32_t g302_ll_transmit_rc        = 0xFFFFFFFFu;
volatile uint32_t g302_datain_mouse_cnt      = 0;
volatile uint32_t g302_datain_mm_cnt         = 0;
volatile uint32_t g302_hidpp_send_entry_cnt  = 0;
volatile uint32_t g302_hidpp_ll_transmit_rc  = 0xFFFFFFFFu;
volatile uint8_t  g302_mouse_ep_ready        = 1;  /* SOF-sync: set by DataIn ISR, cleared by main loop */

uint8_t USBD_G302_SendMouseReport(USBD_HandleTypeDef *pdev,
                                  uint8_t *report, uint16_t len) {
    g302_send_entry_cnt++;
    if (pdev == NULL) { g302_send_pdev_null++; return (uint8_t)USBD_FAIL; }
    USBD_G302_HandleTypeDef *h =
        (USBD_G302_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
    if (h == NULL) { g302_send_h_null++; return (uint8_t)USBD_FAIL; }
    if (pdev->dev_state != USBD_STATE_CONFIGURED) {
        g302_send_devstate_bad++;
        return (uint8_t)USBD_FAIL;
    }
    if (h->mouse_state != G302_STATE_IDLE) {
        g302_send_state_busy++;
        return (uint8_t)USBD_BUSY;
    }

    h->mouse_state = G302_STATE_BUSY;
    g302_send_transmit_called++;
    g302_ll_transmit_rc =
        (uint32_t)USBD_LL_Transmit(pdev, G302_EP_MOUSE_IN, report, len);
    return (uint8_t)USBD_OK;
}

uint8_t USBD_G302_SendHidpp(USBD_HandleTypeDef *pdev,
                            uint8_t *report, uint16_t len) {
    g302_hidpp_send_entry_cnt++;
    if (pdev == NULL) return (uint8_t)USBD_FAIL;
    USBD_G302_HandleTypeDef *h =
        (USBD_G302_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
    if (h == NULL) return (uint8_t)USBD_FAIL;
    if (pdev->dev_state != USBD_STATE_CONFIGURED) return (uint8_t)USBD_FAIL;
    if (h->mm_state != G302_STATE_IDLE) return (uint8_t)USBD_BUSY;

    h->mm_state = G302_STATE_BUSY;
    g302_hidpp_ll_transmit_rc =
        (uint32_t)USBD_LL_Transmit(pdev, G302_EP_MM_IN, report, len);
    return (uint8_t)USBD_OK;
}

/* ── Descriptor getters ───────────────────────────────────────────────── */

static uint8_t *USBD_G302_GetFSCfgDesc(uint16_t *length) {
    *length = sizeof(USBD_G302_CfgDesc);
    return USBD_G302_CfgDesc;
}
static uint8_t *USBD_G302_GetHSCfgDesc(uint16_t *length) {
    *length = sizeof(USBD_G302_CfgDesc);
    return USBD_G302_CfgDesc;
}
static uint8_t *USBD_G302_GetOtherSpeedCfgDesc(uint16_t *length) {
    *length = sizeof(USBD_G302_CfgDesc);
    return USBD_G302_CfgDesc;
}
static uint8_t *USBD_G302_GetDeviceQualifierDesc(uint16_t *length) {
    *length = sizeof(USBD_G302_DeviceQualifierDesc);
    return USBD_G302_DeviceQualifierDesc;
}
