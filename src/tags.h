#pragma once

#include "config.h"
#include <ArduinoJson.h>

struct TagInfo {
  const char *file;    // required — WAV file path
  const char *img;     // optional — BMP path under /img/
  const char *title;   // optional
  const char *artist;  // optional
  const char *album;   // optional
};

extern JsonDocument tagDoc;
extern bool sdReady;

void printHex(const uint8_t *data, uint8_t len);
void uidToStr(const uint8_t *uid, uint8_t len, char *buf);
TagInfo lookupTag(const uint8_t *uid, uint8_t uidLen);
