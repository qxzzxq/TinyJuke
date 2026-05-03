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

  int ev = readEncoder();
  if (ev == ENC_NONE) return;

  // --- Global: HOLD = back / cancel ---
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
        // Stop web server, return to menu
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
      if (ev == ENC_CW) {
        menuSel = (menuSel + 1) % 3;
      } else if (ev == ENC_CCW) {
        menuSel = (menuSel + 2) % 3;
      } else if (ev == ENC_CLICK) {
        switch (menuSel) {
          case 0: // Manage Tags
            scanFiles();
            fileSel = 0;
            scr = Screen::FILES;
            break;
          case 1: // Web Server
            initWebServer();
            webRunning = true;
            scr = Screen::WEB;
            break;
          case 2: // Volume
            scr = Screen::VOLUME;
            break;
        }
      }
      redraw();
      break;

    // ================ FILES ================
    case Screen::FILES:
      if (fileCount == 0) {
        redraw();
        break;
      }
      if (ev == ENC_CW) {
        fileSel = (fileSel + 1) % fileCount;
      } else if (ev == ENC_CCW) {
        fileSel = (fileSel + fileCount - 1) % fileCount;
      } else if (ev == ENC_CLICK) {
        // Enter link mode
        strncpy(linkFile, files[fileSel], 63);
        linkFile[63] = '\0';
        scr = Screen::LINK;
      }
      redraw();
      break;

    // ================ LINK ================
    case Screen::LINK: {
      // Poll NFC for a tag to link
      uint8_t uid[7];
      uint8_t uidLen;
      bool found = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 100);

      if (found) {
        char buf[32];
        // Build colon-separated UID string
        uint8_t pos = 0;
        for (uint8_t i = 0; i < uidLen; i++) {
          if (uid[i] < 0x10) buf[pos++] = '0';
          else buf[pos++] = "0123456789ABCDEF"[(uid[i] >> 4) & 0x0F];
          buf[pos++] = "0123456789ABCDEF"[uid[i] & 0x0F];
          if (i < uidLen - 1) buf[pos++] = ':';
        }
        buf[pos] = '\0';

        // Store the UID for success screen
        strncpy(linkUID, buf, 31);
        linkUID[31] = '\0';

        // Check if already mapped
        if (!tagDoc[buf].isNull()) {
          // Already mapped — will show it on success screen
        }

        // Create or update the mapping
        tagDoc[buf]["file"] = linkFile;
        saveTagDoc();

        scr = Screen::LINK_OK;
        linkDismiss = millis() + 3000;
      }
      redraw();
      break;
    }

    // ================ LINK_OK ================
    case Screen::LINK_OK:
      if (ev == ENC_CLICK || millis() > linkDismiss) {
        scr = Screen::FILES;
        scanFiles();
      }
      redraw();
      break;

    // ================ VOLUME ================
    case Screen::VOLUME:
      if (ev == ENC_CW && volumeLevel < 100) {
        volumeLevel++;
      } else if (ev == ENC_CCW && volumeLevel > 0) {
        volumeLevel--;
      } else if (ev == ENC_CLICK) {
        saveVolume();
        scr = Screen::MENU; menuSel = 2;
      }
      redraw();
      break;

    // ================ WEB ================
    case Screen::WEB:
      if (webRunning) {
        handleWebClient();  // service HTTP requests
      }
      // CLICK handled above
      break;
  }
}
