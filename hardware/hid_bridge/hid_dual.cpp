#ifdef HID_DUAL
#include "hid_dual.h"
#include "hid_mode.h"
#include "USBHIDMouse.h"
#include "abs_mouse.h"
#include "USBHIDKeyboard.h"
#include "USBHIDConsumerControl.h"

static USBHIDMouse usbMouseDev;
static AbsMouse usbAbsMouseDev;
static USBHIDKeyboard usbKeyboardDev;
static USBHIDConsumerControl usbConsumerDev;
static USBHIDMouse *usbMouse;
static AbsMouse *usbAbsMouse;
static USBHIDKeyboard *usbKeyboard;
static USBHIDConsumerControl *usbConsumer;
static BleHidMouse bleMouse;
static BleHidKeyboard bleKeyboard;
static BleHidConsumer bleConsumer;
static bool dualUsedAbs = false;

void hid_dual_begin() {
  usbMouseDev.begin();
  usbAbsMouseDev.begin();
  usbKeyboardDev.begin();
  usbConsumerDev.begin();
  if (hid_ble()) return;
  usbMouse = &usbMouseDev;
  usbAbsMouse = &usbAbsMouseDev;
  usbKeyboard = &usbKeyboardDev;
  usbConsumer = &usbConsumerDev;
}

void DualMouse::move(int8_t x, int8_t y, int8_t wheel, int8_t pan) {
  dualUsedAbs = false;
  if (usbMouse) usbMouse->move(x, y, wheel, pan);
  else bleMouse.move(x, y, wheel, pan);
}
void DualMouse::moveTo(long x, long y) {
  dualUsedAbs = true;
  if (usbAbsMouse) usbAbsMouse->moveTo(x, y);
  else bleMouse.moveTo(x, y);
}
void DualMouse::glideTo(long x, long y, int step, int ms) {
  dualUsedAbs = true;
  if (usbAbsMouse) usbAbsMouse->glideTo(x, y, step, ms);
  else bleMouse.glideTo(x, y, step, ms);
}
void DualMouse::press(uint8_t b) {
  if (usbMouse) {
    if (dualUsedAbs && usbAbsMouse) usbAbsMouse->press(b);
    else usbMouse->press(b);
  } else {
    bleMouse.press(b);
  }
}
void DualMouse::release(uint8_t b) {
  if (usbMouse) {
    if (dualUsedAbs && usbAbsMouse) usbAbsMouse->release(b);
    else usbMouse->release(b);
  } else {
    bleMouse.release(b);
  }
}
bool DualMouse::held() const {
  if (usbMouse) return dualUsedAbs && usbAbsMouse ? usbAbsMouse->held() : false;
  return bleMouse.held();
}

size_t DualKeyboard::press(uint8_t k) {
  if (usbKeyboard) return usbKeyboard->press(k);
  bleKeyboard.press(k);
  return 1;
}
void DualKeyboard::releaseAll() {
  if (usbKeyboard) usbKeyboard->releaseAll();
  else bleKeyboard.releaseAll();
}
size_t DualKeyboard::write(uint8_t v) { return usbKeyboard ? usbKeyboard->write(v) : bleKeyboard.write(v); }

void DualConsumer::press(uint16_t u) {
  if (usbConsumer) usbConsumer->press(u);
  else bleConsumer.press(u);
}
void DualConsumer::release() {
  if (usbConsumer) usbConsumer->release();
  else bleConsumer.release();
}
#endif
