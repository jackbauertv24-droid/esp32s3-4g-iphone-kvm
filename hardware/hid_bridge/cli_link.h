// The computer's / remote relay's link (esp32agent CLI, docs/cli-protocol.md): pairing, session keys and sealed frames
// on BLE (slots 0..1), local Wi-Fi TCP (CLI_TCP_SLOT = 2), or 4G LTE reverse tunnel (CLI_CELL_SLOT = 3).
#pragma once
#include <Arduino.h>

void cli_begin();                                         // loads the paired clients from NVS
void cli_on_frame(int slot, const uint8_t *p, size_t n);  // a cli_in / TCP / 4G frame
void cli_reset(int slot);                                 // that peer disconnected
bool cli_authed(int slot);                                // its first sealed frame verified
bool cli_known(int slot);                                 // a paired computer's link (its session may be elsewhere now)
int cli_slot();                                           // the authenticated CLI's slot, -1 if none
uint32_t cli_gen();                                       // bumps when a session authenticates (video starts over)
bool cli_wifi();                                          // that session runs over Wi-Fi (cli_tcp.h)
bool cli_cell();                                          // that session runs over 4G LTE tunnel (modem_4g.h)
const char *cli_via_name();                               // "wifi", "cell", or "ble"
uint16_t cli_mtu();                                       // its link's MTU (23 without one)
void cli_drop();                                          // drops the CLI's links
void cli_forget_all();                                    // unpair: every client must pair again
bool cli_send_ctrl(char kind, const char *text, size_t n, uint32_t timeout_ms);
bool cli_send_video(const uint8_t *chunk, size_t n);
void cli_audio(bool on);
bool cli_audio_wanted();
bool cli_send_audio(const uint8_t *rec, size_t n);
size_t cli_video_room();                                  // payload bytes per video chunk; 0 = no CLI
