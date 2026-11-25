// Pure-C AT command, GPS NMEA, and SMS command parser for the SIMCom A7670E 4G LTE Cat-1 modem.
// Provides out-of-band cellular telemetry, GPS coordinate decoding, allowlisted SMS emergency
// recovery commands, and TCP reverse-tunnel frame framing without dynamic allocation.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODEM_MAX_ALLOWLIST 4
#define MODEM_PHONE_MAX     24
#define MODEM_SMS_TEXT_MAX  160

typedef enum {
  CELL_REG_NONE = 0,
  CELL_REG_HOME = 1,
  CELL_REG_SEARCHING = 2,
  CELL_REG_DENIED = 3,
  CELL_REG_UNKNOWN = 4,
  CELL_REG_ROAMING = 5
} cell_reg_t;

typedef enum {
  SMS_CMD_NONE = 0,
  SMS_CMD_STATUS = 1,   // reply with battery %, 4G signal, AirPlay/eye state, GPS link
  SMS_CMD_WAKE = 2,     // wake iPhone screen
  SMS_CMD_UNLOCK = 3,   // wake + swipe up (+ optional configured unlock macro)
  SMS_CMD_AIRPLAY = 4,  // trigger autonomous Control Center -> Screen Mirroring recovery
  SMS_CMD_CAMSNAP = 5,  // force optical camera mode & capture snapshot
  SMS_CMD_REBOOT = 6    // safely reboot the ESP32-S3 bridge
} sms_cmd_kind_t;

typedef struct {
  bool valid;
  double lat;        // signed decimal degrees (+N, -S)
  double lon;        // signed decimal degrees (+E, -W)
  float alt_m;
  float speed_knots;
} modem_gps_t;

typedef struct {
  int rssi_raw;          // 0..31 or 99 (unknown)
  int rssi_dbm;          // -113..-51 dBm, or 0 when unknown
  uint8_t bars;          // 0..4 signal bars
  cell_reg_t reg;
  bool net_open;         // +NETOPEN: 0 active
  bool tcp_connected;    // link 0 open to remote relay
  int last_cip_err;
  modem_gps_t gps;
  char allowlist[MODEM_MAX_ALLOWLIST][MODEM_PHONE_MAX];
  int allowlist_count;
  // Two-line +CMGR SMS parser state
  bool awaiting_sms_body;
  char pending_sender[MODEM_PHONE_MAX];
  // Latest verified SMS emergency command
  sms_cmd_kind_t pending_cmd;
  char pending_cmd_arg[16];
  char last_sms_sender[MODEM_PHONE_MAX];
  uint32_t sms_rejected;
} modem_state_t;

void modem_state_init(modem_state_t *st);
bool modem_allowlist_add(modem_state_t *st, const char *phone);
void modem_allowlist_clear(modem_state_t *st);
bool modem_allowlist_ok(const modem_state_t *st, const char *sender);

// Feeds one trimmed line (without \r\n) from the A7670E UART into the state machine.
// Updates signal, registration, GPS, tunnel status, and queues allowlisted SMS commands.
void modem_parse_line(modem_state_t *st, const char *line);

// Pops the latest authorized SMS command (returns SMS_CMD_NONE if none queued).
sms_cmd_kind_t modem_pop_sms_cmd(modem_state_t *st, char *arg_out, size_t arg_cap, char *sender_out, size_t sender_cap);

// Formats a compact human-readable status SMS (< 160 chars) for `STATUS` replies.
size_t modem_format_status_sms(const modem_state_t *st, uint8_t battery_pct, const char *eye_state,
                               const char *ap_state, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
