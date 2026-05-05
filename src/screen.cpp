#include "screen.h"
#include "tags.h"
#include "encoder.h"
#include "audio.h"
#include <SD.h>

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
  gfx.setTextColor(C_MUTED);
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
  centerText("Jukebox", 100, C_TEXT, 3);
  centerText("scan a tag", 170, C_MUTED, 2);
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
      if (album) snprintf(artistBuf, sizeof(artistBuf), "%s  \267  %s", artist, album);
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

static const char *MENU_ITEMS[] = { "Web Server", "Volume" };

void drawMenuScreen(int selected) {
  drawHeader("Menu", "");
  const int startY = 50, itemH = 34;

  for (int i = 0; i < 2; i++) {
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
  drawHintBar("hold to return");
}

// ================================================================
//  Volume screen
// ================================================================

static int s_volumeDrawn = -1;

void drawVolumeScreen(int level) {
  drawHeader("Volume", "back");

  const int barX = 24, barY = 140, barW = 192, barH = 20;
  gfx.fillRect(barX, barY, barW, barH, C_LINE);
  int fillW = (barW - 4) * level / 100;
  if (fillW > 0)
    gfx.fillRect(barX + 2, barY + 2, fillW, barH - 4, C_ACCENT);

  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", level);
  centerText(pct, 180, C_TEXT, 3);

  drawHintBar("turn to adjust \267 click to save");

  s_volumeDrawn = level;
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
  gfx.setTextColor(C_MUTED);
  gfx.setTextSize(2);
  gfx.setCursor(12, y); gfx.print("Open in browser to manage tags");

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
// ================================================================

void updateMenuSelection(int oldSel, int newSel) {
  const int startY = 50, itemH = 34;

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

void updateVolumeDisplay(int level) {
  const int barX = 24, barY = 140, barW = 192, barH = 20;
  int fillW = (barW - 4) * level / 100;

  if (s_volumeDrawn >= 0) {
    int prevFillW = (barW - 4) * s_volumeDrawn / 100;
    if (fillW < prevFillW) {
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
  gfx.fillRect(0, 176, gfx.width(), 26, C_BG);
  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", level);
  centerText(pct, 180, C_TEXT, 3);

  s_volumeDrawn = level;
}
