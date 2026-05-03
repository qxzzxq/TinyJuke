#include "encoder.h"
#include <SD.h>

int volumeLevel = VOLUME_DEFAULT;

// --- ISR state ---
static volatile int  encDelta   = 0;   // accumulated rotation steps from ISR
static volatile bool btnPressed = false; // set on FALLING edge of button

static uint32_t btnDownAt  = 0;    // millis() when button went down
static bool     btnWasDown = false;
static bool     holdFired  = false;

// --- ISR helpers ---
static void IRAM_ATTR encISR() {
  // On CLK falling edge, sample DT for direction
  if (digitalRead(ENC_DT) == LOW) {
    encDelta = encDelta + 1;
  } else {
    encDelta = encDelta - 1;
  }
}

static void IRAM_ATTR btnISR() {
  // Record button press edge; readEncoder() handles timing
  btnPressed = true;
}

void initEncoder() {
  pinMode(ENC_CLK, INPUT);
  pinMode(ENC_DT,  INPUT);
  pinMode(ENC_SW,  INPUT);  // KY-040 has external 10k pull-up

  attachInterrupt(digitalPinToInterrupt(ENC_CLK), encISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(ENC_SW),  btnISR, FALLING);

  // Load saved volume
  if (SD.exists("/volume.cfg")) {
    File f = SD.open("/volume.cfg", FILE_READ);
    if (f) {
      int v = f.readString().toInt();
      f.close();
      if (v >= 0 && v <= 100) volumeLevel = v;
    }
  }
}

int readEncoder() {
  noInterrupts();
  int d = encDelta;
  encDelta = 0;
  interrupts();

  if (d > 0) return ENC_CW;
  if (d < 0) return ENC_CCW;

  // --- button state machine ---
  bool down = (digitalRead(ENC_SW) == LOW);

  if (down && !btnWasDown) {
    // just pressed
    btnDownAt  = millis();
    holdFired  = false;
    btnPressed = false;
  } else if (down && btnWasDown && !holdFired && (millis() - btnDownAt > 600)) {
    holdFired = true;
    btnWasDown = true;
    return ENC_HOLD;
  } else if (!down && btnWasDown) {
    // just released
    btnWasDown = false;
    if (!holdFired)
      return ENC_CLICK;
  }

  btnWasDown = down;

  // Also check ISR-flagged button press (edge-triggered fallback)
  if (btnPressed) {
    btnPressed = false;
    if (!down) {
      // Already released — treat as click
      return ENC_CLICK;
    } else {
      // Still down — let polling handle timing
      btnDownAt  = millis();
      holdFired  = false;
      btnWasDown = true;
    }
  }

  return ENC_NONE;
}

void saveVolume() {
  if (SD.exists("/volume.cfg")) SD.remove("/volume.cfg");
  File f = SD.open("/volume.cfg", FILE_WRITE);
  if (f) {
    f.print(volumeLevel);
    f.close();
  }
}
