#include "vision_mux.h"

#define VISION_NORM_MAX 10000u
#define VISION_ABS_MAX  32767u

void vision_mux_init(vision_mux_t *m, bool cam_present) {
  if (!m) return;
  *m = (vision_mux_t){0};
  m->mode = EYE_MODE_AUTO;
  m->cam_present = cam_present;
  m->active = cam_present ? EYE_ACTIVE_CAMERA : EYE_ACTIVE_NONE;
  m->reason = cam_present ? EYE_REASON_AIRPLAY_IDLE : EYE_REASON_NO_CAMERA;
  m->stale_timeout_ms = 3500;
  m->cam_interval_ms = 500;
  m->blackout_au_bytes = 180;
  m->blackout_count_thresh = 25;
  m->roi.x0 = 0;
  m->roi.y0 = 0;
  m->roi.x1 = VISION_NORM_MAX;
  m->roi.y1 = VISION_NORM_MAX;
  m->roi.rotation_deg = 0;
}

void vision_mux_set_mode(vision_mux_t *m, eye_mode_t mode) {
  if (!m) return;
  m->mode = mode;
}

bool vision_mux_set_roi(vision_mux_t *m, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t rotation_deg) {
  if (!m) return false;
  if (x0 >= x1 || y0 >= y1 || x1 > VISION_NORM_MAX || y1 > VISION_NORM_MAX) return false;
  if (rotation_deg != 0 && rotation_deg != 90 && rotation_deg != 180 && rotation_deg != 270) return false;
  m->roi.x0 = x0;
  m->roi.y0 = y0;
  m->roi.x1 = x1;
  m->roi.y1 = y1;
  m->roi.rotation_deg = rotation_deg;
  return true;
}

void vision_mux_on_airplay_au(vision_mux_t *m, size_t au_bytes, bool is_idr, uint32_t now_ms) {
  if (!m) return;
  m->last_airplay_ms = now_ms ? now_ms : 1;
  // iOS sends a tiny solid-black IDR (< 900 B at 480x1040) followed by sub-180 B empty P-frames
  // when a passcode or password sheet blacks out Screen Mirroring.
  bool tiny = is_idr ? (au_bytes < 900) : (au_bytes <= m->blackout_au_bytes);
  if (tiny) {
    if (m->tiny_au_streak < 65535) m->tiny_au_streak++;
    m->healthy_au_streak = 0;
  } else {
    m->tiny_au_streak = 0;
    if (m->healthy_au_streak < 65535) m->healthy_au_streak++;
  }
}

static void set_active(vision_mux_t *m, eye_active_t next, eye_reason_t reason) {
  if (m->active == EYE_ACTIVE_AIRPLAY && next == EYE_ACTIVE_CAMERA && m->mode == EYE_MODE_AUTO) {
    m->fallback_switches++;
  }
  m->active = next;
  m->reason = reason;
}

eye_active_t vision_mux_poll(vision_mux_t *m, bool airplay_streaming, bool airplay_frozen, uint32_t now_ms) {
  if (!m) return EYE_ACTIVE_NONE;
  m->airplay_streaming = airplay_streaming;
  m->airplay_frozen = airplay_frozen;

  if (m->mode == EYE_MODE_AIRPLAY) {
    set_active(m, airplay_streaming ? EYE_ACTIVE_AIRPLAY : EYE_ACTIVE_NONE,
               airplay_streaming ? EYE_REASON_Forced : EYE_REASON_AIRPLAY_IDLE);
    return m->active;
  }
  if (m->mode == EYE_MODE_CAMERA) {
    set_active(m, m->cam_present ? EYE_ACTIVE_CAMERA : EYE_ACTIVE_NONE,
               m->cam_present ? EYE_REASON_Forced : EYE_REASON_NO_CAMERA);
    return m->active;
  }

  // EYE_MODE_AUTO: prefer AirPlay when healthy; fall back to optical camera immediately on trouble.
  eye_reason_t trouble = EYE_REASON_AIRPLAY_OK;
  if (!airplay_streaming) {
    trouble = EYE_REASON_AIRPLAY_IDLE;
  } else if (airplay_frozen) {
    trouble = EYE_REASON_AIRPLAY_FROZEN;
  } else if (m->last_airplay_ms == 0 || (uint32_t)(now_ms - m->last_airplay_ms) > m->stale_timeout_ms) {
    trouble = EYE_REASON_AIRPLAY_STALE;
  } else if (m->blackout_count_thresh > 0 && m->tiny_au_streak >= m->blackout_count_thresh) {
    trouble = EYE_REASON_AIRPLAY_BLACKOUT;
  }

  if (trouble == EYE_REASON_AIRPLAY_OK) {
    // Require at least 2 healthy frames before leaving camera fallback to prevent rapid flapping.
    if (m->active == EYE_ACTIVE_CAMERA && m->healthy_au_streak < 2) {
      return m->active;
    }
    set_active(m, EYE_ACTIVE_AIRPLAY, EYE_REASON_AIRPLAY_OK);
  } else if (m->cam_present) {
    set_active(m, EYE_ACTIVE_CAMERA, trouble);
  } else {
    // No physical camera attached: keep AirPlay if still streaming (even frozen/tiny), else none.
    set_active(m, airplay_streaming ? EYE_ACTIVE_AIRPLAY : EYE_ACTIVE_NONE, trouble);
  }
  return m->active;
}

bool vision_mux_cam_due(vision_mux_t *m, uint32_t now_ms) {
  if (!m || m->active != EYE_ACTIVE_CAMERA || !m->cam_present) return false;
  if (m->last_cam_snap_ms != 0 && (uint32_t)(now_ms - m->last_cam_snap_ms) < m->cam_interval_ms) {
    return false;
  }
  m->last_cam_snap_ms = now_ms ? now_ms : 1;
  return true;
}

bool vision_map_cam_to_abs(const vision_roi_t *roi, uint16_t cam_x, uint16_t cam_y, uint16_t *abs_x, uint16_t *abs_y) {
  if (!roi || !abs_x || !abs_y) return false;
  if (cam_x < roi->x0 || cam_x > roi->x1 || cam_y < roi->y0 || cam_y > roi->y1) return false;
  uint32_t w = roi->x1 - roi->x0;
  uint32_t h = roi->y1 - roi->y0;
  if (w == 0 || h == 0) return false;

  uint32_t nx = ((uint32_t)(cam_x - roi->x0) * VISION_NORM_MAX) / w;
  uint32_t ny = ((uint32_t)(cam_y - roi->y0) * VISION_NORM_MAX) / h;
  uint32_t rx = nx, ry = ny;

  switch (roi->rotation_deg) {
    case 0:
      rx = nx;
      ry = ny;
      break;
    case 90:
      rx = ny;
      ry = VISION_NORM_MAX - nx;
      break;
    case 180:
      rx = VISION_NORM_MAX - nx;
      ry = VISION_NORM_MAX - ny;
      break;
    case 270:
      rx = VISION_NORM_MAX - ny;
      ry = nx;
      break;
    default:
      return false;
  }

  *abs_x = (uint16_t)((rx * VISION_ABS_MAX + (VISION_NORM_MAX / 2)) / VISION_NORM_MAX);
  *abs_y = (uint16_t)((ry * VISION_ABS_MAX + (VISION_NORM_MAX / 2)) / VISION_NORM_MAX);
  return true;
}

const char *vision_mode_name(eye_mode_t mode) {
  switch (mode) {
    case EYE_MODE_AUTO: return "auto";
    case EYE_MODE_AIRPLAY: return "airplay";
    case EYE_MODE_CAMERA: return "cam";
    default: return "unknown";
  }
}

const char *vision_active_name(eye_active_t active) {
  switch (active) {
    case EYE_ACTIVE_AIRPLAY: return "airplay";
    case EYE_ACTIVE_CAMERA: return "cam";
    default: return "none";
  }
}

const char *vision_reason_name(eye_reason_t reason) {
  switch (reason) {
    case EYE_REASON_Forced: return "forced";
    case EYE_REASON_AIRPLAY_OK: return "airplay_ok";
    case EYE_REASON_AIRPLAY_IDLE: return "airplay_idle";
    case EYE_REASON_AIRPLAY_STALE: return "airplay_stale";
    case EYE_REASON_AIRPLAY_FROZEN: return "airplay_frozen";
    case EYE_REASON_AIRPLAY_BLACKOUT: return "airplay_blackout";
    case EYE_REASON_NO_CAMERA: return "no_camera";
    default: return "unknown";
  }
}
