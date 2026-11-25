// Unit test suite for the Waveshare ESP32-S3-A7670E-4G iPhone KVM upgrades:
//   1. Dual-Eye Vision Multiplexer (AirPlay primary + OV2640 optical fallback + ROI mapping)
//   2. Wireless BLE/USB Absolute Pointer descriptor & deterministic iOS recovery macros
//   3. SIMCom A7670E 4G LTE AT/GPS/SMS parser & sender allowlist security
//   4. GOP buffer & chunker compatibility with VF_JPEG optical frames
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../hid_bridge/src/video/gop.h"
#include "../hid_bridge/src/video/vision_mux.h"
#include "../hid_bridge/src/hid/abs_desc.h"
#include "../hid_bridge/src/hid/ios_macros.h"
#include "../hid_bridge/src/modem/at_parser.h"

static void test_vision_mux_fallback_and_recovery(void) {
  vision_mux_t m;
  vision_mux_init(&m, true);

  // Boot with AirPlay idle -> optical camera immediately active so lock screen is visible!
  assert(vision_mux_poll(&m, false, false, 1000) == EYE_ACTIVE_CAMERA);
  assert(m.reason == EYE_REASON_AIRPLAY_IDLE);
  assert(vision_mux_cam_due(&m, 1000) == true);
  assert(vision_mux_cam_due(&m, 1200) == false);  // throttled by 500 ms interval
  assert(vision_mux_cam_due(&m, 1505) == true);

  // AirPlay starts streaming healthy H.264 frames -> switches cleanly to AirPlay after 2 frames
  vision_mux_on_airplay_au(&m, 18500, true, 2000);
  assert(vision_mux_poll(&m, true, false, 2010) == EYE_ACTIVE_CAMERA);  // hysteresis: 1st frame
  vision_mux_on_airplay_au(&m, 1400, false, 2200);
  assert(vision_mux_poll(&m, true, false, 2210) == EYE_ACTIVE_AIRPLAY);
  assert(m.reason == EYE_REASON_AIRPLAY_OK);
  assert(vision_mux_cam_due(&m, 3000) == false);  // camera sleeps while AirPlay is healthy

  // Simulate iOS Passcode / 2FA blackout: AirPlay stays "streaming" but sends tiny blank slices
  for (int i = 0; i < 25; i++) {
    vision_mux_on_airplay_au(&m, 120, false, 3000 + i * 100);
  }
  assert(vision_mux_poll(&m, true, false, 5600) == EYE_ACTIVE_CAMERA);
  assert(m.reason == EYE_REASON_AIRPLAY_BLACKOUT);
  assert(m.fallback_switches == 1);

  // Passcode entered -> real UI frames resume -> back to crisp AirPlay
  vision_mux_on_airplay_au(&m, 22000, true, 6000);
  vision_mux_on_airplay_au(&m, 2400, false, 6200);
  assert(vision_mux_poll(&m, true, false, 6250) == EYE_ACTIVE_AIRPLAY);

  // Simulate stalled AirPlay stream (> 3500 ms without AUs) -> falls back to camera
  assert(vision_mux_poll(&m, true, false, 10000) == EYE_ACTIVE_CAMERA);
  assert(m.reason == EYE_REASON_AIRPLAY_STALE);
  assert(m.fallback_switches == 2);

  // Manual override modes
  vision_mux_set_mode(&m, EYE_MODE_CAMERA);
  vision_mux_on_airplay_au(&m, 25000, true, 10500);
  vision_mux_on_airplay_au(&m, 3000, false, 10600);
  assert(vision_mux_poll(&m, true, false, 10650) == EYE_ACTIVE_CAMERA);
}

static void test_vision_roi_mapping(void) {
  vision_mux_t m;
  vision_mux_init(&m, true);
  assert(!vision_mux_set_roi(&m, 5000, 1000, 4000, 9000, 0));   // invalid x0 >= x1
  assert(!vision_mux_set_roi(&m, 1000, 1000, 9000, 9000, 45));  // invalid rotation
  assert(vision_mux_set_roi(&m, 2000, 1000, 8000, 9000, 0));

  uint16_t ax = 0, ay = 0;
  // Outside the iPhone bezel in the camera photo -> rejected safely
  assert(!vision_map_cam_to_abs(&m.roi, 1500, 5000, &ax, &ay));

  // Dead center of the cropped iPhone region -> (16384, 16384)
  assert(vision_map_cam_to_abs(&m.roi, 5000, 5000, &ax, &ay));
  assert(ax == 16384 && ay == 16384);

  // Corners at 0 deg
  assert(vision_map_cam_to_abs(&m.roi, 2000, 1000, &ax, &ay) && ax == 0 && ay == 0);
  assert(vision_map_cam_to_abs(&m.roi, 8000, 9000, &ax, &ay) && ax == 32767 && ay == 32767);

  // 180-degree inverted camera mount
  assert(vision_mux_set_roi(&m, 2000, 1000, 8000, 9000, 180));
  assert(vision_map_cam_to_abs(&m.roi, 2000, 1000, &ax, &ay) && ax == 32767 && ay == 32767);
}

static void test_abs_hid_and_ios_macros(void) {
  uint8_t desc[] = { HID_REPORT_DESC_ABS_POINTER(4) };
  assert(sizeof(desc) > 40);
  uint8_t pkt[6];
  hid_pack_abs_report(pkt, 1, 16384, 32767, -3);
  assert(pkt[0] == 1);
  assert((pkt[1] | (pkt[2] << 8)) == 16384);
  assert((pkt[3] | (pkt[4] << 8)) == 32767);
  assert((int8_t)pkt[5] == -3);

  ios_macro_plan_t plan;
  // BLE homescreen replacement
  assert(ios_macro_build("homeswipe", NULL, &plan));
  assert(plan.count == 1 && plan.steps[0].kind == IOS_STEP_ABS_SWIPE);
  assert(plan.steps[0].y > plan.steps[0].y2);

  // Unlock with PIN validation (rejects non-numeric injection!)
  assert(!ios_macro_build("unlock 12ab", NULL, &plan));
  assert(ios_macro_build("unlock 258096", NULL, &plan));
  assert(plan.count == 5);
  assert(plan.steps[4].kind == IOS_STEP_TYPE_PIN && strcmp(plan.steps[4].text, "258096") == 0);

  // Autonomous AirPlay self-recovery sequence
  assert(ios_macro_build("airplay 22000 9500 16000 14500", NULL, &plan));
  assert(plan.count == 7);
  assert(plan.steps[0].kind == IOS_STEP_ABS_SWIPE);  // Control Center pull-down
  assert(plan.steps[2].kind == IOS_STEP_ABS_TAP && plan.steps[2].x == 22000);
  assert(plan.steps[4].kind == IOS_STEP_ABS_TAP && plan.steps[4].y == 14500);
}

static void test_a7670e_at_gps_and_sms_security(void) {
  modem_state_t st;
  modem_state_init(&st);

  // Signal + LTE registration + TCP tunnel
  modem_parse_line(&st, "+CSQ: 22,99");
  assert(st.rssi_dbm == -69 && st.bars == 4);
  modem_parse_line(&st, "+CEREG: 0,5");
  assert(st.reg == CELL_REG_ROAMING);
  modem_parse_line(&st, "+NETOPEN: 0");
  modem_parse_line(&st, "+CIPOPEN: 0,0");
  assert(st.net_open && st.tcp_connected);

  // GPS NMEA ddmm.mmmmmm conversion
  modem_parse_line(&st, "+CGPSINFO: 4042.768000,N,07400.360000,W,241126,223000.0,12.5,1.2,0");
  assert(st.gps.valid);
  assert(fabs(st.gps.lat - 40.7128) < 0.0001);
  assert(fabs(st.gps.lon - (-74.0060)) < 0.0001);

  // Fail-closed SMS security: empty allowlist MUST reject all SMS commands!
  modem_parse_line(&st, "+CMGR: \"REC UNREAD\",\"+15559998888\",\"\",\"26/11/24,22:30:00\"");
  modem_parse_line(&st, "UNLOCK 1234");
  assert(modem_pop_sms_cmd(&st, NULL, 0, NULL, 0) == SMS_CMD_NONE);
  assert(st.sms_rejected == 1);

  // Add authorized owner number -> attacker number still rejected, owner accepted!
  assert(modem_allowlist_add(&st, "+1 (555) 010-2030"));
  modem_parse_line(&st, "+CMGR: \"REC UNREAD\",\"+15559998888\",\"\",\"26/11/24,22:31:00\"");
  modem_parse_line(&st, "REBOOT");
  assert(modem_pop_sms_cmd(&st, NULL, 0, NULL, 0) == SMS_CMD_NONE);
  assert(st.sms_rejected == 2);

  modem_parse_line(&st, "+CMGR: \"REC UNREAD\",\"+15550102030\",\"\",\"26/11/24,22:32:00\"");
  modem_parse_line(&st, "unlock 8426; AT+CFUN=1,1");  // attempts trailing injection -> sanitized to digits only!
  char arg[16] = {0}, sender[24] = {0};
  assert(modem_pop_sms_cmd(&st, arg, sizeof arg, sender, sizeof sender) == SMS_CMD_UNLOCK);
  assert(strcmp(arg, "8426") == 0);
  assert(strcmp(sender, "+15550102030") == 0);

  char sms[160];
  size_t n = modem_format_status_sms(&st, 84, "cam", "idle", sms, sizeof sms);
  assert(n > 20 && n < 160);
  assert(strstr(sms, "bat:84%") != NULL && strstr(sms, "eye:cam") != NULL);
}

static void test_jpeg_vchunk_framing(void) {
  // Verify VF_JPEG (0x80) chunks cleanly through gop.c's vchunk_begin / vchunk_next
  uint8_t fake_jpeg[600];
  fake_jpeg[0] = 0xFF;
  fake_jpeg[1] = 0xD8;  // SOI
  for (int i = 2; i < 598; i++) fake_jpeg[i] = (uint8_t)(i & 0xFF);
  fake_jpeg[598] = 0xFF;
  fake_jpeg[599] = 0xD9;  // EOI

  vchunk c;
  vchunk_begin(&c, fake_jpeg, sizeof fake_jpeg, VF_JPEG | VF_SNAP, 12345);
  uint8_t out[256];
  uint8_t reassembled[600];
  size_t rpos = 0;
  uint16_t seq = 1;
  int chunks = 0;
  for (size_t n; (n = vchunk_next(&c, out, 200, seq)) > 0; seq++, chunks++) {
    uint8_t flags = out[2];
    assert((flags & VF_JPEG) != 0);
    size_t hdr = VCHUNK_HDR + ((flags & VF_START) ? 4 : 0);
    memcpy(reassembled + rpos, out + hdr, n - hdr);
    rpos += n - hdr;
  }
  assert(chunks == 4 && rpos == sizeof fake_jpeg);
  assert(memcmp(reassembled, fake_jpeg, sizeof fake_jpeg) == 0);
}

int main(void) {
  test_vision_mux_fallback_and_recovery();
  test_vision_roi_mapping();
  test_abs_hid_and_ios_macros();
  test_a7670e_at_gps_and_sms_security();
  test_jpeg_vchunk_framing();
  puts("All ESP32-S3-A7670E-4G Dual-Eye, BLE Abs-HID, Macro & Cellular unit tests PASSED!");
  return 0;
}
