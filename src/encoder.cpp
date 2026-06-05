#include "encoder.h"
#include "encoder_gray.h"
#include "value_array.h"
#include "storage.h"
#include <SD.h>

int volumeLevel      = VOLUME_DEFAULT;
int maxVolumeLevel   = MAXVOLUME_DEFAULT;
int brightnessLevel  = BRIGHTNESS_DEFAULT;
int powerSaveMinutes = POWERSAVE_DEFAULT;
int sleepTimerMinutes = SLEEPTIMER_DEFAULT;

// --- Gray-code state machine ---
// Tracks the full 2-bit quadrature state. Only valid gray-code transitions
// are counted; bounce edges that skip states are silently ignored.
// 4 valid transitions = 1 full detent → 1 reported step.
static volatile int encAccum = 0;  // partial detent (-3..+3)
static volatile int encDelta = 0;  // full detents ready to report
static          uint8_t encState;  // current (CLK<<1)|DT

static void IRAM_ATTR encISR() {
  uint8_t s = (digitalRead(ENC_DT) << 1) | digitalRead(ENC_CLK);
  int dir = grayStep(encState, s);
  encState = s;
  if (dir != 0) {
    encAccum += dir;
    if (encAccum >=  4) { encDelta = encDelta + 1; encAccum = 0; }
    if (encAccum <= -4) { encDelta = encDelta - 1; encAccum = 0; }
  }
}

// --- Button debounce (polled) ---
//
// State machine waits for release to decide click vs hold:
//   IDLE → MAYBE → ARMED → release → ENC_CLICK
//                       → 600ms   → ENC_HOLD → release → IDLE (silent)
enum { IDLE, MAYBE, ARMED, HOLD } static btnState = IDLE;
static uint32_t btnTimer = 0;

void initEncoder() {
  pinMode(ENC_CLK, INPUT);
  pinMode(ENC_DT,  INPUT);
  pinMode(ENC_SW,  INPUT);   // KY-040 has external 10k pull-up

  // Sample initial state so we start from a known position
  encState = (digitalRead(ENC_DT) << 1) | digitalRead(ENC_CLK);

  // Interrupt on every edge of both pins — state machine filters invalid ones
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_DT),  encISR, CHANGE);

  if (SD.exists("/volume.cfg")) {
    File f = SD.open("/volume.cfg", FILE_READ);
    if (f) {
      int v = f.readString().toInt();
      f.close();
      if (v >= 0 && v <= 100) volumeLevel = v;
    }
  }

  if (SD.exists("/maxvolume.cfg")) {
    File f = SD.open("/maxvolume.cfg", FILE_READ);
    if (f) {
      int v = f.readString().toInt();
      f.close();
      if (v >= 0 && v <= 100) maxVolumeLevel = v;
    }
  }
  if (volumeLevel > maxVolumeLevel) volumeLevel = maxVolumeLevel;
}

int readEncoder() {
  // --- Rotation: return full accumulated delta ---
  noInterrupts();
  int d = encDelta;
  encDelta = 0;
  interrupts();

  if (d != 0) return d;  // ±N steps (CW positive, CCW negative)

  // --- Button (debounced state machine) ---
  bool raw = (digitalRead(ENC_SW) == LOW);
  uint32_t now = millis();

  switch (btnState) {
  case IDLE:
    if (raw) { btnTimer = now; btnState = MAYBE; }
    break;
  case MAYBE:
    if (!raw) { btnState = IDLE; break; }          // bounce
    if (now - btnTimer < 30) break;                 // debounce window
    btnState = ARMED;
    btnTimer = now;                                  // reset timer for hold detection
    break;
  case ARMED:
    if (!raw) { btnState = IDLE; return ENC_CLICK; } // short press → click
    if (now - btnTimer > 600) {                       // long press → hold
      btnState = HOLD;
      return ENC_HOLD;
    }
    break;
  case HOLD:
    if (!raw) { btnState = IDLE; break; }            // release after hold (silent)
    break;
  }
  return ENC_NONE;
}

void saveVolume() {
  if (!sdReady) return;
  if (SD.exists("/volume.cfg")) SD.remove("/volume.cfg");
  File f = SD.open("/volume.cfg", FILE_WRITE);
  if (f) {
    f.print(volumeLevel);
    f.close();
  }
}

void saveMaxVolume() {
  if (!sdReady) return;
  if (SD.exists("/maxvolume.cfg")) SD.remove("/maxvolume.cfg");
  File f = SD.open("/maxvolume.cfg", FILE_WRITE);
  if (f) {
    f.print(maxVolumeLevel);
    f.close();
  }
}

void loadBrightness() {
  brightnessLevel = BRIGHTNESS_DEFAULT;
  if (!sdReady) return;
  if (SD.exists("/brightness.cfg")) {
    File f = SD.open("/brightness.cfg", FILE_READ);
    if (f) {
      int v = f.readString().toInt();
      f.close();
      if (v >= 0 && v <= 100) brightnessLevel = v;
    }
  }
}

void saveBrightness() {
  if (!sdReady) return;
  if (SD.exists("/brightness.cfg")) SD.remove("/brightness.cfg");
  File f = SD.open("/brightness.cfg", FILE_WRITE);
  if (f) {
    f.print(brightnessLevel);
    f.close();
  }
}

void applyBrightness() {
  int duty = map(brightnessLevel, 0, 100, BRIGHTNESS_MIN, 255);
  ledcWrite(TFT_BL, duty);
}

// ----------------------------------------------------------------
//  Power saving persistence (screen off after idle)
// ----------------------------------------------------------------

static const int POWERSAVE_VALUES[] = {
  0,
#ifdef DEV_MODE
  1,
#endif
  5, 15, 30, 60
};
static const int POWERSAVE_COUNT = sizeof(POWERSAVE_VALUES) / sizeof(POWERSAVE_VALUES[0]);

void loadPowerSave() {
  powerSaveMinutes = POWERSAVE_DEFAULT;
  if (!sdReady) return;
  if (SD.exists("/powersave.cfg")) {
    File f = SD.open("/powersave.cfg", FILE_READ);
    if (f) {
      int v = f.readString().toInt();
      f.close();
      for (int i = 0; i < POWERSAVE_COUNT; i++) {
        if (POWERSAVE_VALUES[i] == v) { powerSaveMinutes = v; break; }
      }
    }
  }
}

void savePowerSave() {
  if (!sdReady) return;
  if (SD.exists("/powersave.cfg")) SD.remove("/powersave.cfg");
  File f = SD.open("/powersave.cfg", FILE_WRITE);
  if (f) {
    f.print(powerSaveMinutes);
    f.close();
  }
}

int powerSaveToIndex(int minutes) {
  return valueToIndex(POWERSAVE_VALUES, POWERSAVE_COUNT, minutes);
}

int powerSaveToMinutes(int index) {
  return indexToValue(POWERSAVE_VALUES, POWERSAVE_COUNT, index);
}

// ----------------------------------------------------------------
//  Audio sleep timer persistence (stop playback after X minutes)
// ----------------------------------------------------------------

static const int SLEEP_VALUES[] = {
  0,
#ifdef DEV_MODE
  1,
#endif
  15, 30, 60, 120
};
static const int SLEEP_COUNT = sizeof(SLEEP_VALUES) / sizeof(SLEEP_VALUES[0]);

void loadSleepTimer() {
  sleepTimerMinutes = SLEEPTIMER_DEFAULT;
  if (!sdReady) return;
  if (SD.exists("/sleeptimer.cfg")) {
    File f = SD.open("/sleeptimer.cfg", FILE_READ);
    if (f) {
      int v = f.readString().toInt();
      f.close();
      for (int i = 0; i < SLEEP_COUNT; i++) {
        if (SLEEP_VALUES[i] == v) { sleepTimerMinutes = v; break; }
      }
    }
  }
}

void saveSleepTimer() {
  if (!sdReady) return;
  if (SD.exists("/sleeptimer.cfg")) SD.remove("/sleeptimer.cfg");
  File f = SD.open("/sleeptimer.cfg", FILE_WRITE);
  if (f) {
    f.print(sleepTimerMinutes);
    f.close();
  }
}

int sleepTimerToIndex(int minutes) {
  return valueToIndex(SLEEP_VALUES, SLEEP_COUNT, minutes);
}

int sleepTimerToMinutes(int index) {
  return indexToValue(SLEEP_VALUES, SLEEP_COUNT, index);
}
