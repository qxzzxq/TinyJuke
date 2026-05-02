#include "screen.h"
#include "tags.h"   // for uidToStr

static int16_t textWidth(const char *str) {
  int16_t x1, y1;
  uint16_t w, h;
  gfx.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return (int16_t)w;
}

static void centerText(const char *str, int16_t y, uint16_t color, uint8_t size) {
  gfx.setTextColor(color);
  gfx.setTextSize(size);
  int16_t w = textWidth(str);
  gfx.setCursor((gfx.width() - w) / 2, y);
  gfx.print(str);
}

// ----------------------------------------------------------------

void drawWaitingScreen() {
  gfx.fillScreen(C_BG);
  centerText("Jukebox", 20, C_TEXT, 2);
  centerText("Waiting for", 80, C_DIM, 1);
  centerText("tag...", 95, C_DIM, 1);
}

void drawTagScreen(const uint8_t *uid, uint8_t uidLen) {
  gfx.fillScreen(C_BG);
  centerText("TAG SCANNED", 15, C_ACCENT, 2);

  char uidStr[64];
  uidToStr(uid, uidLen, uidStr);

  if (textWidth(uidStr) > 110) {
    if (uidLen > 4) {
      int split = (uidLen + 1) / 2;
      char lineBuf[32];
      uidToStr(uid, split, lineBuf);
      centerText(lineBuf, 55, C_TEXT, 1);
      uidToStr(uid + split, uidLen - split, lineBuf);
      centerText(lineBuf, 70, C_TEXT, 1);
    } else {
      centerText(uidStr, 60, C_TEXT, 1);
    }
  } else {
    centerText(uidStr, 60, C_TEXT, 2);
  }
}

void drawUnknownTagScreen(const uint8_t *uid, uint8_t uidLen) {
  gfx.fillScreen(C_BG);
  centerText("UNKNOWN", 20, C_RED, 2);
  char uidStr[64];
  uidToStr(uid, uidLen, uidStr);
  centerText(uidStr, 70, C_TEXT, 1);
  centerText("Remove...", 120, C_DIM, 1);
}

void drawNowPlayingScreen(const char *filepath) {
  gfx.fillScreen(C_BG);
  centerText("PLAYING", 15, C_ACCENT, 2);

  const char *name = strrchr(filepath, '/');
  if (name) name++; else name = filepath;

  gfx.setTextColor(C_TEXT);
  gfx.setTextSize(1);

  int16_t w = textWidth(name);
  if (w > 120) {
    char trunc[20];
    int maxLen = 16;
    strncpy(trunc, name, maxLen);
    trunc[maxLen] = '\0';
    strcat(trunc, "...");
    centerText(trunc, 70, C_TEXT, 1);
  } else {
    centerText(name, 70, C_TEXT, 1);
  }

  centerText("Remove to stop", 130, C_DIM, 1);
}

void drawSDErrorScreen() {
  gfx.fillScreen(C_BG);
  centerText("SD CARD", 50, C_RED, 2);
  centerText("NOT FOUND", 80, C_RED, 1);
}
