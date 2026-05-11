#pragma once

#include <stdint.h>

// Pure quadrature gray-code step.
// prev and curr are 2-bit states encoded as (DT << 1) | CLK.
// Returns +1 for one valid CW transition, -1 for CCW, 0 for invalid/no-change.
// Four valid transitions equal one full detent.
static inline int grayStep(uint8_t prev, uint8_t curr) {
  if (curr == prev) return 0;
  switch (prev) {
    case 0:  // 00
      if (curr == 1) return  1;  // 00→01 CW
      if (curr == 2) return -1;  // 00→10 CCW
      break;
    case 1:  // 01
      if (curr == 3) return  1;  // 01→11 CW
      if (curr == 0) return -1;  // 01→00 CCW
      break;
    case 3:  // 11
      if (curr == 2) return  1;  // 11→10 CW
      if (curr == 1) return -1;  // 11→01 CCW
      break;
    case 2:  // 10
      if (curr == 0) return  1;  // 10→00 CW
      if (curr == 3) return -1;  // 10→11 CCW
      break;
  }
  return 0;
}
