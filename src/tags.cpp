#include "tags.h"
#include <Arduino.h>

// tagDoc, uidToStr() and lookupTag() live in tag_utils.cpp so the native test
// environment can compile them without pulling in <Arduino.h>/<Serial>.

bool sdReady = false;

void printHex(const uint8_t *data, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    if (data[i] < 0x10) Serial.print('0');
    Serial.print(data[i], HEX);
    if (i < len - 1) Serial.print(':');
  }
}
