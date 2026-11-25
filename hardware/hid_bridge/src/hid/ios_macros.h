// Deterministic iOS recovery and system navigation macros in 0..32767 absolute HID space.
// Fixes two major shortcomings of the upstream firmware:
//   1. `homescreen` failed over Bluetooth HID because iOS ignores Consumer Control AC Home over BLE;
//      `homeswipe` performs a fast upward flick from the bottom home bar that works on both BLE & USB.
//   2. Autonomous out-of-band recovery when AirPlay drops or the phone locks (`unlock`, `controlcenter`,
//      `airplay_recover`, `spotlight`, `appswitcher`).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IOS_MACRO_MAX_STEPS 24

typedef enum {
  IOS_STEP_ABS_MOVE = 1,   // move absolute pointer to (x, y) without pressing
  IOS_STEP_ABS_TAP = 2,    // move to (x, y), settle, click
  IOS_STEP_ABS_SWIPE = 3,  // press at (x, y), glide to (x2, y2) with step_size, release
  IOS_STEP_KEY_COMBO = 4,  // press combo string (e.g. "space", "cmd+space", "esc")
  IOS_STEP_TYPE_PIN = 5,   // type digits of a passcode with safe inter-key spacing
  IOS_STEP_DELAY = 6       // wait ms (feeding watchdog in caller)
} ios_step_kind_t;

typedef struct {
  ios_step_kind_t kind;
  uint16_t x, y;
  uint16_t x2, y2;
  uint16_t step_size;
  uint16_t delay_ms;
  char text[16];
} ios_macro_step_t;

typedef struct {
  ios_macro_step_t steps[IOS_MACRO_MAX_STEPS];
  int count;
} ios_macro_plan_t;

typedef struct {
  uint16_t mirror_tile_x, mirror_tile_y;  // Screen Mirroring icon in Control Center (0..32767)
  uint16_t receiver_x, receiver_y;        // First AirPlay receiver row ("ESP32 Agent") in sheet
} ios_airplay_coords_t;

void ios_default_airplay_coords(ios_airplay_coords_t *c);

// Parses `arg` (e.g. "homeswipe", "controlcenter", "notifications", "appswitcher", "spotlight",
// "wake", "unlock [PIN]", "airplay [mx my rx ry]") and fills `plan`.
// Returns true on valid macro, false on invalid name or bad PIN/coordinates.
bool ios_macro_build(const char *arg, const ios_airplay_coords_t *ap_cfg, ios_macro_plan_t *plan);

#ifdef __cplusplus
}
#endif
