#include "screen.h"
#include "tags.h"
#include "encoder.h"
#include "audio.h"  // for WavMeta, parseWavMeta
#include <SD.h>

// Screen is 128×160 portrait. Text size 2 = ~12px, size 1 = ~6px.

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
  gfx.fillRect(0, 0, 128, 22, C_SURFACE);
  gfx.setTextColor(C_MUTED);
  gfx.setTextSize(1);
  gfx.setCursor(4, 3);
  gfx.print(leftHint);
  gfx.setTextColor(C_TEXT);
  gfx.setTextSize(2);
  int16_t w = textWidth(title);
  gfx.setCursor((128 - w) / 2, 4);
  gfx.print(title);
}

static void drawHintBar(const char *hint) {
  gfx.setTextColor(C_MUTED);
  gfx.setTextSize(1);
  int16_t w = textWidth(hint);
  gfx.setCursor((128 - w) / 2, 148);
  gfx.print(hint);
}

static const char *trimFilename(const char *path) {
  const char *name = strrchr(path, '/');
  return name ? name + 1 : path;
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
  centerText("Jukebox", 40, C_TEXT, 2);
  centerText("scan a tag", 85, C_MUTED, 1);
  drawHintBar("hold for menu");
}

void drawTagScreen(const uint8_t *uid, uint8_t uidLen) {
  gfx.fillScreen(C_BG);
  centerText("TAG", 20, C_ACCENT, 2);
  char uidStr[64];
  uidToStr(uid, uidLen, uidStr);
  uint8_t sz = (textWidth(uidStr) > 110) ? 1 : 2;
  centerText(uidStr, 60, C_TEXT, sz);
}

void drawUnknownTagScreen(const uint8_t *uid, uint8_t uidLen) {
  gfx.fillScreen(C_BG);
  centerText("UNKNOWN", 30, C_RED, 2);
  char uidStr[64];
  uidToStr(uid, uidLen, uidStr);
  centerText(uidStr, 65, C_TEXT, 1);
  centerText("click to dismiss", 100, C_ACCENT, 1);
  drawHintBar("hold to dismiss");
}

// BMP loader (24-bit only, 128x160)
// Decode a 24-bit BMP, return heap-allocated RGB565 buffer + dimensions.
// Caller must free(). Returns nullptr on failure.
static uint16_t *loadBMP(const char *path, int *outW, int *outH) {
  File f = SD.open(path);
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
  uint16_t *buf = (uint16_t *)malloc(pixels * 2);
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
      dst[x] = ((b & 0xF8) << 8) | ((g & 0xFC) << 3) | (r >> 3);
    }
  }

  free(row);
  f.close();
  *outW = w;
  *outH = ht;
  return buf;
}

// Nearest-neighbor scale from src to 128x128 dst buffer.
static void scaleTo128(const uint16_t *src, int srcW, int srcH, uint16_t *dst) {
  for (int dy = 0; dy < 128; dy++) {
    int sy = dy * srcH / 128;
    const uint16_t *srcRow = src + sy * srcW;
    uint16_t *dstRow = dst + dy * 128;
    for (int dx = 0; dx < 128; dx++)
      dstRow[dx] = srcRow[dx * srcW / 128];
  }
}

void drawNowPlayingScreen(const TagInfo &tag) {
  // Reset volume overlay state so the next encoder rotation
  // does a full first draw for the new song.
  s_playbackVolDrawn = -1;

  // ---- load image ----
  uint16_t *rawBmp = nullptr;
  uint16_t *scaled = (uint16_t *)malloc(128 * 128 * 2);
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
      scaleTo128(rawBmp, bmpW, bmpH, scaled);
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
    // 128x128 album art at top, text bar at bottom
    gfx.draw16bitRGBBitmap(0, 0, scaled, 128, 128);

    // Text area: y=128..159 (32px)
    gfx.fillRect(0, 128, 128, 32, C_SURFACE);
    gfx.fillRect(0, 128, 128, 1, C_ACCENT);

    const char *line1 = title ? title : fallback;
    char buf[64];
    int16_t tw = textWidth(line1);
    if (tw > 122) {
      strncpy(buf, line1, 19); buf[19] = '\0'; strcat(buf, "...");
      centerText(buf, 132, C_TEXT, 1);
      strncpy(s_botTitle, buf, sizeof(s_botTitle) - 1);
    } else {
      centerText(line1, 132, C_TEXT, 1);
      strncpy(s_botTitle, line1, sizeof(s_botTitle) - 1);
    }

    if (artist) {
      if (album) snprintf(buf, sizeof(buf), "%s  \267  %s", artist, album);
      else snprintf(buf, sizeof(buf), "%s", artist);
      centerText(buf, 146, C_MUTED, 1);
      strncpy(s_botSub, buf, sizeof(s_botSub) - 1);
    } else if (album) {
      centerText(album, 146, C_MUTED, 1);
      strncpy(s_botSub, album, sizeof(s_botSub) - 1);
    } else {
      s_botSub[0] = '\0';
    }
    s_hasImg = true;
  } else {
    // Text-only layout
    if (title) {
      centerText("Playing", 20, C_ACCENT, 2);
      centerText(title, 55, C_TEXT, 1);
      if (artist) centerText(artist, 68, C_MUTED, 1);
      if (album)  centerText(album, 80, C_ACCENT, 1);
    } else {
      centerText("Playing", 20, C_ACCENT, 2);
      int16_t w = textWidth(fallback);
      if (w > 120) {
        char trunc[20];
        strncpy(trunc, fallback, 16); trunc[16] = '\0'; strcat(trunc, "...");
        centerText(trunc, 65, C_TEXT, 1);
      } else {
        centerText(fallback, 65, C_TEXT, (w > 60) ? 1 : 2);
      }
    }
    drawHintBar("remove tag to stop");
    s_hasImg = false;
  }

  if (scaled) free(scaled);
}

void drawSDErrorScreen() {
  gfx.fillScreen(C_BG);
  centerText("SD CARD", 60, C_RED, 2);
  centerText("NOT FOUND", 85, C_RED, 1);
}

// ================================================================
//  Menu screen
// ================================================================

static const char *MENU_ITEMS[] = { "Web Server", "Volume" };

void drawMenuScreen(int selected) {
  drawHeader("Menu", "back");
  const int startY = 36, itemH = 28;

  for (int i = 0; i < 2; i++) {
    int y = startY + i * itemH;
    if (i == selected)
      gfx.fillRect(6, y - 1, 116, itemH - 2, C_SURFACE);

    gfx.setTextColor(i == selected ? C_ACCENT : C_TEXT);
    gfx.setTextSize(1);
    gfx.setCursor(14, y + (itemH - 8) / 2);
    gfx.print(MENU_ITEMS[i]);
    if (i == selected) {
      gfx.setCursor(116, y + (itemH - 8) / 2);
      gfx.print(">");
    }
  }
  drawHintBar("turn \267 click \267 hold");
}

// ================================================================
//  Volume screen
// ================================================================

// Track the last level drawn on screen, so updateVolumeDisplay can
// do differential updates without flickering. Shared between
// drawVolumeScreen (initial draw) and updateVolumeDisplay (incremental).
static int s_volumeDrawn = -1;

void drawVolumeScreen(int level) {
  drawHeader("Volume", "back");

  const int barX = 14, barY = 70, barW = 100, barH = 14;
  gfx.fillRect(barX, barY, barW, barH, C_LINE);
  int fillW = (barW - 4) * level / 100;
  if (fillW > 0)
    gfx.fillRect(barX + 2, barY + 2, fillW, barH - 4, C_ACCENT);

  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", level);
  centerText(pct, 100, C_TEXT, 2);

  drawHintBar("turn to adjust \267 click to save");

  s_volumeDrawn = level;
}

// ================================================================
//  Web server screen
// ================================================================

// ================================================================
//  Playback volume overlay — replaces the bottom text section (y=128–159)
//  during playback. Uses incremental updates to avoid flicker.
//  On clear, the bottom section is redrawn from saved state.
// ================================================================

// Redraw just the bottom text section (y=128–159) from saved state.
// Called by clearPlaybackVolumeOverlay after the 5-second timeout.
static void redrawNowPlayingBottom() {
  if (s_hasImg) {
    gfx.fillRect(0, 128, 128, 32, C_SURFACE);
    gfx.fillRect(0, 128, 128, 1, C_ACCENT);

    if (s_botTitle[0])
      centerText(s_botTitle, 132, C_TEXT, 1);

    if (s_botSub[0])
      centerText(s_botSub, 146, C_MUTED, 1);
  } else {
    drawHintBar("remove tag to stop");
  }
}

void drawPlaybackVolumeOverlay(int level) {
  const int barX = 28, barY = 141, barW = 94, barH = 8;

  if (s_playbackVolDrawn < 0) {
    // Replace the bottom text section (y=128–159)
    gfx.fillRect(0, 128, 128, 32, C_SURFACE);
    gfx.fillRect(0, 128, 128, 1, C_ACCENT);

    gfx.setTextColor(C_TEXT);
    gfx.setTextSize(1);
    gfx.setCursor(6, 142);
    gfx.print("VOL");

    gfx.drawRect(barX - 1, barY - 1, barW + 2, barH + 2, C_LINE);
  }

  // Incremental bar fill
  int innerW = barW - 4;
  int fillW  = innerW * level / 100;
  int prevFillW = (s_playbackVolDrawn >= 0) ? innerW * s_playbackVolDrawn / 100 : 0;

  if (fillW < prevFillW)
    gfx.fillRect(barX + 2 + fillW, barY + 2, prevFillW - fillW, barH - 4, C_SURFACE);
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
//  No fillScreen() → no flicker on encoder rotation.
// ================================================================

void updateMenuSelection(int oldSel, int newSel) {
  const int startY = 36, itemH = 28;

  // Deselect old
  int yo = startY + oldSel * itemH;
  gfx.fillRect(6, yo - 1, 116, itemH - 2, C_BG);
  gfx.setTextColor(C_TEXT);
  gfx.setTextSize(1);
  gfx.setCursor(14, yo + (itemH - 8) / 2);
  gfx.print(MENU_ITEMS[oldSel]);

  // Select new
  int yn = startY + newSel * itemH;
  gfx.fillRect(6, yn - 1, 116, itemH - 2, C_SURFACE);
  gfx.setTextColor(C_ACCENT);
  gfx.setTextSize(1);
  gfx.setCursor(14, yn + (itemH - 8) / 2);
  gfx.print(MENU_ITEMS[newSel]);
  gfx.setCursor(116, yn + (itemH - 8) / 2);
  gfx.print(">");
}

void updateVolumeDisplay(int level) {
  const int barX = 14, barY = 70, barW = 100, barH = 14;
  int fillW = (barW - 4) * level / 100;

  if (s_volumeDrawn >= 0) {
    int prevFillW = (barW - 4) * s_volumeDrawn / 100;
    if (fillW < prevFillW) {
      // Volume decreased — only clear the portion that's no longer filled.
      int clearX = barX + 2 + fillW;
      int clearW = prevFillW - fillW;
      gfx.fillRect(clearX, barY + 2, clearW, barH - 4, C_LINE);
    }
  } else {
    gfx.fillRect(barX + 2, barY + 2, barW - 4, barH - 4, C_LINE);
  }

  if (fillW > 0)
    gfx.fillRect(barX + 2, barY + 2, fillW, barH - 4, C_ACCENT);

  // Erase and redraw percentage
  gfx.fillRect(0, 98, 128, 22, C_BG);
  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", level);
  centerText(pct, 100, C_TEXT, 2);

  s_volumeDrawn = level;
}

static int s_webDrawn = -1;

void drawWebServerScreen(int connections) {
  gfx.fillScreen(C_BG);
  centerText("Web Server", 18, C_ACCENT, 2);

  gfx.setTextSize(1);
  gfx.setTextColor(C_TEXT);
  gfx.setCursor(6, 46); gfx.print("SSID:");
  gfx.setTextColor(C_ACCENT);
  gfx.setCursor(42, 46); gfx.print(WIFI_SSID);

  gfx.setTextColor(C_TEXT);
  gfx.setCursor(6, 60); gfx.print("Pass:");
  gfx.setTextColor(C_ACCENT);
  gfx.setCursor(42, 60); gfx.print(WIFI_PASSWORD);

  gfx.setTextColor(C_TEXT);
  gfx.setCursor(6, 74); gfx.print("URL:");
  gfx.setTextColor(C_ACCENT);
  gfx.setCursor(42, 74); gfx.print("192.168.4.1");

  char conn[24];
  snprintf(conn, sizeof(conn), "%d web connection(s)", connections);
  centerText(conn, 94, C_TEXT, 1);

  gfx.setTextColor(C_MUTED);
  gfx.setCursor(6, 114); gfx.print("Open in browser");
  gfx.setCursor(6, 124); gfx.print("to manage files & tags");

  drawHintBar("click to stop server");
  s_webDrawn = connections;
}

void updateWebConnectionCount(int connections) {
  if (connections == s_webDrawn) return;

  gfx.fillRect(0, 92, 128, 16, C_BG);
  char conn[24];
  snprintf(conn, sizeof(conn), "%d web connection(s)", connections);
  centerText(conn, 94, C_TEXT, 1);
  s_webDrawn = connections;
}
