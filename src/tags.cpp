#include "tags.h"

JsonDocument tagDoc;
bool sdReady = false;

void printHex(const uint8_t *data, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    if (data[i] < 0x10) Serial.print('0');
    Serial.print(data[i], HEX);
    if (i < len - 1) Serial.print(':');
  }
}

void uidToStr(const uint8_t *uid, uint8_t len, char *buf) {
  uint8_t pos = 0;
  for (uint8_t i = 0; i < len; i++) {
    if (uid[i] < 0x10) buf[pos++] = '0';
    else buf[pos++] = "0123456789ABCDEF"[(uid[i] >> 4) & 0x0F];
    buf[pos++] = "0123456789ABCDEF"[uid[i] & 0x0F];
    if (i < len - 1) buf[pos++] = ':';
  }
  buf[pos] = '\0';
}

void uidToStrCompact(const uint8_t *uid, uint8_t len, char *buf) {
  for (uint8_t i = 0; i < len; i++) {
    buf[i * 2]     = "0123456789ABCDEF"[uid[i] >> 4];
    buf[i * 2 + 1] = "0123456789ABCDEF"[uid[i] & 0x0F];
  }
  buf[len * 2] = '\0';
}

const char *lookupTag(const uint8_t *uid, uint8_t uidLen) {
  char key[32];
  uidToStrCompact(uid, uidLen, key);
  if (!tagDoc[key].isNull())
    return tagDoc[key]["file"].as<const char *>();
  return nullptr;
}
