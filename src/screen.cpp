#include "screen.h"
#include "tags.h"
#include "encoder.h"

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

void drawNowPlayingScreen(const char *filepath) {
  gfx.fillScreen(C_BG);
  centerText("Playing", 20, C_ACCENT, 2);

  const char *name = trimFilename(filepath);
  gfx.setTextColor(C_TEXT);
  gfx.setTextSize(1);
  int16_t w = textWidth(name);
  if (w > 120) {
    char trunc[20];
    strncpy(trunc, name, 16);
    trunc[16] = '\0';
    strcat(trunc, "...");
    centerText(trunc, 65, C_TEXT, 1);
  } else {
    centerText(name, 65, C_TEXT, 1);
  }
  drawHintBar("remove tag to stop");
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
