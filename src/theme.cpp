// SPDX-FileCopyrightText: 2026 qxzzxq
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"     // THEME_DEFAULT (also pulls in theme.h)
#include "storage.h"    // sdReady
#include <Arduino.h>
#include <SD.h>

// Active palette. Seeded with the default theme so colors are valid even if a
// draw happens before loadTheme() runs (e.g. the SD-error boot screen).
uint16_t C_BG      = rgb565hex(0x0C120C);
uint16_t C_SURFACE = rgb565hex(0x141E14);
uint16_t C_TEXT    = rgb565hex(0xE8F0E2);
uint16_t C_MUTED   = rgb565hex(0x87977D);
uint16_t C_ACCENT  = rgb565hex(0x8FC46B);
uint16_t C_DIM     = rgb565hex(0x47563C);
uint16_t C_RED     = rgb565hex(0xF0776B);
uint16_t C_LINE    = rgb565hex(0x202C1F);
uint16_t C_HINT    = rgb565hex(0xB8D9A0);

// Dark palettes, one distilled from each source card: the card's signature
// color is the accent, on a base tinted toward that hue. Index 0 is the
// default (THEME_DEFAULT in config.h).
static const Theme THEMES[] = {
  //          name             bg          surface     text        muted       accent      dim         red         line        hint
  { "Bamboo Moss",  rgb565hex(0x0C120C), rgb565hex(0x141E14), rgb565hex(0xE8F0E2), rgb565hex(0x87977D), rgb565hex(0x8FC46B), rgb565hex(0x47563C), rgb565hex(0xF0776B), rgb565hex(0x202C1F), rgb565hex(0xB8D9A0) },
  { "Deep Ocean",   rgb565hex(0x0A0E1A), rgb565hex(0x111A2E), rgb565hex(0xE4ECF7), rgb565hex(0x6E88A4), rgb565hex(0x56A8E8), rgb565hex(0x33506E), rgb565hex(0xF87171), rgb565hex(0x1E293B), rgb565hex(0x9CC5E8) },
  { "Slate",        rgb565hex(0x0C0F12), rgb565hex(0x161A20), rgb565hex(0xE8ECF1), rgb565hex(0x8595A6), rgb565hex(0xA0B4C9), rgb565hex(0x45505D), rgb565hex(0xF08A8A), rgb565hex(0x232A33), rgb565hex(0xC2CEDA) },
  { "Iris Violet",  rgb565hex(0x0F0C16), rgb565hex(0x181322), rgb565hex(0xECE7F5), rgb565hex(0x8C7DA6), rgb565hex(0xA98CE8), rgb565hex(0x4E3C6E), rgb565hex(0xF0776B), rgb565hex(0x251D34), rgb565hex(0xC6B4EA) },
  { "Gilded Amber", rgb565hex(0x12100A), rgb565hex(0x1E1810), rgb565hex(0xF7EFDC), rgb565hex(0x9E8C69), rgb565hex(0xF5B027), rgb565hex(0x5C4A2B), rgb565hex(0xF0776B), rgb565hex(0x2C2417), rgb565hex(0xE6C87A) },
  { "Sunset Orange",rgb565hex(0x140D0A), rgb565hex(0x201510), rgb565hex(0xF7EADF), rgb565hex(0xA6897B), rgb565hex(0xFF7A42), rgb565hex(0x6E4A37), rgb565hex(0xEF5B5B), rgb565hex(0x2F1F16), rgb565hex(0xE8B18E) },
};

static const int THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);
static int       s_current   = THEME_DEFAULT;

int themeCount() { return THEME_COUNT; }

const char *themeName(int index) {
  if (index < 0 || index >= THEME_COUNT) index = THEME_DEFAULT;
  return THEMES[index].name;
}

int currentTheme() { return s_current; }

void applyTheme(int index) {
  if (index < 0) index = 0;
  if (index >= THEME_COUNT) index = THEME_COUNT - 1;
  s_current = index;
  const Theme &t = THEMES[index];
  C_BG = t.bg; C_SURFACE = t.surface; C_TEXT = t.text; C_MUTED = t.muted;
  C_ACCENT = t.accent; C_DIM = t.dim; C_RED = t.red; C_LINE = t.line; C_HINT = t.hint;
}

void loadTheme() {
  int idx = THEME_DEFAULT;
  if (sdReady && SD.exists("/theme.cfg")) {
    File f = SD.open("/theme.cfg", FILE_READ);
    if (f) {
      int v = f.readString().toInt();
      f.close();
      if (v >= 0 && v < THEME_COUNT) idx = v;
    }
  }
  applyTheme(idx);
}

void saveTheme() {
  if (!sdReady) return;
  if (SD.exists("/theme.cfg")) SD.remove("/theme.cfg");
  File f = SD.open("/theme.cfg", FILE_WRITE);
  if (f) {
    f.print(s_current);
    f.close();
  }
}
