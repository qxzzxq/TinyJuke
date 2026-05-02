#include "encoder.h"

// Placeholder — rotary encoder support not yet wired.
// Will be implemented when the KY-040 encoder is connected to
// ENC_CLK (GPIO 2), ENC_DT (GPIO 15), and ENC_SW (GPIO 34).

void initEncoder() {
  // TODO: attach interrupts on ENC_CLK, poll ENC_SW
}

int readEncoder() {
  return 0;  // no event
}
