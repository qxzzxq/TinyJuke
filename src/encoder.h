#pragma once

#include "config.h"

// readEncoder() return values:
//   0           — no event
//  +1..+N       — N clockwise steps
//  -1..-N       — N counter-clockwise steps
//   100 (ENC_CLICK) — short press
//   101 (ENC_HOLD)  — long press (>600ms)
#define ENC_NONE   0
#define ENC_CLICK  100
#define ENC_HOLD   101

// Runtime volume 0–100, set by encoder ISR, read by playWav() for scaling.
// Loaded from /volume.cfg at boot, saved on change.
extern int volumeLevel;
extern int brightnessLevel;

void initEncoder();
int  readEncoder();      // returns ENC_* event, clears after read
void saveVolume();       // persist volumeLevel to SD card
void loadBrightness();   // read /brightness.cfg into brightnessLevel
void saveBrightness();   // persist brightnessLevel to SD card
void applyBrightness();  // write brightnessLevel to LEDC PWM
