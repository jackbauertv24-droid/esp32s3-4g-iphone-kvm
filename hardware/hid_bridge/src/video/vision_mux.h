// Dual-Eye Vision Multiplexer: AirPlay H.264 primary + OV2640/OV5640 optical camera fallback.
// Solves the iOS AirPlay blindspot when the iPhone locks, reboots, shows a passcode / 2FA prompt
// (blackout stream), or drops Screen Mirroring. Pure C11 with caller-owned state for host testing.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Video chunk flag for JPEG frames from the optical fallback camera (extends gop.h VF_* flags).
#define VF_JPEG 0x80

typedef enum {
  EYE_MODE_AUTO = 0,     // AirPlay when healthy; automatic fallback to camera when down/blacked out
  EYE_MODE_AIRPLAY = 1,  // Force AirPlay only
  EYE_MODE_CAMERA = 2    // Force optical camera only (e.g. passcode entry or BIOS-style recovery)
} eye_mode_t;

typedef enum {
  EYE_ACTIVE_NONE = 0,
  EYE_ACTIVE_AIRPLAY = 1,
  EYE_ACTIVE_CAMERA = 2
} eye_active_t;

typedef enum {
  EYE_REASON_Forced = 0,
  EYE_REASON_AIRPLAY_OK = 1,
  EYE_REASON_AIRPLAY_IDLE = 2,
  EYE_REASON_AIRPLAY_STALE = 3,
  EYE_REASON_AIRPLAY_FROZEN = 4,
  EYE_REASON_AIRPLAY_BLACKOUT = 5,
  EYE_REASON_NO_CAMERA = 6
} eye_reason_t;

// Normalized iPhone screen bounding box inside the camera sensor frame (0..10000 units),
// plus clockwise rotation (0, 90, 180, 270 degrees) so camera clicks map to 0..32767 HID coordinates.
typedef struct {
  uint16_t x0, y0;  // top-left of iPhone screen in camera image (0..10000)
  uint16_t x1, y1;  // bottom-right of iPhone screen in camera image (0..10000)
  uint16_t rotation_deg;
} vision_roi_t;

typedef struct {
  eye_mode_t mode;
  eye_active_t active;
  eye_reason_t reason;
  bool cam_present;
  bool airplay_streaming;
  bool airplay_frozen;
  uint32_t last_airplay_ms;
  uint32_t last_cam_snap_ms;
  uint32_t stale_timeout_ms;       // switch to camera if no AirPlay AU arrives within this window (default 3500 ms)
  uint32_t cam_interval_ms;        // optical fallback frame interval while active (default 500 ms = 2 fps)
  uint16_t blackout_au_bytes;      // P-frame byte ceiling considered suspiciously blank/blackout (default 180 B)
  uint16_t blackout_count_thresh;  // consecutive tiny frames after IDR blackout to flag (default 25 = ~4 s)
  uint16_t tiny_au_streak;
  uint16_t healthy_au_streak;
  uint32_t fallback_switches;      // count of automatic AirPlay -> Camera transitions since boot
  vision_roi_t roi;
} vision_mux_t;

void vision_mux_init(vision_mux_t *m, bool cam_present);
void vision_mux_set_mode(vision_mux_t *m, eye_mode_t mode);
bool vision_mux_set_roi(vision_mux_t *m, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t rotation_deg);

// Called on each AirPlay AU arrival to track freshness and detect iOS passcode/DRM screen blackouts.
void vision_mux_on_airplay_au(vision_mux_t *m, size_t au_bytes, bool is_idr, uint32_t now_ms);

// Evaluates current state and updates m->active and m->reason. Returns the active eye.
eye_active_t vision_mux_poll(vision_mux_t *m, bool airplay_streaming, bool airplay_frozen, uint32_t now_ms);

// True if the optical camera should capture and send a periodic fallback JPEG frame at now_ms.
bool vision_mux_cam_due(vision_mux_t *m, uint32_t now_ms);

// Maps normalized click coordinates (0..10000) on the raw camera image into iPhone HID absolute
// pointer coordinates (0..32767), accounting for the calibrated screen ROI and mount rotation.
// Returns false if the click falls outside the calibrated iPhone screen region.
bool vision_map_cam_to_abs(const vision_roi_t *roi, uint16_t cam_x, uint16_t cam_y, uint16_t *abs_x, uint16_t *abs_y);

const char *vision_mode_name(eye_mode_t mode);
const char *vision_active_name(eye_active_t active);
const char *vision_reason_name(eye_reason_t reason);

#ifdef __cplusplus
}
#endif
