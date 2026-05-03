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
static bool loadBMP(const char *path, uint16_t *buf) {
  File f = SD.open(path);
  if (!f) return false;

  uint8_t h[54];
  if (f.read(h, 54) != 54 || h[0] != 'B' || h[1] != 'M') { f.close(); return false; }

  uint32_t off = h[10] | (h[11] << 8) | (h[12] << 16) | (h[13] << 24);
  int32_t  w   = h[18] | (h[19] << 8) | (h[20] << 16) | (h[21] << 24);
  int32_t  ht  = h[22] | (h[23] << 8) | (h[24] << 16) | (h[25] << 24);
  uint16_t bpp = h[28] | (h[29] << 8);

  if (w != 128 || (ht != 160 && ht != -160)) { f.close(); return false; }
  if (ht < 0) ht = -ht;

  int rowBytes = ((w * bpp + 31) / 32) * 4;
  f.seek(off);

  if (bpp == 24) {
    uint8_t row[128 * 3 + 4];
    for (int y = ht - 1; y >= 0; y--) {
      f.read(row, rowBytes);
      uint16_t *dst = buf + y * 128;
      for (int x = 0; x < 128; x++) {
        uint8_t b = row[x * 3];
        uint8_t g = row[x * 3 + 1];
        uint8_t r = row[x * 3 + 2];
        dst[x] = ((b & 0xF8) << 8) | ((g & 0xFC) << 3) | (r >> 3);
      }
    }
  } else {
    f.close(); return false;
  }

  f.close();
  return true;
}

void drawNowPlayingScreen(const TagInfo &tag) {
  // ---- determine image source ----
  // Priority: tags.json img field > WAV metadata > none
  uint16_t *bmpBuf = (uint16_t *)malloc(128 * 160 * 2);
  bool hasBmp = false;

  if (tag.img) {
    // Load from /img/ directory
    char bmpPath[192];
    if (tag.img[0] == '/')
      snprintf(bmpPath, sizeof(bmpPath), "%s", tag.img);
    else
      snprintf(bmpPath, sizeof(bmpPath), "/img/%s", tag.img);
    hasBmp = bmpBuf && loadBMP(bmpPath, bmpBuf);
  }

  // ---- determine text fields ----
  // Priority: tags.json fields > WAV metadata > filename fallback
  const char *title  = tag.title;
  const char *artist = tag.artist;
  const char *album  = tag.album;

  // If no tags.json metadata, try WAV LIST INFO
  char metaTitle[64]  = {};
  char metaArtist[64] = {};
  if (!title || !artist) {
    WavMeta meta;
    parseWavMeta(tag.file, meta);
    if (!title  && meta.title[0])  { metaTitle[63] = '\0'; strncpy(metaTitle, meta.title, 63); title = metaTitle; }
    if (!artist && meta.artist[0]) { metaArtist[63] = '\0'; strncpy(metaArtist, meta.artist, 63); artist = metaArtist; }
  }

  // Filename fallback
  const char *name = trimFilename(tag.file);
  char fallback[32];
  strncpy(fallback, name, sizeof(fallback) - 1);
  fallback[sizeof(fallback) - 1] = '\0';
  char *ext = strrchr(fallback, '.');
  if (ext) *ext = '\0';

  // ---- draw ----
  if (hasBmp) {
    gfx.draw16bitRGBBitmap(0, 0, bmpBuf, 128, 160);

    // Choose overlay height based on content
    int barH = (artist || album) ? 36 : 22;
    int barY = 160 - barH;
    gfx.fillRect(0, barY, 128, barH, C_BG);
    gfx.fillRect(0, barY, 128, 1, C_ACCENT);

    const char *line1 = title ? title : fallback;
    int16_t tw = textWidth(line1);
    if (tw > 120) {
      char trunc[20];
      strncpy(trunc, line1, 16); trunc[16] = '\0'; strcat(trunc, "...");
      centerText(trunc, barY + 4, C_TEXT, 1);
    } else {
      centerText(line1, barY + 4, C_TEXT, 1);
    }
    if (artist) {
      char line2[64];
      if (album) snprintf(line2, sizeof(line2), "%s \267 %s", artist, album);
      else snprintf(line2, sizeof(line2), "%s", artist);
      centerText(line2, barY + 16, C_MUTED, 1);
    }
  } else {
    gfx.fillScreen(C_BG);

    if (title) {
      centerText(title, 35, C_TEXT, 1);
      if (artist) centerText(artist, 50, C_MUTED, 1);
      if (album)  centerText(album, 62, C_ACCENT, 1);
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

  if (hasBmp) free(bmpBuf);
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

void drawVolumeScreen(int level) {
  drawHeader("Volume", "back");

  const int barX = 14, barY = 70, barW = 100, barH = 14;
  gfx.fillRoundRect(barX, barY, barW, barH, 4, C_LINE);
  int fillW = (barW - 4) * level / 100;
  if (fillW > 0)
    gfx.fillRoundRect(barX + 2, barY + 2, fillW, barH - 4, 3, C_ACCENT);

  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", level);
  centerText(pct, 100, C_TEXT, 2);

  drawHintBar("turn to adjust \267 click to save");
}

// ================================================================
//  Web server screen
// ================================================================

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
