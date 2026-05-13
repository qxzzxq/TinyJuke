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
void drawVolumeScreen(int level);
void drawBrightnessScreen(int level);
void drawPowerSaveScreen(int minutes);
void drawSleepTimerScreen(int minutes);
void drawVersionScreen();
void drawWebServerScreen(int connections);
void drawRebootConfirmScreen();
void drawRebootingScreen();
void drawBluetoothScreen(bool connected, const char *peer,
                         const char *title, const char *artist, int volume);
void drawBluetoothTagPromptScreen();

// Playback volume overlay — shown at bottom during audio playback
void drawPlaybackVolumeOverlay(int level);
void clearPlaybackVolumeOverlay();

// Sleep timer countdown — shown in bottom section during playback
void drawSleepTimerCountdown(unsigned long remainingMs);
void updateSleepTimerCountdown(unsigned long remainingMs);

// Incremental updates — only redraw changed items (no flicker)
void updateMenuSelection(int oldSel, int newSel);
void updateVolumeDisplay(int level);
void updateBrightnessDisplay(int level);
void updatePowerSaveDisplay(int minutes);
void updateSleepTimerDisplay(int minutes);
void updateWebConnectionCount(int connections);
