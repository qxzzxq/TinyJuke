#pragma once

#include "config.h"
#include <Arduino_GFX_Library.h>

// gfx object is defined in main.cpp — screen functions reference it.
extern Arduino_ST7735 gfx;

void drawWaitingScreen();
void drawTagScreen(const uint8_t *uid, uint8_t uidLen);
void drawUnknownTagScreen(const uint8_t *uid, uint8_t uidLen);
void drawNowPlayingScreen(const char *filepath);
void drawSDErrorScreen();
