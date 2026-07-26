// SPDX-FileCopyrightText: 2026 qxzzxq
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui.h"
#include "screen.h"
#include "encoder.h"
#include "web.h"
#include "bluetooth.h"
#include "timer_logic.h"
#include "volume_logic.h"
#include "anim.h"

// ----------------------------------------------------------------
//  State
// ----------------------------------------------------------------

enum class Screen {
  MENU,
  VOLUME,
  BRIGHTNESS,
  THEME,
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
static constexpr int MI_THEME       = 4;
static constexpr int MI_POWERSAVING = 5;
static constexpr int MI_SLEEPTIMER  = 6;
static constexpr int MI_VERSION     = 7;
static constexpr int MI_REBOOT      = 8;

static Screen    scr        = Screen::MENU;
static bool      active     = false;
static int       menuSel    = 0;
static bool      webRunning = false;
static bool      btRunning  = false;
static bool      volAdjMax  = false;  // Volume screen: encoder adjusts max volume
static int       btVolDrawn = -1;
static bool      btConnDrawn = false;

// ----------------------------------------------------------------
//  Animation state
// ----------------------------------------------------------------
//
// Values the UI animates toward. The true values (volumeLevel, menuSel, ...)
// are applied immediately on the encoder event; only their on-screen
// representation is interpolated, so nothing ever waits on an animation.

static uint32_t animFrameMs = 0;   // last frame timestamp (frame-rate gate)
static AnimI32  selAnim;           // menu selector bar, in pixels
static int      selDrawnY = -1;    // last painted selector y (-1 = not drawn)
static AnimI32  volAnim, maxVolAnim, brtAnim;

// ----------------------------------------------------------------
//  Draw current screen
// ----------------------------------------------------------------

static void drawBluetoothCurrent() {
  drawBluetoothScreen(btIsConnected(), btDeviceName(), btPeerName(),
                      btTrackTitle(), btTrackArtist(), volumeLevel);
  btVolDrawn  = volumeLevel;
  btConnDrawn = btIsConnected();
}

// Settle every animation at the value the screen was just painted with, so a
// frame left over from the previous screen can't paint over the new one.
static void animReset() {
  uint32_t now = millis();
  animSettle(selAnim, menuRowY(menuSel), now);
  animSettle(volAnim, volumeLevel, now);
  animSettle(maxVolAnim, maxVolumeLevel, now);
  animSettle(brtAnim, brightnessLevel, now);
  // drawMenuScreen() already painted the selector at the selected row.
  selDrawnY = (scr == Screen::MENU) ? menuRowY(menuSel) : -1;
  clearHoldProgress();
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
    case Screen::THEME:
      drawThemeScreen(currentTheme());
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
  animReset();
}

// Retarget the selector at the newly selected row, starting from wherever it
// currently is rather than from the old row — a fast spin then chases the
// selection continuously instead of snapping back on every detent.
static void startSelectorSlide() {
  uint32_t now = millis();
  animStart(selAnim, animValue(selAnim, now), menuRowY(menuSel), now, ANIM_MENU_MS);
}

// Advance in-flight animations and paint one frame. Called every guiLoop
// iteration *before* the no-event early return — that early return is what
// otherwise makes idle-time rendering impossible. It also runs straight after
// readEncoder(), so the button state encHoldMs() reads is fresh.
static void guiAnimTick() {
  uint32_t now = millis();
  if (now - animFrameMs < ANIM_FRAME_MS) return;
  animFrameMs = now;

  // --- Long-press progress (every screen that accepts a hold) ---
  // encHoldMs() returns 0 once ENC_HOLD has fired, so the bar clears itself
  // the moment the gesture completes.
  int holdPct = holdProgressPct(encHoldMs(), HOLD_HINT_DELAY_MS, ENC_HOLD_MS);
  if (holdPct >= 0) drawHoldProgress(holdPct);
  else              clearHoldProgress();   // no-op when nothing is drawn

  // --- Per-screen value animations ---
  switch (scr) {
    case Screen::MENU: {
      int y = animValue(selAnim, now);
      if (y != selDrawnY) {
        drawMenuSelector(selDrawnY, y);
        selDrawnY = y;
      }
      break;
    }
    case Screen::VOLUME:
      updateVolumeBars(animValue(volAnim, now), animValue(maxVolAnim, now));
      break;
    case Screen::BRIGHTNESS:
      updateBrightnessBar(animValue(brtAnim, now));
      break;
    default:
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
  guiAnimTick();
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
      case Screen::THEME:
        saveTheme();
        scr = Screen::MENU; menuSel = MI_THEME;
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
        startSelectorSlide();
      } else if (ev < 0) {
        int prev = menuSel;
        menuSel = (menuSel + ev % MENU_ITEM_COUNT + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
        updateMenuSelection(prev, menuSel);
        startSelectorSlide();
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
          case MI_THEME:       scr = Screen::THEME; break;
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
          (volumeLevel != oldLevel || maxVolumeLevel != oldMax || volAdjMax != oldAdjMax)) {
        // Labels and percentages track the encoder exactly; only the fills ease.
        updateVolumeText(volumeLevel, maxVolumeLevel, volAdjMax);
        uint32_t now = millis();
        if (volumeLevel != oldLevel)
          animStart(volAnim, animValue(volAnim, now), volumeLevel, now, ANIM_BAR_MS);
        if (maxVolumeLevel != oldMax)
          animStart(maxVolAnim, animValue(maxVolAnim, now), maxVolumeLevel, now, ANIM_BAR_MS);
      }
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
        applyBrightness();   // backlight follows the encoder immediately
        // Only paint if we're still on this screen: a click can be drained in
        // the same burst as a rotation, which already switched us to the menu.
        if (scr == Screen::BRIGHTNESS) {
          updateBrightnessText(brightnessLevel);
          uint32_t now = millis();
          animStart(brtAnim, animValue(brtAnim, now), brightnessLevel, now, ANIM_BAR_MS);
        }
      }
      break;
    }

    // ================ THEME ================
    // Rotate cycles palettes with instant live preview; CLICK/HOLD saves the
    // choice to /theme.cfg and returns to the menu.
    case Screen::THEME: {
      int oldIdx = currentTheme();
      int n = themeCount();

      for (;;) {
        if ((ev > 0 && ev < ENC_CLICK) || ev < 0) {
          int idx = (currentTheme() + ev) % n;
          if (idx < 0) idx += n;
          applyTheme(idx);
        } else if (ev == ENC_CLICK || ev == ENC_HOLD) {
          saveTheme();
          scr = Screen::MENU; menuSel = MI_THEME;
          redraw();
          break;
        }
        ev = readEncoder();
        if (ev == ENC_NONE) break;
      }

      // Palette changed but still on the theme screen -> full repaint in the
      // new colors (background + swatches change, so no incremental update).
      if (scr == Screen::THEME && currentTheme() != oldIdx)
        drawThemeScreen(currentTheme());
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

      // Only paint if we're still on this screen: a click can be drained in
      // the same burst as a rotation, which already switched us to the menu.
      if (powerSaveMinutes != oldMinutes && scr == Screen::POWERSAVING)
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

      // Same current-screen guard as the other settings screens.
      if (sleepTimerMinutes != oldMinutes && scr == Screen::SLEEPTIMER)
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
    // Rotation adjusts volume live; CLICK saves; HOLD exits.
    case Screen::BLUETOOTH: {
      int oldLevel = volumeLevel;
      for (;;) {
        if ((ev > 0 && ev < ENC_CLICK) || ev < 0) {
          int nv = volumeLevel + ev;
          volumeLevel = (nv > maxVolumeLevel) ? maxVolumeLevel : (nv < 0 ? 0 : nv);
        } else if (ev == ENC_CLICK) {
          saveVolume();
          break;
        } else if (ev == ENC_HOLD) {
          // A hold reached inside this loop has already passed the global
          // handler, so it must be honoured here or it is swallowed and the
          // user has to release and hold again. Mirrors the other screens.
          saveVolume();
          stopBluetoothMode();
          btRunning = false;
          scr = Screen::MENU; menuSel = MI_BLUETOOTH;
          redraw();
          break;
        }
        ev = readEncoder();
        if (ev == ENC_NONE) break;
      }
      if (volumeLevel != oldLevel && scr == Screen::BLUETOOTH) {
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
