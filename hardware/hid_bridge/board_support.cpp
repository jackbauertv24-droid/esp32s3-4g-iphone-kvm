#include "board_support.h"
#include "a7670e_pins.h"
#ifdef ZECTRIX_NOTE4
#include <Arduino.h>
#include <driver/gpio.h>
#include "note4_battery.h"
#endif
#ifdef M5_STOPWATCH
#include "stopwatch_ui.h"
#endif
#if defined(WAVESHARE_A7670E) && defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <Wire.h>
static bool fuelGaugeReady = false;
#endif

// ESP-IDF places this marker after the 256-byte app descriptor so OTA can check the target before writing.
extern const char firmware_target[16]
#ifdef ARDUINO_ARCH_ESP32
    __attribute__((section(".rodata_custom_desc"), used))
#endif
    =
#ifdef M5_STOPWATCH
    "ESPA:stopwatch";
#elif defined(ZECTRIX_NOTE4)
    "ESPA:note4-ble";
#elif defined(WAVESHARE_A7670E)
    "ESPA:a7670e";
#elif defined(HID_DUAL)
    "ESPA:dual-8m";  // no release channel takes it yet
#elif defined(HID_TRANSPORT_BLE)
    "ESPA:ble-8m";
#else
    "ESPA:usb-8m";
#endif

void board_begin() {
#ifdef M5_STOPWATCH
  stopwatch_begin();  // M5Unified: power rails, display, touch, IMU and vibration (stopwatch_ui.h)
#endif
#ifdef ZECTRIX_NOTE4
  // Latch battery power before disabling idle peripherals to avoid noise and wasted power.
  gpio_hold_dis(GPIO_NUM_17);
  gpio_set_level(GPIO_NUM_17, 1);
  gpio_set_direction(GPIO_NUM_17, GPIO_MODE_OUTPUT);
  gpio_hold_en(GPIO_NUM_17);
  for (int pin : {6, 42, 46}) {
    gpio_hold_dis(static_cast<gpio_num_t>(pin));
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  gpio_hold_dis(GPIO_NUM_3);
  pinMode(3, OUTPUT);
  digitalWrite(3, HIGH);
  analogSetPinAttenuation(4, ADC_11db);
#endif
#if defined(WAVESHARE_A7670E) && defined(ARDUINO_ARCH_ESP32)
  pinMode(A7670E_PWRKEY_PIN, OUTPUT);
  digitalWrite(A7670E_PWRKEY_PIN, HIGH);
  // Probe onboard MAX17048 18650 Li-ion / Solar fuel gauge on I2C (0x36)
  if (Wire.begin(MAX17048_SDA_PIN, MAX17048_SCL_PIN, 400000)) {
    Wire.beginTransmission(MAX17048_I2C_ADDR);
    fuelGaugeReady = (Wire.endTransmission() == 0);
  }
  board_set_led(0, 0, 16);  // dim blue during boot
#endif
}

uint8_t board_battery_percent() {
#ifdef M5_STOPWATCH
  return stopwatch_battery();
#elif defined(ZECTRIX_NOTE4)
  uint32_t total = 0;
  for (int i = 0; i < 10; i++) total += 2 * analogReadMilliVolts(4);
  return note4_battery_percent(total / 10);
#elif defined(WAVESHARE_A7670E) && defined(ARDUINO_ARCH_ESP32)
  if (!fuelGaugeReady) return 0;
  // MAX17048 register 0x04 holds integer state-of-charge % in the MSB, 1/256% in the LSB.
  Wire.beginTransmission(MAX17048_I2C_ADDR);
  Wire.write(0x04);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom((uint8_t)MAX17048_I2C_ADDR, (uint8_t)2) != 2) return 0;
  uint8_t soc_int = Wire.read();
  (void)Wire.read();
  return soc_int > 100 ? 100 : soc_int;
#else
  // This target has no battery monitor; BAS 3.1.1 specifies zero for a device without a battery.
  return 0;
#endif
}

const char *board_status_fields() {
#ifdef M5_STOPWATCH
  return ",\"board\":\"stopwatch\",\"ui\":1,\"ota\":1";  // ui: it takes `ui <json>` and sends `btn` lines; ota: `ota on`
#elif defined(ZECTRIX_NOTE4)
  return ",\"board\":\"note4\"";
#elif defined(WAVESHARE_A7670E)
  return ",\"board\":\"a7670e\",\"cam\":1,\"cell\":1";
#else
  return "";
#endif
}

bool board_supports_usb_release() {
#if defined(ZECTRIX_NOTE4) || defined(M5_STOPWATCH) || defined(WAVESHARE_A7670E) || defined(HID_TRANSPORT_BLE)
  return false;
#else
  return true;
#endif
}

void board_set_led(uint8_t r, uint8_t g, uint8_t b) {
#if defined(WAVESHARE_A7670E) && defined(ARDUINO_ARCH_ESP32)
  rgbLedWrite(A7670E_RGB_LED_PIN, r, g, b);
#else
  (void)r;
  (void)g;
  (void)b;
#endif
}
