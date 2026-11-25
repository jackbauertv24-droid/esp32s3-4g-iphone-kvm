// Shared USB & BLE HID Report Descriptor for 16-bit Absolute Pointer (0..32767 X/Y + Wheel).
// Removes the upstream limitation where absolute coordinates only worked over wired USB HID:
// including this report (Report ID 4 on BLE, Report ID 10 on USB) lets AssistiveTouch map X/Y
// directly to screen fractions over wireless Bluetooth HID with zero pointer drift or calibration.
#pragma once
#include <stdint.h>

#define HID_ABS_MAX 32767

#define HID_REPORT_DESC_ABS_POINTER(report_id) \
  0x05, 0x01,                    /* Usage Page (Generic Desktop)        */ \
  0x09, 0x02,                    /* Usage (Mouse)                       */ \
  0xA1, 0x01,                    /* Collection (Application)            */ \
  0x85, (report_id),             /*   Report ID                         */ \
  0x09, 0x01,                    /*   Usage (Pointer)                   */ \
  0xA1, 0x00,                    /*   Collection (Physical)             */ \
  0x05, 0x09,                    /*     Usage Page (Button)             */ \
  0x19, 0x01,                    /*     Usage Minimum (Button 1)        */ \
  0x29, 0x03,                    /*     Usage Maximum (Button 3)        */ \
  0x15, 0x00,                    /*     Logical Minimum (0)             */ \
  0x25, 0x01,                    /*     Logical Maximum (1)             */ \
  0x75, 0x01,                    /*     Report Size (1)                 */ \
  0x95, 0x03,                    /*     Report Count (3)                */ \
  0x81, 0x02,                    /*     Input (Data,Var,Abs)            */ \
  0x95, 0x05,                    /*     Report Count (5) - padding      */ \
  0x81, 0x03,                    /*     Input (Const,Var,Abs)           */ \
  0x05, 0x01,                    /*     Usage Page (Generic Desktop)    */ \
  0x09, 0x30,                    /*     Usage (X)                       */ \
  0x09, 0x31,                    /*     Usage (Y)                       */ \
  0x15, 0x00,                    /*     Logical Minimum (0)             */ \
  0x26, 0xFF, 0x7F,              /*     Logical Maximum (32767)         */ \
  0x35, 0x00,                    /*     Physical Minimum (0)            */ \
  0x46, 0xFF, 0x7F,              /*     Physical Maximum (32767)        */ \
  0x75, 0x10,                    /*     Report Size (16)                */ \
  0x95, 0x02,                    /*     Report Count (2)                */ \
  0x81, 0x02,                    /*     Input (Data,Var,Abs)            */ \
  0x09, 0x38,                    /*     Usage (Wheel)                   */ \
  0x15, 0x81,                    /*     Logical Minimum (-127)          */ \
  0x25, 0x7F,                    /*     Logical Maximum (127)           */ \
  0x35, 0x00,                    /*     Physical Minimum (0)            */ \
  0x45, 0x00,                    /*     Physical Maximum (0)            */ \
  0x75, 0x08,                    /*     Report Size (8)                 */ \
  0x95, 0x01,                    /*     Report Count (1)                */ \
  0x81, 0x06,                    /*     Input (Data,Var,Rel)            */ \
  0xC0,                          /*   End Collection                    */ \
  0xC0                           /* End Collection                      */

// Packs a 6-byte absolute pointer report matching HID_REPORT_DESC_ABS_POINTER.
static inline void hid_pack_abs_report(uint8_t out[6], uint8_t buttons, uint16_t x, uint16_t y, int8_t wheel) {
  if (x > HID_ABS_MAX) x = HID_ABS_MAX;
  if (y > HID_ABS_MAX) y = HID_ABS_MAX;
  out[0] = buttons;
  out[1] = (uint8_t)(x & 0xFF);
  out[2] = (uint8_t)((x >> 8) & 0xFF);
  out[3] = (uint8_t)(y & 0xFF);
  out[4] = (uint8_t)((y >> 8) & 0xFF);
  out[5] = (uint8_t)wheel;
}
