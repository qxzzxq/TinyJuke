// SPDX-FileCopyrightText: 2026 qxzzxq
// SPDX-License-Identifier: GPL-3.0-or-later

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

// ----------------------------------------------------------------
//  Canonical LIST INFO chunk — fixed-capacity INAM/IART fields so
//  metadata edits can patch bytes in place instead of rewriting the
//  whole file. Layout (placed between "fmt " and "data"):
//    "LIST" | 148 | "INFO" | "INAM" | 64 | title | "IART" | 64 | artist
// ----------------------------------------------------------------

#define WAV_INFO_CAP        64   // capacity of each text field (even → no pad bytes)
#define WAV_CANON_LIST_SIZE 156  // full chunk size on disk incl. 8-byte LIST header

// Scan top-level RIFF chunks for a canonical LIST INFO chunk appearing
// before the data chunk. Returns true and sets *listOffset to the byte
// offset of "LIST" if found.
bool findCanonicalListInfo(const uint8_t *data, size_t len, size_t *listOffset);

// Absolute byte offsets of the title/artist text for an in-place patch.
void canonicalListFieldOffsets(size_t listOffset, size_t *titleOff, size_t *artistOff);

// Fill out (>= WAV_CANON_LIST_SIZE bytes) with a complete canonical LIST
// INFO chunk. Strings are truncated to WAV_INFO_CAP-1 and NUL-padded.
// Returns WAV_CANON_LIST_SIZE.
size_t buildCanonicalListInfo(uint8_t *out, const char *title, const char *artist);

// Fill one WAV_INFO_CAP-byte field region: NUL-terminated, NUL-padded.
void writeCanonicalField(uint8_t *out, const char *s);

// Duration in whole seconds from a parsed header (0 if rate/channels/bits is 0).
uint32_t wavDurationSeconds(const WavHeader &hdr);
