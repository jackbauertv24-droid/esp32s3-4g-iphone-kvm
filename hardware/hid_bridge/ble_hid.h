#pragma once
#ifdef HID_TRANSPORT_BLE
#include <Arduino.h>
#include "src/hid/abs_desc.h"
class NimBLEServer;

void ble_hid_begin(NimBLEServer *server);
void ble_hid_disconnected(uint16_t handle);
bool ble_hid_ready();
void ble_hid_poll(bool timedHold);
uint32_t ble_hid_session();
bool ble_hid_allows(int handle);
void ble_hid_claim(uint16_t handle, bool subscribed);
bool ble_hid_command_begin(uint32_t session);
bool ble_hid_command_ok();
void ble_hid_diag(uint32_t *sent, uint32_t *failed, uint32_t *waited, uint32_t *max_ms);

// Supports BOTH relative reports (Report ID 1) AND zero-calibration 0..32767 absolute touch/pointer
// reports (Report ID 4) over Bluetooth HID—removing the upstream USB-only limitation on `abs`!
class BleHidMouse {
public:
  static const uint16_t FULL = HID_ABS_MAX;
  static const int MOVE_SCALE = 50;
  static const long MAX_MOVE = FULL / MOVE_SCALE + 1;
  static const int MIN_STEP = 64;
  static const long MAX_REPORTS = 250;

  void begin() {}
  void move(int8_t x, int8_t y, int8_t wheel = 0, int8_t pan = 0);
  void moveTo(long x, long y);
  void glideTo(long x, long y, int step, int ms);
  void press(uint8_t buttons);
  void release(uint8_t buttons);
  bool held() const;
};

class BleHidKeyboard : public Print {
public:
  void begin() {}
  void press(uint8_t key);
  void releaseAll();
  size_t write(uint8_t value) override;
};

class BleHidConsumer {
public:
  void begin() {}
  void press(uint16_t usage);
  void release();
};
#endif
