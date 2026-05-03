#pragma once

#include "config.h"
#include "tags.h"  // for TagInfo
#include <Arduino_GFX_Library.h>

// gfx object is defined in main.cpp — screen functions reference it.
extern Arduino_ST7735 gfx;

void drawWaitingScreen();
void drawTagScreen(const uint8_t *uid, uint8_t uidLen);
void drawUnknownTagScreen(const uint8_t *uid, uint8_t uidLen);
void drawNowPlayingScreen(const TagInfo &tag);
void drawSDErrorScreen();

// GUI screens
void drawMenuScreen(int selected);
void drawFileBrowser(const char *files[], int count, int selected);
void drawLinkScreen(const char *filename);
void drawLinkSuccess(const char *uid, const char *filename);
void drawVolumeScreen(int level);
void drawWebServerScreen();
