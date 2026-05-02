#pragma once

#include "config.h"
#include <SD.h>
#include <PN532.h>

struct WavHeader {
  uint16_t channels;
  uint32_t sampleRate;
  uint16_t bitsPerSample;
  uint32_t dataSize;
  uint32_t dataOffset;
};

extern bool audioPlaying;
extern bool stopRequested;

void playWav(const char *filepath, PN532 &nfc);
void stopPlayback();
