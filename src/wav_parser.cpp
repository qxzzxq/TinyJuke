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
