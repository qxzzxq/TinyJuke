// SPDX-FileCopyrightText: 2026 qxzzxq
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui.h"
#include "screen.h"
#include "encoder.h"
#include "web.h"
#include "bluetooth.h"
#include "timer_logic.h"
#include "volume_logic.h"

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
  BLUETOOTH,
  BLUETOOTH_TAG_PROMPT,
};

// Menu indices (must match MENU_ITEMS[] in screen.cpp)
static constexpr int MI_WEB         = 0;
static constexpr int MI_BLUETOOTH   = 1;
static constexpr int MI_VOLUME      = 2;
static constexpr int MI_BRIGHTNESS  = 3;
static constexpr int MI_POWERSAVING = 4;
static constexpr int MI_SLEEPTIMER  = 5;
static constexpr int MI_VERSION     = 6;
static constexpr int MI_REBOOT      = 7;

static Screen    scr        = Screen::MENU;
static bool      active     = false;
static int       menuSel    = 0;
static bool      webRunning = false;
static bool      btRunning  = false;
static bool      volAdjMax  = false;  // Volume screen: encoder adjusts max volume
static int       btVolDrawn = -1;
static bool      btConnDrawn = false;

// ----------------------------------------------------------------
//  Draw current screen
// ----------------------------------------------------------------

static void drawBluetoothCurrent() {
  drawBluetoothScreen(btIsConnected(), btPeerName(),
                      btTrackTitle(), btTrackArtist(), volumeLevel);
  btVolDrawn  = volumeLevel;
  btConnDrawn = btIsConnected();
}

static void redraw() {
  switch (scr) {
    case Screen::MENU:
      drawMenuScreen(menuSel);
      break;
    case Screen::VOLUME:
      drawVolumeScreen(volumeLevel, maxVolumeLevel, volAdjMax);
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
    case Screen::BLUETOOTH:
      drawBluetoothCurrent();
      break;
    case Screen::BLUETOOTH_TAG_PROMPT:
      drawBluetoothTagPromptScreen();
      break;
  }
}

// ================================================================
//  Public API
// ================================================================

void guiEnter() {
  scr        = Screen::MENU;
  menuSel    = 0;
  active     = true;
  webRunning = false;
  btRunning  = false;
  redraw();
}

bool guiActive() {
  return active;
}

void guiLoop() {
  if (!active) return;

  // Idle on the menu honours power-save: exit GUI so the main loop's FSM
  // sees Waiting + no tag + elapsed idle and emits EnterSleep. Wake from
  // sleep lands on the waiting screen via the existing FSM path.
  if (scr == Screen::MENU &&
      powerSaveShouldSleep(powerSaveMinutes, activityIdleMs())) {
    drawWaitingScreen();
    active = false;
    return;
  }

  // --- Continuous processing (runs regardless of encoder events) ---

  // WEB screen: always service HTTP requests, update connection count
  if (scr == Screen::WEB && webRunning) {
    handleWebClient();
    updateWebConnectionCount(getWebConnectionCount());
  }

  // BLUETOOTH screen: service A2DP + NFC, watch for tag / sleep timer
  if ((scr == Screen::BLUETOOTH || scr == Screen::BLUETOOTH_TAG_PROMPT) && btRunning) {
    handleBluetoothLoop();

    // Sleep timer fired -> tear down BT entirely and return to menu
    if (btSleepFired()) {
      stopBluetoothMode();
      btRunning = false;
      scr = Screen::MENU; menuSel = MI_BLUETOOTH;
      redraw();
      return;
    }

    // Tag detected on the BT screen -> raise prompt
    if (scr == Screen::BLUETOOTH && btTagDetected()) {
      scr = Screen::BLUETOOTH_TAG_PROMPT;
      drawBluetoothTagPromptScreen();
    }
    // Tag was lifted while prompt was up -> back to BT screen
    else if (scr == Screen::BLUETOOTH_TAG_PROMPT && !btTagDetected()) {
      scr = Screen::BLUETOOTH;
      drawBluetoothCurrent();
    }

    // Periodic refresh of the BT screen when metadata or volume changes
    if (scr == Screen::BLUETOOTH) {
      bool needRedraw = btMetadataChanged() || btConnDrawn != btIsConnected();
      if (needRedraw) drawBluetoothCurrent();
      else if (btVolDrawn != volumeLevel) {
        // Volume-only change (e.g. AVRCP push from the phone): redraw just
        // the bottom bar to avoid full-screen flicker.
        updateBluetoothVolume(volumeLevel);
        btVolDrawn = volumeLevel;
      }
    }
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
        saveMaxVolume();
        scr = Screen::MENU; menuSel = MI_VOLUME;
        break;
      case Screen::BRIGHTNESS:
        saveBrightness();
        scr = Screen::MENU; menuSel = MI_BRIGHTNESS;
        break;
      case Screen::POWERSAVING:
        savePowerSave();
        scr = Screen::MENU; menuSel = MI_POWERSAVING;
        break;
      case Screen::SLEEPTIMER:
        saveSleepTimer();
        scr = Screen::MENU; menuSel = MI_SLEEPTIMER;
        break;
      case Screen::VERSION:
        scr = Screen::MENU; menuSel = MI_VERSION;
        break;
      case Screen::WEB:
        stopWebServer();
        webRunning = false;
        scr = Screen::MENU; menuSel = MI_WEB;
        break;
      case Screen::BLUETOOTH:
        stopBluetoothMode();
        btRunning = false;
        scr = Screen::MENU; menuSel = MI_BLUETOOTH;
        break;
      case Screen::BLUETOOTH_TAG_PROMPT:
        // Dismiss the prompt; stay in BT mode.
        scr = Screen::BLUETOOTH;
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
          case MI_WEB:         {
            extern PN532 nfc;  // defined in main.cpp
            initWebServer(nfc); webRunning = true; scr = Screen::WEB;
            break;
          }
          case MI_BLUETOOTH:   {
            extern PN532 nfc;  // defined in main.cpp
            initBluetoothMode(nfc); btRunning = true; scr = Screen::BLUETOOTH;
            break;
          }
          case MI_VOLUME:      scr = Screen::VOLUME; volAdjMax = false; break;
          case MI_BRIGHTNESS:  scr = Screen::BRIGHTNESS; break;
          case MI_POWERSAVING: scr = Screen::POWERSAVING; break;
          case MI_SLEEPTIMER:  scr = Screen::SLEEPTIMER; break;
          case MI_VERSION:     scr = Screen::VERSION; break;
          case MI_REBOOT:      scr = Screen::REBOOT; break;
        }
        redraw();
      }
      break;

    // ================ VOLUME ================
    // Rotate adjusts the active parameter, CLICK toggles Volume/Max Volume,
    // HOLD saves both and returns to the menu.
    case Screen::VOLUME: {
      int  oldLevel  = volumeLevel;
      int  oldMax    = maxVolumeLevel;
      bool oldAdjMax = volAdjMax;

      for (;;) {
        if ((ev > 0 && ev < ENC_CLICK) || ev < 0) {
          volumeAdjust(volumeLevel, maxVolumeLevel, volAdjMax, ev);
        } else if (ev == ENC_CLICK) {
          volAdjMax = !volAdjMax;
        }
        else if (ev == ENC_HOLD) {
          saveVolume();
          saveMaxVolume();
          scr = Screen::MENU; menuSel = MI_VOLUME;
          redraw();
          break;
        }
        ev = readEncoder();
        if (ev == ENC_NONE) break;
      }

      if (scr == Screen::VOLUME &&
          (volumeLevel != oldLevel || maxVolumeLevel != oldMax || volAdjMax != oldAdjMax))
        updateVolumeDisplay(volumeLevel, maxVolumeLevel, volAdjMax);
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
          scr = Screen::MENU; menuSel = MI_BRIGHTNESS;
          redraw();
          break;
        }
        else if (ev == ENC_HOLD) {
          saveBrightness();
          scr = Screen::MENU; menuSel = MI_BRIGHTNESS;
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
          scr = Screen::MENU; menuSel = MI_POWERSAVING;
          redraw();
          break;
        }
        else if (ev == ENC_HOLD) {
          savePowerSave();
          scr = Screen::MENU; menuSel = MI_POWERSAVING;
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
          scr = Screen::MENU; menuSel = MI_SLEEPTIMER;
          redraw();
          break;
        }
        else if (ev == ENC_HOLD) {
          saveSleepTimer();
          scr = Screen::MENU; menuSel = MI_SLEEPTIMER;
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
        scr = Screen::MENU; menuSel = MI_VERSION;
        redraw();
      }
      break;

    // ================ WEB (encoder only used for HOLD/CLICK — HTTP serviced above) ================
    case Screen::WEB:
      if (ev == ENC_CLICK) {
        stopWebServer();
        webRunning = false;
        scr = Screen::MENU; menuSel = MI_WEB;
        redraw();
      }
      break;

    // ================ REBOOT (HOLD is handled above and reboots; click/rotate cancels) ================
    case Screen::REBOOT:
      if (ev == ENC_CLICK || (ev != ENC_NONE && ev != ENC_HOLD)) {
        scr = Screen::MENU; menuSel = MI_REBOOT;
        redraw();
      }
      break;

    // ================ BLUETOOTH ================
    // Rotation adjusts volume live; CLICK = no-op; HOLD = exit (handled above).
    case Screen::BLUETOOTH: {
      int oldLevel = volumeLevel;
      for (;;) {
        if ((ev > 0 && ev < ENC_CLICK) || ev < 0) {
          int nv = volumeLevel + ev;
          volumeLevel = (nv > maxVolumeLevel) ? maxVolumeLevel : (nv < 0 ? 0 : nv);
        } else if (ev == ENC_CLICK) {
          saveVolume();
          break;
        }
        ev = readEncoder();
        if (ev == ENC_NONE) break;
      }
      if (volumeLevel != oldLevel) {
        updateBluetoothVolume(volumeLevel);
        btVolDrawn = volumeLevel;
      }
      break;
    }

    // ================ BLUETOOTH_TAG_PROMPT ================
    // CLICK = leave BT, hand off to main loop which sees the tag.
    case Screen::BLUETOOTH_TAG_PROMPT:
      if (ev == ENC_CLICK) {
        stopBluetoothMode();
        btRunning = false;
        // Exit GUI entirely; main loop's next tick polls NFC, FSM triggers
        // playback for the tag that's still on the reader.
        active = false;
        drawWaitingScreen();
      }
      break;
  }
}
