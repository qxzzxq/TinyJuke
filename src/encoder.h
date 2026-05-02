#pragma once

#include "config.h"

// Rotary encoder with push button.
// Uses the KY-040 module (external pull-ups, so INPUT on ESP32 side).
//
// initEncoder()  — set up GPIOs and interrupts
// readEncoder()  — call from loop(); returns:
//    0 = no event
//    1 = clockwise step
//   -1 = counter-clockwise step
//    2 = button short press
//    3 = button long press (>600 ms)

void initEncoder();
int  readEncoder();
