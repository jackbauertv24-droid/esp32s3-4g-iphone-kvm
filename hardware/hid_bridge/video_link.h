// Mirrored H.264 from AirPlay + fallback OV2640 JPEG frames to BLE / Wi-Fi / 4G:
// keeps the current GOP in PSRAM (auto-scaled for 2 MB Quad-PSRAM boards like ESP32-S3-A7670E-4G)
// and sends it on `snap`, or every access unit while `stream on`.
#pragma once
#include <Arduino.h>

bool video_begin();
void video_on_config(const uint8_t *sps_pps, size_t len);  // AirPlay callbacks (mirror task)
void video_on_au(const uint8_t *au, size_t len, bool idr);
void video_on_lost();
void video_stream(bool on, char who);
void video_snap(const String &id, char who);
void video_bench(const String &id, uint32_t kb, char who);

// Sends a JPEG frame from the optical fallback camera over the active video chunk channel (VF_JPEG).
bool video_send_jpeg(const uint8_t *jpeg, size_t len, bool is_snap, char who);
bool video_anyone_streaming(char *active_who);

struct VideoStats {
  uint32_t kbps, idr_ms, gop_bytes;
  uint32_t age_ms;
  float fps;
  uint32_t overflows;
  bool frozen;
  uint32_t lost;
};
VideoStats video_stats(char who);
String video_debug();

struct VideoDiag {
  uint32_t aus, bytes, idrs;
  uint32_t maxGapMs;
  uint32_t sinceIdrMs;
  int gopAus;
  uint32_t gopKb, overflows, frozenMs;
  bool frozen;
  uint32_t wifiChunks, wifiFails, bleChunks, bleFails, flips;
  uint32_t resyncs, resumes, resumeMisses, tailBreaks;
  bool following;
  char lastWhy;
};
VideoDiag video_diag();
