#include "modem_4g.h"
#include <Preferences.h>
#include "a7670e_pins.h"
#include "cli_link.h"
#include "logbuf.h"

static modem_state_t mst;
static SemaphoreHandle_t txLock;
static char cfgApn[48] = "internet";
static char cfgHost[64] = "";
static uint16_t cfgPort = 0;
static char cfgToken[48] = "";
static volatile bool modemPresent = false;
static volatile bool tunnelAuthed = false;
static uint32_t winStart = 0, winBytes = 0;
static volatile uint32_t cellKbps = 0;

// Outgoing SMS queue (1 slot) so main loop never touches UART1 directly
static char outSmsTo[MODEM_PHONE_MAX];
static char outSmsBody[MODEM_SMS_TEXT_MAX];
static volatile bool outSmsPending = false;

#if defined(WAVESHARE_A7670E) && defined(ARDUINO_ARCH_ESP32)
static HardwareSerial ModemSerial(A7670E_UART_NUM);

static void loadCellPrefs() {
  Preferences p;
  if (!p.begin("cell4g", true)) return;
  p.getString("apn", cfgApn, sizeof cfgApn);
  p.getString("host", cfgHost, sizeof cfgHost);
  cfgPort = p.getUShort("port", 0);
  p.getString("tok", cfgToken, sizeof cfgToken);
  int n = p.getUChar("aln", 0);
  for (int i = 0; i < n && i < MODEM_MAX_ALLOWLIST; i++) {
    char key[8], num[MODEM_PHONE_MAX];
    snprintf(key, sizeof key, "al%d", i);
    if (p.getString(key, num, sizeof num) > 0) modem_allowlist_add(&mst, num);
  }
  p.end();
}

static void saveCellPrefs() {
  Preferences p;
  if (!p.begin("cell4g", false)) return;
  p.putString("apn", cfgApn);
  p.putString("host", cfgHost);
  p.putUShort("port", cfgPort);
  p.putString("tok", cfgToken);
  p.putUChar("aln", (uint8_t)mst.allowlist_count);
  for (int i = 0; i < mst.allowlist_count; i++) {
    char key[8];
    snprintf(key, sizeof key, "al%d", i);
    p.putString(key, mst.allowlist[i]);
  }
  p.end();
}

static bool sendAtWait(const char *cmd, const char *expect, uint32_t timeout_ms) {
  if (cmd && *cmd) {
    ModemSerial.print(cmd);
    ModemSerial.print("\r\n");
  }
  uint32_t start = millis();
  String line;
  while ((uint32_t)(millis() - start) < timeout_ms) {
    while (ModemSerial.available()) {
      char c = (char)ModemSerial.read();
      if (c == '\r') continue;
      if (c == '\n') {
        line.trim();
        if (line.length()) {
          modem_parse_line(&mst, line.c_str());
          if (line.indexOf(expect) >= 0) return true;
          if (line == "ERROR" || line.startsWith("+CME ERROR")) return false;
        }
        line = "";
      } else if (line.length() < 256) {
        line += c;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return false;
}

// Reads framed [u16 LE len][payload] stream bytes arriving via SIMCom `+IPD<len>\r\n<bytes>`.
static uint8_t rxFrame[CLI_CELL_MTU + 64];
static size_t rxAccum = 0;

static void feedTunnelBytes(const uint8_t *buf, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (rxAccum < sizeof rxFrame) rxFrame[rxAccum++] = buf[i];
    while (rxAccum >= 2) {
      size_t flen = rxFrame[0] | ((size_t)rxFrame[1] << 8);
      if (flen == 0) {
        // Keepalive record
        memmove(rxFrame, rxFrame + 2, rxAccum - 2);
        rxAccum -= 2;
        continue;
      }
      if (flen > CLI_CELL_MTU + 32) {
        // Corrupt stream framing -> drop and reconnect cleanly
        rxAccum = 0;
        modem_tunnel_drop();
        return;
      }
      if (rxAccum < 2 + flen) break;
      cli_on_frame(CLI_CELL_SLOT, rxFrame + 2, flen);
      size_t rem = rxAccum - (2 + flen);
      if (rem) memmove(rxFrame, rxFrame + 2 + flen, rem);
      rxAccum = rem;
    }
  }
}

static bool sendRawTcp(const uint8_t *data, size_t len) {
  if (!mst.tcp_connected || len == 0) return false;
  char hdr[32];
  snprintf(hdr, sizeof hdr, "AT+CIPSEND=0,%u\r\n", (unsigned)len);
  ModemSerial.print(hdr);
  uint32_t t0 = millis();
  bool gotPrompt = false;
  while ((uint32_t)(millis() - t0) < 1500) {
    if (ModemSerial.available()) {
      char ch = (char)ModemSerial.read();
      if (ch == '>') { gotPrompt = true; break; }
    } else {
      vTaskDelay(1);
    }
  }
  if (!gotPrompt) return false;
  ModemSerial.write(data, len);
  return sendAtWait("", "+CIPSEND:", 2500);
}

static void openReverseTunnel() {
  if (!cfgHost[0] || !cfgPort || !mst.net_open) return;
  char cmd[128];
  snprintf(cmd, sizeof cmd, "AT+CIPOPEN=0,\"TCP\",\"%s\",%u", cfgHost, cfgPort);
  rxAccum = 0;
  tunnelAuthed = false;
  if (sendAtWait(cmd, "+CIPOPEN: 0,0", 8000)) {
    // Authenticate this 4G board on the remote relay with a single registration line:
    // "REG <board_mac_hex> <token>\n" before binary [u16 len][frame] records begin.
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    char reg[128];
    int n = snprintf(reg, sizeof reg, "REG %02x%02x%02x%02x%02x%02x %s\n",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], cfgToken[0] ? cfgToken : "default");
    xSemaphoreTake(txLock, portMAX_DELAY);
    bool ok = sendRawTcp((const uint8_t *)reg, (size_t)n);
    xSemaphoreGive(txLock);
    if (ok) {
      tunnelAuthed = true;
      board_log("cell: 4G reverse tunnel connected to %s:%u", cfgHost, cfgPort);
    }
  }
}

static void modemTask(void *) {
  // Ensure A7670E PWRKEY (GPIO 33) is asserted HIGH and wait for AT readiness
  pinMode(A7670E_PWRKEY_PIN, OUTPUT);
  digitalWrite(A7670E_PWRKEY_PIN, HIGH);
  vTaskDelay(pdMS_TO_TICKS(1200));

  ModemSerial.setRxBufferSize(4096);
  ModemSerial.begin(A7670E_DEFAULT_BAUD, SERIAL_8N1, A7670E_RX_PIN, A7670E_TX_PIN);

  // Sync at default 115200 or already-negotiated 921600 baud
  bool synced = false;
  for (int tries = 0; tries < 5 && !synced; tries++) {
    synced = sendAtWait("AT", "OK", 600);
  }
  if (!synced) {
    ModemSerial.updateBaudRate(A7670E_FAST_BAUD);
    synced = sendAtWait("AT", "OK", 600);
  } else {
    if (sendAtWait("AT+IPR=921600", "OK", 800)) {
      ModemSerial.updateBaudRate(A7670E_FAST_BAUD);
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
  modemPresent = synced;
  if (!modemPresent) {
    board_log("cell: A7670E not responding (check 4G DIP switch & 18650 battery)");
    for (;;) vTaskDelay(pdMS_TO_TICKS(5000));
  }

  board_log("cell: A7670E online at %d baud", A7670E_FAST_BAUD);
  sendAtWait("ATE0", "OK", 500);
  sendAtWait("AT+CMGF=1", "OK", 500);             // Text-mode SMS
  sendAtWait("AT+CNMI=2,1,0,0,0", "OK", 500);     // Notify on new SMS (+CMTI)
  sendAtWait("AT+CGNSSPWR=1", "OK", 1000);        // Power on GNSS engine
  sendAtWait("AT+CIPCCFG=5,500,0,1,0", "OK", 500);

  char apnCmd[96];
  snprintf(apnCmd, sizeof apnCmd, "AT+CGDCONT=1,\"IP\",\"%s\"", cfgApn);
  sendAtWait(apnCmd, "OK", 1000);

  uint32_t lastPoll = 0, lastTunnelTry = 0, lastKeepalive = 0;
  String line;

  for (;;) {
    // Drain incoming UART stream (handling both line URCs and +IPD<len> binary payloads)
    while (ModemSerial.available()) {
      char c = (char)ModemSerial.read();
      if (c == '\r') continue;
      if (c == '\n') {
        line.trim();
        if (line.startsWith("+IPD")) {
          int blen = atoi(line.c_str() + 4);
          if (blen > 0 && blen <= 2048) {
            uint8_t tmp[256];
            int rem = blen;
            uint32_t t0 = millis();
            while (rem > 0 && (uint32_t)(millis() - t0) < 1000) {
              int chunk = ModemSerial.readBytes(tmp, min((int)sizeof(tmp), rem));
              if (chunk > 0) {
                feedTunnelBytes(tmp, (size_t)chunk);
                rem -= chunk;
              } else {
                vTaskDelay(1);
              }
            }
          }
        } else if (line.startsWith("+CMTI:")) {
          int comma = line.indexOf(',');
          if (comma > 0) {
            int idx = line.substring(comma + 1).toInt();
            char rcmd[32];
            snprintf(rcmd, sizeof rcmd, "AT+CMGR=%d", idx);
            xSemaphoreTake(txLock, portMAX_DELAY);
            sendAtWait(rcmd, "OK", 1500);
            snprintf(rcmd, sizeof rcmd, "AT+CMGD=%d", idx);
            sendAtWait(rcmd, "OK", 1000);
            xSemaphoreGive(txLock);
          }
        } else if (line.length()) {
          bool wasUp = mst.tcp_connected;
          modem_parse_line(&mst, line.c_str());
          if (wasUp && !mst.tcp_connected) {
            tunnelAuthed = false;
            cli_reset(CLI_CELL_SLOT);
            board_log("cell: 4G tunnel closed");
          }
        }
        line = "";
      } else if (line.length() < 256) {
        line += c;
      }
    }

    // Outgoing SMS send
    if (outSmsPending) {
      xSemaphoreTake(txLock, portMAX_DELAY);
      char cmgs[48];
      snprintf(cmgs, sizeof cmgs, "AT+CMGS=\"%s\"", outSmsTo);
      if (sendAtWait(cmgs, ">", 2000)) {
        ModemSerial.print(outSmsBody);
        ModemSerial.write(0x1A);  // Ctrl+Z
        sendAtWait("", "OK", 5000);
      }
      outSmsPending = false;
      xSemaphoreGive(txLock);
    }

    uint32_t now = millis();
    if (now - lastPoll >= 10000) {
      lastPoll = now;
      xSemaphoreTake(txLock, portMAX_DELAY);
      sendAtWait("AT+CSQ", "OK", 500);
      sendAtWait("AT+CEREG?", "OK", 500);
      sendAtWait("AT+CGPSINFO", "OK", 500);
      if ((mst.reg == CELL_REG_HOME || mst.reg == CELL_REG_ROAMING) && !mst.net_open) {
        sendAtWait("AT+NETOPEN", "+NETOPEN:", 5000);
      }
      xSemaphoreGive(txLock);
    }

    if (cfgHost[0] && cfgPort && mst.net_open && !mst.tcp_connected && (now - lastTunnelTry >= 12000)) {
      lastTunnelTry = now;
      openReverseTunnel();
    }

    if (tunnelAuthed && mst.tcp_connected && (now - lastKeepalive >= 15000)) {
      lastKeepalive = now;
      uint8_t ka[2] = {0, 0};
      xSemaphoreTake(txLock, portMAX_DELAY);
      sendRawTcp(ka, 2);
      xSemaphoreGive(txLock);
    }

    vTaskDelay(pdMS_TO_TICKS(15));
  }
}
#endif

bool modem_4g_begin() {
  modem_state_init(&mst);
  txLock = xSemaphoreCreateMutex();
#if defined(WAVESHARE_A7670E) && defined(ARDUINO_ARCH_ESP32)
  loadCellPrefs();
  return xTaskCreatePinnedToCore(modemTask, "modem4g", 6144, NULL, 2, NULL, 0) == pdPASS;
#else
  return false;
#endif
}

bool modem_tunnel_up() { return tunnelAuthed && mst.tcp_connected; }

bool modem_tunnel_send(const uint8_t *p, size_t n, uint32_t) {
#if defined(WAVESHARE_A7670E) && defined(ARDUINO_ARCH_ESP32)
  if (!modem_tunnel_up() || n > CLI_CELL_MTU) return false;
  static uint8_t pkt[CLI_CELL_MTU + 2];
  xSemaphoreTake(txLock, portMAX_DELAY);
  pkt[0] = (uint8_t)(n & 0xFF);
  pkt[1] = (uint8_t)((n >> 8) & 0xFF);
  memcpy(pkt + 2, p, n);
  bool ok = sendRawTcp(pkt, n + 2);
  if (!ok) {
    mst.tcp_connected = false;
    tunnelAuthed = false;
  }
  xSemaphoreGive(txLock);
  if (ok) {
    uint32_t now = millis();
    winBytes += n + 2;
    if (now - winStart >= 1000) {
      cellKbps = winBytes * 8 / (now - winStart);
      winBytes = 0;
      winStart = now;
    }
  }
  return ok;
#else
  (void)p;
  (void)n;
  return false;
#endif
}

void modem_tunnel_drop() {
#if defined(WAVESHARE_A7670E) && defined(ARDUINO_ARCH_ESP32)
  xSemaphoreTake(txLock, portMAX_DELAY);
  if (mst.tcp_connected) {
    ModemSerial.print("AT+CIPCLOSE=0\r\n");
    mst.tcp_connected = false;
    tunnelAuthed = false;
  }
  xSemaphoreGive(txLock);
#endif
}

uint32_t modem_tunnel_kbps() { return millis() - winStart > 2000 ? 0 : cellKbps; }

bool modem_4g_send_sms(const char *to, const char *text) {
  if (!modemPresent || !to || !*to || !text || outSmsPending) return false;
  snprintf(outSmsTo, sizeof outSmsTo, "%s", to);
  snprintf(outSmsBody, sizeof outSmsBody, "%s", text);
  outSmsPending = true;
  return true;
}

sms_cmd_kind_t modem_4g_pop_sms_action(String &arg_out, String &sender_out) {
  char a[16] = {0}, s[MODEM_PHONE_MAX] = {0};
  sms_cmd_kind_t k = modem_pop_sms_cmd(&mst, a, sizeof a, s, sizeof s);
  if (k != SMS_CMD_NONE) {
    arg_out = String(a);
    sender_out = String(s);
  }
  return k;
}

String modem_4g_status_json() {
  char buf[260];
  if (mst.gps.valid) {
    snprintf(buf, sizeof buf,
             "{\"on\":%d,\"reg\":%d,\"dbm\":%d,\"bars\":%u,\"apn\":\"%s\",\"tun\":%d,\"host\":\"%s:%u\","
             "\"gps\":[%.5f,%.5f,%.1f],\"al\":%d}",
             modemPresent ? 1 : 0, (int)mst.reg, mst.rssi_dbm, mst.bars, cfgApn,
             modem_tunnel_up() ? 1 : 0, cfgHost, cfgPort, mst.gps.lat, mst.gps.lon, mst.gps.alt_m,
             mst.allowlist_count);
  } else {
    snprintf(buf, sizeof buf,
             "{\"on\":%d,\"reg\":%d,\"dbm\":%d,\"bars\":%u,\"apn\":\"%s\",\"tun\":%d,\"host\":\"%s:%u\","
             "\"gps\":null,\"al\":%d}",
             modemPresent ? 1 : 0, (int)mst.reg, mst.rssi_dbm, mst.bars, cfgApn,
             modem_tunnel_up() ? 1 : 0, cfgHost, cfgPort, mst.allowlist_count);
  }
  return String(buf);
}

String modem_4g_command(const String &arg) {
  if (!arg.length() || arg == "status") return "ok " + modem_4g_status_json();
  if (arg.startsWith("apn ")) {
    String a = arg.substring(4);
    a.trim();
    if (!a.length() || a.length() >= sizeof cfgApn) return "err usage: cell apn <APN>";
    snprintf(cfgApn, sizeof cfgApn, "%s", a.c_str());
#if defined(WAVESHARE_A7670E) && defined(ARDUINO_ARCH_ESP32)
    saveCellPrefs();
#endif
    return "ok";
  }
  if (arg.startsWith("tunnel ")) {
    String rest = arg.substring(7);
    rest.trim();
    if (rest == "off") {
      cfgHost[0] = '\0';
      cfgPort = 0;
      modem_tunnel_drop();
#if defined(WAVESHARE_A7670E) && defined(ARDUINO_ARCH_ESP32)
      saveCellPrefs();
#endif
      return "ok";
    }
    char h[64] = {0}, tok[48] = {0};
    unsigned p = 0;
    int n = sscanf(rest.c_str(), "%63s %u %47s", h, &p, tok);
    if (n < 2 || p == 0 || p > 65535) return "err usage: cell tunnel <host> <port> [token] | off";
    snprintf(cfgHost, sizeof cfgHost, "%s", h);
    cfgPort = (uint16_t)p;
    if (n >= 3) snprintf(cfgToken, sizeof cfgToken, "%s", tok);
    modem_tunnel_drop();
#if defined(WAVESHARE_A7670E) && defined(ARDUINO_ARCH_ESP32)
    saveCellPrefs();
#endif
    return "ok";
  }
  if (arg.startsWith("allow ")) {
    String rest = arg.substring(6);
    rest.trim();
    if (rest == "clear") {
      modem_allowlist_clear(&mst);
    } else if (!modem_allowlist_add(&mst, rest.c_str())) {
      return "err usage: cell allow <+phone> | clear";
    }
#if defined(WAVESHARE_A7670E) && defined(ARDUINO_ARCH_ESP32)
    saveCellPrefs();
#endif
    return "ok";
  }
  if (arg.startsWith("sms ")) {
    String rest = arg.substring(4);
    int sp = rest.indexOf(' ');
    if (sp <= 0) return "err usage: cell sms <+phone> <message>";
    String to = rest.substring(0, sp);
    String msg = rest.substring(sp + 1);
    return modem_4g_send_sms(to.c_str(), msg.c_str()) ? "ok" : "err modem busy or offline";
  }
  return "err usage: cell [status | apn <apn> | tunnel <host> <port> [token] | allow <+phone>|clear | sms <+phone> <text>]";
}
