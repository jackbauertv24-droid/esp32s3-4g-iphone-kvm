// CLI link, see cli_link.h. Supports BLE (slots 0..1), Wi-Fi TCP (slot 2), and 4G LTE reverse tunnel (slot 3).
#include "cli_link.h"
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_random.h>
#include "ble_link.h"
#include "cli_tcp.h"
#include "modem_4g.h"
#include "src/cli/cli_proto.h"
#include "logbuf.h"

const int MAX_CLIENTS = 4;
const int BLE_SLOTS = 2;                   // ble_link's MAX_PEERS
const int SLOTS = BLE_SLOTS + 2;           // + Wi-Fi TCP (slot 2) + 4G LTE tunnel (slot 3)
const uint32_t VIDEO_TIMEOUT_MS = 1500;

struct PairedClient {
  uint8_t id[CLI_ID_LEN], key[CLI_KEY_LEN];
};
struct Session {
  volatile bool welcomed, authed;
  int client;
  uint8_t c2b[16], b2c[16];
  uint32_t rx, txCtrl, txVideo, txAudio;
};

static Preferences prefs;
static PairedClient clients[MAX_CLIENTS];
static int clientCount, nextClient;
static Session sessions[SLOTS];
static uint8_t boardId[6];
static volatile uint32_t gen;
// Frame buffers in PSRAM: one RX per task (0=BLE, 1=Wi-Fi TCP, 2=4G LTE), plus TX buffers.
static uint8_t *rxText[3], *ctrlBuf, *ctrlOut, *videoOut, *audioOut;
static volatile bool audioOn;
const size_t RX_CAP = 520, CTRL_CAP = BOARD_STATUS_CAP, OUT_CAP = CTRL_CAP + CLI_OVERHEAD, VIDEO_CAP = CLI_TCP_MTU;
const size_t AUDIO_CAP = 1400 + CLI_OVERHEAD;
const size_t BUF_SIZE = RX_CAP * 3 + CTRL_CAP + OUT_CAP + VIDEO_CAP + AUDIO_CAP;

extern "C" void cli_random(uint8_t *p, size_t n) { esp_fill_random(p, n); }

static void saveClients() {
  prefs.begin("cli", false);
  prefs.putBytes("c", clients, sizeof clients);
  prefs.putUChar("n", clientCount);
  prefs.putUChar("next", nextClient);
  prefs.end();
}

static uint16_t mtu(int slot) {
  if (slot == CLI_TCP_SLOT) return CLI_TCP_MTU + 3;
  if (slot == CLI_CELL_SLOT) return CLI_CELL_MTU + 3;
  return ble_cli_mtu(slot);
}
static bool linkUp(int slot) {
  if (slot == CLI_TCP_SLOT) return cli_tcp_up();
  if (slot == CLI_CELL_SLOT) return modem_tunnel_up();
  return ble_cli_subscribed(slot);
}
static bool notify(int slot, const uint8_t *p, size_t n, bool video, uint32_t timeout_ms) {
  if (slot == CLI_TCP_SLOT) return cli_tcp_send(p, n, timeout_ms);
  if (slot == CLI_CELL_SLOT) return modem_tunnel_send(p, n, timeout_ms);
  return ble_cli_notify(slot, p, n, video, timeout_ms);
}
static void drop(int slot) {
  if (slot == CLI_TCP_SLOT) cli_tcp_drop();
  else if (slot == CLI_CELL_SLOT) modem_tunnel_drop();
  else ble_cli_drop(slot);
}

void cli_begin() {
  uint8_t *p = (uint8_t *)heap_caps_malloc(BUF_SIZE, MALLOC_CAP_SPIRAM);
  if (!p) p = (uint8_t *)malloc(BUF_SIZE);
  if (!p) return;
  rxText[0] = p;
  rxText[1] = p + RX_CAP;
  rxText[2] = p + RX_CAP * 2;
  ctrlBuf = p + RX_CAP * 3;
  ctrlOut = ctrlBuf + CTRL_CAP;
  videoOut = ctrlOut + OUT_CAP;
  audioOut = videoOut + VIDEO_CAP;
  esp_read_mac(boardId, ESP_MAC_BT);
  prefs.begin("cli", true);
  clientCount = prefs.getUChar("n", 0);
  nextClient = prefs.getUChar("next", 0);
  if (prefs.getBytes("c", clients, sizeof clients) != sizeof clients || clientCount > MAX_CLIENTS) clientCount = nextClient = 0;
  prefs.end();
}

void cli_forget_all() {
  memset(clients, 0, sizeof clients);
  clientCount = nextClient = 0;
  saveClients();
  for (int i = 0; i < SLOTS; i++)
    if (sessions[i].welcomed) drop(i);
}

static int findClient(const uint8_t *id) {
  for (int i = 0; i < clientCount; i++)
    if (!memcmp(clients[i].id, id, CLI_ID_LEN)) return i;
  return -1;
}

static void plain(int slot, uint8_t type, const uint8_t *p, size_t n) {
  uint8_t out[80];
  out[0] = type;
  memcpy(out + 1, p, n);
  notify(slot, out, n + 1, false, 1000);
}

static void error(int slot, const char *why) { plain(slot, CLI_ERROR, (const uint8_t *)why, strlen(why)); }

static void pair(int slot, const uint8_t *p, size_t n) {
  if (n != 32 + CLI_ID_LEN) return error(slot, "bad pair request");
  // Security: never allow unauthenticated key-exchange pairing over Wi-Fi or 4G cellular!
  if (slot == CLI_TCP_SLOT || slot == CLI_CELL_SLOT) return error(slot, "pair over Bluetooth");
  if (!ble_pair_window_open())
    return error(slot, "pairing window closed: unplug and replug the board, then pair within 3 minutes");
  uint8_t priv[32], pub[32], key[CLI_KEY_LEN];
  esp_fill_random(priv, sizeof priv);
  bool ok = cli_pair(priv, p, pub, key);
  memset(priv, 0, sizeof priv);
  if (!ok) return error(slot, "bad key");
  const uint8_t *id = p + 32;
  int i = findClient(id);
  if (i < 0) {
    i = clientCount < MAX_CLIENTS ? clientCount++ : nextClient;
    nextClient = (i + 1) % MAX_CLIENTS;
  }
  memcpy(clients[i].id, id, CLI_ID_LEN);
  memcpy(clients[i].key, key, CLI_KEY_LEN);
  saveClients();
  uint8_t out[32 + 6 + CLI_MAC_LEN];
  memcpy(out, pub, 32);
  memcpy(out + 32, boardId, 6);
  cli_mac(key, "paired", id, CLI_ID_LEN, NULL, 0, out + 38);
  board_log("cli: paired a new client");
  plain(slot, CLI_PAIRED, out, sizeof out);
}

static void hello(int slot, const uint8_t *p, size_t n) {
  if (n != CLI_ID_LEN + CLI_NONCE_LEN) return error(slot, "bad hello");
  int c = findClient(p);
  if (c < 0) return error(slot, "not paired");
  Session &s = sessions[slot];
  uint8_t bn[CLI_NONCE_LEN], out[CLI_NONCE_LEN + 6 + CLI_MAC_LEN];
  esp_fill_random(bn, sizeof bn);
  s.authed = false;
  if (!cli_session(clients[c].key, p + CLI_ID_LEN, bn, s.c2b, s.b2c)) return error(slot, "session failed");
  s.client = c;
  s.rx = s.txCtrl = s.txVideo = s.txAudio = 0;
  s.welcomed = true;
  memcpy(out, bn, CLI_NONCE_LEN);
  memcpy(out + CLI_NONCE_LEN, boardId, 6);
  cli_mac(clients[c].key, "hello", p + CLI_ID_LEN, CLI_NONCE_LEN, bn, CLI_NONCE_LEN, out + CLI_NONCE_LEN + 6);
  plain(slot, CLI_WELCOME, out, sizeof out);
}

static void data(int slot, const uint8_t *p, size_t n) {
  Session &s = sessions[slot];
  int rxIdx = (slot == CLI_TCP_SLOT) ? 1 : ((slot == CLI_CELL_SLOT) ? 2 : 0);
  uint8_t *text = rxText[rxIdx];
  uint32_t ctr;
  if (!s.welcomed || !text || n > RX_CAP + CLI_OVERHEAD) return;
  int len = cli_open(s.c2b, 'C', p, n, &ctr, text);
  if (len < 0) return;
  if (ctr != s.rx + 1) { drop(slot); return; }
  s.rx = ctr;
  if (!s.authed) {
    int old = cli_slot();
    if (old >= 0 && sessions[old].client != s.client) { drop(slot); return; }
    if (old >= 0) sessions[old].authed = false;
    s.authed = true;
    audioOn = false;
    gen++;
    board_log(slot == CLI_TCP_SLOT ? "cli: session up (wifi)"
              : (slot == CLI_CELL_SLOT ? "cli: session up (4g)" : "cli: session up (ble)"));
  }
  if (len > 1 && text[0] == 'c') ble_cli_push_cmd((const char *)text + 1, len - 1);
}

void cli_on_frame(int slot, const uint8_t *p, size_t n) {
  if (slot < 0 || slot >= SLOTS || !n) return;
  switch (p[0]) {
    case CLI_PAIR: return pair(slot, p + 1, n - 1);
    case CLI_HELLO: return hello(slot, p + 1, n - 1);
    case CLI_DATA: return data(slot, p, n);
  }
}

void cli_reset(int slot) {
  if (slot < 0 || slot >= SLOTS) return;
  sessions[slot].authed = sessions[slot].welcomed = false;
}

bool cli_authed(int slot) { return slot >= 0 && slot < SLOTS && sessions[slot].authed; }
bool cli_known(int slot) { return slot >= 0 && slot < SLOTS && sessions[slot].welcomed; }
uint32_t cli_gen() { return gen; }
bool cli_wifi() { return cli_slot() == CLI_TCP_SLOT; }
bool cli_cell() { return cli_slot() == CLI_CELL_SLOT; }
const char *cli_via_name() {
  int slot = cli_slot();
  if (slot == CLI_TCP_SLOT) return "wifi";
  if (slot == CLI_CELL_SLOT) return "cell";
  return "ble";
}
uint16_t cli_mtu() { int slot = cli_slot(); return slot < 0 ? 23 : mtu(slot); }

void cli_drop() {
  int slot = cli_slot();
  if (slot >= 0) drop(slot);
  for (int i = 0; i < SLOTS; i++)
    if (i != slot && sessions[i].welcomed) drop(i);
}

int cli_slot() {
  for (int i = 0; i < SLOTS; i++)
    if (sessions[i].authed) return i;
  return -1;
}

bool cli_send_ctrl(char kind, const char *text, size_t n, uint32_t timeout_ms) {
  int slot = cli_slot();
  if (slot < 0) return false;
  Session &s = sessions[slot];
  uint8_t *buf = ctrlBuf, *out = ctrlOut;
  size_t room = cli_ctrl_room(mtu(slot));
  if (kind == 's' && n > room) return false;
  if (n > room) n = room;
  buf[0] = (uint8_t)kind;
  memcpy(buf + 1, text, n);
  size_t len = cli_seal(s.b2c, CLI_DATA, 'B', s.txCtrl + 1, buf, n + 1, out);
  if (!len || !notify(slot, out, len, false, timeout_ms)) {
    drop(slot);
    return false;
  }
  s.txCtrl++;
  return true;
}

size_t cli_video_room() {
  int slot = cli_slot();
  if (slot < 0 || !linkUp(slot)) return 0;
  return mtu(slot) - 3 - CLI_OVERHEAD - 3;
}

bool cli_send_video(const uint8_t *chunk, size_t n) {
  int slot = cli_slot();
  if (slot < 0) return false;
  Session &s = sessions[slot];
  uint8_t *out = videoOut;
  if (n > VIDEO_CAP - CLI_OVERHEAD) return false;
  size_t len = cli_seal(s.b2c, CLI_VIDEO, 'V', s.txVideo + 1, chunk, n, out);
  s.txVideo++;
  return len && notify(slot, out, len, true, VIDEO_TIMEOUT_MS);
}

void cli_audio(bool on) { audioOn = on; }
bool cli_audio_wanted() { int slot = cli_slot(); return audioOn && (slot == CLI_TCP_SLOT || slot == CLI_CELL_SLOT); }

bool cli_send_audio(const uint8_t *rec, size_t n) {
  int slot = cli_slot();
  if ((slot != CLI_TCP_SLOT && slot != CLI_CELL_SLOT) || !audioOut || n + CLI_OVERHEAD > AUDIO_CAP) return false;
  Session &s = sessions[slot];
  size_t len = cli_seal(s.b2c, CLI_AUDIO, 'A', s.txAudio + 1, rec, n, audioOut);
  s.txAudio++;
  return len && notify(slot, audioOut, len, true, VIDEO_TIMEOUT_MS);
}
