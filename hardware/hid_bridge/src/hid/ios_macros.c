#include "ios_macros.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

void ios_default_airplay_coords(ios_airplay_coords_t *c) {
  if (!c) return;
  // Standard iOS 17/18 portrait Control Center Screen Mirroring tile & first receiver list item
  c->mirror_tile_x = 22500;
  c->mirror_tile_y = 9800;
  c->receiver_x = 16384;
  c->receiver_y = 14200;
}

static bool push_step(ios_macro_plan_t *p, ios_macro_step_t s) {
  if (!p || p->count >= IOS_MACRO_MAX_STEPS) return false;
  p->steps[p->count++] = s;
  return true;
}

static bool add_tap(ios_macro_plan_t *p, uint16_t x, uint16_t y) {
  ios_macro_step_t s = {0};
  s.kind = IOS_STEP_ABS_TAP;
  s.x = x;
  s.y = y;
  return push_step(p, s);
}

static bool add_swipe(ios_macro_plan_t *p, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t step_size) {
  ios_macro_step_t s = {0};
  s.kind = IOS_STEP_ABS_SWIPE;
  s.x = x1;
  s.y = y1;
  s.x2 = x2;
  s.y2 = y2;
  s.step_size = step_size;
  return push_step(p, s);
}

static bool add_combo(ios_macro_plan_t *p, const char *combo) {
  ios_macro_step_t s = {0};
  s.kind = IOS_STEP_KEY_COMBO;
  snprintf(s.text, sizeof s.text, "%s", combo);
  return push_step(p, s);
}

static bool add_delay(ios_macro_plan_t *p, uint16_t ms) {
  ios_macro_step_t s = {0};
  s.kind = IOS_STEP_DELAY;
  s.delay_ms = ms;
  return push_step(p, s);
}

bool ios_macro_build(const char *arg, const ios_airplay_coords_t *ap_cfg, ios_macro_plan_t *plan) {
  if (!arg || !plan) return false;
  while (*arg == ' ') arg++;
  plan->count = 0;

  char name[24] = {0};
  int pos = 0;
  while (arg[pos] && arg[pos] != ' ' && pos < (int)sizeof(name) - 1) {
    name[pos] = arg[pos];
    pos++;
  }
  const char *rest = arg + pos;
  while (*rest == ' ') rest++;

  if (strcmp(name, "homeswipe") == 0) {
    if (*rest) return false;
    // Fast flick up from the bottom home bar (step=1800 finishes in ~7 reports = 56 ms, avoiding App Switcher)
    return add_swipe(plan, 16384, 32300, 16384, 20000, 1800);
  }

  if (strcmp(name, "appswitcher") == 0) {
    if (*rest) return false;
    // Slower glide up from home bar + brief pause opens the iOS App Switcher
    return add_swipe(plan, 16384, 32300, 16384, 19000, 320);
  }

  if (strcmp(name, "controlcenter") == 0) {
    if (*rest) return false;
    // Pull down from the top-right status bar corner (battery/Wi-Fi icon zone)
    return add_swipe(plan, 29500, 350, 29500, 12000, 900);
  }

  if (strcmp(name, "notifications") == 0) {
    if (*rest) return false;
    // Pull down from top-center notch/island zone
    return add_swipe(plan, 16384, 350, 16384, 16000, 900);
  }

  if (strcmp(name, "spotlight") == 0) {
    if (*rest) return false;
    return add_combo(plan, "cmd+space");
  }

  if (strcmp(name, "wake") == 0) {
    if (*rest) return false;
    return add_combo(plan, "space") && add_delay(plan, 250);
  }

  if (strcmp(name, "unlock") == 0) {
    // Optional 4-to-12 digit numeric passcode after `unlock`
    size_t pin_len = strlen(rest);
    if (pin_len > 12) return false;
    for (size_t i = 0; i < pin_len; i++) {
      if (!isdigit((unsigned char)rest[i])) return false;
    }
    if (!add_combo(plan, "space") || !add_delay(plan, 300)) return false;
    if (!add_swipe(plan, 16384, 29500, 16384, 9000, 1400)) return false;
    if (pin_len > 0) {
      if (!add_delay(plan, 450)) return false;
      ios_macro_step_t pin_step = {0};
      pin_step.kind = IOS_STEP_TYPE_PIN;
      snprintf(pin_step.text, sizeof pin_step.text, "%s", rest);
      if (!push_step(plan, pin_step)) return false;
    }
    return true;
  }

  if (strcmp(name, "airplay") == 0 || strcmp(name, "airplay_recover") == 0) {
    ios_airplay_coords_t cfg;
    if (ap_cfg) cfg = *ap_cfg;
    else ios_default_airplay_coords(&cfg);

    if (*rest) {
      unsigned mx, my, rx, ry;
      if (sscanf(rest, "%u %u %u %u", &mx, &my, &rx, &ry) != 4) return false;
      if (mx > 32767 || my > 32767 || rx > 32767 || ry > 32767) return false;
      cfg.mirror_tile_x = (uint16_t)mx;
      cfg.mirror_tile_y = (uint16_t)my;
      cfg.receiver_x = (uint16_t)rx;
      cfg.receiver_y = (uint16_t)ry;
    }

    // 1. Open Control Center from top-right corner
    // 2. Wait 500 ms for sheet animation
    // 3. Tap Screen Mirroring icon
    // 4. Wait 700 ms for AirPlay receiver list discovery
    // 5. Tap "ESP32 Agent" row
    // 6. Wait 600 ms, then flick Control Center closed
    return add_swipe(plan, 29500, 350, 29500, 12000, 900) &&
           add_delay(plan, 500) &&
           add_tap(plan, cfg.mirror_tile_x, cfg.mirror_tile_y) &&
           add_delay(plan, 700) &&
           add_tap(plan, cfg.receiver_x, cfg.receiver_y) &&
           add_delay(plan, 600) &&
           add_swipe(plan, 16384, 32300, 16384, 20000, 1800);
  }

  return false;
}
