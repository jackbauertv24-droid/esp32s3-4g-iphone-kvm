#pragma once
// --dual-hid: the mouse, keyboard and consumer control the command handler calls, sent to USB or BLE by this
// boot's mode (hid_mode.h). Supports both relative moves and 0..32767 absolute pointer coordinates!
#ifdef HID_DUAL
#include <Arduino.h>
#include "ble_hid.h"

void hid_dual_begin();  // after hid_mode_load(), before USB.begin()

class DualMouse {
public:
  static const uint16_t FULL = HID_ABS_MAX;
  static const int MOVE_SCALE = 50;
  static const long MAX_MOVE = FULL / MOVE_SCALE + 1;
  static const int MIN_STEP = 64;

  void begin() {}
  void move(int8_t x, int8_t y, int8_t wheel = 0, int8_t pan = 0);
  void moveTo(long x, long y);
  void glideTo(long x, long y, int step, int ms);
  void press(uint8_t buttons);
  void release(uint8_t buttons);
  bool held() const;
};

class DualKeyboard {
public:
  void begin() {}
  size_t press(uint8_t key);
  void releaseAll();
  size_t write(uint8_t value);
};

class DualConsumer {
public:
  void begin() {}
  void press(uint16_t usage);
  void release();
};
#endif
