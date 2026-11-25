#include "at_parser.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void modem_state_init(modem_state_t *st) {
  if (!st) return;
  memset(st, 0, sizeof *st);
  st->rssi_raw = 99;
  st->rssi_dbm = 0;
  st->reg = CELL_REG_NONE;
}

static void normalize_phone(const char *in, char *out, size_t cap) {
  size_t j = 0;
  if (!in || !cap) return;
  for (size_t i = 0; in[i] && j + 1 < cap; i++) {
    if (isdigit((unsigned char)in[i]) || (in[i] == '+' && j == 0)) {
      out[j++] = in[i];
    }
  }
  out[j] = '\0';
}

bool modem_allowlist_add(modem_state_t *st, const char *phone) {
  if (!st || !phone || st->allowlist_count >= MODEM_MAX_ALLOWLIST) return false;
  char norm[MODEM_PHONE_MAX];
  normalize_phone(phone, norm, sizeof norm);
  if (strlen(norm) < 5) return false;
  for (int i = 0; i < st->allowlist_count; i++) {
    if (strcmp(st->allowlist[i], norm) == 0) return true;
  }
  snprintf(st->allowlist[st->allowlist_count++], MODEM_PHONE_MAX, "%s", norm);
  return true;
}

void modem_allowlist_clear(modem_state_t *st) {
  if (!st) return;
  memset(st->allowlist, 0, sizeof st->allowlist);
  st->allowlist_count = 0;
}

bool modem_allowlist_ok(const modem_state_t *st, const char *sender) {
  if (!st || !sender || st->allowlist_count <= 0) return false;  // fail-closed when allowlist is empty!
  char norm[MODEM_PHONE_MAX];
  normalize_phone(sender, norm, sizeof norm);
  size_t nlen = strlen(norm);
  if (nlen < 5) return false;
  for (int i = 0; i < st->allowlist_count; i++) {
    if (strcmp(st->allowlist[i], norm) == 0) return true;
  }
  return false;
}

// Converts SIMCom NMEA ddmm.mmmmmm (lat) or dddmm.mmmmmm (lon) to decimal degrees.
static bool parse_nmea_coord(const char *val, char hemi, double *out_deg) {
  if (!val || !*val || !out_deg) return false;
  char *end = NULL;
  double raw = strtod(val, &end);
  if (end == val || raw < 0.0) return false;
  int deg = (int)(raw / 100.0);
  double mins = raw - (double)deg * 100.0;
  if (mins < 0.0 || mins >= 60.0) return false;
  double dec = (double)deg + (mins / 60.0);
  if (hemi == 'S' || hemi == 'W') dec = -dec;
  else if (hemi != 'N' && hemi != 'E') return false;
  *out_deg = dec;
  return true;
}

// +CGPSINFO: 3113.343286,N,12121.234064,E,250311,072809.3,44.1,0.0,0
static void parse_cgpsinfo(modem_state_t *st, const char *p) {
  while (*p == ' ') p++;
  if (*p == ',' || !*p) {
    st->gps.valid = false;
    return;
  }
  char buf[128];
  snprintf(buf, sizeof buf, "%s", p);
  char *fields[9] = {0};
  int nf = 0;
  char *cur = buf;
  while (nf < 9 && cur) {
    fields[nf++] = cur;
    char *comma = strchr(cur, ',');
    if (comma) {
      *comma = '\0';
      cur = comma + 1;
    } else {
      cur = NULL;
    }
  }
  if (nf < 8 || !fields[0][0] || !fields[1][0] || !fields[2][0] || !fields[3][0]) {
    st->gps.valid = false;
    return;
  }
  double lat = 0.0, lon = 0.0;
  if (!parse_nmea_coord(fields[0], fields[1][0], &lat) ||
      !parse_nmea_coord(fields[2], fields[3][0], &lon)) {
    st->gps.valid = false;
    return;
  }
  st->gps.lat = lat;
  st->gps.lon = lon;
  st->gps.alt_m = fields[6][0] ? strtof(fields[6], NULL) : 0.0f;
  st->gps.speed_knots = fields[7][0] ? strtof(fields[7], NULL) : 0.0f;
  st->gps.valid = true;
}

static void parse_sms_body(modem_state_t *st, const char *body) {
  if (!modem_allowlist_ok(st, st->pending_sender)) {
    st->sms_rejected++;
    return;
  }
  while (*body == ' ') body++;
  char verb[16] = {0};
  int i = 0;
  while (body[i] && !isspace((unsigned char)body[i]) && i < (int)sizeof(verb) - 1) {
    verb[i] = (char)toupper((unsigned char)body[i]);
    i++;
  }
  const char *rest = body + i;
  while (*rest == ' ') rest++;

  sms_cmd_kind_t cmd = SMS_CMD_NONE;
  if (strcmp(verb, "STATUS") == 0) cmd = SMS_CMD_STATUS;
  else if (strcmp(verb, "WAKE") == 0) cmd = SMS_CMD_WAKE;
  else if (strcmp(verb, "UNLOCK") == 0) cmd = SMS_CMD_UNLOCK;
  else if (strcmp(verb, "AIRPLAY") == 0) cmd = SMS_CMD_AIRPLAY;
  else if (strcmp(verb, "CAMSNAP") == 0) cmd = SMS_CMD_CAMSNAP;
  else if (strcmp(verb, "REBOOT") == 0) cmd = SMS_CMD_REBOOT;

  if (cmd == SMS_CMD_NONE) return;

  // For UNLOCK, only allow optional pure digits as PIN argument (never arbitrary shell/AT injection!)
  st->pending_cmd_arg[0] = '\0';
  if (cmd == SMS_CMD_UNLOCK && *rest) {
    size_t k = 0;
    while (rest[k] && isdigit((unsigned char)rest[k]) && k < sizeof(st->pending_cmd_arg) - 1) {
      st->pending_cmd_arg[k] = rest[k];
      k++;
    }
    st->pending_cmd_arg[k] = '\0';
  }
  st->pending_cmd = cmd;
  snprintf(st->last_sms_sender, sizeof st->last_sms_sender, "%s", st->pending_sender);
}

void modem_parse_line(modem_state_t *st, const char *line) {
  if (!st || !line) return;

  if (st->awaiting_sms_body) {
    st->awaiting_sms_body = false;
    parse_sms_body(st, line);
    return;
  }

  // +CSQ: 21,99
  if (strncmp(line, "+CSQ:", 5) == 0) {
    int rssi = 99, ber = 99;
    if (sscanf(line + 5, " %d,%d", &rssi, &ber) >= 1) {
      st->rssi_raw = rssi;
      if (rssi >= 0 && rssi <= 31) {
        st->rssi_dbm = -113 + (rssi * 2);
        st->bars = rssi < 6 ? 1 : (rssi < 12 ? 2 : (rssi < 20 ? 3 : 4));
      } else {
        st->rssi_dbm = 0;
        st->bars = 0;
      }
    }
    return;
  }

  // +CREG: 0,1  or  +CEREG: 0,5  or unsolicited +CEREG: 1
  if (strncmp(line, "+CREG:", 6) == 0 || strncmp(line, "+CEREG:", 7) == 0) {
    const char *p = strchr(line, ':') + 1;
    int a = 0, b = -1;
    int n = sscanf(p, " %d,%d", &a, &b);
    int stat = (n == 2) ? b : a;
    if (stat >= 0 && stat <= 5) st->reg = (cell_reg_t)stat;
    return;
  }

  // +CGPSINFO: ...
  if (strncmp(line, "+CGPSINFO:", 10) == 0) {
    parse_cgpsinfo(st, line + 10);
    return;
  }

  // +NETOPEN: 0
  if (strncmp(line, "+NETOPEN:", 9) == 0) {
    int err = -1;
    if (sscanf(line + 9, " %d", &err) == 1) st->net_open = (err == 0);
    return;
  }
  if (strncmp(line, "+NETCLOSE:", 10) == 0) {
    st->net_open = false;
    st->tcp_connected = false;
    return;
  }

  // +CIPOPEN: 0,0
  if (strncmp(line, "+CIPOPEN:", 9) == 0) {
    int link = -1, err = -1;
    if (sscanf(line + 9, " %d,%d", &link, &err) == 2 && link == 0) {
      st->last_cip_err = err;
      st->tcp_connected = (err == 0);
    }
    return;
  }
  if (strncmp(line, "+CIPCLOSE:", 10) == 0 || strncmp(line, "+IPCLOSE:", 9) == 0 || strcmp(line, "CLOSED") == 0) {
    st->tcp_connected = false;
    return;
  }

  // +CMGR: "REC UNREAD","+15550199","","26/11/24,22:10:00-20"
  if (strncmp(line, "+CMGR:", 6) == 0) {
    const char *q1 = strchr(line + 6, '"');
    const char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
    const char *q3 = q2 ? strchr(q2 + 1, '"') : NULL;
    const char *q4 = q3 ? strchr(q3 + 1, '"') : NULL;
    if (q3 && q4 && q4 > q3 + 1) {
      size_t len = (size_t)(q4 - (q3 + 1));
      if (len >= sizeof st->pending_sender) len = sizeof(st->pending_sender) - 1;
      memcpy(st->pending_sender, q3 + 1, len);
      st->pending_sender[len] = '\0';
      st->awaiting_sms_body = true;
    }
    return;
  }
}

sms_cmd_kind_t modem_pop_sms_cmd(modem_state_t *st, char *arg_out, size_t arg_cap, char *sender_out, size_t sender_cap) {
  if (!st || st->pending_cmd == SMS_CMD_NONE) return SMS_CMD_NONE;
  sms_cmd_kind_t cmd = st->pending_cmd;
  if (arg_out && arg_cap) snprintf(arg_out, arg_cap, "%s", st->pending_cmd_arg);
  if (sender_out && sender_cap) snprintf(sender_out, sender_cap, "%s", st->last_sms_sender);
  st->pending_cmd = SMS_CMD_NONE;
  st->pending_cmd_arg[0] = '\0';
  return cmd;
}

size_t modem_format_status_sms(const modem_state_t *st, uint8_t battery_pct, const char *eye_state,
                               const char *ap_state, char *out, size_t cap) {
  if (!st || !out || !cap) return 0;
  int n;
  if (st->gps.valid) {
    n = snprintf(out, cap, "KVM bat:%u%% 4G:%ddBm(%ub) tun:%s ap:%s eye:%s gps:%.5f,%.5f",
                 battery_pct, st->rssi_dbm, st->bars, st->tcp_connected ? "up" : "down",
                 ap_state ? ap_state : "?", eye_state ? eye_state : "?", st->gps.lat, st->gps.lon);
  } else {
    n = snprintf(out, cap, "KVM bat:%u%% 4G:%ddBm(%ub) tun:%s ap:%s eye:%s gps:nofix",
                 battery_pct, st->rssi_dbm, st->bars, st->tcp_connected ? "up" : "down",
                 ap_state ? ap_state : "?", eye_state ? eye_state : "?");
  }
  return n < 0 ? 0 : ((size_t)n >= cap ? cap - 1 : (size_t)n);
}
