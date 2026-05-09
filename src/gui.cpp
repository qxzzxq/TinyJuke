#include "gui.h"
#include "screen.h"
#include "encoder.h"
#include "web.h"

// ----------------------------------------------------------------
//  State
// ----------------------------------------------------------------

enum class Screen {
  MENU,
  VOLUME,
  BRIGHTNESS,
  WEB,
};

static Screen    scr       = Screen::MENU;
static bool      active    = false;
static int       menuSel   = 0;   // main menu selection
static bool      webRunning  = false;

// ----------------------------------------------------------------
//  Draw current screen
// ----------------------------------------------------------------

static void redraw() {
  switch (scr) {
    case Screen::MENU:
      drawMenuScreen(menuSel);
      break;
    case Screen::VOLUME:
      drawVolumeScreen(volumeLevel);
      break;
    case Screen::BRIGHTNESS:
      drawBrightnessScreen(brightnessLevel);
      break;
    case Screen::WEB:
      drawWebServerScreen(getWebConnectionCount());
      break;
  }
}

// ================================================================
//  Public API
// ================================================================

void guiEnter() {
  scr       = Screen::MENU;
  menuSel   = 0;
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

  // WEB screen: always service HTTP requests, update connection count
  if (scr == Screen::WEB && webRunning) {
    handleWebClient();
    updateWebConnectionCount(getWebConnectionCount());
  }

  // --- Encoder events ---
  int ev = readEncoder();
  if (ev == ENC_NONE) return;

  // --- HOLD = back / cancel (always full redraw) ---
  if (ev == ENC_HOLD) {
    switch (scr) {
      case Screen::MENU:
        drawWaitingScreen();
        active = false;  // exit management mode
        return;
      case Screen::VOLUME:
        saveVolume();
        scr = Screen::MENU; menuSel = 1;
        break;
      case Screen::BRIGHTNESS:
        saveBrightness();
        scr = Screen::MENU; menuSel = 2;
        break;
      case Screen::WEB:
        stopWebServer();
        webRunning = false;
        scr = Screen::MENU; menuSel = 0;
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
        menuSel = (menuSel + ev) % MENU_ITEM_COUNT;
        updateMenuSelection(prev, menuSel);
      } else if (ev < 0) {
        int prev = menuSel;
        menuSel = (menuSel + ev % MENU_ITEM_COUNT + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
        updateMenuSelection(prev, menuSel);
      } else if (ev == ENC_CLICK) {
        switch (menuSel) {
          case 0: initWebServer(); webRunning = true; scr = Screen::WEB; break;
          case 1: scr = Screen::VOLUME; break;
          case 2: scr = Screen::BRIGHTNESS; break;
        }
        redraw();
      }
      break;

    // ================ VOLUME ================
    case Screen::VOLUME: {
      int oldLevel = volumeLevel;

      for (;;) {
        if (ev > 0 && ev < ENC_CLICK) {
          int nv = volumeLevel + ev;
          volumeLevel = (nv > 100) ? 100 : (nv < 0 ? 0 : nv);
        } else if (ev < 0) {
          int nv = volumeLevel + ev;
          volumeLevel = (nv < 0) ? 0 : (nv > 100 ? 100 : nv);
        } else if (ev == ENC_CLICK) {
          saveVolume();
          scr = Screen::MENU; menuSel = 1;
          redraw();
          break;
        }
        else if (ev == ENC_HOLD) {
          saveVolume();
          scr = Screen::MENU; menuSel = 1;
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

    // ================ BRIGHTNESS ================
    case Screen::BRIGHTNESS: {
      int oldLevel = brightnessLevel;

      for (;;) {
        if (ev > 0 && ev < ENC_CLICK) {
          int nv = brightnessLevel + ev;
          brightnessLevel = (nv > 100) ? 100 : (nv < 0 ? 0 : nv);
        } else if (ev < 0) {
          int nv = brightnessLevel + ev;
          brightnessLevel = (nv < 0) ? 0 : (nv > 100 ? 100 : nv);
        } else if (ev == ENC_CLICK) {
          saveBrightness();
          scr = Screen::MENU; menuSel = 2;
          redraw();
          break;
        }
        else if (ev == ENC_HOLD) {
          saveBrightness();
          scr = Screen::MENU; menuSel = 2;
          redraw();
          break;
        }
        ev = readEncoder();
        if (ev == ENC_NONE) break;
      }

      if (brightnessLevel != oldLevel) {
        updateBrightnessDisplay(brightnessLevel);
        applyBrightness();
      }
      break;
    }

    // ================ WEB (encoder only used for HOLD/CLICK — HTTP serviced above) ================
    case Screen::WEB:
      if (ev == ENC_CLICK) {
        stopWebServer();
        webRunning = false;
        scr = Screen::MENU; menuSel = 0;
        redraw();
      }
      break;
  }
}
