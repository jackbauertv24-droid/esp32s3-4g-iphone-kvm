<h1 align="center">ESP32-S3 4G Out-of-Band iPhone KVM & AI Agent</h1>

<p align="center">
  <b>True out-of-band iPhone KVM & MCP hardware bridge built for the Waveshare ESP32-S3-A7670E-4G.</b><br>
  Dual-Eye Vision (AirPlay H.264 + OV2640 Optical Camera Fallback) · Wireless BLE Absolute Touch · 4G LTE Reverse Tunnel · 18650 Battery/UPS.
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-111111?style=flat-square" alt="License: MIT"></a>
  <img src="https://img.shields.io/badge/board-Waveshare%20ESP32--S3--A7670E--4G-0066cc?style=flat-square" alt="Board">
  <img src="https://img.shields.io/badge/iOS-16.4%2B-111111?style=flat-square&logo=apple" alt="iOS 16.4+">
</p>

> **Attribution:** Based on **[ESP32 iPhone Agent](https://github.com/onehumanbeing/esp32-iphone-agent) by Henry Yuan** (MIT Licensed; see [`LICENSE`](LICENSE) and [`NOTICE`](NOTICE)).

---

## Why This Fork Exists (Shortcomings Fixed)

Upstream [`esp32-iphone-agent`](https://github.com/onehumanbeing/esp32-iphone-agent) turns a $7 XIAO ESP32-S3 into an AirPlay receiver and USB/BLE mouse+keyboard for stock, non-jailbroken iPhones. However, running an unattended or remote iPhone KVM hits **four major hardware and iOS limits**—all solved in this repository using the **Waveshare ESP32-S3-A7670E-4G**:

| Upstream Shortcoming | How `esp32s3-4g-iphone-kvm` Fixes It |
| :--- | :--- |
| **1. AirPlay Lock-Screen & Passcode Blackout Trap:** iOS disconnects AirPlay when the phone locks/reboots and blacks out the H.264 stream on Passcode, 2FA, and Apple Pay prompts—leaving remote operators completely blind. | **Dual-Eye Vision Multiplexer (`vision_mux` + `cam_eye`):** Pairs crisp AirPlay H.264 mirroring with the board's onboard **OV2640/OV5640 DVP camera** (with locked anti-glare OLED exposure). Automatically falls back to optical JPEG snapshots whenever AirPlay is idle, stalled, frozen, or blacked out, with calibrated ROI click-mapping (`camtap`). |
| **2. Wi-Fi Contention & No Remote Out-of-Band Uplink:** Hosting the AirPlay hotspot occupies the ESP32-S3's Wi-Fi radio, leaving the board with no independent internet access and stranding it behind LAN/BLE range. | **Dedicated 4G LTE Cat-1 Reverse Tunnel (`modem_4g` + `relay/`):** Keeps Wi-Fi 100% dedicated to local AirPlay while the onboard **SIMCom A7670E** dials out over 4G LTE to a zero-trust TCP relay (`CLI_CELL_SLOT`), bypassing carrier CGNAT with end-to-end X25519 + AES-128-GCM encryption—plus **allowlisted SMS emergency recovery**. |
| **3. Relative-Only BLE Mouse & Missing BLE Home Key:** Upstream restricts `0..32767` absolute coordinates (`--abs`) to wired USB HID only, forcing drift-prone relative mouse calibration over Bluetooth, and fails `homescreen` over BLE. | **Wireless BLE Absolute Touch + iOS Recovery Macros (`ble_hid` + `ios_macros`):** Adds a 16-bit `0..32767` Absolute Pointer HID descriptor (Report ID 4) to Bluetooth & Dual HID, fixes `homescreen` over BLE via deterministic `homeswipe`, and adds `macro unlock`, `controlcenter`, `spotlight`, and autonomous `airplay` self-recovery. |
| **4. 8 MB Octal-PSRAM Assumption Crash:** Upstream hardcodes a ~5 MB PSRAM GOP arena (`PSRAM=opi`), which fails to boot or OOMs on ESP32-S3R2 boards with **2 MB Quad-SPI PSRAM**. | **2 MB Quad-PSRAM Auto-Scaling & Fuel Gauge (`board_support` + `video_link`):** Configures `FlashSize=16M,PSRAM=qspi`, scales GOP/tail arenas to fit alongside camera framebuffers, reads real 18650 SoC % over I²C from the onboard **MAX17048**, and drives the `GPIO 38` RGB status LED. |

---

## Architecture

```
                         ┌─────────────── Waveshare ESP32-S3-A7670E-4G ───────────────┐
                         │                                                            │
  ┌──────────┐  AirPlay  │  Wi-Fi AP ──▶ AirPlay H.264 ─┐                             │   4G LTE Outbound Tunnel
  │          │══════════▶│                              ├─▶ Dual-Eye Mux (vision_mux) │   (X25519 + AES-128-GCM)
  │  iPhone  │  Optical  │  OV2640 Cam ─▶ JPEG (VF_JPEG)┘            │                │             │
  │ (Stock)  │──────────▶│  (Lock/2FA/BIOS Recovery Eyes)            ▼                │             ▼
  │          │◀──────────│  BLE / USB Absolute HID (0..32767) ◀── CLI/App Link ═══════╪══▶ A7670E ══▶ VPS Relay (:9001)
  └──────────┘  Touch +  │  MAX17048 18650 UPS + GNSS + Allowlisted SMS Recovery      │                   │
                Keyboard └────────────────────────────────────────────────────────────┘                   ▼
                                                                                             esp32agent CLI / MCP (:7202)
```

---

## Hardware Setup (Waveshare ESP32-S3-A7670E-4G)

1. **Power & UPS:** Insert a charged **18650 Li-ion cell** into the rear holder and switch the power toggle **ON** (cellular transmit bursts can draw up to 2A; USB power alone can brown out the modem).
2. **Rear DIP Switches:**
   - `4G` = **ON** (powers SIMCom A7670E modem; `GPIO 33` controls `PWRKEY`)
   - `CAM` = **ON** (powers 24-pin OV2640/OV5640 DVP camera interface)
   - `USB` = **OFF** (keeps UART1 `GPIO 17/18` routed to the ESP32-S3 instead of direct PC USB pass-through)
3. **V1 vs. V2 Board Revisions:** Both Waveshare PCB revisions are supported:
   - **V2 (Default):** `--a7670e` (`CAM_XCLK=39, VSYNC=42, HREF=41, PCLK=46`, `MAX17048 I2C=15/16`)
   - **V1:** `--a7670e-v1` (`CAM_XCLK=34, VSYNC=36, HREF=35, PCLK=37`, `MAX17048 I2C=3/2`)

---

## Build & Flash

Install [`arduino-cli`](https://arduino.github.io/arduino-cli/), ESP32 Arduino core `3.3.x`, and `NimBLE-Arduino@2.5.1`:

```bash
arduino-cli lib install "NimBLE-Arduino@2.5.1"

# Build & flash Waveshare ESP32-S3-A7670E-4G (V2 hardware)
./hardware/flash.sh --a7670e

# Or for V1 hardware revision:
./hardware/flash.sh --a7670e-v1
```

---

## New Board Commands (Serial / BLE / Wi-Fi / 4G CLI)

In addition to all standard `esp32-iphone-agent` commands (`ping`, `move`, `click`, `type`, `key`, `snap`, `stream on|off`, `hidmode usb|ble`), this firmware adds:

| Command | Description |
| :--- | :--- |
| `abs <x> <y> [step]` | Zero-calibration `0..32767` absolute pointer tap/glide—now works over **wireless BLE HID** as well as USB HID! |
| `eye [status\|auto\|airplay\|cam]` | Query or switch the Dual-Eye Vision Multiplexer (`auto` seamlessly falls back to camera on AirPlay loss/blackout). |
| `camsnap` | Force an immediate JPEG snapshot from the OV2640/OV5640 optical fallback camera (`VF_JPEG`). |
| `camcfg <aec 0\|1> <exp> <qual>` | Tune anti-glare OLED screen exposure (`aec=0`, `exp=280`, `qual=14` default) and save to NVS. |
| `camroi <x0> <y0> <x1> <y1> <rot>` | Calibrate normalized `0..10000` iPhone screen corners & mount rotation (`0/90/180/270`) in the camera view. |
| `camtap <cx> <cy>` | Tap normalized `0..10000` camera image coordinates mapped through `camroi` into `0..32767` iPhone HID space. |
| `macro <name> [args]` | Execute deterministic iOS gestures: `homeswipe`, `appswitcher`, `controlcenter`, `notifications`, `spotlight`, `wake`, `unlock [PIN]`, or `airplay [mx my rx ry]`. |
| `cell status` | Show A7670E 4G registration, RSSI (`dBm` + bars), tunnel state, and live GNSS `[lat, lon, alt]` fix. |
| `cell apn <apn>` | Save cellular carrier APN in NVS. |
| `cell tunnel <host> <port> <tok>` | Configure outbound 4G reverse tunnel to `relay/server.js` (or `cell tunnel off`). |
| `cell allow <+phone>\|clear` | Manage fail-closed phone number allowlist for emergency SMS commands (`STATUS`, `WAKE`, `UNLOCK`, `AIRPLAY`, `CAMSNAP`, `REBOOT`). |
| `cell sms <+phone> <text>` | Send an outbound SMS directly through the A7670E modem. |

---

## Running the 4G Reverse-Tunnel Relay

Because cellular carriers place SIM cards behind CGNAT, deploy [`relay/server.js`](relay/server.js) on any lightweight VPS (Node.js 18+, zero external npm dependencies):

```bash
cd relay
RELAY_TOKEN="your-strong-secret-token" DEVICE_PORT=9001 CLIENT_PORT=7202 HTTP_PORT=8080 node server.js
```

Then configure the board once over USB serial or local BLE:

```text
0 cell apn internet
0 cell tunnel your-vps.example.com 9001 your-strong-secret-token
```

Your paired `esp32agent` CLI or MCP client can now connect to `your-vps.example.com:7202` with full end-to-end X25519 + AES-128-GCM session security!

---

## Running Unit Tests

```bash
# 1. Pure-C11 firmware unit tests (Dual-Eye mux, ROI mapping, BLE Abs-HID, iOS macros, A7670E AT/GPS/SMS security)
cd hardware/tests
mkdir -p ../build/tests
gcc -std=c11 -Wall -Wextra -Werror -o ../build/tests/test_a7670e_kvm \
  test_a7670e_kvm.c ../hid_bridge/src/video/vision_mux.c ../hid_bridge/src/video/gop.c \
  ../hid_bridge/src/hid/ios_macros.c ../hid_bridge/src/modem/at_parser.c -lm
../build/tests/test_a7670e_kvm

# 2. 4G Reverse-Tunnel Relay end-to-end test
cd ../../relay
node --test test_relay.js
```

---

## License & Credits

MIT License — see [`LICENSE`](LICENSE) and [`NOTICE`](NOTICE).

* Derived from **[ESP32 iPhone Agent (Manina AI)](https://github.com/onehumanbeing/esp32-iphone-agent)** — Copyright (c) 2026 Henry Yuan.
* Note 4 battery curve — Copyright (c) 2026 Zectrix Lab (`hardware/hid_bridge/LICENSE-ZECTRIX`).
