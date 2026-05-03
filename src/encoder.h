#pragma once

#include "config.h"

// Encoder event codes returned by readEncoder()
#define ENC_NONE   0
#define ENC_CW     1    // clockwise turn
#define ENC_CCW   -1    // counter-clockwise turn
#define ENC_CLICK  2    // short press
#define ENC_HOLD   3    // long press (>600ms)

// Runtime volume 0–100, set by encoder ISR, read by playWav() for scaling.
// Loaded from /volume.cfg at boot, saved on change.
extern int volumeLevel;

void initEncoder();
int  readEncoder();      // returns ENC_* event, clears after read
void saveVolume();       // persist volumeLevel to SD card
