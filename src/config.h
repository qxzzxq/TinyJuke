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

// --- Colors (RGB565) ---
#define C_BG        0x2104
#define C_TEXT      0xFFFF
#define C_ACCENT    0x07E0
#define C_DIM       0x8410
#define C_RED       0xF800
