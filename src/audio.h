#pragma once

#include "config.h"
#include "encoder.h"  // for volumeLevel
#include <SD.h>
#include <PN532.h>

struct WavMeta {
  char title[64];
  char artist[64];
};

struct WavHeader {
  uint16_t channels;
  uint32_t sampleRate;
  uint16_t bitsPerSample;
  uint32_t dataSize;
  uint32_t dataOffset;
};

extern bool audioPlaying;
extern bool stopRequested;
extern bool sleepTimerFired;
extern uint32_t audioStartTime;

void i2sPrime();
void playWav(const char *filepath, PN532 &nfc, const uint8_t *tagUid, uint8_t tagUidLen);
void stopPlayback();
void parseWavMeta(const char *filepath, WavMeta &meta);
