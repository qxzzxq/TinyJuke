// SPDX-FileCopyrightText: 2026 qxzzxq
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <Arduino.h>

// D32 Pro variant pre-defines TFT_CS/TFT_DC/TFT_RST for the on-board TFT port.
// Undef them so we can use our matching definitions below.
#ifdef TFT_CS
#undef TFT_CS
#endif
#ifdef TFT_DC
#undef TFT_DC
#endif
#ifdef TFT_RST
#undef TFT_RST
#endif

// Pin map is board-specific: -DBOARD_WROVER_E (set by the wrover_e PIO env) selects
// the custom mainboard; the default branch is the Lolin D32 Pro.
#if defined(BOARD_WROVER_E)
// ===== Custom WROVER-E mainboard — see docs/esp32_wrover_e_pin_map.md =====

// --- PN532 (HSU / UART2) ---
#define PN532_TX 27  // PN532 TX → ESP32 RX
#define PN532_RX 26  // ESP32 TX → PN532 RX

// --- Shared SPI bus (VSPI): SD card + TFT ---
#define SCK  18  // TFT: SCL
#define MOSI 23  // TFT: SDA
#define MISO 19  // TFT: null

// --- TFT (ST7789V, 240x320, SPI) ---
#define TFT_CS   22
#define TFT_DC   21
#define TFT_RST  4
#define TFT_BL   13

// --- SD card ---
#define SD_CS 5

// --- MAX98357A (I2S) ---
#define I2S_BCLK 32
#define I2S_LRC  33
#define I2S_DOUT 25

// --- Rotary encoder ---
#define ENC_CLK  36
#define ENC_DT   39
#define ENC_SW   34

#else
// ===== Lolin D32 Pro (default / current hardware) =====

// --- PN532 (HSU / UART2) ---
#define PN532_TX 22  // PN532 TX → ESP32 RX
#define PN532_RX 13  // ESP32 TX → PN532 RX

// --- Shared SPI bus (VSPI): SD card + TFT ---
#define SCK  18
#define MOSI 23
#define MISO 19

// --- TFT (ST7789V, 240x320, SPI) via D32 Pro on-board TFT port ---
#define TFT_CS   14
#define TFT_DC   27
#define TFT_RST  33
#define TFT_BL   32

// --- SD card ---
#define SD_CS 4

// --- MAX98357A (I2S) ---
#define I2S_BCLK 21
#define I2S_LRC  26
#define I2S_DOUT 25

// --- Rotary encoder ---
#define ENC_CLK  36
#define ENC_DT   5
#define ENC_SW   34

#endif

// --- Audio ---
#define VOLUME_DEFAULT 25   // 0–100, default on boot
#define VOLUME_MAX     100
#define MAXVOLUME_DEFAULT 100  // software ceiling on volume, 0–100

// --- Brightness ---
#define BRIGHTNESS_DEFAULT   100
#define BRIGHTNESS_MIN       2     // prevents total blackout
#define BRIGHTNESS_CHANNEL   0
#define BRIGHTNESS_PWM_FREQ  5000
#define BRIGHTNESS_PWM_RES   8

// --- Dev mode: define DEV_MODE via PlatformIO build_flags (-DDEV_MODE) ---
// Use `pio run -e lolin_d32_pro-debug` (or `-e wrover_e-debug`) to build with DEV_MODE.

// --- Power saving ---
#define POWERSAVE_DEFAULT   15    // minutes, 0 = off
#ifdef DEV_MODE
  #define POWERSAVE_OPTIONS 6
#else
  #define POWERSAVE_OPTIONS 5
#endif

// --- Audio sleep timer ---
#define SLEEPTIMER_DEFAULT   0     // minutes, 0 = off
#ifdef DEV_MODE
  #define SLEEP_OPTIONS     6
#else
  #define SLEEP_OPTIONS     5
#endif

// --- Version ---
#define VERSION_STRING       "v1.10.0"

// --- Menu ---
#define MENU_ITEM_COUNT      9

// --- NFC polling / tag-presence policy ---
// Poll periods and the removal-confirmation windows live in tag_presence.h,
// which derives each site's miss count from its own cadence.

// --- UI animation ---
// Motion is only used where it carries information: how far a hold has
// progressed, where the selection moved, how a value changed.
#define ANIM_FRAME_MS        16   // ~60 fps gate on animation frames
#define ANIM_MENU_MS        130   // selector slide between menu rows
#define ANIM_BAR_MS          90   // volume / brightness bar fill
#define HOLD_HINT_DELAY_MS  200   // hold progress appears after this much press

// --- Color theme (default index into the THEMES[] table in theme.cpp) ---
#define THEME_DEFAULT        0   // Bamboo Moss

// --- Web server ---
#define WIFI_SSID     "TinyJuke-Setup"
#define WIFI_PASSWORD "12345678"

// --- Bluetooth ---
// Base prefix; the advertised name appends the last 4 hex of the MAC (TinyJuke-XXXX).
#define BT_DEVICE_NAME "TinyJuke"

// --- Colors ---
// The C_* palette is now a runtime-switchable theme (RGB565), declared as
// globals in theme.h and defined/loaded in theme.cpp. Draw code keeps using
// the C_* names unchanged; the active theme is persisted to /theme.cfg.
#include "theme.h"
