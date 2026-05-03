#pragma once

#include <Arduino.h>

// D32 Pro variant header pre-defines these with wrong values for our build.
// Undef them before setting our actual pin assignments.
#ifdef TFT_CS
#undef TFT_CS
#endif
#ifdef TFT_DC
#undef TFT_DC
#endif
#ifdef TFT_RST
#undef TFT_RST
#endif

// --- PN532 (HSU / UART) ---
#define PN532_TX 33
#define PN532_RX 32

// --- TFT (ST7735S, 128x160, SPI) ---
#define TFT_CS   5
#define TFT_DC   21
#define TFT_RST  14
#define TFT_BL   13
#define TFT_SCK  18
#define TFT_MOSI 23
#define TFT_MISO 19

// --- SD card ---
#define SD_CS 4

// --- MAX98357A (I2S) ---
#define I2S_BCLK 27
#define I2S_LRC  26
#define I2S_DOUT 25

// --- Rotary encoder ---
#define ENC_CLK  2
#define ENC_DT   15
#define ENC_SW   34

// --- Audio ---
#define VOLUME_DEFAULT 25   // 0–100, default on boot
#define VOLUME_MAX     100

// --- Web server ---
#define WIFI_SSID     "Jukebox-Setup"
#define WIFI_PASSWORD "12345678"

// --- Colors (RGB565, dark modern theme) ---
#define C_BG        0x0865   // #0A0E1A — very dark navy
#define C_SURFACE   0x10C7   // #111A2E — slightly lighter surface
#define C_TEXT      0xFFFF   // #FFFFFF — white
#define C_MUTED     0x6B4D   // #6B7B8D — muted blue-gray
#define C_ACCENT    0x46F2   // #4ADE80 — vibrant green
#define C_DIM       0x4228   // #455A6E — dim secondary
#define C_RED       0xF9A6   // #F87171 — soft red
#define C_LINE      0x2169   // #1E293B — subtle divider
