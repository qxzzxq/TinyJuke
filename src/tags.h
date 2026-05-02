#pragma once

#include "config.h"
#include <ArduinoJson.h>

extern JsonDocument tagDoc;
extern bool sdReady;

void printHex(const uint8_t *data, uint8_t len);
void uidToStr(const uint8_t *uid, uint8_t len, char *buf);
void uidToStrCompact(const uint8_t *uid, uint8_t len, char *buf);
const char *lookupTag(const uint8_t *uid, uint8_t uidLen);
