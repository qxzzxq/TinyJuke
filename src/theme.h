// SPDX-FileCopyrightText: 2026 qxzzxq
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

// ----------------------------------------------------------------
//  UI color theme
//
//  The C_* globals hold the active palette in RGB565. All draw code
//  references them by name (gfx.fillScreen(C_BG), ...), so swapping a
//  theme repaints the whole UI. The chosen index is persisted to
//  /theme.cfg on the SD card and restored at boot.
// ----------------------------------------------------------------

// Compile-time 0xRRGGBB (24-bit) -> RGB565 (5-6-5).
constexpr uint16_t rgb565hex(uint32_t c) {
  return (uint16_t)((((c >> 19) & 0x1F) << 11) |  // R: top 5 bits
                    (((c >> 10) & 0x3F) << 5)  |  // G: top 6 bits
                    (((c >> 3)  & 0x1F)));         // B: top 5 bits
}

struct Theme {
  const char *name;
  uint16_t bg, surface, text, muted, accent, dim, red, line, hint;
};

// Active palette — set by applyTheme(), read everywhere via the C_* names.
extern uint16_t C_BG, C_SURFACE, C_TEXT, C_MUTED, C_ACCENT, C_DIM, C_RED, C_LINE, C_HINT;

int         themeCount();
const char *themeName(int index);
int         currentTheme();          // active index
void        applyTheme(int index);   // clamp to range, copy palette into C_* globals
void        loadTheme();             // read /theme.cfg (or default) and apply
void        saveTheme();             // persist currentTheme() to /theme.cfg
