/**
 * Logitech G302 Daedalus Apex — verbatim HID Report Descriptors.
 *
 * Captured from real device (VID 0x046D / PID 0xC07F, bcdDevice 0x9100)
 * via USBPcap (cap10.pcapng frames 2906, 2926). Byte-for-byte copies —
 * do not "clean up" or reformat.
 *
 * 2-interface configuration:
 *   IF0  Mouse (Boot)        67 B   EP 0x81 IN  wMaxPacket  8  bInterval 1
 *   IF1  Multimedia + HID++  151 B  EP 0x82 IN  wMaxPacket 20  bInterval 1
 *
 * IF0 mouse report — 8 bytes, NO report-id prefix:
 *   [btn_lo][btn_hi][x_lo][x_hi][y_lo][y_hi][wheel_i8][pan_i8]
 *   16-bit button field; X/Y are 16-bit relative; wheel/pan are 8-bit signed.
 *
 * IF1 carries 5 top-level collections distinguished by report ID:
 *   0x01  Keyboard  8 B  {modifier, _, key[6]}
 *   0x03  Consumer  5 B  {usage[2] x 2}                  (Generic Consumer)
 *   0x04  SysCtl    1 B  {2-bit Power/Sleep/Wake + 6-bit pad}
 *   0x10  HID++ Short  7 B  vendor 0xFF00              ← anti-cheat probe
 *   0x11  HID++ Long  20 B  vendor 0xFF00              ← long report
 */
#ifndef USB_DESC_G302_H
#define USB_DESC_G302_H

#include <stdint.h>

/* ── Interface 0 — Boot Mouse (67 bytes) ──────────────────────────────── */
#define G302_HID_REPORT_DESC_IF0_LEN  67U

static const uint8_t G302_HID_REPORT_DESC_IF0[G302_HID_REPORT_DESC_IF0_LEN] = {
    0x05, 0x01,        /* Usage Page (Generic Desktop)       */
    0x09, 0x02,        /* Usage (Mouse)                      */
    0xA1, 0x01,        /* Collection (Application)           */
    0x09, 0x01,        /*   Usage (Pointer)                  */
    0xA1, 0x00,        /*   Collection (Physical)            */
    0x05, 0x09,        /*     Usage Page (Button)            */
    0x19, 0x01,        /*     Usage Minimum (Button 1)       */
    0x29, 0x10,        /*     Usage Maximum (Button 16)      */
    0x15, 0x00,        /*     Logical Minimum (0)            */
    0x25, 0x01,        /*     Logical Maximum (1)            */
    0x95, 0x10,        /*     Report Count (16)              */
    0x75, 0x01,        /*     Report Size (1)                */
    0x81, 0x02,        /*     Input (Data,Var,Abs)           */
    0x05, 0x01,        /*     Usage Page (Generic Desktop)   */
    0x16, 0x01, 0x80,  /*     Logical Minimum (-32767)       */
    0x26, 0xFF, 0x7F,  /*     Logical Maximum (32767)        */
    0x75, 0x10,        /*     Report Size (16)               */
    0x95, 0x02,        /*     Report Count (2)               */
    0x09, 0x30,        /*     Usage (X)                      */
    0x09, 0x31,        /*     Usage (Y)                      */
    0x81, 0x06,        /*     Input (Data,Var,Rel)           */
    0x15, 0x81,        /*     Logical Minimum (-127)         */
    0x25, 0x7F,        /*     Logical Maximum (127)          */
    0x75, 0x08,        /*     Report Size (8)                */
    0x95, 0x01,        /*     Report Count (1)               */
    0x09, 0x38,        /*     Usage (Wheel)                  */
    0x81, 0x06,        /*     Input (Data,Var,Rel)           */
    0x05, 0x0C,        /*     Usage Page (Consumer)          */
    0x0A, 0x38, 0x02,  /*     Usage (AC Pan)                 */
    0x95, 0x01,        /*     Report Count (1)               */
    0x81, 0x06,        /*     Input (Data,Var,Rel)           */
    0xC0,              /*   End Collection                   */
    0xC0,              /* End Collection                     */
};

/* ── Interface 1 — Multimedia + HID++ vendor (151 bytes) ──────────────── */
#define G302_HID_REPORT_DESC_IF1_LEN  151U

static const uint8_t G302_HID_REPORT_DESC_IF1[G302_HID_REPORT_DESC_IF1_LEN] = {
    /* ── Keyboard (Report ID 0x01, 8 B) ─────────────────────────────── */
    0x05, 0x01,        /* Usage Page (Generic Desktop)               */
    0x09, 0x06,        /* Usage (Keyboard)                           */
    0xA1, 0x01,        /* Collection (Application)                   */
    0x85, 0x01,        /*   Report ID (1)                            */
    0x05, 0x07,        /*   Usage Page (Keyboard/Keypad)             */
    0x19, 0xE0,        /*   Usage Min (Keyboard LeftControl)         */
    0x29, 0xE7,        /*   Usage Max (Keyboard Right GUI)           */
    0x15, 0x00,        /*   Logical Min (0)                          */
    0x25, 0x01,        /*   Logical Max (1)                          */
    0x75, 0x01,        /*   Report Size (1)                          */
    0x95, 0x08,        /*   Report Count (8)                         */
    0x81, 0x02,        /*   Input (Data,Var,Abs) — modifier bits     */
    0x81, 0x03,        /*   Input (Const,Var,Abs) — reserved byte    */
    0x95, 0x06,        /*   Report Count (6)                         */
    0x75, 0x08,        /*   Report Size (8)                          */
    0x15, 0x00,        /*   Logical Min (0)                          */
    0x26, 0xA4, 0x00,  /*   Logical Max (164)                        */
    0x19, 0x00,        /*   Usage Min (0)                            */
    0x2A, 0xA4, 0x00,  /*   Usage Max (164)                          */
    0x81, 0x00,        /*   Input (Data,Ary,Abs) — 6 key slots       */
    0xC0,              /* End Collection                             */

    /* ── Consumer Control (Report ID 0x03, 5 B) ─────────────────────── */
    0x05, 0x0C,        /* Usage Page (Consumer)                      */
    0x09, 0x01,        /* Usage (Consumer Control)                   */
    0xA1, 0x01,        /* Collection (Application)                   */
    0x85, 0x03,        /*   Report ID (3)                            */
    0x75, 0x10,        /*   Report Size (16)                         */
    0x95, 0x02,        /*   Report Count (2)                         */
    0x15, 0x01,        /*   Logical Min (1)                          */
    0x26, 0x8C, 0x02,  /*   Logical Max (652)                        */
    0x19, 0x01,        /*   Usage Min (1)                            */
    0x2A, 0x8C, 0x02,  /*   Usage Max (652)                          */
    0x81, 0x00,        /*   Input (Data,Ary,Abs)                     */
    0xC0,              /* End Collection                             */

    /* ── System Control (Report ID 0x04, 1 B) ───────────────────────── */
    0x05, 0x01,        /* Usage Page (Generic Desktop)               */
    0x09, 0x80,        /* Usage (System Control)                     */
    0xA1, 0x01,        /* Collection (Application)                   */
    0x85, 0x04,        /*   Report ID (4)                            */
    0x75, 0x02,        /*   Report Size (2)                          */
    0x95, 0x01,        /*   Report Count (1)                         */
    0x15, 0x01,        /*   Logical Min (1)                          */
    0x25, 0x03,        /*   Logical Max (3)                          */
    0x09, 0x82,        /*   Usage (System Sleep)                     */
    0x09, 0x81,        /*   Usage (System Power Down)                */
    0x09, 0x83,        /*   Usage (System Wake Up)                   */
    0x81, 0x60,        /*   Input (Data,Ary,Abs,NoPref,Null)         */
    0x75, 0x06,        /*   Report Size (6)                          */
    0x81, 0x03,        /*   Input (Const,Var,Abs) — padding          */
    0xC0,              /* End Collection                             */

    /* ── HID++ Short (Report ID 0x10) — 7 B vendor ──────────────────── */
    0x06, 0x00, 0xFF,  /* Usage Page (Vendor-Defined 0xFF00)         */
    0x09, 0x01,        /* Usage (Vendor 0x01)                        */
    0xA1, 0x01,        /* Collection (Application)                   */
    0x85, 0x10,        /*   Report ID (16)                           */
    0x75, 0x08,        /*   Report Size (8)                          */
    0x95, 0x06,        /*   Report Count (6)                         */
    0x15, 0x00,        /*   Logical Min (0)                          */
    0x26, 0xFF, 0x00,  /*   Logical Max (255)                        */
    0x09, 0x01,        /*   Usage (Vendor 0x01)                      */
    0x81, 0x00,        /*   Input (Data,Ary,Abs)                     */
    0x09, 0x01,        /*   Usage (Vendor 0x01)                      */
    0x91, 0x00,        /*   Output (Data,Ary,Abs)                    */
    0xC0,              /* End Collection                             */

    /* ── HID++ Long (Report ID 0x11) — 20 B vendor ──────────────────── */
    0x06, 0x00, 0xFF,  /* Usage Page (Vendor-Defined 0xFF00)         */
    0x09, 0x02,        /* Usage (Vendor 0x02)                        */
    0xA1, 0x01,        /* Collection (Application)                   */
    0x85, 0x11,        /*   Report ID (17)                           */
    0x75, 0x08,        /*   Report Size (8)                          */
    0x95, 0x13,        /*   Report Count (19)                        */
    0x15, 0x00,        /*   Logical Min (0)                          */
    0x26, 0xFF, 0x00,  /*   Logical Max (255)                        */
    0x09, 0x02,        /*   Usage (Vendor 0x02)                      */
    0x81, 0x00,        /*   Input (Data,Ary,Abs)                     */
    0x09, 0x02,        /*   Usage (Vendor 0x02)                      */
    0x91, 0x00,        /*   Output (Data,Ary,Abs)                    */
    0xC0,              /* End Collection                             */
};

#endif /* USB_DESC_G302_H */
