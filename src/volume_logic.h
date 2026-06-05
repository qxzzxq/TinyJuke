#pragma once

// Pure volume policy helpers — testable on native.
//
// Max-volume semantics differ by mode:
//   - Jukebox (WAV) playback: scale factor. The user-facing volume keeps its
//     full 0..100 range; the effective output level is vol * maxVol / 100.
//   - Bluetooth: clamp. volumeLevel is limited to maxVolumeLevel (on BT entry,
//     encoder turns, and phone-pushed AVRCP volume) so the value handed to the
//     A2DP stack — and echoed to the phone's slider — is the real loudness.

// Apply an encoder delta to the active parameter (volume or max-volume).
// Both are independent 0..100 values.
inline void volumeAdjust(int &vol, int &maxVol, bool adjustingMax, int delta) {
  int &v = adjustingMax ? maxVol : vol;
  v += delta;
  if (v > 100) v = 100;
  if (v < 0)   v = 0;
}

// Effective output level for WAV playback: volume scaled by the max-volume
// factor (integer percent, truncating).
inline int effectiveVolume(int vol, int maxVol) {
  return vol * maxVol / 100;
}
