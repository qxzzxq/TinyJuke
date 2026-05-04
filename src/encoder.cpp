#include "encoder.h"
#include <SD.h>

int volumeLevel = VOLUME_DEFAULT;

// --- Gray-code state machine ---
// Tracks the full 2-bit quadrature state. Only valid gray-code transitions
// are counted; bounce edges that skip states are silently ignored.
// 4 valid transitions = 1 full detent → 1 reported step.
static volatile int encAccum = 0;  // partial detent (-3..+3)
static volatile int encDelta = 0;  // full detents ready to report
static          uint8_t encState;  // current (CLK<<1)|DT

static void IRAM_ATTR encISR() {
  uint8_t s = (digitalRead(ENC_DT) << 1) | digitalRead(ENC_CLK);
  if (s == encState) return;

  int dir = 0;
  switch (encState) {
    case 0:  // 00
      if (s == 1) dir =  1;  // 00→01 CW
      if (s == 2) dir = -1;  // 00→10 CCW
      break;
    case 1:  // 01
      if (s == 3) dir =  1;  // 01→11 CW
      if (s == 0) dir = -1;  // 01→00 CCW
      break;
    case 3:  // 11
      if (s == 2) dir =  1;  // 11→10 CW
      if (s == 1) dir = -1;  // 11→01 CCW
      break;
    case 2:  // 10
      if (s == 0) dir =  1;  // 10→00 CW
      if (s == 3) dir = -1;  // 10→11 CCW
      break;
  }
  encState = s;

  if (dir != 0) {
    encAccum += dir;
    if (encAccum >=  4) { encDelta = encDelta + 1; encAccum = 0; }
    if (encAccum <= -4) { encDelta = encDelta - 1; encAccum = 0; }
  }
}

// --- Button debounce (polled) ---
enum { IDLE, MAYBE, PRESSED, HOLD } static btnState = IDLE;
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
    if (!raw) { btnState = IDLE; break; }
    if (now - btnTimer < 30) break;
    btnState = PRESSED;
    btnTimer = now;
    return ENC_CLICK;
  case PRESSED:
    if (!raw) { btnState = IDLE; break; }
    if (now - btnTimer > 600) {
      btnState = HOLD;
      return ENC_HOLD;
    }
    break;
  case HOLD:
    if (!raw) { btnState = IDLE; break; }
    break;
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
