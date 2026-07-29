// SPDX-FileCopyrightText: 2026 qxzzxq
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screen.h"
#include "tags.h"
#include "encoder.h"
#include "audio.h"
#include "timer_logic.h"
#include "web.h"  // getWebPin() for the web server screen
#include "storage.h"  // sdOpenRead()
#include "qr_layout.h"
#include <qrcode.h>
#include <SD.h>
#include <esp_heap_caps.h>

// Screen is 240×320 portrait. Text size 3 = ~18px, size 2 = ~12px, size 1 = ~6px.

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

static void drawHeader(const char *title, const char *leftHint) {
  gfx.fillScreen(C_BG);
  gfx.fillRect(0, 0, gfx.width(), 34, C_SURFACE);
  gfx.setTextColor(C_MUTED);
  gfx.setTextSize(1);
  gfx.setCursor(6, 6);
  gfx.print(leftHint);
  gfx.setTextColor(C_TEXT);
  gfx.setTextSize(2);
  int16_t w = textWidth(title);
  gfx.setCursor((gfx.width() - w) / 2, 8);
  gfx.print(title);
}

static void drawHintBar(const char *hint) {
  gfx.setTextColor(C_HINT);
  gfx.setTextSize(1);
  int16_t w = textWidth(hint);
  gfx.setCursor((gfx.width() - w) / 2, gfx.height() - 15);
  gfx.print(hint);
}

static const char *trimFilename(const char *path) {
  const char *name = strrchr(path, '/');
  return name ? name + 1 : path;
}

// Truncate string to fit maxWidth pixels (with "..." appended), accounting
// for a ~2-letter margin on each side of the screen. The margin is derived
// from the text size: 12px per char at size 2, 6px at size 1.
static int marginForSize(uint8_t size) { return 5 * size; }

static void truncateToFit(const char *src, char *dst, size_t dstSize,
                          int maxWidth, uint8_t textSize) {
  gfx.setTextSize(textSize);
  if (textWidth(src) <= maxWidth) {
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
    return;
  }
  size_t len = strlen(src);
  if (len > dstSize - 4) len = dstSize - 4;
  while (len > 0) {
    char tmp[128];
    strncpy(tmp, src, len);
    tmp[len] = '\0';
    strcat(tmp, "...");
    if (textWidth(tmp) <= maxWidth) break;
    len--;
  }
  if (len > 0) {
    strncpy(dst, src, len);
    dst[len] = '\0';
    strcat(dst, "...");
  } else {
    dst[0] = '\0';
  }
}

// ================================================================
//  Jukebox screens
// ================================================================

// Saved bottom-section state for restoring after volume overlay
static bool s_hasImg          = false;
static char s_botTitle[48]    = "";
static char s_botSub[64]      = "";
static int  s_playbackVolDrawn = -1;

void drawWaitingScreen() {
  gfx.fillScreen(C_BG);
  centerText("TinyJuke", 100, C_TEXT, 3);
  centerText("insert a tag", 170, C_MUTED, 2);
  drawHintBar("hold for menu");
}

void drawTagScreen(const uint8_t *uid, uint8_t uidLen) {
  gfx.fillScreen(C_BG);
  centerText("TAG", 50, C_ACCENT, 3);
  char uidStr[64];
  uidToStr(uid, uidLen, uidStr);
  uint8_t sz = (textWidth(uidStr) > 220) ? 1 : 2;
  centerText(uidStr, 140, C_TEXT, sz);
}

void drawUnknownTagScreen(const uint8_t *uid, uint8_t uidLen) {
  gfx.fillScreen(C_BG);
  centerText("UNKNOWN", 60, C_RED, 3);
  char uidStr[64];
  uidToStr(uid, uidLen, uidStr);
  centerText(uidStr, 140, C_TEXT, 1);
  centerText("click to dismiss", 200, C_ACCENT, 2);
  drawHintBar("hold to dismiss");
}

// BMP loader (24-bit only)
// Decode a 24-bit BMP, return heap-allocated RGB565 buffer + dimensions.
// Caller must free(). Returns nullptr on failure.
static uint16_t *loadBMP(const char *path, int *outW, int *outH) {
  // A tag may name art that was never uploaded or has since been deleted;
  // the caller falls back to the text-only layout, so keep it off the log.
  File f = sdOpenRead(path);
  if (!f) return nullptr;

  uint8_t h[54];
  if (f.read(h, 54) != 54 || h[0] != 'B' || h[1] != 'M') { f.close(); return nullptr; }

  uint32_t off = h[10] | (h[11] << 8) | (h[12] << 16) | (h[13] << 24);
  int32_t  w   = h[18] | (h[19] << 8) | (h[20] << 16) | (h[21] << 24);
  int32_t  ht  = h[22] | (h[23] << 8) | (h[24] << 16) | (h[25] << 24);
  uint16_t bpp = h[28] | (h[29] << 8);

  if (bpp != 24 || w < 1 || ht == 0) { f.close(); return nullptr; }
  if (ht < 0) ht = -ht;
  if (w > 600 || ht > 600) { f.close(); return nullptr; } // sanity limit

  int pixels = w * ht;
  uint16_t *buf = (uint16_t *)heap_caps_malloc(pixels * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) buf = (uint16_t *)malloc(pixels * 2);  // fallback to DRAM
  if (!buf) { f.close(); return nullptr; }

  int rowBytes = ((w * bpp + 31) / 32) * 4;
  f.seek(off);

  uint8_t *row = (uint8_t *)malloc(rowBytes);
  if (!row) { free(buf); f.close(); return nullptr; }

  // BMP rows are bottom-up
  for (int y = ht - 1; y >= 0; y--) {
    f.read(row, rowBytes);
    uint16_t *dst = buf + y * w;
    for (int x = 0; x < w; x++) {
      uint8_t b = row[x * 3];
      uint8_t g = row[x * 3 + 1];
      uint8_t r = row[x * 3 + 2];
      dst[x] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }
  }

  free(row);
  f.close();
  *outW = w;
  *outH = ht;
  return buf;
}

// Nearest-neighbor scale from src to ART_SZ×ART_SZ dst buffer.
static const int ART_SZ = 240;

static void scaleToArt(const uint16_t *src, int srcW, int srcH, uint16_t *dst) {
  for (int dy = 0; dy < ART_SZ; dy++) {
    int sy = dy * srcH / ART_SZ;
    const uint16_t *srcRow = src + sy * srcW;
    uint16_t *dstRow = dst + dy * ART_SZ;
    for (int dx = 0; dx < ART_SZ; dx++)
      dstRow[dx] = srcRow[dx * srcW / ART_SZ];
  }
}

void drawNowPlayingScreen(const TagInfo &tag) {
  // Reset volume overlay state so the next encoder rotation
  // does a full first draw for the new song.
  s_playbackVolDrawn = -1;

  // ---- load image ----
  uint16_t *rawBmp = nullptr;
  uint16_t *scaled = (uint16_t *)malloc(ART_SZ * ART_SZ * 2);
  int bmpW = 0, bmpH = 0;
  bool hasImg = false;

  if (tag.img && scaled) {
    char bmpPath[192];
    if (tag.img[0] == '/')
      snprintf(bmpPath, sizeof(bmpPath), "%s", tag.img);
    else
      snprintf(bmpPath, sizeof(bmpPath), "/img/%s", tag.img);
    rawBmp = loadBMP(bmpPath, &bmpW, &bmpH);
    if (rawBmp) {
      scaleToArt(rawBmp, bmpW, bmpH, scaled);
      free(rawBmp);
      hasImg = true;
    }
  }

  // ---- text fields ----
  const char *title  = tag.title;
  const char *artist = tag.artist;
  const char *album  = tag.album;

  char metaTitle[64]  = {};
  char metaArtist[64] = {};
  if (!title || !artist) {
    WavMeta meta;
    parseWavMeta(tag.file, meta);
    if (!title  && meta.title[0])  { strncpy(metaTitle,  meta.title,  63); title  = metaTitle; }
    if (!artist && meta.artist[0]) { strncpy(metaArtist, meta.artist, 63); artist = metaArtist; }
  }

  // Filename fallback
  const char *name = trimFilename(tag.file);
  char fallback[32];
  strncpy(fallback, name, sizeof(fallback) - 1);
  fallback[sizeof(fallback) - 1] = '\0';
  char *ext = strrchr(fallback, '.');
  if (ext) *ext = '\0';

  // ---- draw ----
  gfx.fillScreen(C_BG);

  if (hasImg) {
    // 240x240 album art at top, text bar at bottom
    gfx.draw16bitRGBBitmap(0, 0, scaled, ART_SZ, ART_SZ);

    // Text area: y=240..319 (80px)
    // Title + artist block is ~30px tall; center vertically: top = 265
    gfx.fillRect(0, 240, gfx.width(), 80, C_SURFACE);
    gfx.fillRect(0, 240, gfx.width(), 1, C_ACCENT);

    const char *line1 = title ? title : fallback;
    char buf[64];
    int maxW = gfx.width() - 2 * marginForSize(2);
    truncateToFit(line1, buf, sizeof(buf), maxW, 2);
    centerText(buf, 265, C_TEXT, 2);
    strncpy(s_botTitle, buf, sizeof(s_botTitle) - 1);

    if (artist) {
      char artistBuf[64];
      // Plain ASCII separator: the built-in GFX font is 7-bit, so the Latin-1
      // middle dot (\267) that used to sit here rendered as a garbage glyph.
      if (album) snprintf(artistBuf, sizeof(artistBuf), "%s - %s", artist, album);
      else snprintf(artistBuf, sizeof(artistBuf), "%s", artist);
      int artistMaxW = gfx.width() - 2 * marginForSize(1);
      truncateToFit(artistBuf, buf, sizeof(buf), artistMaxW, 1);
      centerText(buf, 287, C_MUTED, 1);
      strncpy(s_botSub, buf, sizeof(s_botSub) - 1);
    } else if (album) {
      centerText(album, 287, C_MUTED, 1);
      strncpy(s_botSub, album, sizeof(s_botSub) - 1);
    } else {
      s_botSub[0] = '\0';
    }
    s_hasImg = true;
  } else {
    // Text-only layout
    if (title) {
      centerText("Playing", 60, C_ACCENT, 3);
      char buf[64];
      int titleMaxW = gfx.width() - 2 * marginForSize(2);
      truncateToFit(title, buf, sizeof(buf), titleMaxW, 2);
      centerText(buf, 150, C_TEXT, 2);
      if (artist) {
        int artMaxW = gfx.width() - 2 * marginForSize(1);
        truncateToFit(artist, buf, sizeof(buf), artMaxW, 1);
        centerText(buf, 178, C_MUTED, 1);
      }
      if (album)  centerText(album, 195, C_ACCENT, 1);
    } else {
      centerText("Playing", 60, C_ACCENT, 3);
      char buf[64];
      int fbMaxW = gfx.width() - 2 * marginForSize(2);
      truncateToFit(fallback, buf, sizeof(buf), fbMaxW, 2);
      centerText(buf, 160, C_TEXT, 2);
    }
    drawHintBar("remove tag to stop");
    s_hasImg = false;
  }

  if (scaled) free(scaled);
}

void drawSDErrorScreen() {
  gfx.fillScreen(C_BG);
  centerText("SD CARD", 130, C_RED, 3);
  centerText("NOT FOUND", 165, C_RED, 2);
}

// ================================================================
//  Menu screen
// ================================================================

static const char *MENU_ITEMS[] = { "Web Management", "Bluetooth Mode", "Volume", "Brightness", "Color Theme", "Power Saving", "Sleep Timer", "About", "Reboot" };

// 9 items at MENU_ITEM_H=28 fit in 44..(44+9*28)=296, leaving room for the hint bar.
static const int MENU_START_Y = 44, MENU_ITEM_H = 28;

// Selector bar: a slim accent marker in the left margin, clear of the row
// highlight (which starts at x=10) and of the label (x=18). Because it never
// overlaps text, it can be animated between rows with two small fills and no
// offscreen buffer.
static const int SEL_X = 4, SEL_W = 4, SEL_H = MENU_ITEM_H - 2;

int menuRowY(int index) { return MENU_START_Y + index * MENU_ITEM_H; }

// Move the selector from prevY to newY (both are row-top coordinates, as
// returned by menuRowY, or any interpolated value between them). Erases only
// the strip the bar vacates, so there is no clear-then-draw flicker.
// prevY < 0 means "not currently drawn".
void drawMenuSelector(int prevY, int newY) {
  if (prevY >= 0 && prevY != newY) {
    int dy = newY - prevY;
    if (dy >= SEL_H || dy <= -SEL_H) {
      gfx.fillRect(SEL_X, prevY - 1, SEL_W, SEL_H, C_BG);      // no overlap
    } else if (dy > 0) {
      gfx.fillRect(SEL_X, prevY - 1, SEL_W, dy, C_BG);          // vacated at top
    } else {
      gfx.fillRect(SEL_X, newY - 1 + SEL_H, SEL_W, -dy, C_BG);  // vacated at bottom
    }
  }
  gfx.fillRect(SEL_X, newY - 1, SEL_W, SEL_H, C_ACCENT);
}

void drawMenuScreen(int selected) {
  drawHeader("Menu", "");
  const int startY = MENU_START_Y, itemH = MENU_ITEM_H;

  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    int y = startY + i * itemH;
    if (i == selected)
      gfx.fillRect(10, y - 1, gfx.width() - 20, itemH - 2, C_SURFACE);

    gfx.setTextColor(i == selected ? C_ACCENT : C_TEXT);
    gfx.setTextSize(2);
    gfx.setCursor(18, y + (itemH - 14) / 2);
    gfx.print(MENU_ITEMS[i]);
    if (i == selected) {
      gfx.setCursor(gfx.width() - 28, y + (itemH - 14) / 2);
      gfx.print(">");
    }
  }
  drawMenuSelector(-1, menuRowY(selected));
  drawHintBar("hold to return");
}

// ================================================================
//  Long-press progress
// ================================================================
//
// A slim accent line along the very bottom edge, growing from the centre
// outward as the press approaches ENC_HOLD_MS. It sits below the hint-bar
// text (y=305..312), so it works on every screen without colliding with
// content — and every screen that accepts a hold has C_BG there, which is
// what lets clearHoldProgress() erase unconditionally.

static const int HOLD_BAR_Y = 315, HOLD_BAR_H = 4;
static int s_holdDrawn = -1;

void drawHoldProgress(int pct) {
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;
  if (pct == s_holdDrawn) return;   // nothing new to paint

  // The bar only ever grows during a press, so repainting the current extent
  // is enough — no erase needed until the press ends.
  int half = (gfx.width() / 2) * pct / 100;
  if (half > 0)
    gfx.fillRect(gfx.width() / 2 - half, HOLD_BAR_Y, half * 2, HOLD_BAR_H, C_ACCENT);
  s_holdDrawn = pct;
}

void clearHoldProgress() {
  if (s_holdDrawn < 0) return;      // never drawn — don't touch the screen
  gfx.fillRect(0, HOLD_BAR_Y, gfx.width(), HOLD_BAR_H, C_BG);
  s_holdDrawn = -1;
}

// ================================================================
//  Brightness screen
// ================================================================

static const int BRT_BAR_X = 24, BRT_BAR_Y = 140, BRT_BAR_W = 192, BRT_BAR_H = 20;

static int s_brightnessDrawn    = -1;  // percentage text
static int s_brightnessBarDrawn = -1;  // bar fill (animated independently)

void drawBrightnessScreen(int level) {
  drawHeader("Brightness", "back");

  gfx.fillRect(BRT_BAR_X, BRT_BAR_Y, BRT_BAR_W, BRT_BAR_H, C_LINE);
  int fillW = (BRT_BAR_W - 4) * level / 100;
  if (fillW > 0)
    gfx.fillRect(BRT_BAR_X + 2, BRT_BAR_Y + 2, fillW, BRT_BAR_H - 4, C_ACCENT);

  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", level);
  centerText(pct, 180, C_TEXT, 3);

  drawHintBar("turn to adjust - click to save");
  s_brightnessDrawn = s_brightnessBarDrawn = level;
}

// ================================================================
//  Volume screen — two sections: Volume + Max Volume (software cap)
// ================================================================

static int s_volumeDrawn = -1;  // percentage text
static int s_maxVolDrawn = -1;
static int s_adjMaxDrawn = -1;
static int s_volBarDrawn = -1;  // bar fills (animated independently of the text)
static int s_maxBarDrawn = -1;

// Section geometry (header occupies y=0..34, hint bar at the bottom)
static const int VOL_BAR_X = 24, VOL_BAR_W = 192, VOL_BAR_H = 16;
static const int VOL_LABEL_Y = 56,  VOL_BAR_Y = 78,  VOL_PCT_Y = 102;
static const int MAX_LABEL_Y = 160, MAX_BAR_Y = 182, MAX_PCT_Y = 206;

// Label row: ">" marker + accent color on the section the encoder adjusts.
static void drawVolumeSectionLabel(const char *text, int y, bool active) {
  // Clear the label band (size 2 text at y occupies y..y+16)
  gfx.fillRect(0, y - 4, gfx.width(), 24, C_BG);
  gfx.setTextSize(2);
  gfx.setTextColor(active ? C_ACCENT : C_MUTED);
  if (active) {
    gfx.setCursor(VOL_BAR_X, y);
    gfx.print(">");
  }
  gfx.setCursor(VOL_BAR_X + 20, y);
  gfx.print(text);
}

// prevLevel < 0 = full draw; otherwise incremental fill update.
static void drawVolumeSectionBar(int barY, int level, int prevLevel, uint16_t fillColor) {
  int fillW = (VOL_BAR_W - 4) * level / 100;
  if (prevLevel < 0) {
    gfx.fillRect(VOL_BAR_X, barY, VOL_BAR_W, VOL_BAR_H, C_LINE);
  } else {
    int prevFillW = (VOL_BAR_W - 4) * prevLevel / 100;
    if (fillW < prevFillW)
      gfx.fillRect(VOL_BAR_X + 2 + fillW, barY + 2, prevFillW - fillW, VOL_BAR_H - 4, C_LINE);
  }
  if (fillW > 0)
    gfx.fillRect(VOL_BAR_X + 2, barY + 2, fillW, VOL_BAR_H - 4, fillColor);
}

static void drawVolumeSectionPct(int pctY, int level) {
  // Clear full text area with margin (size 3 at pctY occupies pctY..pctY+24)
  gfx.fillRect(0, pctY - 8, gfx.width(), 38, C_BG);
  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", level);
  centerText(pct, pctY, C_TEXT, 3);
}

void drawVolumeScreen(int level, int maxLevel, bool adjustingMax) {
  drawHeader("Volume", "back");

  drawVolumeSectionLabel("Volume", VOL_LABEL_Y, !adjustingMax);
  drawVolumeSectionBar(VOL_BAR_Y, level, -1, C_ACCENT);
  drawVolumeSectionPct(VOL_PCT_Y, level);

  drawVolumeSectionLabel("Max Volume", MAX_LABEL_Y, adjustingMax);
  drawVolumeSectionBar(MAX_BAR_Y, maxLevel, -1, C_TEXT);
  drawVolumeSectionPct(MAX_PCT_Y, maxLevel);

  drawHintBar("click to switch - hold to save");

  s_volumeDrawn = s_volBarDrawn = level;
  s_maxVolDrawn = s_maxBarDrawn = maxLevel;
  s_adjMaxDrawn = adjustingMax ? 1 : 0;
}

// ================================================================
//  Web server screen
// ================================================================

static int s_webDrawn = -1;

void drawWebServerScreen(int connections) {
  gfx.fillScreen(C_BG);
  centerText("Web Server", 30, C_ACCENT, 3);

  int y = 80;
  gfx.setTextSize(2);

  gfx.setTextColor(C_TEXT);
  gfx.setCursor(8, y); gfx.print("SSID:");
  gfx.setTextColor(C_ACCENT);
  gfx.setCursor(72, y); gfx.print(WIFI_SSID);

  y += 26;
  gfx.setTextColor(C_TEXT);
  gfx.setCursor(8, y); gfx.print("Pass:");
  gfx.setTextColor(C_ACCENT);
  gfx.setCursor(72, y); gfx.print(WIFI_PASSWORD);

  y += 26;
  gfx.setTextColor(C_TEXT);
  gfx.setCursor(8, y); gfx.print("URL:");
  gfx.setTextColor(C_ACCENT);
  gfx.setCursor(72, y); gfx.print("192.168.4.1");

  y += 36;
  char conn[24];
  snprintf(conn, sizeof(conn), "%d web connection(s)", connections);
  centerText(conn, y, C_TEXT, 2);

  y += 30;
  char pinLine[24];
  snprintf(pinLine, sizeof(pinLine), "Update PIN: %s", getWebPin());
  centerText(pinLine, y, C_MUTED, 2);

  drawHintBar("click to stop server");
  s_webDrawn = connections;
}

void updateWebConnectionCount(int connections) {
  if (connections == s_webDrawn) return;

  // Connection count is at y=168 (80+26+26+36), size 2 = ~14px
  gfx.fillRect(0, 164, gfx.width(), 24, C_BG);
  char conn[24];
  snprintf(conn, sizeof(conn), "%d web connection(s)", connections);
  centerText(conn, 168, C_TEXT, 2);
  s_webDrawn = connections;
}

// Long-operation progress (metadata rewrite, firmware update) — free band
// (y=230..280) between the update-PIN line and the hint bar.
// pct<0 clears the area.
void drawWebProgress(const char *label, int pct) {
  if (pct < 0) {
    gfx.fillRect(0, 230, gfx.width(), 50, C_BG);
    return;
  }
  char text[40];
  snprintf(text, sizeof(text), "%s %d%%", label, pct);
  gfx.fillRect(0, 230, gfx.width(), 24, C_BG);  // full text-area clear (see CLAUDE.md)
  centerText(text, 234, C_TEXT, 2);

  int barW = gfx.width() - 40;
  int fill = barW * pct / 100;
  gfx.drawRect(20, 262, barW, 10, C_MUTED);
  if (fill > 0) gfx.fillRect(20, 262, fill, 10, C_ACCENT);
}

// ================================================================
//  Bluetooth screen
// ================================================================

void drawBluetoothScreen(bool connected, const char *deviceName, const char *peer,
                         const char *title, const char *artist, int volume) {
  gfx.fillScreen(C_BG);
  centerText("Bluetooth", 30, C_ACCENT, 3);

  // Status line
  if (!connected) {
    centerText("Pair on your phone:", 90, C_MUTED, 2);
    centerText(deviceName, 118, C_TEXT, 2);
  } else {
    centerText("Connected", 90, C_MUTED, 2);
    if (peer && peer[0]) {
      char buf[64];
      int maxW = gfx.width() - 2 * marginForSize(2);
      truncateToFit(peer, buf, sizeof(buf), maxW, 2);
      centerText(buf, 118, C_TEXT, 2);
    } else {
      centerText(deviceName, 118, C_TEXT, 2);
    }
  }

  // Track metadata block — title + artist, or fallback "Bluetooth"
  bool haveTitle  = title  && title[0];
  bool haveArtist = artist && artist[0];
  if (haveTitle || haveArtist) {
    if (haveTitle) {
      char buf[64];
      int maxW = gfx.width() - 2 * marginForSize(2);
      truncateToFit(title, buf, sizeof(buf), maxW, 2);
      centerText(buf, 170, C_TEXT, 2);
    }
    if (haveArtist) {
      char buf[64];
      int maxW = gfx.width() - 2 * marginForSize(1);
      truncateToFit(artist, buf, sizeof(buf), maxW, 1);
      centerText(buf, 196, C_MUTED, 1);
    }
  } else {
    centerText("Bluetooth", 170, C_TEXT, 2);
  }

  updateBluetoothVolume(volume);

  drawHintBar("turn=vol - click=settings - hold=exit");
}

void updateBluetoothVolume(int volume) {
  // Volume bar + labels live in a fixed band at the bottom of the BT screen.
  // Repaint only this region on volume change to avoid full-screen flicker.
  const int barX = 24, barY = 240, barW = 192, barH = 16;
  const int labelY = barY - 12;

  // Clear the label strip (height of size-1 text = 8px) and the bar.
  gfx.fillRect(barX, labelY, barW, 8, C_BG);
  gfx.fillRect(barX, barY, barW, barH, C_LINE);
  int fillW = (barW - 4) * volume / 100;
  if (fillW > 0)
    gfx.fillRect(barX + 2, barY + 2, fillW, barH - 4, C_ACCENT);

  gfx.setTextColor(C_MUTED);
  gfx.setTextSize(1);
  gfx.setCursor(barX, labelY);
  gfx.print("VOL");
  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", volume);
  gfx.setCursor(barX + barW - textWidth(pct), labelY);
  gfx.print(pct);
}

void drawBluetoothTagPromptScreen() {
  gfx.fillScreen(C_BG);
  centerText("Tag detected", 70, C_ACCENT, 3);
  centerText("Switch to", 140, C_TEXT, 2);
  centerText("jukebox mode?", 168, C_TEXT, 2);
  centerText("click to switch", 220, C_ACCENT, 2);
  drawHintBar("hold or lift tag to dismiss");
}

// ================================================================
//  Playback volume overlay — replaces the bottom text section (y=240–319)
//  during playback. Uses incremental updates to avoid flicker.
//  On clear, the bottom section is redrawn from saved state.
// ================================================================

static void redrawNowPlayingBottom() {
  if (s_hasImg) {
    gfx.fillRect(0, 240, gfx.width(), 80, C_SURFACE);
    gfx.fillRect(0, 240, gfx.width(), 1, C_ACCENT);

    if (s_botTitle[0])
      centerText(s_botTitle, 265, C_TEXT, 2);

    if (s_botSub[0])
      centerText(s_botSub, 287, C_MUTED, 1);
  } else {
    drawHintBar("remove tag to stop");
  }
}

void drawPlaybackVolumeOverlay(int level) {
  // Center bar vertically in the 80px bottom area (y=240..319, center=280)
  const int barX = 55, barY = 273, barW = 172, barH = 14;

  if (s_playbackVolDrawn < 0) {
    // Replace the bottom text section (y=240–319)
    gfx.fillRect(0, 240, gfx.width(), 80, C_SURFACE);
    gfx.fillRect(0, 240, gfx.width(), 1, C_ACCENT);

    gfx.setTextColor(C_TEXT);
    gfx.setTextSize(2);
    gfx.setCursor(8, barY);
    gfx.print("VOL");

    gfx.fillRect(barX, barY, barW, barH, C_LINE);
  }

  // Incremental bar fill
  int innerW = barW - 4;
  int fillW  = innerW * level / 100;
  int prevFillW = (s_playbackVolDrawn >= 0) ? innerW * s_playbackVolDrawn / 100 : 0;

  if (fillW < prevFillW)
    gfx.fillRect(barX + 2 + fillW, barY + 2, prevFillW - fillW, barH - 4, C_LINE);
  else if (fillW > prevFillW)
    gfx.fillRect(barX + 2 + prevFillW, barY + 2, fillW - prevFillW, barH - 4, C_ACCENT);

  s_playbackVolDrawn = level;
}

void clearPlaybackVolumeOverlay() {
  s_playbackVolDrawn = -1;
  redrawNowPlayingBottom();
}

// ================================================================
//  Incremental screen updates — only redraw changed items
// ================================================================

// Swap the row fill / label colors / chevron. The selector bar is NOT touched
// here — gui.cpp animates it separately so it can glide between the two rows.
void updateMenuSelection(int oldSel, int newSel) {
  const int startY = MENU_START_Y, itemH = MENU_ITEM_H;

  // Deselect old
  int yo = startY + oldSel * itemH;
  gfx.fillRect(10, yo - 1, gfx.width() - 20, itemH - 2, C_BG);
  gfx.setTextColor(C_TEXT);
  gfx.setTextSize(2);
  gfx.setCursor(18, yo + (itemH - 14) / 2);
  gfx.print(MENU_ITEMS[oldSel]);

  // Select new
  int yn = startY + newSel * itemH;
  gfx.fillRect(10, yn - 1, gfx.width() - 20, itemH - 2, C_SURFACE);
  gfx.setTextColor(C_ACCENT);
  gfx.setTextSize(2);
  gfx.setCursor(18, yn + (itemH - 14) / 2);
  gfx.print(MENU_ITEMS[newSel]);
  gfx.setCursor(gfx.width() - 28, yn + (itemH - 14) / 2);
  gfx.print(">");
}

// Section labels + percentages. Driven by the true values so the numbers never
// trail the encoder — only the bar fills are animated (updateVolumeBars).
void updateVolumeText(int level, int maxLevel, bool adjustingMax) {
  int adj = adjustingMax ? 1 : 0;
  if (adj != s_adjMaxDrawn) {
    drawVolumeSectionLabel("Volume", VOL_LABEL_Y, !adjustingMax);
    drawVolumeSectionLabel("Max Volume", MAX_LABEL_Y, adjustingMax);
    s_adjMaxDrawn = adj;
  }
  if (level != s_volumeDrawn) {
    drawVolumeSectionPct(VOL_PCT_Y, level);
    s_volumeDrawn = level;
  }
  if (maxLevel != s_maxVolDrawn) {
    drawVolumeSectionPct(MAX_PCT_Y, maxLevel);
    s_maxVolDrawn = maxLevel;
  }
}

// Bar fills only — fed interpolated values by gui.cpp's animation tick.
void updateVolumeBars(int level, int maxLevel) {
  if (level != s_volBarDrawn) {
    drawVolumeSectionBar(VOL_BAR_Y, level, s_volBarDrawn, C_ACCENT);
    s_volBarDrawn = level;
  }
  if (maxLevel != s_maxBarDrawn) {
    drawVolumeSectionBar(MAX_BAR_Y, maxLevel, s_maxBarDrawn, C_TEXT);
    s_maxBarDrawn = maxLevel;
  }
}

void updateBrightnessText(int level) {
  if (level == s_brightnessDrawn) return;
  // Erase and redraw percentage (text at y=180, size 3 = 24px tall → 180..203)
  gfx.fillRect(0, 172, gfx.width(), 38, C_BG);
  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", level);
  centerText(pct, 180, C_TEXT, 3);
  s_brightnessDrawn = level;
}

void updateBrightnessBar(int level) {
  if (level == s_brightnessBarDrawn) return;
  int fillW = (BRT_BAR_W - 4) * level / 100;

  if (s_brightnessBarDrawn >= 0) {
    int prevFillW = (BRT_BAR_W - 4) * s_brightnessBarDrawn / 100;
    if (fillW < prevFillW)
      gfx.fillRect(BRT_BAR_X + 2 + fillW, BRT_BAR_Y + 2,
                   prevFillW - fillW, BRT_BAR_H - 4, C_LINE);
  } else {
    gfx.fillRect(BRT_BAR_X + 2, BRT_BAR_Y + 2, BRT_BAR_W - 4, BRT_BAR_H - 4, C_LINE);
  }

  if (fillW > 0)
    gfx.fillRect(BRT_BAR_X + 2, BRT_BAR_Y + 2, fillW, BRT_BAR_H - 4, C_ACCENT);

  s_brightnessBarDrawn = level;
}

// ================================================================
//  Power save screen
// ================================================================

static const char *POWERSAVE_LABELS[] = {
  "Off",
#ifdef DEV_MODE
  "1 min",
#endif
  "5 min", "15 min", "30 min", "60 min"
};

static int s_powerSaveDrawn = -1;

void drawPowerSaveScreen(int minutes) {
  drawHeader("Power Saving", "back");

  int idx = powerSaveToIndex(minutes);
  centerText(POWERSAVE_LABELS[idx], 120, C_TEXT, 3);

  // Description in size-2 text (rewrapped so each line fits 240px at this size).
  centerText("Screen turns off", 190, C_MUTED, 2);
  centerText("when idle", 212, C_MUTED, 2);

  drawHintBar("turn to change - click to save");
  s_powerSaveDrawn = idx;
}

void updatePowerSaveDisplay(int minutes) {
  int idx = powerSaveToIndex(minutes);
  if (idx == s_powerSaveDrawn) return;

  // Clear text area with margin (text at y=120, size 3 = 24px tall → 120..143)
  gfx.fillRect(0, 112, gfx.width(), 40, C_BG);
  centerText(POWERSAVE_LABELS[idx], 120, C_TEXT, 3);

  s_powerSaveDrawn = idx;
}

// ================================================================
//  Sleep timer screen
// ================================================================

static const char *SLEEP_LABELS[] = {
  "Off",
#ifdef DEV_MODE
  "1 min",
#endif
  "15 min", "30 min", "60 min", "120 min"
};

static int s_sleepDrawn = -1;

void drawSleepTimerScreen(int minutes) {
  drawHeader("Sleep Timer", "back");

  int idx = sleepTimerToIndex(minutes);
  centerText(SLEEP_LABELS[idx], 140, C_TEXT, 3);

  // Description in size-2 text, matching the Power Saving screen.
  centerText("Audio stops when", 190, C_MUTED, 2);
  centerText("the timer ends", 212, C_MUTED, 2);

  drawHintBar("turn to change - click to save");
  s_sleepDrawn = idx;
}

void updateSleepTimerDisplay(int minutes) {
  int idx = sleepTimerToIndex(minutes);
  if (idx == s_sleepDrawn) return;

  // Clear text area with margin (text at y=140, size 3 = 24px tall → 140..163)
  gfx.fillRect(0, 132, gfx.width(), 40, C_BG);
  centerText(SLEEP_LABELS[idx], 140, C_TEXT, 3);

  s_sleepDrawn = idx;
}

// ================================================================
//  Version screen
// ================================================================

// QR version 4 = 33x33 modules, holding 62 bytes here — comfortably more than
// the releases URL. 33 modules is also about the largest symbol that still
// leaves usefully chunky pixels on a 240 px-wide screen.
//
// Two quirks of ricmoo/QRCode worth knowing before touching any of this:
//
//  - Its ECC_* constants do not line up with its own table order. ECC_LOW is 0,
//    which indexes the row holding the *Medium* codeword counts, so we get
//    Medium correction and Medium's 62-byte capacity. That is fine — stronger
//    correction than we asked for — but it is why the number is 62 rather than
//    the 78 a version-4 / ECC-L table would lead you to expect.
//  - qrcode_initBytes() never checks the data length against that capacity
//    before writing. Over-long input silently overruns the buffer instead of
//    returning an error, so the static_assert below is the only thing standing
//    between a longer URL and memory corruption. Do not weaken it, and do not
//    rely on the initText() return value to catch an oversized string.
static const uint8_t QR_VERSION  = 4;
static const int     QR_MODULES  = 4 * QR_VERSION + 17;   // 33
static const size_t  QR_CAPACITY = 62;

// Fixed black-on-white, never themed — see the polarity note on drawQrCode().
static const uint16_t QR_LIGHT = 0xFFFF;
static const uint16_t QR_DARK  = 0x0000;

static_assert(sizeof(RELEASE_URL) - 1 <= QR_CAPACITY,
              "RELEASE_URL exceeds the QR capacity and would overflow the "
              "encoder buffer at runtime — raise QR_VERSION, re-derive "
              "QR_CAPACITY, and re-check the symbol still fits the screen");
static_assert(sizeof(PROJECT_URL) - 1 <= QR_CAPACITY,
              "PROJECT_URL exceeds the QR capacity and would overflow the "
              "encoder buffer at runtime — see the RELEASE_URL note above");

// Render a QR for `text` into the given box.
//
// Deliberately drawn as black modules on a white patch rather than in theme
// colours: the format assumes dark-on-light, and enough scanners refuse an
// inverted symbol that theming this would leave it looking correct but
// unusable. Same reason the quiet zone is part of the white patch.
static void drawQrCode(const char *text, int boxX, int boxY, int boxW, int boxH) {
  QrPlacement p = qrPlace(QR_MODULES, boxX, boxY, boxW, boxH);
  if (p.scale < 1) return;   // nothing sensible to draw in this space

  QRCode qr;
  uint8_t qrData[qrcode_getBufferSize(QR_VERSION)];
  if (qrcode_initText(&qr, qrData, QR_VERSION, ECC_LOW, text) != 0) return;

  gfx.fillRect(p.x, p.y, p.size, p.size, QR_LIGHT);

  // Draw each row as horizontal runs of dark modules instead of one fillRect
  // per module: a 33x33 symbol is ~1089 rects otherwise, and every one pays
  // its own SPI transaction setup.
  for (uint8_t my = 0; my < qr.size; my++) {
    uint8_t mx = 0;
    while (mx < qr.size) {
      if (!qrcode_getModule(&qr, mx, my)) { mx++; continue; }
      uint8_t run = 0;
      while (mx + run < qr.size && qrcode_getModule(&qr, mx + run, my)) run++;
      gfx.fillRect(p.originX + mx * p.scale, p.originY + my * p.scale,
                   run * p.scale, p.scale, QR_DARK);
      mx += run;
    }
  }
}

// ================================================================
//  About screen (paged)
// ================================================================
//
// Two pages, iOS-style dots at the bottom, encoder rotation flips between
// them. Page 1 points at the latest release, page 2 at the project itself.
//
// Both pages hand drawQrCode() the *same* band (ABOUT_QR_BAND_Y/_H), so the two
// symbols are pixel-identical in size and position and flipping pages moves
// only the text around them. The band is sized so qrPlace() lands on scale 4
// (41 modules incl. quiet zone x 4 = 164 px); a shorter band would silently
// drop to scale 3 and shrink that page's symbol by a quarter.
//
// Each page gets at most two lines of text: one block above the QR and one
// caption below. An earlier revision also put a caption line directly above
// page 2's QR, which left just 3 px of background between them — the symbol
// read as cramped and smaller than page 1's even though both were 164 px.
static const int ABOUT_QR_BAND_Y = 96;
static const int ABOUT_QR_BAND_H = 172;
static const int ABOUT_CAPTION_Y = 272;

// Dots sit in the free band between the caption line (..279) and the hint bar
// (305..), clear of the hold indicator at 315..318.
static void drawPageDots(int count, int active, int y) {
  const int r = 3, pitch = 14;
  const int cx0 = (gfx.width() - (count - 1) * pitch) / 2;
  for (int i = 0; i < count; i++)
    gfx.fillCircle(cx0 + i * pitch, y, r, i == active ? C_ACCENT : C_DIM);
}

// "made with <3 by qxzzxq", with the heart in C_RED.
//
// 0x03 is the heart in the classic CP437 glyph table that Arduino_GFX ships as
// its default font; write() passes every byte except \n and \r straight to
// drawChar(), so it renders without any UTF-8 involvement. (This is why it is
// safe here even though AVRCP metadata is ASCII-filtered — that filter exists
// to reject multi-byte sequences, not the CP437 upper/control range.)
// Split across two lines because the whole phrase at size 2 is 252 px wide on
// a 240 px panel.
static void drawCreditLines(int16_t y) {
  const char *lead = "made with ";
  gfx.setTextSize(2);

  int16_t w = textWidth(lead) + textWidth("\x03");
  gfx.setCursor((gfx.width() - w) / 2, y);
  gfx.setTextColor(C_TEXT);
  gfx.print(lead);
  gfx.setTextColor(C_RED);
  gfx.print("\x03");

  centerText("by " AUTHOR_HANDLE, y + 22, C_TEXT, 2);
}

static void drawAboutVersionPage() {
  const char *mode =
#ifdef DEV_MODE
      "dev";
#else
      "release";
#endif

  centerText(VERSION_STRING, 44, C_TEXT, 3);
  centerText(mode, 74, C_ACCENT, 2);

  drawQrCode(RELEASE_URL, 0, ABOUT_QR_BAND_Y, gfx.width(), ABOUT_QR_BAND_H);
  centerText("scan for latest release", ABOUT_CAPTION_Y, C_MUTED, 1);
}

static void drawAboutProjectPage() {
  drawCreditLines(48);

  drawQrCode(PROJECT_URL, 0, ABOUT_QR_BAND_Y, gfx.width(), ABOUT_QR_BAND_H);
  // Brighter than page 1's caption: this one is the thing to read and type in,
  // not a hint about what the QR does.
  centerText(PROJECT_URL_SHORT, ABOUT_CAPTION_Y, C_TEXT, 1);
}

void drawAboutScreen(int page) {
  drawHeader("About", "back");

  if (page == 0) drawAboutVersionPage();
  else           drawAboutProjectPage();

  drawPageDots(ABOUT_PAGES, page, 289);
  drawHintBar("turn for more - click to return");
}

// ================================================================
//  Color theme screen
// ================================================================
//
// The screen itself is the preview: the header/background/hint bar already
// paint in the active palette. On top we add the theme name (in the accent),
// a position indicator, a swatch row of every role, and a sample card so
// text-on-surface is visible too.

void drawThemeScreen(int index) {
  drawHeader("Color Theme", "back");

  centerText(themeName(index), 88, C_ACCENT, 3);

  char pos[12];
  snprintf(pos, sizeof(pos), "%d / %d", index + 1, themeCount());
  centerText(pos, 128, C_MUTED, 2);

  // Swatch row: every role color as a chip, outlined so dark tones stay visible.
  const uint16_t chips[] = { C_BG, C_SURFACE, C_TEXT, C_MUTED, C_ACCENT,
                             C_DIM, C_RED, C_LINE, C_HINT };
  const int n = sizeof(chips) / sizeof(chips[0]);
  const int sw = 20, gap = 3;
  const int totalW = n * sw + (n - 1) * gap;
  const int x0 = (gfx.width() - totalW) / 2;
  const int y0 = 166;
  for (int i = 0; i < n; i++) {
    int x = x0 + i * (sw + gap);
    gfx.fillRect(x, y0, sw, sw, chips[i]);
    gfx.drawRect(x, y0, sw, sw, C_MUTED);
  }

  // Sample card — previews accent title + muted subtitle on a surface fill.
  gfx.fillRect(20, 208, gfx.width() - 40, 48, C_SURFACE);
  centerText("Now Playing", 216, C_ACCENT, 2);
  centerText("Artist - Track", 238, C_MUTED, 1);

  drawHintBar("turn to change - click to save");
}

// ================================================================
//  Reboot confirm / rebooting screens
// ================================================================

void drawRebootConfirmScreen() {
  drawHeader("Reboot", "back");
  centerText("Reboot?", 90, C_TEXT, 3);
  centerText("Hold to confirm", 150, C_MUTED, 2);
  drawHintBar("click to cancel - hold to reboot");
}

void drawRebootingScreen() {
  gfx.fillScreen(C_BG);
  centerText("Rebooting...", 150, C_ACCENT, 3);
}

// ================================================================
//  Sleep timer countdown — replaces bottom section during playback
// ================================================================

void drawSleepTimerCountdown(unsigned long remainingMs) {
  gfx.fillRect(0, 240, gfx.width(), 80, C_SURFACE);
  gfx.fillRect(0, 240, gfx.width(), 1, C_ACCENT);

  gfx.setTextColor(C_MUTED);
  gfx.setTextSize(1);
  int16_t w = textWidth("Sleep");
  gfx.setCursor((gfx.width() - w) / 2, 250);
  gfx.print("Sleep");

  char buf[8];
  formatCountdownMMSS((uint32_t)remainingMs, buf, sizeof(buf));

  gfx.setTextColor(C_TEXT);
  gfx.setTextSize(3);
  w = textWidth(buf);
  gfx.setCursor((gfx.width() - w) / 2, 268);
  gfx.print(buf);
}

void updateSleepTimerCountdown(unsigned long remainingMs) {
  // Clear time text generously (size 3 at y=268, ~24px tall → 268..291)
  gfx.fillRect(0, 258, gfx.width(), 42, C_SURFACE);

  char buf[8];
  formatCountdownMMSS((uint32_t)remainingMs, buf, sizeof(buf));

  gfx.setTextColor(C_TEXT);
  gfx.setTextSize(3);
  int16_t w = textWidth(buf);
  gfx.setCursor((gfx.width() - w) / 2, 268);
  gfx.print(buf);
}
