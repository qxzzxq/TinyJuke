#include "gui.h"
#include "screen.h"
#include "tags.h"
#include "encoder.h"
#include "web.h"

#include <SD.h>
#include <PN532.h>
#include <WiFi.h>

// The NFC reader is defined in main.cpp — GUI needs it for tag linking.
extern PN532 nfc;

// ----------------------------------------------------------------
//  State
// ----------------------------------------------------------------

enum class Screen {
  MENU,
  FILES,
  LINK,
  LINK_OK,
  VOLUME,
  WEB,
};

static Screen    scr       = Screen::MENU;
static bool      active    = false;
static int       selection = 0;
static int       menuSel   = 0;   // main menu selection
static int       fileSel   = 0;   // file browser selection
static int       fileCount = 0;
static char      linkFile[64] = "";
static char      linkUID[32]  = "";
static bool      webRunning  = false;
static uint32_t  linkDismiss = 0; // auto-dismiss timer for LINK_OK

#define MAX_FILES 64
static char files[MAX_FILES][32];

// ----------------------------------------------------------------
//  File enumeration
// ----------------------------------------------------------------

static void scanFiles() {
  fileCount = 0;
  File dir = SD.open("/music");
  if (!dir || !dir.isDirectory()) return;

  File f;
  while ((f = dir.openNextFile()) && fileCount < MAX_FILES) {
    if (!f.isDirectory()) {
      const char *path = f.name();
      // Store full path relative to SD root for SD.open()
      if (path[0] != '/') {
        snprintf(files[fileCount], 31, "/%s", path);
      } else {
        strncpy(files[fileCount], path, 31);
      }
      files[fileCount][31] = '\0';
      fileCount++;
    }
    f.close();
  }
  dir.close();
}

// ----------------------------------------------------------------
//  Draw current screen
// ----------------------------------------------------------------

static void redraw() {
  switch (scr) {
    case Screen::MENU:
      drawMenuScreen(menuSel);
      break;
    case Screen::FILES:
      drawFileBrowser((const char **)files, fileCount, fileSel);
      break;
    case Screen::LINK:
      drawLinkScreen(linkFile);
      break;
    case Screen::LINK_OK:
      drawLinkSuccess(linkUID, linkFile);
      break;
    case Screen::VOLUME:
      drawVolumeScreen(volumeLevel);
      break;
    case Screen::WEB:
      drawWebServerScreen();
      break;
  }
}

// ----------------------------------------------------------------
//  Save tag mapping to SD
// ----------------------------------------------------------------

static bool saveTagDoc() {
  if (SD.exists("/tags.json")) SD.remove("/tags.json");
  File f = SD.open("/tags.json", FILE_WRITE);
  if (!f) return false;
  serializeJson(tagDoc, f);
  f.close();
  return true;
}

// ================================================================
//  Public API
// ================================================================

void guiEnter() {
  scanFiles();
  scr       = Screen::MENU;
  menuSel   = 0;
  fileSel   = 0;
  active    = true;
  webRunning = false;
  redraw();
}

bool guiActive() {
  return active;
}

void guiLoop() {
  if (!active) return;

  // --- Continuous processing (runs regardless of encoder events) ---

  // LINK screen: always poll for a tag
  if (scr == Screen::LINK) {
    uint8_t uid[7];
    uint8_t uidLen;
    bool found = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 100);
    if (found) {
      char buf[32];
      uint8_t pos = 0;
      for (uint8_t i = 0; i < uidLen; i++) {
        if (uid[i] < 0x10) buf[pos++] = '0';
        else buf[pos++] = "0123456789ABCDEF"[(uid[i] >> 4) & 0x0F];
        buf[pos++] = "0123456789ABCDEF"[uid[i] & 0x0F];
        if (i < uidLen - 1) buf[pos++] = ':';
      }
      buf[pos] = '\0';
      strncpy(linkUID, buf, 31);
      linkUID[31] = '\0';

      tagDoc[buf]["file"] = linkFile;
      saveTagDoc();

      scr = Screen::LINK_OK;
      linkDismiss = millis() + 3000;
      redraw();
      return;
    }
  }

  // LINK_OK screen: auto-dismiss after timeout
  if (scr == Screen::LINK_OK && millis() > linkDismiss) {
    scr = Screen::FILES;
    scanFiles();
    redraw();
    return;
  }

  // WEB screen: always service HTTP requests
  if (scr == Screen::WEB && webRunning) {
    handleWebClient();
  }

  // --- Encoder events ---
  int ev = readEncoder();
  if (ev == ENC_NONE) return;

  // --- HOLD = back / cancel (always full redraw) ---
  if (ev == ENC_HOLD) {
    switch (scr) {
      case Screen::MENU:
        active = false;  // exit management mode
        return;
      case Screen::FILES:
        scr = Screen::MENU; menuSel = 0;
        break;
      case Screen::LINK:
        scr = Screen::FILES;
        break;
      case Screen::LINK_OK:
        scr = Screen::FILES;
        break;
      case Screen::VOLUME:
        saveVolume();
        scr = Screen::MENU; menuSel = 2;
        break;
      case Screen::WEB:
        WiFi.softAPdisconnect(true);
        webRunning = false;
        scr = Screen::MENU; menuSel = 1;
        break;
    }
    redraw();
    return;
  }

  // --- Per-screen handlers ---
  switch (scr) {

    // ================ MENU ================
    case Screen::MENU:
      if (ev > 0 && ev < ENC_CLICK) {
        int prev = menuSel;
        menuSel = (menuSel + ev) % 3;
        updateMenuSelection(prev, menuSel);
      } else if (ev < 0) {
        int prev = menuSel;
        menuSel = (menuSel + ev % 3 + 3) % 3;
        updateMenuSelection(prev, menuSel);
      } else if (ev == ENC_CLICK) {
        switch (menuSel) {
          case 0: scanFiles(); fileSel = 0; scr = Screen::FILES; break;
          case 1: initWebServer(); webRunning = true; scr = Screen::WEB; break;
          case 2: scr = Screen::VOLUME; break;
        }
        redraw();
      }
      break;

    // ================ FILES ================
    case Screen::FILES:
      if (fileCount == 0) {
        if (ev == ENC_HOLD) { scr = Screen::MENU; menuSel = 0; }
        redraw();
        break;
      }
      if (ev > 0 && ev < ENC_CLICK) {
        int prev = fileSel;
        fileSel = (fileSel + ev) % fileCount;
        updateFileSelection(prev, fileSel, (const char **)files, fileCount);
      } else if (ev < 0) {
        int prev = fileSel;
        fileSel = (fileSel + ev % fileCount + fileCount) % fileCount;
        updateFileSelection(prev, fileSel, (const char **)files, fileCount);
      } else if (ev == ENC_CLICK) {
        strncpy(linkFile, files[fileSel], 63);
        linkFile[63] = '\0';
        scr = Screen::LINK;
        redraw();
      }
      break;

    // ================ LINK (encoder events ignored — NFC polled above) ================
    case Screen::LINK:
      break;

    // ================ LINK_OK (click to dismiss early) ================
    case Screen::LINK_OK:
      if (ev == ENC_CLICK) {
        scr = Screen::FILES;
        scanFiles();
        redraw();
      }
      break;

    // ================ VOLUME ================
    case Screen::VOLUME: {
      int oldLevel = volumeLevel;

      // readEncoder now returns the full accumulated delta (±1..±N),
      // so fast encoder turns arrive as a single event. Still batch
      // to catch any steps that land between loop iterations.
      for (;;) {
        if (ev > 0 && ev < ENC_CLICK) {
          int nv = volumeLevel + ev;
          volumeLevel = (nv > 100) ? 100 : (nv < 0 ? 0 : nv);
        } else if (ev < 0) {
          int nv = volumeLevel + ev;
          volumeLevel = (nv < 0) ? 0 : (nv > 100 ? 100 : nv);
        } else if (ev == ENC_CLICK) {
          saveVolume();
          scr = Screen::MENU; menuSel = 2;
          redraw();
          break;
        }
        else if (ev == ENC_HOLD) {
          saveVolume();
          scr = Screen::MENU; menuSel = 2;
          redraw();
          break;
        }
        ev = readEncoder();
        if (ev == ENC_NONE) break;
      }

      if (volumeLevel != oldLevel)
        updateVolumeDisplay(volumeLevel);
      break;
    }

    // ================ WEB (encoder only used for HOLD/CLICK — HTTP serviced above) ================
    case Screen::WEB:
      if (ev == ENC_CLICK) {
        WiFi.softAPdisconnect(true);
        webRunning = false;
        scr = Screen::MENU; menuSel = 1;
        redraw();
      }
      break;
  }
}
