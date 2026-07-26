// SPDX-FileCopyrightText: 2026 qxzzxq
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "config.h"
#include "tags.h"  // for TagInfo
#include <Arduino_GFX_Library.h>

// gfx object is defined in main.cpp — screen functions reference it.
extern Arduino_ST7789 gfx;

void drawWaitingScreen();
void drawTagScreen(const uint8_t *uid, uint8_t uidLen);
void drawUnknownTagScreen(const uint8_t *uid, uint8_t uidLen);
void drawNowPlayingScreen(const TagInfo &tag);
void drawSDErrorScreen();

// GUI screens
void drawMenuScreen(int selected);
void drawVolumeScreen(int level, int maxLevel, bool adjustingMax);
void drawBrightnessScreen(int level);
void drawPowerSaveScreen(int minutes);
void drawSleepTimerScreen(int minutes);
void drawVersionScreen();
void drawThemeScreen(int index);
void drawWebServerScreen(int connections);
void drawRebootConfirmScreen();
void drawRebootingScreen();
void drawBluetoothScreen(bool connected, const char *deviceName, const char *peer,
                         const char *title, const char *artist, int volume);
void drawBluetoothTagPromptScreen();

// Playback volume overlay — shown at bottom during audio playback
void drawPlaybackVolumeOverlay(int level);
void clearPlaybackVolumeOverlay();

// Sleep timer countdown — shown in bottom section during playback
void drawSleepTimerCountdown(unsigned long remainingMs);
void updateSleepTimerCountdown(unsigned long remainingMs);

// Menu geometry + animated selector bar (gui.cpp drives the interpolation)
int  menuRowY(int index);                    // top y of menu row `index`
void drawMenuSelector(int prevY, int newY);  // prevY < 0 = not currently drawn

// Long-press progress along the bottom edge; pct 0..100
void drawHoldProgress(int pct);
void clearHoldProgress();

// Incremental updates — only redraw changed items (no flicker)
void updateMenuSelection(int oldSel, int newSel);
// Text (true values, instant) and bar fills (interpolated) update separately
// so an animated fill never makes the percentage lag behind the encoder.
void updateVolumeText(int level, int maxLevel, bool adjustingMax);
void updateVolumeBars(int level, int maxLevel);
void updateBrightnessText(int level);
void updateBrightnessBar(int level);
void updatePowerSaveDisplay(int minutes);
void updateSleepTimerDisplay(int minutes);
void updateWebConnectionCount(int connections);
void drawWebProgress(const char *label, int pct);  // long-op progress on web screen; pct<0 clears
void updateBluetoothVolume(int volume);
