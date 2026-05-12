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
  POWERSAVING,
  SLEEPTIMER,
  VERSION,
  WEB,
  REBOOT,
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
    case Screen::POWERSAVING:
      drawPowerSaveScreen(powerSaveMinutes);
      break;
    case Screen::SLEEPTIMER:
      drawSleepTimerScreen(sleepTimerMinutes);
      break;
    case Screen::VERSION:
      drawVersionScreen();
      break;
    case Screen::WEB:
      drawWebServerScreen(getWebConnectionCount());
      break;
    case Screen::REBOOT:
      drawRebootConfirmScreen();
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
  resetActivityTimer();  // any interaction resets the idle timer

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
      case Screen::POWERSAVING:
        savePowerSave();
        scr = Screen::MENU; menuSel = 3;
        break;
      case Screen::SLEEPTIMER:
        saveSleepTimer();
        scr = Screen::MENU; menuSel = 4;
        break;
      case Screen::VERSION:
        scr = Screen::MENU; menuSel = 5;
        break;
      case Screen::WEB:
        stopWebServer();
        webRunning = false;
        scr = Screen::MENU; menuSel = 0;
        break;
      case Screen::REBOOT:
        // HOLD on the confirm screen = perform the reboot.
        drawRebootingScreen();
        delay(500);
        ESP.restart();
        return;  // not reached
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
          case 3: scr = Screen::POWERSAVING; break;
          case 4: scr = Screen::SLEEPTIMER; break;
          case 5: scr = Screen::VERSION; break;
          case 6: scr = Screen::REBOOT; break;
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

    // ================ POWERSAVING ================
    case Screen::POWERSAVING: {
      int oldMinutes = powerSaveMinutes;

      for (;;) {
        if (ev > 0 && ev < ENC_CLICK) {
          int idx = powerSaveToIndex(powerSaveMinutes) + ev;
          while (idx < 0) idx += POWERSAVE_OPTIONS;
          while (idx >= POWERSAVE_OPTIONS) idx -= POWERSAVE_OPTIONS;
          powerSaveMinutes = powerSaveToMinutes(idx);
        } else if (ev < 0) {
          int idx = powerSaveToIndex(powerSaveMinutes) + ev;
          while (idx < 0) idx += POWERSAVE_OPTIONS;
          while (idx >= POWERSAVE_OPTIONS) idx -= POWERSAVE_OPTIONS;
          powerSaveMinutes = powerSaveToMinutes(idx);
        } else if (ev == ENC_CLICK) {
          savePowerSave();
          scr = Screen::MENU; menuSel = 3;
          redraw();
          break;
        }
        else if (ev == ENC_HOLD) {
          savePowerSave();
          scr = Screen::MENU; menuSel = 3;
          redraw();
          break;
        }
        ev = readEncoder();
        if (ev == ENC_NONE) break;
      }

      if (powerSaveMinutes != oldMinutes)
        updatePowerSaveDisplay(powerSaveMinutes);
      break;
    }

    // ================ SLEEPTIMER ================
    case Screen::SLEEPTIMER: {
      int oldMinutes = sleepTimerMinutes;

      for (;;) {
        if (ev > 0 && ev < ENC_CLICK) {
          int idx = sleepTimerToIndex(sleepTimerMinutes) + ev;
          while (idx < 0) idx += SLEEP_OPTIONS;
          while (idx >= SLEEP_OPTIONS) idx -= SLEEP_OPTIONS;
          sleepTimerMinutes = sleepTimerToMinutes(idx);
        } else if (ev < 0) {
          int idx = sleepTimerToIndex(sleepTimerMinutes) + ev;
          while (idx < 0) idx += SLEEP_OPTIONS;
          while (idx >= SLEEP_OPTIONS) idx -= SLEEP_OPTIONS;
          sleepTimerMinutes = sleepTimerToMinutes(idx);
        } else if (ev == ENC_CLICK) {
          saveSleepTimer();
          scr = Screen::MENU; menuSel = 4;
          redraw();
          break;
        }
        else if (ev == ENC_HOLD) {
          saveSleepTimer();
          scr = Screen::MENU; menuSel = 4;
          redraw();
          break;
        }
        ev = readEncoder();
        if (ev == ENC_NONE) break;
      }

      if (sleepTimerMinutes != oldMinutes)
        updateSleepTimerDisplay(sleepTimerMinutes);
      break;
    }

    // ================ VERSION ================
    case Screen::VERSION:
      if (ev == ENC_CLICK) {
        scr = Screen::MENU; menuSel = 5;
        redraw();
      }
      break;

    // ================ WEB (encoder only used for HOLD/CLICK — HTTP serviced above) ================
    case Screen::WEB:
      if (ev == ENC_CLICK) {
        stopWebServer();
        webRunning = false;
        scr = Screen::MENU; menuSel = 0;
        redraw();
      }
      break;

    // ================ REBOOT (HOLD is handled above and reboots; click/rotate cancels) ================
    case Screen::REBOOT:
      if (ev == ENC_CLICK || (ev != ENC_NONE && ev != ENC_HOLD)) {
        scr = Screen::MENU; menuSel = 6;
        redraw();
      }
      break;
  }
}
