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

// --- PN532 (HSU / UART2) ---
#define PN532_TX 22  // PN532 TX → ESP32 RX
#define PN532_RX 13  // ESP32 TX → PN532 RX

// --- TFT (ST7789V, 240x320, SPI) via D32 Pro on-board TFT port ---
#define TFT_CS   14
#define TFT_DC   27
#define TFT_RST  33
#define TFT_BL   32
#define TFT_SCK  18
#define TFT_MOSI 23
#define TFT_MISO 19

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

// --- Audio ---
#define VOLUME_DEFAULT 25   // 0–100, default on boot
#define VOLUME_MAX     100

// --- Brightness ---
#define BRIGHTNESS_DEFAULT   100
#define BRIGHTNESS_MIN       2     // prevents total blackout
#define BRIGHTNESS_CHANNEL   0
#define BRIGHTNESS_PWM_FREQ  5000
#define BRIGHTNESS_PWM_RES   8

// --- Dev mode: define DEV_MODE via PlatformIO build_flags (-DDEV_MODE) ---
// Use `pio run -e debug` to build with DEV_MODE enabled.

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
#define VERSION_STRING       "v1.5.0"

// --- Menu ---
#define MENU_ITEM_COUNT      8

// --- Web server ---
#define WIFI_SSID     "Jukebox-Setup"
#define WIFI_PASSWORD "12345678"

// --- Bluetooth ---
#define BT_DEVICE_NAME "Jukebox"

// --- Colors (RGB565, dark modern theme) ---
#define C_BG        0x0865   // #0A0E1A — very dark navy
#define C_SURFACE   0x10C7   // #111A2E — slightly lighter surface
#define C_TEXT      0xFFFF   // #FFFFFF — white
#define C_MUTED     0x6B4D   // #6B7B8D — muted blue-gray
#define C_ACCENT    0x46F2   // #4ADE80 — vibrant green
#define C_DIM       0x4228   // #455A6E — dim secondary
#define C_RED       0xF9A6   // #F87171 — soft red
#define C_LINE      0x2169   // #1E293B — subtle divider
