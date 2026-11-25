// SIMCom A7670E 4G LTE Cat-1 + GNSS + SMS out-of-band driver and reverse-tunnel transport.
// Dedicates ESP32-S3 Wi-Fi 100% to local AirPlay mirroring while A7670E maintains an independent
// cellular TCP reverse tunnel (slot CLI_CELL_SLOT in cli_link.cpp) to a remote relay/MCP server.
#pragma once
#include <Arduino.h>
#include "src/modem/at_parser.h"

const int CLI_CELL_SLOT = 3;       // cli_link.cpp slot after BLE (0,1) and Wi-Fi TCP (2)
const size_t CLI_CELL_MTU = 1360;  // safe cellular TCP record payload below 1400-byte LTE MTU

bool modem_4g_begin();
bool modem_tunnel_up();
bool modem_tunnel_send(const uint8_t *p, size_t n, uint32_t timeout_ms);
void modem_tunnel_drop();
uint32_t modem_tunnel_kbps();

// Configures APN, reverse-tunnel endpoint (host, port, auth token), or SMS allowlist in NVS.
String modem_4g_command(const String &arg);

// Compact & full JSON telemetry for statusJson / `cell` queries.
String modem_4g_status_json();

// Pops any authorized out-of-band SMS emergency command for execution on the main loop.
sms_cmd_kind_t modem_4g_pop_sms_action(String &arg_out, String &sender_out);
bool modem_4g_send_sms(const char *to, const char *text);
