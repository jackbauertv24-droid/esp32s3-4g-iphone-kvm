#ifdef HID_TRANSPORT_BLE
#include "ble_hid.h"
#include "ble_link.h"
#include "hid_keys.h"
#include "board_support.h"
#include "ble_hid_target.h"
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "nimble/nimble/host/include/host/ble_hs.h"
#include "USBHIDKeyboard.h"
#include "tusb.h"

static NimBLEHIDDevice *hidDevice;
static NimBLECharacteristic *inputs[4];  // 0: Rel Mouse (ID 1), 1: Keyboard (ID 2), 2: Consumer (ID 3), 3: Abs Pointer (ID 4)
static BleHidTarget target;
static unsigned lastGeneration;
static bool failed, releasePending, usedAbsMode;
static uint8_t mouseButtons, keys[8];
static uint16_t absX = 16384, absY = 16384;
static uint32_t batteryAt, lastReportAt;
static volatile uint32_t reportsSent, reportsFailed, reportsWaited, reportMaxMs;  // ble_hid_diag

static void resetReports() {
  uint8_t zero[8] = {};
  inputs[0]->setValue(zero, 5);
  inputs[1]->setValue(zero, 8);
  inputs[2]->setValue(zero, 2);
  inputs[3]->setValue(zero, 6);
}

class HidSubscription : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &info, uint16_t sub) override {
    ble_hid_subscribe(info.getConnHandle(), sub & 1);
  }
};
static HidSubscription hidSubscription;

void ble_hid_begin(NimBLEServer *server) {
  auto *hid = new NimBLEHIDDevice(server);
  hidDevice = hid;
  static uint8_t map[] = {
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(1)),
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(2)),
    TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(3)),
    HID_REPORT_DESC_ABS_POINTER(4)
  };
  hid->setReportMap(map, sizeof map);
  hid->setManufacturer("ESP32S3 4G KVM");
  hid->setPnp(2, 0x303a, 0x1001, 0x0102);
  hid->setHidInfo(0, 3);
  for (int i = 0; i < 4; i++) inputs[i] = hid->getInputReport(i + 1);
  resetReports();
  inputs[0]->setCallbacks(&hidSubscription);
  inputs[3]->setCallbacks(&hidSubscription);
  hid->getOutputReport(2)->setValue(uint8_t(0));
  hid->setBatteryLevel(board_battery_percent());
  batteryAt = millis();
}

void ble_hid_disconnected(uint16_t handle) {
  target.disconnect(handle);
}

uint32_t ble_hid_session() { return target.session(); }

void ble_hid_claim(uint16_t handle, bool subscribed) {
  if (target.subscribe(handle, ble_hid_trusted(handle), subscribed)) resetReports();
}

bool ble_hid_allows(int handle) {
  return handle >= 0 && target.allows(handle, ble_hid_trusted(handle));
}

bool ble_hid_ready() {
  return ble_hid_trusted(BleHidTarget::peer(target.session()));
}

bool ble_hid_command_begin(uint32_t session) {
  if (session != target.session()) return false;
  failed = false;
  if (lastGeneration != session) {
    mouseButtons = 0;
    usedAbsMode = false;
    memset(keys, 0, sizeof keys);
    releasePending = false;
    lastGeneration = session;
  }
  return true;
}

bool ble_hid_command_ok() {
  if (failed) releasePending = true;
  return !failed;
}

static void send(int input, const void *data, size_t size) {
  if (failed) return;
  int handle = BleHidTarget::peer(lastGeneration);
  uint32_t start = millis();
  for (;;) {
    if (handle < 0 || target.session() != lastGeneration || !ble_hid_ready()) {
      failed = releasePending = true;
      reportsFailed++;
      return;
    }
    os_mbuf *packet = ble_hs_mbuf_from_flat(data, size);
    int rc = packet ? ble_gatts_notify_custom(handle, inputs[input]->getHandle(), packet) : BLE_HS_ENOMEM;
    if (rc == 0) {
      lastReportAt = millis();
      uint32_t took = lastReportAt - start;
      reportsSent++;
      if (took) reportsWaited++;
      if (took > reportMaxMs) reportMaxMs = took;
      return;
    }
    if (rc != BLE_HS_ENOMEM || millis() - start >= 100) {
      failed = releasePending = true;
      reportsFailed++;
#if DEBUG_SERIAL
      Serial.printf("BLE HID report failed: rc=%d\n", rc);
#endif
      return;
    }
    vTaskDelay(1);
  }
}

void ble_hid_diag(uint32_t *sent, uint32_t *failedReports, uint32_t *waited, uint32_t *maxMs) {
  *sent = reportsSent;
  *failedReports = reportsFailed;
  *waited = reportsWaited;
  *maxMs = reportMaxMs;
  reportMaxMs = 0;
}

void ble_hid_poll(bool timedHold) {
  uint32_t now = millis();
  if (hidDevice && now - batteryAt >= 30000) {
    hidDevice->setBatteryLevel(board_battery_percent(), true);
    batteryAt = now;
  }
  if (ble_hid_ready() && (releasePending || (!timedHold && mouseButtons && now - lastReportAt >= 5000))) {
    if (!ble_hid_command_begin(lastGeneration)) return;
    uint8_t zero[8] = {};
    send(0, zero, 5);
    send(1, zero, 8);
    send(2, zero, 2);
    uint8_t absRel[6];
    hid_pack_abs_report(absRel, 0, absX, absY, 0);
    send(3, absRel, 6);
    if (!failed) {
#if DEBUG_SERIAL
      if (releasePending) Serial.println("BLE HID inputs released after command failure");
#endif
      mouseButtons = 0;
      memset(keys, 0, sizeof keys);
      releasePending = false;
    } else {
      releasePending = true;
    }
  }
}

void BleHidMouse::move(int8_t x, int8_t y, int8_t wheel, int8_t pan) {
  usedAbsMode = false;
  int8_t report[] = {static_cast<int8_t>(mouseButtons), x, y, wheel, pan};
  send(0, report, sizeof report);
}

void BleHidMouse::moveTo(long x, long y) {
  usedAbsMode = true;
  absX = (uint16_t)constrain(x, 0, (long)FULL);
  absY = (uint16_t)constrain(y, 0, (long)FULL);
  uint8_t r[6];
  hid_pack_abs_report(r, mouseButtons, absX, absY, 0);
  send(3, r, sizeof r);
}

void BleHidMouse::glideTo(long x, long y, int step, int ms) {
  step = max(step, MIN_STEP);
  x = constrain(x, 0, (long)FULL);
  y = constrain(y, 0, (long)FULL);
  long x0 = absX, y0 = absY;
  long n = min(max(labs(x - x0), labs(y - y0)) / step + 1, MAX_REPORTS);
  for (long i = 1; i <= n; i++) {
    moveTo(x0 + (x - x0) * i / n, y0 + (y - y0) * i / n);
    if (!ble_hid_command_ok()) return;
    delay(ms);
  }
}

void BleHidMouse::press(uint8_t buttons) {
  mouseButtons |= buttons;
  if (usedAbsMode) {
    uint8_t r[6];
    hid_pack_abs_report(r, mouseButtons, absX, absY, 0);
    send(3, r, sizeof r);
  } else {
    move(0, 0);
  }
}

void BleHidMouse::release(uint8_t buttons) {
  mouseButtons &= ~buttons;
  if (usedAbsMode) {
    uint8_t r[6];
    hid_pack_abs_report(r, mouseButtons, absX, absY, 0);
    send(3, r, sizeof r);
  } else {
    move(0, 0);
  }
}

bool BleHidMouse::held() const { return mouseButtons != 0; }

void BleHidKeyboard::press(uint8_t key) {
  if (!hid_key_press(keys, key, KeyboardLayout_en_US)) { failed = true; return; }
  send(1, keys, sizeof keys);
}
void BleHidKeyboard::releaseAll() { memset(keys, 0, sizeof keys); send(1, keys, sizeof keys); }
size_t BleHidKeyboard::write(uint8_t value) {
  if (value >= 128) { failed = true; return 0; }
  press(value);
  if (failed) return 0;
  delay(8);
  releaseAll();
  delay(8);
  return 1;
}

void BleHidConsumer::press(uint16_t usage) {
  uint8_t report[] = {static_cast<uint8_t>(usage), static_cast<uint8_t>(usage >> 8)};
  send(2, report, sizeof report);
}
void BleHidConsumer::release() { press(0); }
#endif
