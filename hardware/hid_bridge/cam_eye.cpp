#include "cam_eye.h"
#include <Preferences.h>
#include "a7670e_pins.h"
#include "ble_link.h"
#include "video_link.h"
#include "logbuf.h"

#if defined(WAVESHARE_A7670E) && defined(ARDUINO_ARCH_ESP32)
#include "esp_camera.h"
#define HAS_ESP_CAMERA 1
#else
#define HAS_ESP_CAMERA 0
#endif

static vision_mux_t vmux;
static bool camReady = false;
static int cfgAec = 0;        // manual exposure by default to prevent OLED screen blowout in dark enclosures
static int cfgExposure = 280; // crisp shutter time for 40-60% iPhone brightness
static int cfgQuality = 14;   // JPEG quality (lower = sharper, 10..63)

static void applySensorPreset() {
#if HAS_ESP_CAMERA
  sensor_t *s = esp_camera_sensor_get();
  if (!s) return;
  s->set_quality(s, cfgQuality);
  s->set_brightness(s, 0);
  s->set_contrast(s, 1);
  s->set_exposure_ctrl(s, cfgAec ? 1 : 0);
  if (!cfgAec) s->set_aec_value(s, cfgExposure);
  s->set_gain_ctrl(s, 0);
  s->set_agc_gain(s, 2);
  s->set_whitebal(s, 1);
#endif
}

static void loadNvs() {
  Preferences p;
  if (!p.begin("cameye", true)) return;
  uint16_t x0 = p.getUShort("x0", 0);
  uint16_t y0 = p.getUShort("y0", 0);
  uint16_t x1 = p.getUShort("x1", 10000);
  uint16_t y1 = p.getUShort("y1", 10000);
  uint16_t rot = p.getUShort("rot", 0);
  uint8_t mode = p.getUChar("mode", EYE_MODE_AUTO);
  cfgAec = p.getInt("aec", 0);
  cfgExposure = p.getInt("exp", 280);
  cfgQuality = p.getInt("qual", 14);
  p.end();
  vision_mux_set_roi(&vmux, x0, y0, x1, y1, rot);
  if (mode <= EYE_MODE_CAMERA) vision_mux_set_mode(&vmux, (eye_mode_t)mode);
}

bool cam_eye_begin() {
#if HAS_ESP_CAMERA
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = A7670E_CAM_Y2;
  config.pin_d1 = A7670E_CAM_Y3;
  config.pin_d2 = A7670E_CAM_Y4;
  config.pin_d3 = A7670E_CAM_Y5;
  config.pin_d4 = A7670E_CAM_Y6;
  config.pin_d5 = A7670E_CAM_Y7;
  config.pin_d6 = A7670E_CAM_Y8;
  config.pin_d7 = A7670E_CAM_Y9;
  config.pin_xclk = A7670E_CAM_XCLK;
  config.pin_pclk = A7670E_CAM_PCLK;
  config.pin_vsync = A7670E_CAM_VSYNC;
  config.pin_href = A7670E_CAM_HREF;
  config.pin_sccb_sda = A7670E_CAM_SIOD;
  config.pin_sccb_scl = A7670E_CAM_SIOC;
  config.pin_pwdn = A7670E_CAM_PWDN;
  config.pin_reset = A7670E_CAM_RESET;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_SVGA;  // 800x600 JPEG (~25-45 KB in PSRAM, legible iOS text)
  config.jpeg_quality = cfgQuality;
  config.fb_count = 1;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  camReady = (err == ESP_OK);
  if (!camReady) {
    board_log("cameye: init skipped/failed (0x%x) - check CAM DIP switch", (unsigned)err);
  } else {
    board_log("cameye: OV camera ready (SVGA JPEG)");
  }
#else
  camReady = false;
#endif
  vision_mux_init(&vmux, camReady);
  loadNvs();
  if (camReady) applySensorPreset();
  return camReady;
}

bool cam_eye_available() { return camReady; }
vision_mux_t *cam_vision_mux() { return &vmux; }

bool cam_eye_configure(int aec, int exposure, int quality) {
  if ((aec != 0 && aec != 1) || exposure < 10 || exposure > 1200 || quality < 4 || quality > 63) return false;
  cfgAec = aec;
  cfgExposure = exposure;
  cfgQuality = quality;
  Preferences p;
  if (p.begin("cameye", false)) {
    p.putInt("aec", cfgAec);
    p.putInt("exp", cfgExposure);
    p.putInt("qual", cfgQuality);
    p.end();
  }
  if (camReady) applySensorPreset();
  return true;
}

bool cam_eye_set_roi(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t rot) {
  if (!vision_mux_set_roi(&vmux, x0, y0, x1, y1, rot)) return false;
  Preferences p;
  if (p.begin("cameye", false)) {
    p.putUShort("x0", x0);
    p.putUShort("y0", y0);
    p.putUShort("x1", x1);
    p.putUShort("y1", y1);
    p.putUShort("rot", rot);
    p.end();
  }
  return true;
}

bool cam_eye_snap_and_send(const String &id, char who, bool is_manual_snap) {
#if HAS_ESP_CAMERA
  if (!camReady) {
    if (id.length()) ble_reply(id + " err camera not initialized (check CAM DIP switch)", who);
    return false;
  }
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb || !fb->buf || fb->len == 0) {
    if (fb) esp_camera_fb_return(fb);
    if (id.length()) ble_reply(id + " err camera capture failed", who);
    return false;
  }
  size_t len = fb->len;
  bool ok = video_send_jpeg(fb->buf, len, is_manual_snap, who);
  esp_camera_fb_return(fb);
  if (id.length()) {
    ble_reply(ok ? (id + " ok " + String((unsigned long)len)) : (id + " err jpeg send failed"), who);
  }
  return ok;
#else
  (void)who;
  (void)is_manual_snap;
  if (id.length()) ble_reply(id + " err camera support requires --a7670e build", who);
  return false;
#endif
}

void cam_eye_poll(bool airplay_streaming, bool airplay_frozen, bool anyone_streaming, char active_who) {
  eye_active_t prev = vmux.active;
  eye_active_t cur = vision_mux_poll(&vmux, airplay_streaming, airplay_frozen, millis());
  if (cur != prev) {
    board_log("vision: eye %s -> %s (%s)", vision_active_name(prev), vision_active_name(cur),
              vision_reason_name(vmux.reason));
  }
  if (anyone_streaming && cur == EYE_ACTIVE_CAMERA && vision_mux_cam_due(&vmux, millis())) {
    cam_eye_snap_and_send("", active_who, false);
  }
}

String cam_eye_status_json() {
  char buf[220];
  snprintf(buf, sizeof buf,
           "{\"cam\":%d,\"mode\":\"%s\",\"active\":\"%s\",\"why\":\"%s\",\"sw\":%lu,"
           "\"roi\":[%u,%u,%u,%u,%u],\"aec\":%d,\"exp\":%d,\"q\":%d}",
           camReady ? 1 : 0, vision_mode_name(vmux.mode), vision_active_name(vmux.active),
           vision_reason_name(vmux.reason), (unsigned long)vmux.fallback_switches,
           vmux.roi.x0, vmux.roi.y0, vmux.roi.x1, vmux.roi.y1, vmux.roi.rotation_deg,
           cfgAec, cfgExposure, cfgQuality);
  return String(buf);
}
