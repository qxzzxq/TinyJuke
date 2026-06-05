#include "screen.h"
#include "tags.h"
#include "encoder.h"
#include "audio.h"
#include "timer_logic.h"
#include "web.h"  // getWebPin() for the web server screen
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

static const char *MENU_ITEMS[] = { "Web Server", "Bluetooth", "Volume", "Brightness", "Power Saving", "Sleep Timer", "Version", "Reboot" };

void drawMenuScreen(int selected) {
  drawHeader("Menu", "");
  // 7 items at itemH=32 fit in startY=44..(44+7*32)=268, leaving room for the hint bar.
  const int startY = 44, itemH = 32;

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
  drawHintBar("hold to return");
}

// ================================================================
//  Brightness screen
// ================================================================

static int s_brightnessDrawn = -1;

void drawBrightnessScreen(int level) {
  drawHeader("Brightness", "back");

  const int barX = 24, barY = 140, barW = 192, barH = 20;
  gfx.fillRect(barX, barY, barW, barH, C_LINE);
  int fillW = (barW - 4) * level / 100;
  if (fillW > 0)
    gfx.fillRect(barX + 2, barY + 2, fillW, barH - 4, C_TEXT);

  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", level);
  centerText(pct, 180, C_TEXT, 3);

  drawHintBar("turn to adjust \267 click to save");
  s_brightnessDrawn = level;
}

// ================================================================
//  Volume screen — two sections: Volume + Max Volume (software cap)
// ================================================================

static int s_volumeDrawn = -1;
static int s_maxVolDrawn = -1;
static int s_adjMaxDrawn = -1;

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

  drawHintBar("click to switch \267 hold to save");

  s_volumeDrawn = level;
  s_maxVolDrawn = maxLevel;
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

void drawBluetoothScreen(bool connected, const char *peer,
                         const char *title, const char *artist, int volume) {
  gfx.fillScreen(C_BG);
  centerText("Bluetooth", 30, C_ACCENT, 3);

  // Status line
  if (!connected) {
    centerText("Pair on your phone:", 90, C_MUTED, 2);
    centerText(BT_DEVICE_NAME, 118, C_TEXT, 2);
  } else {
    centerText("Connected", 90, C_MUTED, 2);
    if (peer && peer[0]) {
      char buf[64];
      int maxW = gfx.width() - 2 * marginForSize(2);
      truncateToFit(peer, buf, sizeof(buf), maxW, 2);
      centerText(buf, 118, C_TEXT, 2);
    } else {
      centerText(BT_DEVICE_NAME, 118, C_TEXT, 2);
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

  drawHintBar("turn=vol \267 click=settings \267 hold=exit");
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

void updateMenuSelection(int oldSel, int newSel) {
  const int startY = 44, itemH = 32;

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

void updateVolumeDisplay(int level, int maxLevel, bool adjustingMax) {
  int adj = adjustingMax ? 1 : 0;
  if (adj != s_adjMaxDrawn) {
    drawVolumeSectionLabel("Volume", VOL_LABEL_Y, !adjustingMax);
    drawVolumeSectionLabel("Max Volume", MAX_LABEL_Y, adjustingMax);
    s_adjMaxDrawn = adj;
  }
  if (level != s_volumeDrawn) {
    drawVolumeSectionBar(VOL_BAR_Y, level, s_volumeDrawn, C_ACCENT);
    drawVolumeSectionPct(VOL_PCT_Y, level);
    s_volumeDrawn = level;
  }
  if (maxLevel != s_maxVolDrawn) {
    drawVolumeSectionBar(MAX_BAR_Y, maxLevel, s_maxVolDrawn, C_TEXT);
    drawVolumeSectionPct(MAX_PCT_Y, maxLevel);
    s_maxVolDrawn = maxLevel;
  }
}

void updateBrightnessDisplay(int level) {
  const int barX = 24, barY = 140, barW = 192, barH = 20;
  int fillW = (barW - 4) * level / 100;

  if (s_brightnessDrawn >= 0) {
    int prevFillW = (barW - 4) * s_brightnessDrawn / 100;
    if (fillW < prevFillW) {
      int clearX = barX + 2 + fillW;
      int clearW = prevFillW - fillW;
      gfx.fillRect(clearX, barY + 2, clearW, barH - 4, C_LINE);
    }
  } else {
    gfx.fillRect(barX + 2, barY + 2, barW - 4, barH - 4, C_LINE);
  }

  if (fillW > 0)
    gfx.fillRect(barX + 2, barY + 2, fillW, barH - 4, C_TEXT);

  // Erase and redraw percentage (text at y=180, size 3 = 24px tall → 180..203)
  gfx.fillRect(0, 172, gfx.width(), 38, C_BG);
  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", level);
  centerText(pct, 180, C_TEXT, 3);

  s_brightnessDrawn = level;
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

  gfx.setTextColor(C_MUTED);
  gfx.setTextSize(1);
  int16_t lineW;
  int16_t x1, y1;
  uint16_t tw, th;

  const char *line1 = "Screen turns off after a";
  gfx.getTextBounds(line1, 0, 0, &x1, &y1, &tw, &th);
  lineW = (int16_t)tw;
  gfx.setCursor((gfx.width() - lineW) / 2, 190);
  gfx.print(line1);

  const char *line2 = "period of inactivity";
  gfx.getTextBounds(line2, 0, 0, &x1, &y1, &tw, &th);
  lineW = (int16_t)tw;
  gfx.setCursor((gfx.width() - lineW) / 2, 202);
  gfx.print(line2);

  drawHintBar("turn to change \267 click to save");
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

  drawHintBar("turn to change \267 click to save");
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

void drawVersionScreen() {
  drawHeader("Version", "back");

  gfx.setTextColor(C_TEXT);
  gfx.setTextSize(3);
  int16_t w = textWidth(VERSION_STRING);
  gfx.setCursor((gfx.width() - w) / 2, 100);
  gfx.print(VERSION_STRING);

  const char *mode =
#ifdef DEV_MODE
      "dev";
#else
      "release";
#endif

  gfx.setTextColor(C_ACCENT);
  gfx.setTextSize(3);
  w = textWidth(mode);
  gfx.setCursor((gfx.width() - w) / 2, 140);
  gfx.print(mode);

  drawHintBar("click or hold to return");
}

// ================================================================
//  Reboot confirm / rebooting screens
// ================================================================

void drawRebootConfirmScreen() {
  drawHeader("Reboot", "back");
  centerText("Reboot?", 90, C_TEXT, 3);
  centerText("Hold to confirm", 150, C_MUTED, 2);
  drawHintBar("click to cancel \267 hold to reboot");
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
