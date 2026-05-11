#pragma once

#include <stdint.h>
#include <stddef.h>

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

// Parse a RIFF/WAVE header from an in-memory buffer.
// Returns true on success and fills hdr. The buffer should contain at least
// the first ~4 KB of the file so the data chunk can be located.
bool parseWavHeaderBuffer(const uint8_t *data, size_t len, WavHeader &hdr);

// Parse the LIST INFO (INAM/IART) tags from a RIFF/WAVE buffer.
// meta is zero-filled first; missing tags leave empty strings.
void parseWavMetaBuffer(const uint8_t *data, size_t len, WavMeta &meta);
