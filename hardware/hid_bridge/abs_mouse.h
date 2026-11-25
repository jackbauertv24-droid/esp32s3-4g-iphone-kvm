// An absolute mouse (ABS_POINTER builds): the same calls as USBHIDMouse, plus moveTo(). iOS maps X/Y straight to
// screen fractions while AssistiveTouch is on, so no homing or calibration. It ignores X/Y in a report that changes
// a button: move in one report, wait, then press (spikes/abs-touch/README.md).
#pragma once
#include "USBHID.h"
#include "src/hid/abs_desc.h"

class AbsMouse : public USBHIDDevice {
public:
  static const uint16_t FULL = HID_ABS_MAX;
  static const int MOVE_SCALE = 50;
  static const long MAX_MOVE = FULL / MOVE_SCALE + 1;

  AbsMouse() { hid.addDevice(this, sizeof DESCRIPTOR); }  // must be global: USB starts before setup()
  void begin() { hid.begin(); }

  void moveTo(long x, long y) {
    x_ = constrain(x, 0, FULL);
    y_ = constrain(y, 0, FULL);
    send(0);
  }
  static const int MIN_STEP = 64;
  static const long MAX_REPORTS = 250;  // ~2 s at 8 ms
  void glideTo(long x, long y, int step, int ms) {
    step = max(step, MIN_STEP);
    x = constrain(x, 0, FULL);
    y = constrain(y, 0, FULL);
    long x0 = x_, y0 = y_;
    long n = min(max(labs(x - x0), labs(y - y0)) / step + 1, MAX_REPORTS);
    for (long i = 1; i <= n; i++) {
      moveTo(x0 + (x - x0) * i / n, y0 + (y - y0) * i / n);
      delay(ms);
    }
  }
  void move(int dx, int dy, int8_t wheel = 0, int8_t = 0) {
    if (dx || dy) moveTo(x_ + dx * MOVE_SCALE, y_ + dy * MOVE_SCALE);
    if (wheel) send(wheel);
  }
  void press(uint8_t b) { buttons_ |= b; send(0); }
  void release(uint8_t b) { buttons_ &= ~b; send(0); }
  bool held() const { return buttons_; }

  uint16_t _onGetDescriptor(uint8_t *buffer) override {
    memcpy(buffer, DESCRIPTOR, sizeof DESCRIPTOR);
    return sizeof DESCRIPTOR;
  }

private:
  static const uint8_t REPORT_ID = 10;  // USBHID.h takes 1-6
  static constexpr uint8_t DESCRIPTOR[] = {
    HID_REPORT_DESC_ABS_POINTER(REPORT_ID)
  };

  void send(int8_t wheel) {
    uint8_t r[6];
    hid_pack_abs_report(r, buttons_, x_, y_, wheel);
    hid.SendReport(REPORT_ID, r, sizeof r);
  }

  USBHID hid;
  uint8_t buttons_ = 0;
  uint16_t x_ = 0, y_ = 0;
};
