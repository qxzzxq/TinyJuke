#pragma once

// Pure volume-adjustment policy for the Volume screen — testable on native.
//
// Applies an encoder delta to either the volume or the max-volume (software
// ceiling) and maintains the invariant vol <= maxVol:
//   - vol is clamped to 0..maxVol
//   - maxVol is clamped to 0..100; lowering it below vol pulls vol down
inline void volumeAdjust(int &vol, int &maxVol, bool adjustingMax, int delta) {
  if (adjustingMax) {
    maxVol += delta;
    if (maxVol > 100) maxVol = 100;
    if (maxVol < 0)   maxVol = 0;
    if (vol > maxVol) vol = maxVol;
  } else {
    vol += delta;
    if (vol > maxVol) vol = maxVol;
    if (vol < 0)      vol = 0;
  }
}
