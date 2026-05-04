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
  centerText("click to link", 100, C_ACCENT, 1);
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
    } else {
      centerText(line1, 132, C_TEXT, 1);
    }

    if (artist) {
      if (album) snprintf(buf, sizeof(buf), "%s  \267  %s", artist, album);
      else snprintf(buf, sizeof(buf), "%s", artist);
      centerText(buf, 146, C_MUTED, 1);
    } else if (album) {
      centerText(album, 146, C_MUTED, 1);
    }
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

static const char *MENU_ITEMS[] = { "Manage Tags", "Web Server", "Volume" };

void drawMenuScreen(int selected) {
  drawHeader("Menu", "back");
  const int startY = 36, itemH = 28;

  for (int i = 0; i < 3; i++) {
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
//  File browser
// ================================================================

#define MAX_VISIBLE_FILES 4

void drawFileBrowser(const char *files[], int count, int selected) {
  drawHeader("Files", "back");
  if (count == 0) {
    centerText("No files", 80, C_MUTED, 1);
    drawHintBar("hold to go back");
    return;
  }

  int topIdx = selected - 1;
  if (topIdx < 0) topIdx = 0;
  if (topIdx + MAX_VISIBLE_FILES > count)
    topIdx = count - MAX_VISIBLE_FILES;
  if (topIdx < 0) topIdx = 0;

  const int startY = 32, itemH = 24;

  for (int i = 0; i < MAX_VISIBLE_FILES && (topIdx + i) < count; i++) {
    int idx = topIdx + i;
    int y = startY + i * itemH;

    if (idx == selected)
      gfx.fillRect(6, y - 1, 116, itemH - 2, C_SURFACE);

    gfx.setTextColor(idx == selected ? C_ACCENT : C_TEXT);
    gfx.setTextSize(1);

    const char *name = trimFilename(files[idx]);
    char line[22];
    if (textWidth(name) > 110) {
      strncpy(line, name, 18);
      line[18] = '\0';
      strcat(line, "...");
    } else {
      strncpy(line, name, sizeof(line) - 1);
      line[sizeof(line) - 1] = '\0';
    }
    gfx.setCursor(14, y + (itemH - 8) / 2);
    gfx.print(line);

    if (idx == selected) {
      gfx.setCursor(116, y + (itemH - 8) / 2);
      gfx.print(">");
    }
  }

  if (count > MAX_VISIBLE_FILES) {
    int barH = (MAX_VISIBLE_FILES * 100) / count;
    int barY = startY + (topIdx * (MAX_VISIBLE_FILES * itemH)) / count;
    gfx.fillRect(124, barY, 2, barH, C_MUTED);
  }
  drawHintBar("click to link tag");
}

// ================================================================
//  Link tag screen
// ================================================================

void drawLinkScreen(const char *filename) {
  drawHeader("Link Tag", "cancel");
  centerText("Link to:", 50, C_MUTED, 1);

  const char *name = trimFilename(filename);
  gfx.setTextColor(C_TEXT);
  int16_t w = textWidth(name);
  if (w > 120) {
    char trunc[20];
    strncpy(trunc, name, 16);
    trunc[16] = '\0';
    strcat(trunc, "...");
    centerText(trunc, 65, C_TEXT, 1);
  } else {
    centerText(name, 65, C_TEXT, (w > 60) ? 1 : 2);
  }
  centerText("Scan tag now", 105, C_ACCENT, 1);
  drawHintBar("hold to cancel");
}

// ================================================================
//  Link success screen
// ================================================================

void drawLinkSuccess(const char *uid, const char *filename) {
  drawHeader("Linked", "");
  centerText("Tag Linked", 45, C_ACCENT, 2);
  centerText(uid, 70, C_TEXT, 1);

  const char *name = trimFilename(filename);
  char arrow[64];
  snprintf(arrow, sizeof(arrow), "-> %s", name);
  centerText(arrow, 85, C_MUTED, 1);
  drawHintBar("click to continue");
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

void updateFileSelection(int oldSel, int newSel, const char *files[], int count) {
  if (count == 0) return;

  auto topFor = [count](int sel) {
    int t = sel - 1;
    if (t < 0) t = 0;
    if (t + MAX_VISIBLE_FILES > count) t = count - MAX_VISIBLE_FILES;
    if (t < 0) t = 0;
    return t;
  };

  int oldTop = topFor(oldSel);
  int newTop = topFor(newSel);

  const int startY = 32, itemH = 24;

  auto drawOne = [startY, itemH, files](int idx, int top, bool sel) {
    int y = startY + (idx - top) * itemH;
    gfx.fillRect(6, y - 1, 116, itemH - 2, sel ? C_SURFACE : C_BG);

    const char *name = trimFilename(files[idx]);
    char line[22];
    if (textWidth(name) > 110) {
      strncpy(line, name, 18); line[18] = '\0'; strcat(line, "...");
    } else {
      strncpy(line, name, sizeof(line) - 1); line[sizeof(line) - 1] = '\0';
    }

    gfx.setTextColor(sel ? C_ACCENT : C_TEXT);
    gfx.setTextSize(1);
    gfx.setCursor(14, y + (itemH - 8) / 2);
    gfx.print(line);
    if (sel) {
      gfx.setCursor(116, y + (itemH - 8) / 2);
      gfx.print(">");
    }
  };

  if (oldTop == newTop) {
    // Same visible window — just swap two items
    drawOne(oldSel, oldTop, false);
    drawOne(newSel, newTop, true);
  } else {
    // Window scrolled — redraw all visible items
    gfx.fillRect(0, 22, 128, 126, C_BG);
    for (int i = 0; i < MAX_VISIBLE_FILES && (newTop + i) < count; i++)
      drawOne(newTop + i, newTop, (newTop + i) == newSel);
  }

  // Update scrollbar
  if (count > MAX_VISIBLE_FILES) {
    gfx.fillRect(124, startY, 2, MAX_VISIBLE_FILES * itemH, C_BG);
    int barH = (MAX_VISIBLE_FILES * 100) / count;
    int barY = startY + (newTop * (MAX_VISIBLE_FILES * itemH)) / count;
    gfx.fillRect(124, barY, 2, barH, C_MUTED);
  }
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

void drawWebServerScreen() {
  gfx.fillScreen(C_BG);
  centerText("Web Server", 18, C_ACCENT, 2);

  gfx.setTextSize(1);
  gfx.setTextColor(C_TEXT);
  gfx.setCursor(6, 46); gfx.print("SSID:");
  gfx.setTextColor(C_ACCENT);
  gfx.setCursor(42, 46); gfx.print(WIFI_SSID);

  gfx.setTextColor(C_TEXT);
  gfx.setCursor(6, 62); gfx.print("Pass:");
  gfx.setTextColor(C_ACCENT);
  gfx.setCursor(42, 62); gfx.print(WIFI_PASSWORD);

  gfx.setTextColor(C_TEXT);
  gfx.setCursor(6, 78); gfx.print("URL:");
  gfx.setTextColor(C_ACCENT);
  gfx.setCursor(42, 78); gfx.print("192.168.4.1");

  gfx.setTextColor(C_MUTED);
  gfx.setCursor(6, 100); gfx.print("Open in browser");
  gfx.setCursor(6, 110); gfx.print("to manage files & tags");

  drawHintBar("click to stop server");
}
