#include "encoder.h"
#include <SD.h>

int volumeLevel = VOLUME_DEFAULT;

// Placeholder — encoder not yet wired. Interrupts on ENC_CLK/ENC_DT
// will update volumeLevel and rotation flags directly.
//
// When the KY-040 is connected:
//   - ENC_CLK (GPIO 2)  → attachInterrupt(digitalPinToInterrupt(ENC_CLK), isr, CHANGE)
//   - ENC_DT  (GPIO 15) → read direction from DT state
//   - ENC_SW  (GPIO 34) → poll for button press, track duration for hold

void initEncoder() {
  // TODO: attach interrupts, load volume from SD
  // pinMode(ENC_CLK, INPUT);
  // pinMode(ENC_DT, INPUT);
  // pinMode(ENC_SW, INPUT);
  //
  // Try loading saved volume
  if (SD.exists("/volume.cfg")) {
    File f = SD.open("/volume.cfg", FILE_READ);
    if (f) {
      String s = f.readString();
      f.close();
      int v = s.toInt();
      if (v >= 0 && v <= 100) volumeLevel = v;
    }
  }
}

int readEncoder() {
  return ENC_NONE;  // placeholder
}

void saveVolume() {
  if (SD.exists("/volume.cfg")) SD.remove("/volume.cfg");
  File f = SD.open("/volume.cfg", FILE_WRITE);
  if (f) {
    f.print(volumeLevel);
    f.close();
  }
}
