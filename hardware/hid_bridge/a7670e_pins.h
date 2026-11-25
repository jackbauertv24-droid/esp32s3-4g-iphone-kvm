// Hardware pinout definitions for the Waveshare ESP32-S3-A7670E-4G (V1 and V2 revisions).
// Reference: https://www.waveshare.com/wiki/ESP32-S3-A7670E-4G
#pragma once

#if defined(WAVESHARE_A7670E_V1) || defined(WAVESHARE_A7670E_V2)
#ifndef WAVESHARE_A7670E
#define WAVESHARE_A7670E 1
#endif
#endif

#if defined(WAVESHARE_A7670E) && !defined(WAVESHARE_A7670E_V1) && !defined(WAVESHARE_A7670E_V2)
#define WAVESHARE_A7670E_V2 1  // V2 is current production hardware; pass --a7670e-v1 for V1 boards
#endif

// --- SIMCom A7670E 4G LTE Cat-1 + GNSS Modem ---
#define A7670E_UART_NUM       1
#define A7670E_TX_PIN         18   // ESP32-S3 GPIO18 (TX) -> A7670E RXD
#define A7670E_RX_PIN         17   // ESP32-S3 GPIO17 (RX) <- A7670E TXD
#define A7670E_PWRKEY_PIN     33   // HIGH = power on / wake A7670E
#define A7670E_DEFAULT_BAUD   115200
#define A7670E_FAST_BAUD      921600

// --- MAX17048 18650 Li-ion / Solar Fuel Gauge (I2C 0x36) ---
#define MAX17048_I2C_ADDR     0x36
#ifdef WAVESHARE_A7670E_V1
#define MAX17048_SDA_PIN      3
#define MAX17048_SCL_PIN      2
#else
// On V2, MAX17048 shares the SCCB/I2C bus on GPIO 15/16
#define MAX17048_SDA_PIN      15
#define MAX17048_SCL_PIN      16
#endif

// --- 24-Pin DVP Camera Interface (OV2640 / OV5640) ---
// Ensure physical CAM DIP switch on the rear of the PCB is switched ON when using optical fallback.
#define A7670E_CAM_PWDN       -1
#define A7670E_CAM_RESET      -1
#define A7670E_CAM_SIOD       15
#define A7670E_CAM_SIOC       16
#define A7670E_CAM_Y2         7
#define A7670E_CAM_Y3         8
#define A7670E_CAM_Y4         9
#define A7670E_CAM_Y5         10
#define A7670E_CAM_Y6         11
#define A7670E_CAM_Y7         12
#define A7670E_CAM_Y8         13
#define A7670E_CAM_Y9         14

#ifdef WAVESHARE_A7670E_V1
#define A7670E_CAM_XCLK       34
#define A7670E_CAM_VSYNC      36
#define A7670E_CAM_HREF       35
#define A7670E_CAM_PCLK       37
#else
#define A7670E_CAM_XCLK       39
#define A7670E_CAM_VSYNC      42
#define A7670E_CAM_HREF       41
#define A7670E_CAM_PCLK       46
#endif

// --- MicroSD Card Slot (SDMMC 1-bit) ---
#define A7670E_SD_CLK         5
#define A7670E_SD_CMD         4
#define A7670E_SD_DATA        6

// --- Onboard WS2812B Status RGB LED ---
#define A7670E_RGB_LED_PIN    38
