// Optical fallback camera ("Recovery Eyes") for Waveshare ESP32-S3-A7670E-4G (OV2640 / OV5640).
// Captures glare-controlled JPEG snapshots of the physical iPhone screen whenever AirPlay is
// disconnected, stalled, or blacked out by an iOS passcode / 2FA sheet.
#pragma once
#include <Arduino.h>
#include "src/video/vision_mux.h"

bool cam_eye_begin();
bool cam_eye_available();
vision_mux_t *cam_vision_mux();

// Captures one JPEG frame and sends it as VF_JPEG chunks to `who` (FROM_APP or FROM_CLI).
// If `id` is non-empty, replies `<id> ok <bytes>` or `<id> err ...` when done.
bool cam_eye_snap_and_send(const String &id, char who, bool is_manual_snap);

// Periodic poll called from loop(): updates Dual-Eye mux and sends fallback frames when streaming.
void cam_eye_poll(bool airplay_streaming, bool airplay_frozen, bool anyone_streaming, char active_who);

// Configures OLED screen exposure preset: `aec` (0=locked manual, 1=auto), `exposure` (0..1200), `quality` (4..63).
bool cam_eye_configure(int aec, int exposure, int quality);

// Saves & applies normalized ROI crop (0..10000) and rotation (0, 90, 180, 270) in NVS.
bool cam_eye_set_roi(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t rot);

// Formats JSON status snippet for statusJson / `eye` command.
String cam_eye_status_json();
