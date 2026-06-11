// SPDX-FileCopyrightText: 2026 qxzzxq
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wav_parser.h"
#include <string.h>

static inline uint32_t readLE32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint16_t readLE16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

bool parseWavHeaderBuffer(const uint8_t *data, size_t len, WavHeader &hdr) {
  if (len < 44) return false;
  if (memcmp(data, "RIFF", 4) != 0) return false;
  if (memcmp(data + 8, "WAVE", 4) != 0) return false;

  // --- Locate "fmt " chunk (must appear within first 256 bytes) ---
  size_t pos = 12;
  bool fmtFound = false;
  while (pos + 8 <= len && pos < 256) {
    uint32_t chunkSize = readLE32(data + pos + 4);
    if (memcmp(data + pos, "fmt ", 4) == 0) {
      if (chunkSize < 16 || chunkSize > 64) return false;
      if (pos + 8 + chunkSize > len) return false;
      const uint8_t *f = data + pos + 8;
      hdr.channels      = readLE16(f + 2);
      hdr.sampleRate    = readLE32(f + 4);
      hdr.bitsPerSample = readLE16(f + 14);
      pos += 8 + chunkSize;
      fmtFound = true;
      break;
    }
    pos += 8 + chunkSize;
  }
  if (!fmtFound) return false;

  // --- Locate "data" chunk (must appear within first 4096 bytes) ---
  while (pos + 8 <= len && pos < 4096) {
    uint32_t chunkSize = readLE32(data + pos + 4);
    if (memcmp(data + pos, "data", 4) == 0) {
      hdr.dataSize   = chunkSize;
      hdr.dataOffset = (uint32_t)(pos + 8);
      return true;
    }
    pos += 8 + chunkSize;
  }
  return false;
}

static inline void writeLE32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

bool findCanonicalListInfo(const uint8_t *data, size_t len, size_t *listOffset) {
  if (len < 12) return false;
  if (memcmp(data, "RIFF", 4) != 0) return false;
  if (memcmp(data + 8, "WAVE", 4) != 0) return false;

  size_t pos = 12;
  while (pos + 8 <= len) {
    uint32_t size = readLE32(data + pos + 4);
    if (memcmp(data + pos, "data", 4) == 0) return false;  // must appear before data
    if (memcmp(data + pos, "LIST", 4) == 0 &&
        size == WAV_CANON_LIST_SIZE - 8 &&
        pos + WAV_CANON_LIST_SIZE <= len &&
        memcmp(data + pos + 8,  "INFO", 4) == 0 &&
        memcmp(data + pos + 12, "INAM", 4) == 0 &&
        readLE32(data + pos + 16) == WAV_INFO_CAP &&
        memcmp(data + pos + 20 + WAV_INFO_CAP, "IART", 4) == 0 &&
        readLE32(data + pos + 24 + WAV_INFO_CAP) == WAV_INFO_CAP) {
      *listOffset = pos;
      return true;
    }
    pos += 8 + size + (size & 1);
  }
  return false;
}

void canonicalListFieldOffsets(size_t listOffset, size_t *titleOff, size_t *artistOff) {
  *titleOff  = listOffset + 20;
  *artistOff = listOffset + 28 + WAV_INFO_CAP;
}

void writeCanonicalField(uint8_t *out, const char *s) {
  memset(out, 0, WAV_INFO_CAP);
  if (s) {
    size_t n = strlen(s);
    if (n > WAV_INFO_CAP - 1) n = WAV_INFO_CAP - 1;
    memcpy(out, s, n);
  }
}

size_t buildCanonicalListInfo(uint8_t *out, const char *title, const char *artist) {
  memcpy(out, "LIST", 4);
  writeLE32(out + 4, WAV_CANON_LIST_SIZE - 8);
  memcpy(out + 8, "INFO", 4);
  memcpy(out + 12, "INAM", 4);
  writeLE32(out + 16, WAV_INFO_CAP);
  writeCanonicalField(out + 20, title);
  memcpy(out + 20 + WAV_INFO_CAP, "IART", 4);
  writeLE32(out + 24 + WAV_INFO_CAP, WAV_INFO_CAP);
  writeCanonicalField(out + 28 + WAV_INFO_CAP, artist);
  return WAV_CANON_LIST_SIZE;
}

uint32_t wavDurationSeconds(const WavHeader &hdr) {
  uint32_t bytesPerSec = hdr.sampleRate * hdr.channels * (hdr.bitsPerSample / 8);
  if (bytesPerSec == 0) return 0;
  return hdr.dataSize / bytesPerSec;
}

void parseWavMetaBuffer(const uint8_t *data, size_t len, WavMeta &meta) {
  memset(&meta, 0, sizeof(meta));
  if (len < 12) return;
  if (memcmp(data, "RIFF", 4) != 0) return;

  uint32_t riffSize = readLE32(data + 4);
  size_t end = (size_t)riffSize + 8;
  if (end > len) end = len;

  size_t pos = 12;
  while (pos + 8 <= end) {
    uint32_t size = readLE32(data + pos + 4);
    if (memcmp(data + pos, "LIST", 4) == 0 && size >= 4 && pos + 8 + size <= end) {
      if (memcmp(data + pos + 8, "INFO", 4) != 0) { pos += 8 + size; continue; }

      size_t infoEnd = pos + 8 + size;
      size_t ip = pos + 12;
      while (ip + 8 <= infoEnd) {
        uint32_t sub = readLE32(data + ip + 4);
        char *dst = nullptr;
        int max = 0;
        if      (memcmp(data + ip, "INAM", 4) == 0) { dst = meta.title;  max = 63; }
        else if (memcmp(data + ip, "IART", 4) == 0) { dst = meta.artist; max = 63; }

        if (dst && sub > 0 && ip + 8 + sub <= infoEnd) {
          int n = ((int)sub < max) ? (int)sub : max;
          memcpy(dst, data + ip + 8, n);
          dst[n] = '\0';
          while (n > 0 && (dst[n-1] == 0 || dst[n-1] == ' ')) dst[--n] = '\0';
        }
        ip += 8 + sub + (sub & 1);
      }
      return;
    }
    pos += 8 + size;
  }
}
