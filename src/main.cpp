// SPDX-FileCopyrightText: 2026 qxzzxq
// SPDX-License-Identifier: GPL-3.0-or-later

// TinyJuke
//
// TFT (ST7789V, 240x320) and SD card share the VSPI bus (GPIO 18/19/23).
// Arduino_ESP32SPI and the SD library both use bare-metal SPI — only one CS
// is active at a time.
//
// Jukebox mode: scan tag → play WAV, encoder adjusts volume, hold for menu.
// Menu mode: manage tags, start web server, adjust volume.

#include "config.h"
#include "tags.h"
#include "screen.h"
#include "audio.h"
#include "encoder.h"
#include "gui.h"
#include "web.h"
#include "storage.h"
#include "timer_logic.h"
#include "jukebox_state.h"
#include "anim.h"

#include <PN532_HSU.h>
#include <PN532.h>
#include <SD.h>
#include <Arduino_GFX_Library.h>

// --- Peripherals ---
PN532_HSU pn532hsu(Serial2);
PN532      nfc(pn532hsu);

Arduino_ESP32SPI bus(TFT_DC, TFT_CS, SCK, MOSI, MISO, VSPI);
Arduino_ST7789  gfx(&bus, TFT_RST, 0, true, 240, 320, 0, 0, 0, 0);

// --- State ---
// All tag/sleep/powersave state lives in s_state (see jukebox_state.h).
// The play loop owns its own local lastTagUid for swap detection.
static JukeboxState s_state;

// SD-card lifecycle flag — set by initSDAndLoadTags() below, read by any
// module that gates on SD via storage.h.
bool sdReady = false;

// Mount the SD card and parse /tags.json into tagDoc. Sets sdReady; on
// failure (no card, bad JSON) the device continues to boot — playback paths
// gate on sdReady themselves.
static void initSDAndLoadTags() {
  Serial.print("Mounting SD... ");
  // SD.begin defaults to a 4 MHz SPI clock (~500 KB/s ceiling) — a
  // streaming bottleneck. The shared VSPI bus already runs the TFT much
  // faster; mount at 20 MHz (SD SPI-mode spec max is 25) and fall back
  // to the conservative default if the card won't mount.
  if (SD.begin(SD_CS, SPI, 20000000)) {
    Serial.print("(20MHz) ");
  } else if (SD.begin(SD_CS)) {
    Serial.print("(4MHz fallback) ");
  } else {
    sdReady = false;
    Serial.println("FAILED");
    return;
  }
  sdReady = true;
  Serial.println("OK");

  // Fresh cards lack these dirs; create them so the listing/upload paths
  // don't later SD.open() a missing dir (which logs the ESP32 VFS
  // "does not exist, no permits for creation" error). SD.mkdir() is a
  // silent no-op when the directory already exists.
  SD.mkdir("/music");
  SD.mkdir("/img");

  // A card with no tags yet is a normal state, not an error — sdOpenRead()
  // keeps the VFS "does not exist" log line out of the boot output.
  File f = sdOpenRead("/tags.json");
  if (!f) {
    Serial.println("tags.json not found.");
    return;
  }
  DeserializationError err = deserializeJson(tagDoc, f);
  f.close();
  if (err) {
    Serial.print("tags.json err: ");
    Serial.println(err.c_str());
    tagDoc.clear();
    return;
  }
  Serial.print(tagDoc.size());
  Serial.println(" tags loaded.");
}

// --- Sleep helpers (need gfx + nfc in scope; mode flip lives in the FSM) ---
// PN532 didn't respond on UART. Show an on-screen error, then bypass the
// library and poke the chip with a raw wake + GetFirmwareVersion frame so the
// serial log captures whether ANY bytes come back (wiring/power vs dead chip).
// Hangs the device — there is no recovery path.
static void handlePN532NotFound() {
  Serial.println("PN532 not found.");
  if (sdReady) {
    gfx.fillScreen(C_BG);
    gfx.setTextColor(C_RED);  gfx.setTextSize(2);
    gfx.setCursor(30, 40); gfx.print("PN532");
    gfx.setTextSize(1);
    gfx.setCursor(30, 65); gfx.print("NOT FOUND");
  }
  // PN532 HSU wake-up frame (0x55 0x55 + nulls to flush UART).
  const uint8_t wake[] = {0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  Serial2.write(wake, sizeof(wake));
  // Raw GetFirmwareVersion command frame in PN532 wire format.
  const uint8_t gfv[] = {0x00, 0x00, 0xFF, 0x02, 0xFE, 0xD4, 0x02, 0x2A, 0x00};
  Serial2.write(gfv, sizeof(gfv));
  Serial2.flush();
  uint32_t t0 = millis();
  Serial.print("RX: ");
  while (millis() - t0 < 500)
    while (Serial2.available()) {
      uint8_t b = Serial2.read();
      if (b < 0x10) Serial.print('0');
      Serial.print(b, HEX); Serial.print(' ');
    }
  Serial.println();
  while (true) delay(1000);
}

static void doEnterSleep() {
  gfx.displayOff();
  ledcWrite(TFT_BL, 0);
  Serial.println("Sleep mode entered.");
}

static void doWakeFromSleep() {
  gfx.displayOn();
  delay(5);
  applyBrightness();
  while (readEncoder() != ENC_NONE) {}  // drain accumulated encoder events
  nfc.SAMConfig();                       // re-sync PN532 after idle
  drawWaitingScreen();
  Serial.println("Woke from sleep.");
}

void resetActivityTimer() {
  s_state.lastActivityMs = millis();
}

uint32_t activityIdleMs() {
  return millis() - s_state.lastActivityMs;
}

// Map raw encoder event integer to FSM-normalized EncEvent.
static EncEvent normalizeEnc(int ev) {
  if (ev == ENC_NONE)  return EncEvent::None;
  if (ev == ENC_HOLD)  return EncEvent::Hold;
  if (ev == ENC_CLICK) return EncEvent::Click;
  return EncEvent::Rotated;  // ±N rotation steps
}

// --- TriggerPlayback action handler: drives the play loop / unknown-tag screen.
//  Returns updates to s_state via direct mutation. After return, the next
//  jukeboxStep() tick observes the new sleepStopped / tagPresent values.
static void runPlayback(const uint8_t *uid, uint8_t uidLength) {
  if (!sdReady) {
    drawTagScreen(uid, uidLength);
    return;
  }

  TagInfo tag = lookupTag(uid, uidLength);
  if (!tag.file) {
    Serial.println("Unknown tag.");
    drawUnknownTagScreen(uid, uidLength);
    char shownUid[32]; uidToStr(uid, uidLength, shownUid);
    uint32_t t = millis();
    while (millis() - t < 10000) {
      int eu = readEncoder();
      if (eu == ENC_CLICK || eu == ENC_HOLD) break;
      uint8_t u[10]; uint8_t uLen = 0;
      if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, u, &uLen, 200)) {
        s_state.tagPresent = false;
        Serial.println("Tag removed.");
        drawWaitingScreen();
        return;
      }
      if (uLen <= 10) {
        char currentUid[32]; uidToStr(u, uLen, currentUid);
        if (strcmp(currentUid, shownUid) != 0) {
          s_state.tagPresent = false;  // swap — next FSM tick re-triggers
          return;
        }
      }
      delay(30);
    }
    drawWaitingScreen();
    return;
  }

  // Known tag — loop playback while the same tag remains on the reader.
  audioStartTime = millis();
  char localTagUid[32]; uidToStr(uid, uidLength, localTagUid);
  bool localPresent = true;

  while (localPresent) {
    Serial.print("Playing: "); Serial.println(tag.file);
    drawNowPlayingScreen(tag);
    if (sleepTimerMinutes > 0) {
      uint32_t remaining = timerRemainingMs(sleepTimerMinutes, millis() - audioStartTime);
      drawSleepTimerCountdown(remaining);
    }
    playWav(tag.file, nfc, uid, uidLength);

    if (sleepTimerFired) {
      // Sleep timer fired — tag is still on reader. The FSM next tick will
      // observe sleepTimerJustFired and set s_state.sleepStopped = true.
      // We keep s_state.tagPresent = true to require physical removal.
      break;
    }

    // Quick NFC check: detect tag removal or tag swap before replay
    uint8_t u[10]; uint8_t uLen = 0;
    if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, u, &uLen, 50)) {
      Serial.println("Tag removed.");
      localPresent = false;
      s_state.tagPresent = false;
      s_state.lastActivityMs = millis();
    } else if (uLen <= 10) {
      char currentUid[32]; uidToStr(u, uLen, currentUid);
      if (strcmp(currentUid, localTagUid) != 0) {
        // Swap — next FSM tick will treat the new tag as a fresh arrival.
        localPresent = false;
        s_state.tagPresent = false;
        s_state.lastActivityMs = millis();
      }
    }
  }
  drawWaitingScreen();
}

// ================================================================

void setup() {
  // 0. Force backlight off as the very first thing. Before this point
  //    TFT_BL is a floating input that reads HIGH on the D32 Pro, which
  //    briefly turns the backlight on and exposes the TFT's uninitialized
  //    RAM (visible noise) during the Serial.begin + delay window below.
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);

  Serial.begin(115200);
  while (!Serial) delay(10);
  delay(200);
  Serial.println("\nTinyJuke starting...");

  // 1. Init TFT (backlight already held off by the digitalWrite above; the
  //    LEDC takeover preserves the LOW level).
  ledcAttach(TFT_BL, BRIGHTNESS_PWM_FREQ, BRIGHTNESS_PWM_RES);
  ledcWrite(TFT_BL, 0);
  gfx.begin();

  // 2. Init SD + load tags.json
  initSDAndLoadTags();

  // 2.5 Load saved color theme (falls back to default if SD/file absent) so
  //     the boot screen already paints in the chosen palette.
  loadTheme();

  // 3. Boot screen (drawn with backlight off to avoid power-on noise)
  if (!sdReady) {
    drawSDErrorScreen();
  } else {
    gfx.fillScreen(C_BG);
    drawWaitingScreen();
  }
  ledcWrite(TFT_BL, 255);  // turn on backlight once content is ready

  // 3.5 Prime I2S pins so MAX98357A BCLK isn't floating (touch-sensitive)
  i2sPrime();

  // 4. Init PN532
  Serial2.begin(115200, SERIAL_8N1, PN532_TX, PN532_RX);
  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();

  if (!versiondata) handlePN532NotFound();  // does not return

  Serial.printf("Found PN532 fw %d.%d\n",
                (versiondata >> 16) & 0xFF,
                (versiondata >> 8) & 0xFF);

  nfc.SAMConfig();

  // 5. Init encoder (loads saved volume, sets up interrupts)
  initEncoder();

  // 6. Apply saved brightness
  loadBrightness();
  applyBrightness();

  // 7. Load power save & sleep timer, initialize FSM
  loadPowerSave();
  loadSleepTimer();
  s_state = jukeboxInitialState(millis());

  Serial.println("TinyJuke Ready.");
}

// ================================================================

void loop() {
  // Menu is handled externally — short-circuit the FSM while guiActive.
  if (guiActive()) {
    guiLoop();
    return;
  }

  // ---- Gather inputs for this tick ----
  TickInput in = {};
  in.nowMs             = millis();
  in.encoderEvent      = normalizeEnc(readEncoder());
  in.audioPlaying      = audioPlaying;
  in.sleepTimerJustFired = sleepTimerFired;
  if (sleepTimerFired) sleepTimerFired = false;  // consume edge
  in.powerSaveMinutes  = powerSaveMinutes;

  // "Hold for menu" is the one gesture on this screen, so show its progress.
  // Drawn right after readEncoder() so the button state is fresh; every screen
  // this path can be showing paints C_BG in the indicator's band.
  uint32_t heldMs   = encHoldMs();
  bool     idleWait = (s_state.mode == Mode::Waiting) && !s_state.tagPresent;
  if (idleWait) {
    int pct = holdProgressPct(heldMs, HOLD_HINT_DELAY_MS, ENC_HOLD_MS);
    if (pct >= 0) drawHoldProgress(pct);
    else          clearHoldProgress();
  }

  // Don't waste an NFC poll while sleeping with a tag still on the reader.
  uint8_t uid[10] = {0};
  uint8_t uidLength = 0;
  // Skip the blocking poll entirely for the whole press on the idle waiting
  // screen: that frees the loop to run at full rate, so the hold indicator
  // animates smoothly instead of stepping once per poll, and the hold
  // threshold stops depending on where the press lands in the poll cycle.
  // Safe because no tag is present, so the removal debounce isn't armed and a
  // skipped poll can't be misread as a tag disappearing.
  bool holdingIdle = idleWait && encPressActive();
  if (!holdingIdle && !(s_state.mode == Mode::Sleeping && s_state.sleepStopped)) {
    uint16_t timeout = (s_state.mode == Mode::Sleeping) ? 100 : NFC_POLL_MS;
    in.nfcFound = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, timeout);
    if (uidLength > 10) uidLength = 10;
  }

  // ---- Run the state machine ----
  TickResult r = jukeboxStep(s_state, in);
  s_state = r.state;

  // ---- Dispatch actions ----
  for (uint8_t i = 0; i < r.actions.count; i++) {
    switch (r.actions.items[i]) {
      case Action::EnterSleep:
        doEnterSleep();
        break;

      case Action::WakeFromSleep:
        doWakeFromSleep();
        break;

      case Action::EnterMenu:
        saveVolume();
        guiEnter();
        return;  // gui takes over next tick

      case Action::TriggerPlayback:
        Serial.print("Tag UID("); Serial.print(uidLength); Serial.print("): ");
        printHex(uid, uidLength); Serial.println();
        runPlayback(uid, uidLength);
        break;

      case Action::ConfirmTagRemoved:
        Serial.println("Tag removed.");
        if (audioPlaying) stopPlayback();
        drawWaitingScreen();
        break;

      case Action::None:
        break;
    }
  }

  if (s_state.mode == Mode::Sleeping) delay(50);
}
