// SPDX-FileCopyrightText: 2026 qxzzxq
// SPDX-License-Identifier: GPL-3.0-or-later

// Native unit tests — pure logic only. Run with:
//   ~/.platformio/penv/bin/pio test -e native
//
// To keep the native build free of Arduino/ESP-IDF, this file pulls in the
// pure-logic source files directly (not the SD/I2S/Serial-coupled ones).

#include <unity.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

// Sources under test — pure C++, no Arduino deps.
#include "wav_parser.cpp"
#include "tag_utils.cpp"
#include "encoder_gray.h"
#include "value_array.h"
#include "timer_logic.h"
#include "volume_logic.h"
#include "jukebox_state.cpp"
#include "theme.h"
#include "anim.h"
#include "qr_layout.h"
#include <qrcode.h>

// ----------------------------------------------------------------
//  uidToStr — colon-separated uppercase hex
// ----------------------------------------------------------------

void test_uid_to_str_simple() {
  uint8_t uid[] = {0x04, 0xA2, 0x24};
  char buf[32];
  uidToStr(uid, 3, buf);
  TEST_ASSERT_EQUAL_STRING("04:A2:24", buf);
}

void test_uid_to_str_pads_low_nibble() {
  // Single-byte values < 0x10 must keep the leading '0'.
  uint8_t uid[] = {0x00, 0x0F, 0x05};
  char buf[32];
  uidToStr(uid, 3, buf);
  TEST_ASSERT_EQUAL_STRING("00:0F:05", buf);
}

void test_uid_to_str_seven_bytes() {
  // Real-world 7-byte NFC UID.
  uint8_t uid[] = {0x04, 0xA2, 0x24, 0xB2, 0xC3, 0x80, 0x81};
  char buf[32];
  uidToStr(uid, 7, buf);
  TEST_ASSERT_EQUAL_STRING("04:A2:24:B2:C3:80:81", buf);
}

void test_uid_to_str_single_byte() {
  uint8_t uid[] = {0xAB};
  char buf[8];
  uidToStr(uid, 1, buf);
  TEST_ASSERT_EQUAL_STRING("AB", buf);
}

// ----------------------------------------------------------------
//  lookupTag — JsonDocument-backed UID → TagInfo
// ----------------------------------------------------------------

void test_lookup_tag_hit() {
  const char *json = R"({
    "04:A2:24": {
      "file": "music/song.wav",
      "img":  "cover.bmp",
      "title": "Hello",
      "artist": "World"
    }
  })";
  deserializeJson(tagDoc, json);

  uint8_t uid[] = {0x04, 0xA2, 0x24};
  TagInfo info = lookupTag(uid, 3);
  TEST_ASSERT_NOT_NULL(info.file);
  TEST_ASSERT_EQUAL_STRING("music/song.wav", info.file);
  TEST_ASSERT_EQUAL_STRING("cover.bmp", info.img);
  TEST_ASSERT_EQUAL_STRING("Hello", info.title);
  TEST_ASSERT_EQUAL_STRING("World", info.artist);
}

void test_lookup_tag_miss() {
  const char *json = R"({"04:A2:24":{"file":"a.wav"}})";
  tagDoc.clear();
  deserializeJson(tagDoc, json);

  uint8_t uid[] = {0xDE, 0xAD, 0xBE, 0xEF};
  TagInfo info = lookupTag(uid, 4);
  TEST_ASSERT_NULL(info.file);
}

void test_lookup_tag_optional_fields_missing() {
  const char *json = R"({"81:0C:2B:07":{"file":"music/only.wav"}})";
  tagDoc.clear();
  deserializeJson(tagDoc, json);

  uint8_t uid[] = {0x81, 0x0C, 0x2B, 0x07};
  TagInfo info = lookupTag(uid, 4);
  TEST_ASSERT_EQUAL_STRING("music/only.wav", info.file);
  // Missing optional fields surface as null pointers.
  TEST_ASSERT_NULL(info.img);
  TEST_ASSERT_NULL(info.title);
  TEST_ASSERT_NULL(info.artist);
}

// ----------------------------------------------------------------
//  WAV header parsing — buffer-based
// ----------------------------------------------------------------

// Helpers to assemble little-endian fields into a buffer.
static void putLE32(uint8_t *p, uint32_t v) {
  p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
  p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}
static void putLE16(uint8_t *p, uint16_t v) {
  p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
}

// Build a canonical PCM WAV: RIFF / fmt(16) / data. dataBytes content is zeroed
// (we only test header parsing, not audio decoding).
static size_t buildMinimalWav(uint8_t *buf, size_t bufLen,
                              uint16_t channels, uint32_t sampleRate,
                              uint16_t bits, uint32_t dataBytes) {
  const uint32_t fmtSize = 16;
  size_t total = 12 + 8 + fmtSize + 8 + dataBytes;
  if (total > bufLen) return 0;

  memcpy(buf, "RIFF", 4);
  putLE32(buf + 4, (uint32_t)(total - 8));
  memcpy(buf + 8, "WAVE", 4);

  memcpy(buf + 12, "fmt ", 4);
  putLE32(buf + 16, fmtSize);
  putLE16(buf + 20, 1);              // PCM
  putLE16(buf + 22, channels);
  putLE32(buf + 24, sampleRate);
  putLE32(buf + 28, sampleRate * channels * (bits / 8));  // byte rate
  putLE16(buf + 32, channels * (bits / 8));               // block align
  putLE16(buf + 34, bits);

  memcpy(buf + 36, "data", 4);
  putLE32(buf + 40, dataBytes);
  memset(buf + 44, 0, dataBytes);
  return total;
}

void test_wav_header_mono_16bit() {
  uint8_t buf[256];
  size_t n = buildMinimalWav(buf, sizeof(buf), 1, 44100, 16, 100);
  TEST_ASSERT_GREATER_THAN(0, n);

  WavHeader hdr = {};
  TEST_ASSERT_TRUE(parseWavHeaderBuffer(buf, n, hdr));
  TEST_ASSERT_EQUAL_UINT16(1, hdr.channels);
  TEST_ASSERT_EQUAL_UINT32(44100, hdr.sampleRate);
  TEST_ASSERT_EQUAL_UINT16(16, hdr.bitsPerSample);
  TEST_ASSERT_EQUAL_UINT32(100, hdr.dataSize);
  TEST_ASSERT_EQUAL_UINT32(44, hdr.dataOffset);
}

void test_wav_header_stereo_24bit() {
  uint8_t buf[256];
  size_t n = buildMinimalWav(buf, sizeof(buf), 2, 48000, 24, 60);
  WavHeader hdr = {};
  TEST_ASSERT_TRUE(parseWavHeaderBuffer(buf, n, hdr));
  TEST_ASSERT_EQUAL_UINT16(2, hdr.channels);
  TEST_ASSERT_EQUAL_UINT32(48000, hdr.sampleRate);
  TEST_ASSERT_EQUAL_UINT16(24, hdr.bitsPerSample);
}

void test_wav_header_rejects_bad_magic() {
  uint8_t buf[64] = {0};
  memcpy(buf, "RIFX", 4);  // wrong
  memcpy(buf + 8, "WAVE", 4);
  WavHeader hdr;
  TEST_ASSERT_FALSE(parseWavHeaderBuffer(buf, 64, hdr));
}

void test_wav_header_rejects_truncated() {
  uint8_t buf[16] = {0};
  memcpy(buf, "RIFF", 4);
  memcpy(buf + 8, "WAVE", 4);
  WavHeader hdr;
  TEST_ASSERT_FALSE(parseWavHeaderBuffer(buf, 16, hdr));
}

void test_wav_header_skips_unknown_chunk_before_data() {
  // RIFF/fmt/<JUNK chunk>/data — parser must walk past JUNK to find data.
  uint8_t buf[256] = {0};
  memcpy(buf, "RIFF", 4); putLE32(buf + 4, 200); memcpy(buf + 8, "WAVE", 4);
  memcpy(buf + 12, "fmt ", 4); putLE32(buf + 16, 16);
  putLE16(buf + 20, 1); putLE16(buf + 22, 1);
  putLE32(buf + 24, 22050); putLE32(buf + 28, 22050);
  putLE16(buf + 32, 2); putLE16(buf + 34, 16);

  // JUNK chunk of 20 bytes payload.
  memcpy(buf + 36, "JUNK", 4); putLE32(buf + 40, 20);
  memset(buf + 44, 0xAA, 20);

  memcpy(buf + 64, "data", 4); putLE32(buf + 68, 50);

  WavHeader hdr = {};
  TEST_ASSERT_TRUE(parseWavHeaderBuffer(buf, sizeof(buf), hdr));
  TEST_ASSERT_EQUAL_UINT32(22050, hdr.sampleRate);
  TEST_ASSERT_EQUAL_UINT32(50, hdr.dataSize);
  TEST_ASSERT_EQUAL_UINT32(72, hdr.dataOffset);
}

void test_wav_header_rejects_missing_data_chunk() {
  // RIFF/fmt/<JUNK that fills past the 4 KB data-search window>.
  uint8_t buf[5000] = {0};
  memcpy(buf, "RIFF", 4); putLE32(buf + 4, 4992); memcpy(buf + 8, "WAVE", 4);
  memcpy(buf + 12, "fmt ", 4); putLE32(buf + 16, 16);
  putLE16(buf + 20, 1); putLE16(buf + 22, 1);
  putLE32(buf + 24, 44100); putLE32(buf + 28, 44100);
  putLE16(buf + 32, 2); putLE16(buf + 34, 16);
  // Huge filler chunk that overflows the 4 KB search.
  memcpy(buf + 36, "FILL", 4); putLE32(buf + 40, 4950);

  WavHeader hdr;
  TEST_ASSERT_FALSE(parseWavHeaderBuffer(buf, sizeof(buf), hdr));
}

// ----------------------------------------------------------------
//  WAV LIST/INFO metadata parsing
// ----------------------------------------------------------------

void test_wav_meta_inam_iart() {
  // RIFF / fmt / data(0) / LIST INFO { INAM, IART }
  uint8_t buf[256] = {0};
  size_t p = 0;
  memcpy(buf + p, "RIFF", 4); p += 4;
  size_t riffSizePos = p; p += 4;
  memcpy(buf + p, "WAVE", 4); p += 4;
  memcpy(buf + p, "fmt ", 4); p += 4; putLE32(buf + p, 16); p += 4;
  putLE16(buf + p, 1); p += 2; putLE16(buf + p, 1); p += 2;
  putLE32(buf + p, 44100); p += 4; putLE32(buf + p, 44100); p += 4;
  putLE16(buf + p, 2); p += 2; putLE16(buf + p, 16); p += 2;
  memcpy(buf + p, "data", 4); p += 4; putLE32(buf + p, 0); p += 4;

  // LIST INFO chunk
  size_t listStart = p;
  memcpy(buf + p, "LIST", 4); p += 4;
  size_t listSizePos = p; p += 4;
  size_t listBodyStart = p;
  memcpy(buf + p, "INFO", 4); p += 4;

  memcpy(buf + p, "INAM", 4); p += 4;
  const char *title = "My Song";
  uint32_t inamSize = (uint32_t)strlen(title) + 1;  // include NUL
  putLE32(buf + p, inamSize); p += 4;
  memcpy(buf + p, title, inamSize); p += inamSize;
  if (inamSize & 1) p += 1;  // RIFF word-alignment

  memcpy(buf + p, "IART", 4); p += 4;
  const char *artist = "Some Artist";
  uint32_t iartSize = (uint32_t)strlen(artist) + 1;
  putLE32(buf + p, iartSize); p += 4;
  memcpy(buf + p, artist, iartSize); p += iartSize;
  if (iartSize & 1) p += 1;

  putLE32(buf + listSizePos, (uint32_t)(p - listBodyStart));
  putLE32(buf + riffSizePos, (uint32_t)(p - 8));

  WavMeta meta;
  parseWavMetaBuffer(buf, p, meta);
  TEST_ASSERT_EQUAL_STRING("My Song", meta.title);
  TEST_ASSERT_EQUAL_STRING("Some Artist", meta.artist);
}

void test_wav_meta_no_list_returns_empty() {
  uint8_t buf[64];
  size_t n = buildMinimalWav(buf, sizeof(buf), 1, 44100, 16, 0);
  WavMeta meta;
  parseWavMetaBuffer(buf, n, meta);
  TEST_ASSERT_EQUAL_STRING("", meta.title);
  TEST_ASSERT_EQUAL_STRING("", meta.artist);
}

void test_wav_meta_rejects_non_riff() {
  uint8_t buf[16] = {'X','Y','Z','W'};
  WavMeta meta;
  parseWavMetaBuffer(buf, sizeof(buf), meta);
  TEST_ASSERT_EQUAL_STRING("", meta.title);
}

// ----------------------------------------------------------------
//  Canonical LIST INFO chunk (in-place metadata editing)
// ----------------------------------------------------------------

// RIFF / fmt / LIST(canonical) / data — the layout written by the web UI.
static size_t buildCanonicalWav(uint8_t *buf, size_t bufLen,
                                const char *title, const char *artist,
                                uint32_t dataBytes) {
  size_t total = 12 + 24 + WAV_CANON_LIST_SIZE + 8 + dataBytes;
  if (total > bufLen) return 0;

  memcpy(buf, "RIFF", 4);
  putLE32(buf + 4, (uint32_t)(total - 8));
  memcpy(buf + 8, "WAVE", 4);

  memcpy(buf + 12, "fmt ", 4);
  putLE32(buf + 16, 16);
  putLE16(buf + 20, 1); putLE16(buf + 22, 1);
  putLE32(buf + 24, 44100); putLE32(buf + 28, 88200);
  putLE16(buf + 32, 2); putLE16(buf + 34, 16);

  buildCanonicalListInfo(buf + 36, title, artist);

  memcpy(buf + 36 + WAV_CANON_LIST_SIZE, "data", 4);
  putLE32(buf + 40 + WAV_CANON_LIST_SIZE, dataBytes);
  memset(buf + 44 + WAV_CANON_LIST_SIZE, 0, dataBytes);
  return total;
}

void test_canon_build_layout() {
  uint8_t out[WAV_CANON_LIST_SIZE];
  size_t n = buildCanonicalListInfo(out, "Title", "Artist");
  TEST_ASSERT_EQUAL_UINT32(WAV_CANON_LIST_SIZE, (uint32_t)n);
  TEST_ASSERT_EQUAL_MEMORY("LIST", out, 4);
  TEST_ASSERT_EQUAL_UINT32(WAV_CANON_LIST_SIZE - 8, out[4] | (out[5] << 8) | (out[6] << 16) | (out[7] << 24));
  TEST_ASSERT_EQUAL_MEMORY("INFO", out + 8, 4);
  TEST_ASSERT_EQUAL_MEMORY("INAM", out + 12, 4);
  TEST_ASSERT_EQUAL_UINT32(WAV_INFO_CAP, out[16]);
  TEST_ASSERT_EQUAL_STRING("Title", (const char *)(out + 20));
  TEST_ASSERT_EQUAL_MEMORY("IART", out + 20 + WAV_INFO_CAP, 4);
  TEST_ASSERT_EQUAL_UINT32(WAV_INFO_CAP, out[24 + WAV_INFO_CAP]);
  TEST_ASSERT_EQUAL_STRING("Artist", (const char *)(out + 28 + WAV_INFO_CAP));
}

void test_canon_build_truncates_long_strings() {
  char longStr[100];
  memset(longStr, 'A', sizeof(longStr) - 1);
  longStr[sizeof(longStr) - 1] = '\0';

  uint8_t out[WAV_CANON_LIST_SIZE];
  buildCanonicalListInfo(out, longStr, longStr);
  const char *title = (const char *)(out + 20);
  TEST_ASSERT_EQUAL_UINT32(WAV_INFO_CAP - 1, (uint32_t)strlen(title));
  // Field must not bleed into the IART magic.
  TEST_ASSERT_EQUAL_MEMORY("IART", out + 20 + WAV_INFO_CAP, 4);
}

void test_canon_write_field_pads_with_nul() {
  uint8_t out[WAV_INFO_CAP];
  memset(out, 0xFF, sizeof(out));
  writeCanonicalField(out, "Hi");
  TEST_ASSERT_EQUAL_STRING("Hi", (const char *)out);
  for (size_t i = 2; i < WAV_INFO_CAP; i++) TEST_ASSERT_EQUAL_UINT8(0, out[i]);
}

void test_canon_find_positive() {
  uint8_t buf[512];
  size_t n = buildCanonicalWav(buf, sizeof(buf), "T", "A", 16);
  TEST_ASSERT_GREATER_THAN(0, n);
  size_t off = 0;
  TEST_ASSERT_TRUE(findCanonicalListInfo(buf, n, &off));
  TEST_ASSERT_EQUAL_UINT32(36, (uint32_t)off);
}

void test_canon_find_rejects_noncanonical_size() {
  uint8_t buf[512];
  size_t n = buildCanonicalWav(buf, sizeof(buf), "T", "A", 16);
  putLE32(buf + 36 + 16, 32);  // corrupt INAM size: 64 → 32
  size_t off = 0;
  TEST_ASSERT_FALSE(findCanonicalListInfo(buf, n, &off));
}

void test_canon_find_rejects_list_after_data() {
  // RIFF / fmt / data / LIST(canonical) — must NOT be found (firmware
  // playback only scans the front of the file).
  uint8_t buf[512] = {0};
  size_t n = buildMinimalWav(buf, sizeof(buf), 1, 44100, 16, 16);
  size_t p = n;
  p += buildCanonicalListInfo(buf + p, "T", "A");
  putLE32(buf + 4, (uint32_t)(p - 8));
  size_t off = 0;
  TEST_ASSERT_FALSE(findCanonicalListInfo(buf, p, &off));
}

void test_canon_field_offsets() {
  size_t titleOff = 0, artistOff = 0;
  canonicalListFieldOffsets(36, &titleOff, &artistOff);
  TEST_ASSERT_EQUAL_UINT32(56, (uint32_t)titleOff);            // 36 + 20
  TEST_ASSERT_EQUAL_UINT32(128, (uint32_t)artistOff);          // 36 + 28 + 64
}

void test_canon_roundtrip_firmware_parser() {
  // Chunk written by the web UI must read back through the same parser the
  // playback screen uses (proves padded fields are trimmed correctly).
  uint8_t buf[512];
  size_t n = buildCanonicalWav(buf, sizeof(buf), "My Song", "Some Artist", 16);
  WavMeta meta;
  parseWavMetaBuffer(buf, n, meta);
  TEST_ASSERT_EQUAL_STRING("My Song", meta.title);
  TEST_ASSERT_EQUAL_STRING("Some Artist", meta.artist);

  // In-place patch via field offsets, then re-parse.
  size_t off = 0, titleOff = 0, artistOff = 0;
  TEST_ASSERT_TRUE(findCanonicalListInfo(buf, n, &off));
  canonicalListFieldOffsets(off, &titleOff, &artistOff);
  writeCanonicalField(buf + titleOff, "New Title");
  writeCanonicalField(buf + artistOff, "");
  parseWavMetaBuffer(buf, n, meta);
  TEST_ASSERT_EQUAL_STRING("New Title", meta.title);
  TEST_ASSERT_EQUAL_STRING("", meta.artist);
}

void test_wav_duration_basic() {
  WavHeader hdr = {};
  hdr.channels = 1; hdr.sampleRate = 44100; hdr.bitsPerSample = 16;
  hdr.dataSize = 88200;  // 1 second mono 16-bit
  TEST_ASSERT_EQUAL_UINT32(1, wavDurationSeconds(hdr));

  hdr.channels = 2; hdr.sampleRate = 48000; hdr.bitsPerSample = 24;
  hdr.dataSize = 48000 * 2 * 3 * 182;  // 182 seconds stereo 24-bit
  TEST_ASSERT_EQUAL_UINT32(182, wavDurationSeconds(hdr));
}

void test_wav_duration_zero_rate_is_zero() {
  WavHeader hdr = {};
  hdr.dataSize = 1000;
  TEST_ASSERT_EQUAL_UINT32(0, wavDurationSeconds(hdr));
}

// ----------------------------------------------------------------
//  Encoder gray-code step
// ----------------------------------------------------------------

void test_gray_step_no_change() {
  TEST_ASSERT_EQUAL_INT(0, grayStep(0, 0));
  TEST_ASSERT_EQUAL_INT(0, grayStep(3, 3));
}

void test_gray_step_one_cw_cycle() {
  // 00→01→11→10→00 is one full detent (4 transitions CW).
  TEST_ASSERT_EQUAL_INT(+1, grayStep(0, 1));
  TEST_ASSERT_EQUAL_INT(+1, grayStep(1, 3));
  TEST_ASSERT_EQUAL_INT(+1, grayStep(3, 2));
  TEST_ASSERT_EQUAL_INT(+1, grayStep(2, 0));
}

void test_gray_step_one_ccw_cycle() {
  // Reverse direction: 00→10→11→01→00.
  TEST_ASSERT_EQUAL_INT(-1, grayStep(0, 2));
  TEST_ASSERT_EQUAL_INT(-1, grayStep(2, 3));
  TEST_ASSERT_EQUAL_INT(-1, grayStep(3, 1));
  TEST_ASSERT_EQUAL_INT(-1, grayStep(1, 0));
}

void test_gray_step_invalid_transition() {
  // Diagonals (skipping a gray state) are bounce — must be ignored.
  TEST_ASSERT_EQUAL_INT(0, grayStep(0, 3));
  TEST_ASSERT_EQUAL_INT(0, grayStep(3, 0));
  TEST_ASSERT_EQUAL_INT(0, grayStep(1, 2));
  TEST_ASSERT_EQUAL_INT(0, grayStep(2, 1));
}

// ----------------------------------------------------------------
//  value_array helpers (used by power-save / sleep-timer settings)
// ----------------------------------------------------------------

void test_value_to_index_hit() {
  static const int vals[] = {0, 5, 15, 30, 60};
  TEST_ASSERT_EQUAL_INT(0, valueToIndex(vals, 5, 0));
  TEST_ASSERT_EQUAL_INT(2, valueToIndex(vals, 5, 15));
  TEST_ASSERT_EQUAL_INT(4, valueToIndex(vals, 5, 60));
}

void test_value_to_index_miss_returns_zero() {
  static const int vals[] = {0, 5, 15, 30, 60};
  // Unknown value (e.g. corrupted .cfg) maps to the first option (= disabled).
  TEST_ASSERT_EQUAL_INT(0, valueToIndex(vals, 5, 99));
  TEST_ASSERT_EQUAL_INT(0, valueToIndex(vals, 5, -1));
}

void test_index_to_value_clamps() {
  static const int vals[] = {0, 15, 30, 60, 120};
  TEST_ASSERT_EQUAL_INT(0,   indexToValue(vals, 5, -1));
  TEST_ASSERT_EQUAL_INT(0,   indexToValue(vals, 5,  0));
  TEST_ASSERT_EQUAL_INT(30,  indexToValue(vals, 5,  2));
  TEST_ASSERT_EQUAL_INT(120, indexToValue(vals, 5,  4));
  TEST_ASSERT_EQUAL_INT(120, indexToValue(vals, 5, 99));  // clamps high
}

// ----------------------------------------------------------------
//  Timer policy: sleepTimerShouldFire / powerSaveShouldSleep
// ----------------------------------------------------------------

void test_sleep_timer_disabled_never_fires() {
  // 0 minutes = disabled; must never fire even after a huge elapsed value.
  TEST_ASSERT_FALSE(sleepTimerShouldFire(0, 0));
  TEST_ASSERT_FALSE(sleepTimerShouldFire(0, 999999999UL));
  TEST_ASSERT_FALSE(sleepTimerShouldFire(-5, 999999999UL));
}

void test_sleep_timer_fires_at_boundary() {
  // 1 min = 60_000 ms. Below total → false; at/above → true.
  TEST_ASSERT_FALSE(sleepTimerShouldFire(1, 0));
  TEST_ASSERT_FALSE(sleepTimerShouldFire(1, 59999UL));
  TEST_ASSERT_TRUE (sleepTimerShouldFire(1, 60000UL));
  TEST_ASSERT_TRUE (sleepTimerShouldFire(1, 60001UL));
}

void test_sleep_timer_fires_at_max_supported_value() {
  // Largest user-facing option is 120 min — must still compute correctly.
  uint32_t total = 120UL * 60000UL;
  TEST_ASSERT_FALSE(sleepTimerShouldFire(120, total - 1));
  TEST_ASSERT_TRUE (sleepTimerShouldFire(120, total));
}

void test_power_save_disabled_never_sleeps() {
  TEST_ASSERT_FALSE(powerSaveShouldSleep(0, 0));
  TEST_ASSERT_FALSE(powerSaveShouldSleep(0, 999999999UL));
  TEST_ASSERT_FALSE(powerSaveShouldSleep(-1, 999999999UL));
}

void test_power_save_fires_at_boundary() {
  // 5 min = 300_000 ms.
  TEST_ASSERT_FALSE(powerSaveShouldSleep(5, 299999UL));
  TEST_ASSERT_TRUE (powerSaveShouldSleep(5, 300000UL));
}

void test_timer_predicates_handle_unsigned_subtraction_wrap() {
  // millis() wraps at ~49.7 days; the helper takes the already-computed
  // elapsed value (`millis() - start`), which is unsigned and tolerates one
  // wrap as long as the wrap-adjusted result fits in uint32_t. Verify the
  // helper itself doesn't introduce signed-overflow surprises.
  uint32_t wrappedElapsed = (uint32_t)(0u - 1000u) + 65000u;  // ~64 sec
  // 1-min timer should fire after 64 sec.
  TEST_ASSERT_TRUE(sleepTimerShouldFire(1, wrappedElapsed));
}

// ----------------------------------------------------------------
//  Timer policy: timerRemainingMs
// ----------------------------------------------------------------

void test_remaining_disabled_is_zero() {
  TEST_ASSERT_EQUAL_UINT32(0, timerRemainingMs(0, 1234));
  TEST_ASSERT_EQUAL_UINT32(0, timerRemainingMs(-1, 1234));
}

void test_remaining_full_at_start() {
  // 1 min timer, 0 ms elapsed → full minute remaining.
  TEST_ASSERT_EQUAL_UINT32(60000UL, timerRemainingMs(1, 0));
}

void test_remaining_counts_down() {
  // 2 min total, 30 sec in → 90 sec left.
  TEST_ASSERT_EQUAL_UINT32(90000UL, timerRemainingMs(2, 30000UL));
}

void test_remaining_clamps_to_zero_after_fire() {
  // Once elapsed reaches/exceeds total the remaining is pinned to 0.
  TEST_ASSERT_EQUAL_UINT32(0, timerRemainingMs(1, 60000UL));
  TEST_ASSERT_EQUAL_UINT32(0, timerRemainingMs(1, 999999UL));
}

// ----------------------------------------------------------------
//  Countdown formatting (drives the on-screen "MM:SS" display)
// ----------------------------------------------------------------

void test_format_countdown_zero() {
  char buf[8];
  formatCountdownMMSS(0, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("00:00", buf);
}

void test_format_countdown_truncates_subsecond() {
  // 1500 ms = 1 whole second (sub-second part is dropped).
  char buf[8];
  formatCountdownMMSS(1500, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("00:01", buf);
}

void test_format_countdown_one_minute() {
  char buf[8];
  formatCountdownMMSS(60000UL, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("01:00", buf);
}

void test_format_countdown_minute_and_seconds() {
  char buf[8];
  formatCountdownMMSS(90500UL, buf, sizeof(buf));  // 1 min 30.5 sec
  TEST_ASSERT_EQUAL_STRING("01:30", buf);
}

void test_format_countdown_two_hour_max() {
  // Upper-bound user option (120 min). MM widens to 3 digits — buffer must fit.
  char buf[8];
  formatCountdownMMSS(120UL * 60000UL, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("120:00", buf);
}

// ----------------------------------------------------------------
//  Volume policy (Volume screen: vol + max-volume scale factor)
// ----------------------------------------------------------------

void test_volume_adjust_normal_steps() {
  int vol = 50, maxVol = 100;
  volumeAdjust(vol, maxVol, false, 3);
  TEST_ASSERT_EQUAL_INT(53, vol);
  volumeAdjust(vol, maxVol, false, -5);
  TEST_ASSERT_EQUAL_INT(48, vol);
  TEST_ASSERT_EQUAL_INT(100, maxVol);  // untouched
}

void test_volume_adjust_params_are_independent() {
  // Max-volume is a scale factor, not a ceiling on the bar: vol keeps its
  // full 0..100 range regardless of maxVol, and adjusting one never moves
  // the other.
  int vol = 58, maxVol = 60;
  volumeAdjust(vol, maxVol, false, 10);
  TEST_ASSERT_EQUAL_INT(68, vol);

  vol = 80; maxVol = 90;
  volumeAdjust(vol, maxVol, true, -50);
  TEST_ASSERT_EQUAL_INT(40, maxVol);
  TEST_ASSERT_EQUAL_INT(80, vol);  // not pulled down
}

void test_volume_adjust_clamps_both_params_at_bounds() {
  int vol = 2, maxVol = 95;
  volumeAdjust(vol, maxVol, false, -10);
  TEST_ASSERT_EQUAL_INT(0, vol);
  volumeAdjust(vol, maxVol, false, 150);
  TEST_ASSERT_EQUAL_INT(100, vol);

  volumeAdjust(vol, maxVol, true, 20);
  TEST_ASSERT_EQUAL_INT(100, maxVol);
  volumeAdjust(vol, maxVol, true, -150);
  TEST_ASSERT_EQUAL_INT(0, maxVol);
}

void test_effective_volume_scales_by_max() {
  // WAV playback loudness is the product of the two percentages — this is
  // what makes max-volume a cap even though the bar still reads 0..100.
  TEST_ASSERT_EQUAL_INT(100, effectiveVolume(100, 100));
  TEST_ASSERT_EQUAL_INT(50,  effectiveVolume(100, 50));
  TEST_ASSERT_EQUAL_INT(25,  effectiveVolume(50, 50));
  TEST_ASSERT_EQUAL_INT(0,   effectiveVolume(0, 100));
  TEST_ASSERT_EQUAL_INT(0,   effectiveVolume(100, 0));
  TEST_ASSERT_EQUAL_INT(49,  effectiveVolume(99, 50));  // truncates
}

// ----------------------------------------------------------------
//  Jukebox FSM — top-level state machine
// ----------------------------------------------------------------

// Helpers for tersely building inputs and asserting on outputs.
static TickInput baseInput(uint32_t nowMs) {
  TickInput in = {};
  in.nowMs = nowMs;
  in.encoderEvent = EncEvent::None;
  in.nfcFound = false;
  in.audioPlaying = false;
  in.sleepTimerJustFired = false;
  in.powerSaveMinutes = 5;
  return in;
}

static bool hasAction(const ActionList &al, Action a) {
  for (uint8_t i = 0; i < al.count; i++)
    if (al.items[i] == a) return true;
  return false;
}

void test_fsm_initial_state_is_waiting_idle() {
  JukeboxState s = jukeboxInitialState(0);
  TEST_ASSERT_EQUAL(Mode::Waiting, (int)s.mode);
  TEST_ASSERT_FALSE(s.tagPresent);
  TEST_ASSERT_FALSE(s.sleepStopped);
  TEST_ASSERT_EQUAL_UINT8(0, s.tagAbsentCount);
}

// --- Tag arrival / playback trigger ------------------------------

void test_fsm_tag_arrival_triggers_playback() {
  JukeboxState s = jukeboxInitialState(0);
  TickInput in = baseInput(100);
  in.nfcFound = true;
  TickResult r = jukeboxStep(s, in);
  TEST_ASSERT_TRUE(hasAction(r.actions, Action::TriggerPlayback));
  TEST_ASSERT_TRUE(r.state.tagPresent);
  TEST_ASSERT_EQUAL_UINT32(100, r.state.lastActivityMs);
}

void test_fsm_no_retrigger_while_tag_present() {
  // After playback triggers, subsequent ticks with the tag still present
  // must NOT emit another TriggerPlayback.
  JukeboxState s = jukeboxInitialState(0);
  s.tagPresent = true;
  TickInput in = baseInput(200);
  in.nfcFound = true;
  TickResult r = jukeboxStep(s, in);
  TEST_ASSERT_FALSE(hasAction(r.actions, Action::TriggerPlayback));
  TEST_ASSERT_TRUE(r.state.tagPresent);
}

void test_fsm_arrival_suppressed_when_sleep_stopped() {
  // sleepStopped=true means a tag was suppressed by the sleep timer. Even
  // if (defensively) tagPresent is false here, an NFC find must not trigger.
  JukeboxState s = jukeboxInitialState(0);
  s.sleepStopped = true;
  TickInput in = baseInput(200);
  in.nfcFound = true;
  TickResult r = jukeboxStep(s, in);
  TEST_ASSERT_FALSE(hasAction(r.actions, Action::TriggerPlayback));
}

// --- Tag removal debounce ----------------------------------------

void test_fsm_tag_removal_requires_full_miss_run() {
  JukeboxState s = jukeboxInitialState(0);
  s.tagPresent = true;
  s.lastActivityMs = 0;

  for (int miss = 1; miss < TAG_ABSENT_CONFIRM; miss++) {
    TickResult r = jukeboxStep(s, baseInput(miss * 100));
    s = r.state;
    TEST_ASSERT_FALSE(hasAction(r.actions, Action::ConfirmTagRemoved));
    TEST_ASSERT_TRUE(s.tagPresent);
    TEST_ASSERT_EQUAL_UINT8(miss, s.tagAbsentCount);
  }
  // The final consecutive miss confirms removal.
  TickResult rN = jukeboxStep(s, baseInput(TAG_ABSENT_CONFIRM * 100));
  TEST_ASSERT_TRUE(hasAction(rN.actions, Action::ConfirmTagRemoved));
  TEST_ASSERT_FALSE(rN.state.tagPresent);
  TEST_ASSERT_FALSE(rN.state.sleepStopped);
  TEST_ASSERT_EQUAL_UINT8(0, rN.state.tagAbsentCount);
}

void test_fsm_removal_tolerance_covers_a_read_glitch() {
  // The miss count exists to ride out PN532 read glitches, and it is sized
  // against the ~100 ms waiting-screen poll period (NFC_POLL_MS). If those two
  // drift apart, a tag that flickers for a moment would stop playback.
  TEST_ASSERT_TRUE(TAG_ABSENT_CONFIRM * 100 >= 800);
}

void test_fsm_intermittent_glitch_does_not_remove_tag() {
  // A found read anywhere in the run resets the counter, so an intermittent
  // tag never accumulates enough consecutive misses to be called removed.
  JukeboxState s = jukeboxInitialState(0);
  s.tagPresent = true;
  TickResult r = {};
  uint32_t t = 10;

  for (int round = 0; round < 3; round++) {
    for (int miss = 0; miss < TAG_ABSENT_CONFIRM - 1; miss++, t += 10) {
      r = jukeboxStep(s, baseInput(t));
      s = r.state;
      TEST_ASSERT_FALSE(hasAction(r.actions, Action::ConfirmTagRemoved));
    }
    TickInput inFound = baseInput(t); t += 10;
    inFound.nfcFound = true;
    r = jukeboxStep(s, inFound);
    s = r.state;
    TEST_ASSERT_EQUAL_UINT8(0, s.tagAbsentCount);
  }
  TEST_ASSERT_TRUE(s.tagPresent);
}

void test_fsm_removal_clears_sleep_stopped() {
  // After sleep timer fired, tag removal should clear sleepStopped so the
  // next arrival can trigger playback normally.
  JukeboxState s = jukeboxInitialState(0);
  s.tagPresent = true;
  s.sleepStopped = true;
  s.tagAbsentCount = TAG_ABSENT_CONFIRM - 1;   // one miss short of confirmed
  TickResult r = jukeboxStep(s, baseInput(500));
  TEST_ASSERT_TRUE(hasAction(r.actions, Action::ConfirmTagRemoved));
  TEST_ASSERT_FALSE(r.state.sleepStopped);
  TEST_ASSERT_FALSE(r.state.tagPresent);
}

// --- Sleep timer interaction --------------------------------------

void test_fsm_sleep_timer_fired_sets_sleep_stopped() {
  JukeboxState s = jukeboxInitialState(0);
  s.tagPresent = true;  // tag was still on reader when timer fired
  TickInput in = baseInput(1000);
  in.sleepTimerJustFired = true;
  in.nfcFound = true;  // tag is still here this tick
  TickResult r = jukeboxStep(s, in);
  TEST_ASSERT_TRUE(r.state.sleepStopped);
  TEST_ASSERT_TRUE(r.state.tagPresent);                   // still there physically
  TEST_ASSERT_FALSE(hasAction(r.actions, Action::TriggerPlayback));  // suppressed
}

void test_fsm_sleep_timer_then_tag_stays_does_not_replay() {
  // The bug we're guarding: sleep timer fires, user leaves tag on reader.
  // Subsequent ticks must NOT re-trigger playback no matter how long the
  // tag stays.
  JukeboxState s = jukeboxInitialState(0);
  s.tagPresent = true;
  s.sleepStopped = true;
  for (int i = 0; i < 10; i++) {
    TickInput in = baseInput((uint32_t)(i * 100));
    in.nfcFound = true;
    TickResult r = jukeboxStep(s, in);
    TEST_ASSERT_FALSE(hasAction(r.actions, Action::TriggerPlayback));
    s = r.state;
  }
}

// --- Power-save sleep entry --------------------------------------

void test_fsm_idle_with_no_tag_eventually_sleeps() {
  JukeboxState s = jukeboxInitialState(0);
  s.lastActivityMs = 0;
  TickInput in = baseInput(5UL * 60000UL);  // exactly the powersave threshold
  in.powerSaveMinutes = 5;
  TickResult r = jukeboxStep(s, in);
  TEST_ASSERT_TRUE(hasAction(r.actions, Action::EnterSleep));
  TEST_ASSERT_EQUAL(Mode::Sleeping, (int)r.state.mode);
}

void test_fsm_does_not_sleep_during_audio() {
  JukeboxState s = jukeboxInitialState(0);
  s.tagPresent = true;
  s.lastActivityMs = 0;
  TickInput in = baseInput(60UL * 60000UL);  // way past any threshold
  in.audioPlaying = true;                    // …but audio is streaming
  in.powerSaveMinutes = 5;
  TickResult r = jukeboxStep(s, in);
  TEST_ASSERT_FALSE(hasAction(r.actions, Action::EnterSleep));
  TEST_ASSERT_EQUAL(Mode::Waiting, (int)r.state.mode);
}

void test_fsm_does_not_sleep_with_active_tag() {
  // tagPresent && !sleepStopped → user is actively playing a tag, don't sleep
  // even if idle (no encoder events).
  JukeboxState s = jukeboxInitialState(0);
  s.tagPresent = true;
  s.lastActivityMs = 0;
  TickInput in = baseInput(60UL * 60000UL);
  in.powerSaveMinutes = 5;
  TickResult r = jukeboxStep(s, in);
  TEST_ASSERT_FALSE(hasAction(r.actions, Action::EnterSleep));
}

void test_fsm_sleeps_with_suppressed_tag() {
  // After sleep timer fires (sleepStopped=true), the idle clock should run
  // and eventually transition into display sleep.
  JukeboxState s = jukeboxInitialState(0);
  s.tagPresent = true;
  s.sleepStopped = true;
  s.lastActivityMs = 0;
  TickInput in = baseInput(5UL * 60000UL);
  in.nfcFound = true;  // tag still on reader
  in.powerSaveMinutes = 5;
  TickResult r = jukeboxStep(s, in);
  TEST_ASSERT_TRUE(hasAction(r.actions, Action::EnterSleep));
}

void test_fsm_powersave_disabled_never_sleeps() {
  JukeboxState s = jukeboxInitialState(0);
  TickInput in = baseInput(999999UL);
  in.powerSaveMinutes = 0;
  TickResult r = jukeboxStep(s, in);
  TEST_ASSERT_FALSE(hasAction(r.actions, Action::EnterSleep));
}

// --- Wake from sleep ---------------------------------------------

void test_fsm_encoder_wakes_from_sleep() {
  JukeboxState s = jukeboxInitialState(0);
  s.mode = Mode::Sleeping;
  TickInput in = baseInput(1000);
  in.encoderEvent = EncEvent::Click;
  TickResult r = jukeboxStep(s, in);
  TEST_ASSERT_TRUE(hasAction(r.actions, Action::WakeFromSleep));
  TEST_ASSERT_EQUAL(Mode::Waiting, (int)r.state.mode);
}

void test_fsm_hold_in_sleep_only_wakes_does_not_enter_menu() {
  // ENC_HOLD while sleeping is consumed by the wake — no EnterMenu action,
  // matching the pre-FSM behavior.
  JukeboxState s = jukeboxInitialState(0);
  s.mode = Mode::Sleeping;
  TickInput in = baseInput(1000);
  in.encoderEvent = EncEvent::Hold;
  TickResult r = jukeboxStep(s, in);
  TEST_ASSERT_TRUE(hasAction(r.actions, Action::WakeFromSleep));
  TEST_ASSERT_FALSE(hasAction(r.actions, Action::EnterMenu));
}

void test_fsm_nfc_wakes_from_sleep_when_not_suppressed() {
  JukeboxState s = jukeboxInitialState(0);
  s.mode = Mode::Sleeping;
  TickInput in = baseInput(1000);
  in.nfcFound = true;
  TickResult r = jukeboxStep(s, in);
  TEST_ASSERT_TRUE(hasAction(r.actions, Action::WakeFromSleep));
}

void test_fsm_nfc_does_not_wake_when_sleep_stopped() {
  // Tag was already on the reader when sleep timer fired; we don't want it
  // to immediately wake the device after powerSave kicks in.
  JukeboxState s = jukeboxInitialState(0);
  s.mode = Mode::Sleeping;
  s.sleepStopped = true;
  s.tagPresent = true;
  TickInput in = baseInput(1000);
  in.nfcFound = true;
  TickResult r = jukeboxStep(s, in);
  TEST_ASSERT_FALSE(hasAction(r.actions, Action::WakeFromSleep));
  TEST_ASSERT_EQUAL(Mode::Sleeping, (int)r.state.mode);
}

void test_fsm_sleep_idle_no_event_stays_asleep() {
  JukeboxState s = jukeboxInitialState(0);
  s.mode = Mode::Sleeping;
  TickInput in = baseInput(1000);
  TickResult r = jukeboxStep(s, in);
  TEST_ASSERT_EQUAL_UINT8(0, r.actions.count);
  TEST_ASSERT_EQUAL(Mode::Sleeping, (int)r.state.mode);
}

// --- Menu entry --------------------------------------------------

void test_fsm_hold_enters_menu_from_waiting() {
  JukeboxState s = jukeboxInitialState(0);
  TickInput in = baseInput(500);
  in.encoderEvent = EncEvent::Hold;
  TickResult r = jukeboxStep(s, in);
  TEST_ASSERT_TRUE(hasAction(r.actions, Action::EnterMenu));
  // Mode stays Waiting in the FSM (menu is external).
  TEST_ASSERT_EQUAL(Mode::Waiting, (int)r.state.mode);
}

void test_fsm_hold_outcome_ignores_nfc_state() {
  // main.cpp skips the blocking NFC read on the tick a hold fires, so the menu
  // opens immediately instead of after a dead poll. That is only safe because
  // the Hold path returns before consulting nfcFound — pin it here so the
  // skip can't be silently invalidated by a future change to the FSM.
  for (int found = 0; found < 2; found++) {
    JukeboxState s = jukeboxInitialState(0);
    TickInput in = baseInput(1000);
    in.encoderEvent = EncEvent::Hold;
    in.nfcFound = (found != 0);

    TickResult r = jukeboxStep(s, in);
    TEST_ASSERT_TRUE(hasAction(r.actions, Action::EnterMenu));
    TEST_ASSERT_EQUAL_UINT8(1, r.actions.count);  // nothing else emitted
    // A tag arrival would otherwise have been registered here; it must not be,
    // or a skipped poll would change the outcome.
    TEST_ASSERT_FALSE(r.state.tagPresent);
    TEST_ASSERT_FALSE(hasAction(r.actions, Action::TriggerPlayback));
  }
}

void test_fsm_encoder_rotation_resets_idle_clock() {
  // Even without other actions, rotation should reset lastActivityMs so the
  // idle counter restarts.
  JukeboxState s = jukeboxInitialState(0);
  s.lastActivityMs = 0;
  TickInput in = baseInput(1000);
  in.encoderEvent = EncEvent::Rotated;
  TickResult r = jukeboxStep(s, in);
  TEST_ASSERT_EQUAL_UINT32(1000, r.state.lastActivityMs);
}

// --- Full scenario: the bug we're guarding ------------------------

void test_fsm_full_sleep_timer_recovery_scenario() {
  // 1) Idle waiting. 2) Tag arrives → playback triggers. 3) Sleep timer fires
  // mid-play (set externally via sleepTimerJustFired). 4) Tag stays on
  // reader — no re-trigger. 5) PowerSave elapses → display sleep with tag
  // still present. 6) NFC poll does NOT wake (sleepStopped). 7) User
  // presses encoder → wake. 8) User removes tag → ConfirmTagRemoved
  // (sleepStopped cleared). 9) Same tag back → playback triggers fresh.
  JukeboxState s = jukeboxInitialState(0);

  // 2) Tag arrives at t=1s.
  TickInput a = baseInput(1000); a.nfcFound = true;
  TickResult r = jukeboxStep(s, a); s = r.state;
  TEST_ASSERT_TRUE(hasAction(r.actions, Action::TriggerPlayback));

  // 3) Tag is still on reader at t=2s and sleep timer just fired.
  TickInput b = baseInput(2000); b.nfcFound = true; b.sleepTimerJustFired = true;
  r = jukeboxStep(s, b); s = r.state;
  TEST_ASSERT_TRUE(s.sleepStopped);
  TEST_ASSERT_FALSE(hasAction(r.actions, Action::TriggerPlayback));

  // 4) Tag stays — several ticks, no replay.
  for (int i = 0; i < 5; i++) {
    TickInput k = baseInput(2000 + (uint32_t)(i * 100)); k.nfcFound = true;
    r = jukeboxStep(s, k); s = r.state;
    TEST_ASSERT_FALSE(hasAction(r.actions, Action::TriggerPlayback));
  }

  // 5) Idle past powerSaveMinutes (5 min from sleepStopped reset at t=2000).
  TickInput c = baseInput(2000 + 5UL * 60000UL); c.nfcFound = true;
  c.powerSaveMinutes = 5;
  r = jukeboxStep(s, c); s = r.state;
  TEST_ASSERT_TRUE(hasAction(r.actions, Action::EnterSleep));
  TEST_ASSERT_EQUAL(Mode::Sleeping, (int)s.mode);

  // 6) NFC poll while suppressed-asleep does NOT wake.
  TickInput d = baseInput(2000 + 5UL * 60000UL + 1000); d.nfcFound = true;
  r = jukeboxStep(s, d); s = r.state;
  TEST_ASSERT_FALSE(hasAction(r.actions, Action::WakeFromSleep));
  TEST_ASSERT_EQUAL(Mode::Sleeping, (int)s.mode);

  // 7) Encoder press wakes.
  TickInput e = baseInput(2000 + 5UL * 60000UL + 2000);
  e.encoderEvent = EncEvent::Click;
  r = jukeboxStep(s, e); s = r.state;
  TEST_ASSERT_TRUE(hasAction(r.actions, Action::WakeFromSleep));
  TEST_ASSERT_EQUAL(Mode::Waiting, (int)s.mode);
  TEST_ASSERT_TRUE(s.sleepStopped);  // still suppressed until removal

  // 8) Tag removed for a full run of consecutive misses.
  for (int i = 0; i < TAG_ABSENT_CONFIRM; i++) {
    TickInput f = baseInput(2000 + 5UL * 60000UL + 3000 + (uint32_t)(i * 100));
    r = jukeboxStep(s, f); s = r.state;
  }
  TEST_ASSERT_FALSE(s.sleepStopped);
  TEST_ASSERT_FALSE(s.tagPresent);

  // 9) Same tag returns → fresh playback trigger.
  TickInput g = baseInput(2000 + 5UL * 60000UL + 4000); g.nfcFound = true;
  r = jukeboxStep(s, g); s = r.state;
  TEST_ASSERT_TRUE(hasAction(r.actions, Action::TriggerPlayback));
}

// ----------------------------------------------------------------
//  rgb565hex — 0xRRGGBB (24-bit) -> RGB565 (5-6-5)
// ----------------------------------------------------------------

void test_rgb565_primaries_and_extremes() {
  TEST_ASSERT_EQUAL_HEX16(0x0000, rgb565hex(0x000000));
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, rgb565hex(0xFFFFFF));
  TEST_ASSERT_EQUAL_HEX16(0xF800, rgb565hex(0xFF0000));  // red   -> top 5 bits
  TEST_ASSERT_EQUAL_HEX16(0x07E0, rgb565hex(0x00FF00));  // green -> mid 6 bits
  TEST_ASSERT_EQUAL_HEX16(0x001F, rgb565hex(0x0000FF));  // blue  -> low 5 bits
}

void test_rgb565_truncates_low_bits() {
  // Channels are truncated (not rounded): R keeps 5 MSB, G 6 MSB, B 5 MSB.
  TEST_ASSERT_EQUAL_HEX16(0x8E2D, rgb565hex(0x8FC46B));  // Bamboo Moss accent
}

// ----------------------------------------------------------------
//  Animation helpers — fixed-point progress, easing, interpolation
// ----------------------------------------------------------------

void test_anim_progress_spans_zero_to_full() {
  TEST_ASSERT_EQUAL_INT32(0,          animProgress(0, 100));
  TEST_ASSERT_EQUAL_INT32(500,        animProgress(50, 100));
  TEST_ASSERT_EQUAL_INT32(ANIM_SCALE, animProgress(100, 100));
}

void test_anim_progress_clamps_past_duration() {
  // A frame that lands late must not overshoot — callers use the result
  // directly as an interpolation factor.
  TEST_ASSERT_EQUAL_INT32(ANIM_SCALE, animProgress(150, 100));
  TEST_ASSERT_EQUAL_INT32(ANIM_SCALE, animProgress(0xFFFFFFFFUL, 100));
}

void test_anim_progress_zero_duration_is_complete() {
  // Zero duration means "no animation" — settle immediately rather than
  // dividing by zero.
  TEST_ASSERT_EQUAL_INT32(ANIM_SCALE, animProgress(0, 0));
}

void test_ease_out_cubic_endpoints_are_exact() {
  // Endpoints must be exact or the animation visibly snaps at the end.
  TEST_ASSERT_EQUAL_INT32(0,          easeOutCubic(0));
  TEST_ASSERT_EQUAL_INT32(ANIM_SCALE, easeOutCubic(ANIM_SCALE));
  TEST_ASSERT_EQUAL_INT32(0,          easeOutCubic(-50));
  TEST_ASSERT_EQUAL_INT32(ANIM_SCALE, easeOutCubic(ANIM_SCALE + 50));
}

void test_ease_out_cubic_front_loads_the_motion() {
  // 1-(1-t)^3: at the halfway point most of the distance is already covered.
  // That front-loading is what makes the motion read as "snappy".
  TEST_ASSERT_EQUAL_INT32(875, easeOutCubic(500));
  TEST_ASSERT_EQUAL_INT32(271, easeOutCubic(100));
  TEST_ASSERT_TRUE(easeOutCubic(500) > 500);
}

void test_ease_out_cubic_is_monotonic() {
  // A non-monotonic curve would make the highlight jitter backwards.
  int32_t prev = -1;
  for (int32_t q = 0; q <= ANIM_SCALE; q += 10) {
    int32_t v = easeOutCubic(q);
    TEST_ASSERT_TRUE(v >= prev);
    prev = v;
  }
}

void test_anim_lerp_endpoints_and_midpoint() {
  TEST_ASSERT_EQUAL_INT32(0,   animLerp(0, 100, 0));
  TEST_ASSERT_EQUAL_INT32(100, animLerp(0, 100, ANIM_SCALE));
  TEST_ASSERT_EQUAL_INT32(50,  animLerp(0, 100, 500));
  // Menu rows: item 0 (y=44) to item 8 (y=268).
  TEST_ASSERT_EQUAL_INT32(156, animLerp(44, 268, 500));
}

void test_anim_lerp_handles_descending_range() {
  // Scrolling up means to < from; the delta must stay signed.
  TEST_ASSERT_EQUAL_INT32(50,  animLerp(100, 0, 500));
  TEST_ASSERT_EQUAL_INT32(156, animLerp(268, 44, 500));
  TEST_ASSERT_EQUAL_INT32(-25, animLerp(-50, 0, 500));
}

void test_anim_value_interpolates_then_settles() {
  AnimI32 a;
  animStart(a, 44, 268, 1000, 130);   // menu row 0 -> row 8 over 130 ms
  TEST_ASSERT_EQUAL_INT32(44,  animValue(a, 1000));
  TEST_ASSERT_EQUAL_INT32(240, animValue(a, 1065));   // eased, not linear (156)
  TEST_ASSERT_EQUAL_INT32(268, animValue(a, 1130));
  TEST_ASSERT_EQUAL_INT32(268, animValue(a, 5000));   // stays put afterwards
}

void test_anim_done_reports_completion() {
  AnimI32 a;
  animStart(a, 0, 100, 1000, 130);
  TEST_ASSERT_FALSE(animDone(a, 1000));
  TEST_ASSERT_FALSE(animDone(a, 1129));
  TEST_ASSERT_TRUE(animDone(a, 1130));
  TEST_ASSERT_TRUE(animDone(a, 9999));
}

void test_anim_retarget_mid_flight_starts_from_current_value() {
  // A fast encoder spin retargets while the previous move is still running.
  // Restarting from the *current* value is what keeps the motion continuous —
  // restarting from the old row endpoint would visibly jump backwards.
  AnimI32 a;
  animStart(a, 44, 72, 1000, 130);
  int32_t mid = animValue(a, 1065);
  TEST_ASSERT_EQUAL_INT32(68, mid);           // 44 + 28*0.875

  animStart(a, mid, 100, 1065, 130);
  TEST_ASSERT_EQUAL_INT32(68,  animValue(a, 1065));   // no discontinuity
  TEST_ASSERT_EQUAL_INT32(100, animValue(a, 1195));
}

void test_anim_survives_millis_wrap() {
  // Elapsed time uses unsigned subtraction, so an animation started just
  // before the 49-day millis() rollover keeps running across it.
  const uint32_t nearMax = 0xFFFFFFFFUL - 49;   // 50 ms before wrap
  AnimI32 a;
  animStart(a, 0, 200, nearMax, 200);
  TEST_ASSERT_EQUAL_INT32(0, animValue(a, nearMax));
  // 50 ms later the counter has wrapped to 50 → 100 ms elapsed → q=500.
  TEST_ASSERT_EQUAL_INT32(175, animValue(a, 50));    // 200 * 0.875
  TEST_ASSERT_FALSE(animDone(a, 50));
  TEST_ASSERT_TRUE(animDone(a, 150));
}

void test_anim_settle_is_an_idle_animation() {
  AnimI32 a;
  animSettle(a, 156, 5000);
  TEST_ASSERT_EQUAL_INT32(156, animValue(a, 5000));
  TEST_ASSERT_EQUAL_INT32(156, animValue(a, 6000));
  TEST_ASSERT_TRUE(animDone(a, 5000));
}

// ----------------------------------------------------------------
//  Tag-presence policy — miss counts derived from poll cadence
// ----------------------------------------------------------------

void test_tag_absent_misses_never_accepts_a_single_miss() {
  // The whole point of this helper. A tag placed quickly skims the edge of
  // the reader's field and can miss a read while it settles; if one miss
  // could confirm removal, playback would stop and instantly restart.
  TEST_ASSERT_GREATER_OR_EQUAL_UINT8(2, tagAbsentMisses(450, 150));
  TEST_ASSERT_GREATER_OR_EQUAL_UINT8(2, tagAbsentMisses(900, 100));
  // Even when the poll period alone already exceeds the window.
  TEST_ASSERT_EQUAL_UINT8(2, tagAbsentMisses(50, 500));
  TEST_ASSERT_EQUAL_UINT8(2, tagAbsentMisses(0, 100));
}

void test_tag_absent_misses_covers_the_confirm_window() {
  // count * period must reach the requested duration, or a tag would be
  // called removed sooner than the policy says.
  const uint32_t cases[][2] = {
    {900, 100}, {450, 150}, {450, 50}, {900, 230}, {600, 70},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    uint32_t confirmMs = cases[i][0], period = cases[i][1];
    uint32_t covered = (uint32_t)tagAbsentMisses(confirmMs, period) * period;
    TEST_ASSERT_TRUE(covered >= confirmMs);
  }
}

void test_tag_absent_misses_rounds_up_not_down() {
  // Truncating would under-cover the window by up to one whole poll period.
  TEST_ASSERT_EQUAL_UINT8(9, tagAbsentMisses(900, 100));   // exact
  TEST_ASSERT_EQUAL_UINT8(3, tagAbsentMisses(450, 150));   // exact
  TEST_ASSERT_EQUAL_UINT8(5, tagAbsentMisses(900, 200));   // 4.5 -> 5
  TEST_ASSERT_EQUAL_UINT8(4, tagAbsentMisses(900, 230));   // 3.9 -> 4
}

void test_tag_absent_misses_saturates_at_uint8() {
  TEST_ASSERT_EQUAL_UINT8(255, tagAbsentMisses(1000000, 1));
}

void test_tag_presence_periods_stay_within_their_windows() {
  // The FSM stores tagAbsentCount as a uint8_t, so every configured pairing
  // must fit — this is what stops a future cadence change from silently
  // wrapping the counter and never confirming a removal.
  TEST_ASSERT_TRUE(tagAbsentMisses(TAG_ABSENT_IDLE_MS,    NFC_POLL_MS)          < 255);
  TEST_ASSERT_TRUE(tagAbsentMisses(TAG_ABSENT_PLAYING_MS, NFC_PLAYBACK_POLL_MS) < 255);
  TEST_ASSERT_TRUE(tagAbsentMisses(TAG_ABSENT_PLAYING_MS, NFC_REPLAY_POLL_MS)   < 255);

  // Pin what the shipped configuration actually resolves to. The idle and
  // playback counts match the values these sites used before the policy was
  // consolidated, so this change is behaviour-preserving there; the replay
  // re-poll was previously a single read, which is the bug being fixed.
  TEST_ASSERT_EQUAL_UINT8(9, tagAbsentMisses(TAG_ABSENT_IDLE_MS,    NFC_POLL_MS));
  TEST_ASSERT_EQUAL_UINT8(3, tagAbsentMisses(TAG_ABSENT_PLAYING_MS, NFC_PLAYBACK_POLL_MS));
  TEST_ASSERT_EQUAL_UINT8(9, tagAbsentMisses(TAG_ABSENT_PLAYING_MS, NFC_REPLAY_POLL_MS));
  // Removal while audio is playing should be confirmed sooner than while
  // idle — the sound keeps going until we accept it.
  TEST_ASSERT_TRUE(TAG_ABSENT_PLAYING_MS < TAG_ABSENT_IDLE_MS);
}

void test_playback_ignores_misses_while_the_tag_is_landing() {
  // Observed on hardware: one detection, then playWav() stopping on its own
  // miss run and the track restarting from zero before the tag came to rest.
  uint8_t misses = 0; bool confirmed = false;
  for (uint32_t t = 0; t < TAG_SETTLE_MS; t += NFC_PLAYBACK_POLL_MS)
    TEST_ASSERT_FALSE(tagMissTick(misses, confirmed, false, t));
  TEST_ASSERT_FALSE(confirmed);
}

void test_early_misses_are_discarded_not_banked() {
  // The regression that made the first attempt at this only *reduce* the
  // restart rate: misses accumulated during the landing window, so the
  // counter was already at the threshold when the window closed and the very
  // next miss stopped playback. After a long unreadable run the tag must
  // still get a full fresh debounce.
  const uint8_t need = tagAbsentMisses(TAG_ABSENT_PLAYING_MS, NFC_PLAYBACK_POLL_MS);
  uint8_t misses = 0; bool confirmed = false;

  for (uint32_t t = 0; t < TAG_SETTLE_MS; t += 10)
    tagMissTick(misses, confirmed, false, t);      // long unreadable run
  TEST_ASSERT_EQUAL_UINT8(0, misses);              // banked nothing

  tagMissTick(misses, confirmed, true, TAG_SETTLE_MS);  // tag lands
  for (uint8_t i = 1; i < need; i++)
    TEST_ASSERT_FALSE(tagMissTick(misses, confirmed, false, TAG_SETTLE_MS + i));
  TEST_ASSERT_TRUE(tagMissTick(misses, confirmed, false, TAG_SETTLE_MS + need));
}

void test_playback_debounces_normally_once_the_tag_is_seen() {
  // After the first successful read, removal must be responsive again — the
  // landing grace does not linger for the rest of the track.
  const uint8_t need = tagAbsentMisses(TAG_ABSENT_PLAYING_MS, NFC_PLAYBACK_POLL_MS);
  uint8_t misses = 0; bool confirmed = false;

  tagMissTick(misses, confirmed, true, 10);        // seen almost immediately
  TEST_ASSERT_TRUE(confirmed);
  for (uint8_t i = 1; i < need; i++)
    TEST_ASSERT_FALSE(tagMissTick(misses, confirmed, false, 10 + i));
  TEST_ASSERT_TRUE(tagMissTick(misses, confirmed, false, 10 + need));
}

void test_playback_stops_for_a_tag_that_never_lands() {
  // A tag waved past the reader is detected once but never read again. The
  // TAG_SETTLE_MS backstop has to stop playback rather than wait forever.
  uint8_t misses = 0; bool confirmed = false;
  bool stopped = false;
  for (uint32_t t = 0; t < TAG_SETTLE_MS + TAG_ABSENT_PLAYING_MS + 1000;
       t += NFC_PLAYBACK_POLL_MS)
    if (tagMissTick(misses, confirmed, false, t)) { stopped = true; break; }
  TEST_ASSERT_TRUE(stopped);
}

void test_a_single_miss_never_stops_playback() {
  uint8_t misses = 0; bool confirmed = true;   // already established
  TEST_ASSERT_FALSE(tagMissTick(misses, confirmed, false, 999999));
}

void test_intermittent_reads_never_accumulate_to_a_stop() {
  // miss, seen, miss, seen ... a flaky-but-present tag must keep playing.
  uint8_t misses = 0; bool confirmed = false;
  for (uint32_t t = 0; t < 60000; t += NFC_PLAYBACK_POLL_MS) {
    TEST_ASSERT_FALSE(tagMissTick(misses, confirmed, false, t));
    TEST_ASSERT_FALSE(tagMissTick(misses, confirmed, true,  t + 1));
  }
}

void test_nfc_read_budget_fits_the_dma_ring_at_every_rate() {
  // The read blocks the streaming loop, so it must fit inside the audio
  // already queued. The ring holds a fixed frame count, so its duration
  // shrinks as the rate rises — at 192 kHz it is only ~43 ms, less than the
  // 50 ms nominal read, and a fixed timeout would underrun on every poll.
  const uint32_t rates[] = {8000, 22050, 44100, 48000, 96000, 176400, 192000};
  for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
    uint32_t ringMs = (8UL * 1024UL * 1000UL) / rates[i];
    uint32_t budget = nfcReadBudgetMs(rates[i], NFC_RING_USE_PCT);
    TEST_ASSERT_TRUE(budget <= NFC_PLAYBACK_READ_MS);   // never exceeds nominal
    TEST_ASSERT_TRUE(budget < ringMs);                  // leaves refill headroom
    TEST_ASSERT_TRUE(budget >= 1);                      // always makes progress
  }
}

void test_nfc_read_budget_is_unchanged_at_normal_rates() {
  // Everything the web UI produces is 44.1 kHz, and the whole point of the
  // widening was to reach 50 ms there — the cap must not claw that back.
  TEST_ASSERT_EQUAL_UINT32(NFC_PLAYBACK_READ_MS, nfcReadBudgetMs(44100, NFC_RING_USE_PCT));
  TEST_ASSERT_EQUAL_UINT32(NFC_PLAYBACK_READ_MS, nfcReadBudgetMs(48000, NFC_RING_USE_PCT));
  TEST_ASSERT_EQUAL_UINT32(NFC_PLAYBACK_READ_MS, nfcReadBudgetMs(96000, NFC_RING_USE_PCT));
  // ...and must claw it back where the ring is genuinely too small.
  TEST_ASSERT_TRUE(nfcReadBudgetMs(192000, NFC_RING_USE_PCT) < NFC_PLAYBACK_READ_MS);
  TEST_ASSERT_TRUE(nfcReadBudgetMs(176400, NFC_RING_USE_PCT) < NFC_PLAYBACK_READ_MS);
}

void test_nfc_read_budget_handles_a_bogus_sample_rate() {
  // A corrupt header could report 0; don't divide by it.
  TEST_ASSERT_EQUAL_UINT32(NFC_PLAYBACK_READ_MS, nfcReadBudgetMs(0, NFC_RING_USE_PCT));
}

void test_settle_backstop_is_not_absurdly_long() {
  // It only delays a genuine stop; audio must not run on for ages after a
  // tag is lifted before it was ever read.
  TEST_ASSERT_TRUE(TAG_SETTLE_MS <= 2000);
  // And each in-playback read must fit comfortably inside its own interval.
  TEST_ASSERT_TRUE(NFC_PLAYBACK_READ_MS < NFC_PLAYBACK_POLL_MS);
}

// --- Long-press indicator ------------------------------------------

void test_hold_progress_hidden_until_hint_delay() {
  // A normal click must never flash the indicator, so anything shorter than
  // the hint delay reports "hidden" rather than 0%.
  TEST_ASSERT_EQUAL_INT(-1, holdProgressPct(0, 200, 600));
  TEST_ASSERT_EQUAL_INT(-1, holdProgressPct(199, 200, 600));
  TEST_ASSERT_EQUAL_INT(0,  holdProgressPct(200, 200, 600));
}

void test_hold_progress_is_linear_to_the_threshold() {
  // Linear, not eased: the bar reports how much longer the user must hold.
  TEST_ASSERT_EQUAL_INT(25,  holdProgressPct(300, 200, 600));
  TEST_ASSERT_EQUAL_INT(50,  holdProgressPct(400, 200, 600));
  TEST_ASSERT_EQUAL_INT(75,  holdProgressPct(500, 200, 600));
  TEST_ASSERT_EQUAL_INT(100, holdProgressPct(600, 200, 600));
}

void test_hold_progress_saturates_past_threshold() {
  TEST_ASSERT_EQUAL_INT(100, holdProgressPct(5000, 200, 600));
}

void test_hold_progress_survives_degenerate_config() {
  // hintDelay >= holdMs would divide by zero; once the delay is reached there
  // is no span left to ramp over, so report complete instead.
  TEST_ASSERT_EQUAL_INT(100, holdProgressPct(600, 600, 600));
  TEST_ASSERT_EQUAL_INT(100, holdProgressPct(800, 800, 600));
  // The hint delay still gates: below it, nothing is shown either way.
  TEST_ASSERT_EQUAL_INT(-1, holdProgressPct(500, 600, 600));
  TEST_ASSERT_EQUAL_INT(-1, holdProgressPct(700, 800, 600));
}

// ----------------------------------------------------------------
//  QR code — placement maths and the encoder's real limits
// ----------------------------------------------------------------

// Mirrors the constants in screen.cpp.
static const uint8_t TEST_QR_VERSION  = 4;
static const int     TEST_QR_MODULES  = 33;
static const size_t  TEST_QR_CAPACITY = 62;

// NOTE: do not probe capacity by feeding the encoder ever-longer strings.
// qrcode_initBytes() writes the codewords without ever checking them against
// the symbol's capacity, so over-long input overruns the buffer and crashes
// rather than returning an error. An earlier version of this test did exactly
// that and segfaulted. Everything below stays at or under capacity.

void test_qr_capacity_is_the_medium_ecc_figure() {
  // A string exactly at capacity must encode cleanly. This pins the constant
  // that screen.cpp's static_assert relies on — and that assert is the only
  // guard against the overflow described above, so it has to be right.
  //
  // 62 (not 78) because the library's ECC_* constants are off by one against
  // its own table: ECC_LOW indexes the Medium codeword counts, so we get
  // Medium correction and Medium capacity.
  QRCode qr;
  uint8_t buf[qrcode_getBufferSize(TEST_QR_VERSION)];
  char s[TEST_QR_CAPACITY + 1];
  memset(s, 'a', TEST_QR_CAPACITY);
  s[TEST_QR_CAPACITY] = '\0';

  TEST_ASSERT_EQUAL_INT(0, qrcode_initText(&qr, buf, TEST_QR_VERSION, ECC_LOW, s));
  TEST_ASSERT_EQUAL_INT(TEST_QR_MODULES, qr.size);
}

void test_release_url_fits_with_headroom() {
  // The shipped URL must not sit right on the limit: at capacity, adding a
  // character to the org or repo name would overflow rather than fail loudly.
  const char *url = "https://github.com/qxzzxq/TinyJuke/releases/latest";
  TEST_ASSERT_TRUE(strlen(url) < TEST_QR_CAPACITY);
  TEST_ASSERT_TRUE(TEST_QR_CAPACITY - strlen(url) >= 8);   // room to rename

  QRCode qr;
  uint8_t buf[qrcode_getBufferSize(TEST_QR_VERSION)];
  TEST_ASSERT_EQUAL_INT(0, qrcode_initText(&qr, buf, TEST_QR_VERSION, ECC_LOW, url));
}

void test_project_url_fits_with_headroom() {
  // Page 2 of the About screen encodes the repo root. Same overflow hazard as
  // RELEASE_URL: initBytes() would write past the buffer rather than fail.
  const char *url = "https://github.com/qxzzxq/TinyJuke";
  TEST_ASSERT_TRUE(strlen(url) < TEST_QR_CAPACITY);
  TEST_ASSERT_TRUE(TEST_QR_CAPACITY - strlen(url) >= 8);   // room to rename

  QRCode qr;
  uint8_t buf[qrcode_getBufferSize(TEST_QR_VERSION)];
  TEST_ASSERT_EQUAL_INT(0, qrcode_initText(&qr, buf, TEST_QR_VERSION, ECC_LOW, url));
}

void test_about_page_qr_bands_both_land_on_scale_four() {
  // Both About pages must render the symbol at the same 4 px/module scale, and
  // the page-dot row added at y=289 left very little slack: a band one pixel
  // short drops to scale 3, shrinking the QR by a quarter on one page only.
  // These are the literal bands passed by drawAboutVersionPage() and
  // drawAboutProjectPage().
  const int W = 240, CAPTION_Y = 272, DOTS_TOP = 289 - 3;

  const QrPlacement page1 = qrPlace(33, 0, 96, W, 172);
  const QrPlacement page2 = qrPlace(33, 0, 104, W, 164);

  TEST_ASSERT_EQUAL_INT(4, page1.scale);
  TEST_ASSERT_EQUAL_INT(4, page2.scale);
  TEST_ASSERT_EQUAL_INT(page1.size, page2.size);   // same symbol size on both
  TEST_ASSERT_EQUAL_INT(page1.x, page2.x);         // and the same left edge

  // Neither symbol may reach the caption line below it, and the caption in turn
  // must clear the dots — otherwise the QR's white patch eats the text.
  TEST_ASSERT_TRUE(page1.y + page1.size <= CAPTION_Y);
  TEST_ASSERT_TRUE(page2.y + page2.size <= CAPTION_Y);
  TEST_ASSERT_TRUE(CAPTION_Y + 8 <= DOTS_TOP);     // size-1 text is 8 px tall
}

void test_qr_symbol_size_matches_the_version_formula() {
  // QR_MODULES in screen.cpp is computed as 4*version+17; the drawing code
  // sizes the whole layout from it, so a mismatch would misplace every module.
  QRCode qr;
  uint8_t buf[qrcode_getBufferSize(TEST_QR_VERSION)];
  TEST_ASSERT_EQUAL_INT(0, qrcode_initText(&qr, buf, TEST_QR_VERSION, ECC_LOW,
                                           "https://example.com/releases/latest"));
  TEST_ASSERT_EQUAL_INT(TEST_QR_MODULES, qr.size);
  TEST_ASSERT_EQUAL_INT(TEST_QR_MODULES, 4 * TEST_QR_VERSION + 17);
}

void test_qr_has_finder_patterns_in_three_corners() {
  // Structural sanity that we produced a real symbol rather than an empty or
  // corrupted buffer: a finder is a solid 7x7 dark ring with a light gap.
  QRCode qr;
  uint8_t buf[qrcode_getBufferSize(TEST_QR_VERSION)];
  TEST_ASSERT_EQUAL_INT(0, qrcode_initText(&qr, buf, TEST_QR_VERSION, ECC_LOW,
                                           "https://example.com/releases/latest"));
  const int last = TEST_QR_MODULES - 7;
  const int corners[3][2] = {{0, 0}, {last, 0}, {0, last}};
  for (int c = 0; c < 3; c++) {
    int ox = corners[c][0], oy = corners[c][1];
    for (int i = 0; i < 7; i++) {
      TEST_ASSERT_TRUE(qrcode_getModule(&qr, ox + i, oy));         // top edge
      TEST_ASSERT_TRUE(qrcode_getModule(&qr, ox + i, oy + 6));     // bottom edge
      TEST_ASSERT_TRUE(qrcode_getModule(&qr, ox, oy + i));         // left edge
      TEST_ASSERT_TRUE(qrcode_getModule(&qr, ox + 6, oy + i));     // right edge
    }
    TEST_ASSERT_FALSE(qrcode_getModule(&qr, ox + 1, oy + 1));      // light gap
    TEST_ASSERT_TRUE(qrcode_getModule(&qr, ox + 3, oy + 3));       // dark core
  }
}

void test_qr_place_centres_and_reserves_the_quiet_zone() {
  // The quiet zone is not decoration — without it many scanners never lock on,
  // so it must be inside the drawn (light) square, not borrowed from the
  // surrounding dark UI.
  QrPlacement p = qrPlace(33, 0, 96, 240, 168);
  const int total = 33 + 2 * QR_QUIET_MODULES;      // 41

  TEST_ASSERT_EQUAL_INT(168 / total, p.scale);      // 4 px per module
  TEST_ASSERT_EQUAL_INT(total * p.scale, p.size);
  TEST_ASSERT_EQUAL_INT((240 - p.size) / 2, p.x);   // centred horizontally
  TEST_ASSERT_EQUAL_INT(96 + (168 - p.size) / 2, p.y);
  // Modules start one quiet zone in on both axes.
  TEST_ASSERT_EQUAL_INT(p.x + QR_QUIET_MODULES * p.scale, p.originX);
  TEST_ASSERT_EQUAL_INT(p.y + QR_QUIET_MODULES * p.scale, p.originY);
  // And the symbol plus both quiet zones stays inside the box.
  TEST_ASSERT_TRUE(p.x >= 0 && p.x + p.size <= 240);
  TEST_ASSERT_TRUE(p.y >= 96 && p.y + p.size <= 96 + 168);
}

void test_qr_place_uses_whole_pixel_modules() {
  // A fractional scale would make some modules a pixel wider than others and
  // smear the edges scanners measure against.
  for (int box = 60; box <= 240; box += 7) {
    QrPlacement p = qrPlace(33, 0, 0, box, box);
    if (p.scale < 1) continue;
    TEST_ASSERT_EQUAL_INT(p.size, p.scale * (33 + 2 * QR_QUIET_MODULES));
    TEST_ASSERT_TRUE(p.size <= box);
  }
}

void test_qr_place_reports_when_it_cannot_fit() {
  // Too small to render legibly — the caller draws nothing rather than a
  // scrambled half-symbol.
  TEST_ASSERT_EQUAL_INT(0, qrPlace(33, 0, 0, 40, 40).scale);
  TEST_ASSERT_EQUAL_INT(0, qrPlace(0,  0, 0, 240, 240).scale);
  TEST_ASSERT_EQUAL_INT(0, qrPlace(-1, 0, 0, 240, 240).scale);
}

// ----------------------------------------------------------------
//  Unity entry point
// ----------------------------------------------------------------

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();

  RUN_TEST(test_rgb565_primaries_and_extremes);
  RUN_TEST(test_rgb565_truncates_low_bits);

  RUN_TEST(test_uid_to_str_simple);
  RUN_TEST(test_uid_to_str_pads_low_nibble);
  RUN_TEST(test_uid_to_str_seven_bytes);
  RUN_TEST(test_uid_to_str_single_byte);

  RUN_TEST(test_lookup_tag_hit);
  RUN_TEST(test_lookup_tag_miss);
  RUN_TEST(test_lookup_tag_optional_fields_missing);

  RUN_TEST(test_wav_header_mono_16bit);
  RUN_TEST(test_wav_header_stereo_24bit);
  RUN_TEST(test_wav_header_rejects_bad_magic);
  RUN_TEST(test_wav_header_rejects_truncated);
  RUN_TEST(test_wav_header_skips_unknown_chunk_before_data);
  RUN_TEST(test_wav_header_rejects_missing_data_chunk);

  RUN_TEST(test_wav_meta_inam_iart);
  RUN_TEST(test_wav_meta_no_list_returns_empty);
  RUN_TEST(test_wav_meta_rejects_non_riff);

  RUN_TEST(test_canon_build_layout);
  RUN_TEST(test_canon_build_truncates_long_strings);
  RUN_TEST(test_canon_write_field_pads_with_nul);
  RUN_TEST(test_canon_find_positive);
  RUN_TEST(test_canon_find_rejects_noncanonical_size);
  RUN_TEST(test_canon_find_rejects_list_after_data);
  RUN_TEST(test_canon_field_offsets);
  RUN_TEST(test_canon_roundtrip_firmware_parser);
  RUN_TEST(test_wav_duration_basic);
  RUN_TEST(test_wav_duration_zero_rate_is_zero);

  RUN_TEST(test_gray_step_no_change);
  RUN_TEST(test_gray_step_one_cw_cycle);
  RUN_TEST(test_gray_step_one_ccw_cycle);
  RUN_TEST(test_gray_step_invalid_transition);

  RUN_TEST(test_value_to_index_hit);
  RUN_TEST(test_value_to_index_miss_returns_zero);
  RUN_TEST(test_index_to_value_clamps);

  RUN_TEST(test_sleep_timer_disabled_never_fires);
  RUN_TEST(test_sleep_timer_fires_at_boundary);
  RUN_TEST(test_sleep_timer_fires_at_max_supported_value);
  RUN_TEST(test_power_save_disabled_never_sleeps);
  RUN_TEST(test_power_save_fires_at_boundary);
  RUN_TEST(test_timer_predicates_handle_unsigned_subtraction_wrap);

  RUN_TEST(test_remaining_disabled_is_zero);
  RUN_TEST(test_remaining_full_at_start);
  RUN_TEST(test_remaining_counts_down);
  RUN_TEST(test_remaining_clamps_to_zero_after_fire);

  RUN_TEST(test_format_countdown_zero);
  RUN_TEST(test_format_countdown_truncates_subsecond);
  RUN_TEST(test_format_countdown_one_minute);
  RUN_TEST(test_format_countdown_minute_and_seconds);
  RUN_TEST(test_format_countdown_two_hour_max);

  RUN_TEST(test_volume_adjust_normal_steps);
  RUN_TEST(test_volume_adjust_params_are_independent);
  RUN_TEST(test_volume_adjust_clamps_both_params_at_bounds);
  RUN_TEST(test_effective_volume_scales_by_max);

  RUN_TEST(test_fsm_initial_state_is_waiting_idle);
  RUN_TEST(test_fsm_tag_arrival_triggers_playback);
  RUN_TEST(test_fsm_no_retrigger_while_tag_present);
  RUN_TEST(test_fsm_arrival_suppressed_when_sleep_stopped);
  RUN_TEST(test_fsm_tag_removal_requires_full_miss_run);
  RUN_TEST(test_fsm_removal_tolerance_covers_a_read_glitch);
  RUN_TEST(test_fsm_intermittent_glitch_does_not_remove_tag);
  RUN_TEST(test_fsm_removal_clears_sleep_stopped);
  RUN_TEST(test_fsm_sleep_timer_fired_sets_sleep_stopped);
  RUN_TEST(test_fsm_sleep_timer_then_tag_stays_does_not_replay);
  RUN_TEST(test_fsm_idle_with_no_tag_eventually_sleeps);
  RUN_TEST(test_fsm_does_not_sleep_during_audio);
  RUN_TEST(test_fsm_does_not_sleep_with_active_tag);
  RUN_TEST(test_fsm_sleeps_with_suppressed_tag);
  RUN_TEST(test_fsm_powersave_disabled_never_sleeps);
  RUN_TEST(test_fsm_encoder_wakes_from_sleep);
  RUN_TEST(test_fsm_hold_in_sleep_only_wakes_does_not_enter_menu);
  RUN_TEST(test_fsm_nfc_wakes_from_sleep_when_not_suppressed);
  RUN_TEST(test_fsm_nfc_does_not_wake_when_sleep_stopped);
  RUN_TEST(test_fsm_sleep_idle_no_event_stays_asleep);
  RUN_TEST(test_fsm_hold_enters_menu_from_waiting);
  RUN_TEST(test_fsm_hold_outcome_ignores_nfc_state);
  RUN_TEST(test_fsm_encoder_rotation_resets_idle_clock);
  RUN_TEST(test_fsm_full_sleep_timer_recovery_scenario);

  RUN_TEST(test_anim_progress_spans_zero_to_full);
  RUN_TEST(test_anim_progress_clamps_past_duration);
  RUN_TEST(test_anim_progress_zero_duration_is_complete);
  RUN_TEST(test_ease_out_cubic_endpoints_are_exact);
  RUN_TEST(test_ease_out_cubic_front_loads_the_motion);
  RUN_TEST(test_ease_out_cubic_is_monotonic);
  RUN_TEST(test_anim_lerp_endpoints_and_midpoint);
  RUN_TEST(test_anim_lerp_handles_descending_range);
  RUN_TEST(test_anim_value_interpolates_then_settles);
  RUN_TEST(test_anim_done_reports_completion);
  RUN_TEST(test_anim_retarget_mid_flight_starts_from_current_value);
  RUN_TEST(test_anim_survives_millis_wrap);
  RUN_TEST(test_anim_settle_is_an_idle_animation);

  RUN_TEST(test_tag_absent_misses_never_accepts_a_single_miss);
  RUN_TEST(test_tag_absent_misses_covers_the_confirm_window);
  RUN_TEST(test_tag_absent_misses_rounds_up_not_down);
  RUN_TEST(test_tag_absent_misses_saturates_at_uint8);
  RUN_TEST(test_tag_presence_periods_stay_within_their_windows);
  RUN_TEST(test_playback_ignores_misses_while_the_tag_is_landing);
  RUN_TEST(test_early_misses_are_discarded_not_banked);
  RUN_TEST(test_playback_debounces_normally_once_the_tag_is_seen);
  RUN_TEST(test_playback_stops_for_a_tag_that_never_lands);
  RUN_TEST(test_a_single_miss_never_stops_playback);
  RUN_TEST(test_intermittent_reads_never_accumulate_to_a_stop);
  RUN_TEST(test_nfc_read_budget_fits_the_dma_ring_at_every_rate);
  RUN_TEST(test_nfc_read_budget_is_unchanged_at_normal_rates);
  RUN_TEST(test_nfc_read_budget_handles_a_bogus_sample_rate);
  RUN_TEST(test_settle_backstop_is_not_absurdly_long);

  RUN_TEST(test_hold_progress_hidden_until_hint_delay);
  RUN_TEST(test_hold_progress_is_linear_to_the_threshold);
  RUN_TEST(test_hold_progress_saturates_past_threshold);
  RUN_TEST(test_hold_progress_survives_degenerate_config);

  RUN_TEST(test_qr_capacity_is_the_medium_ecc_figure);
  RUN_TEST(test_release_url_fits_with_headroom);
  RUN_TEST(test_project_url_fits_with_headroom);
  RUN_TEST(test_about_page_qr_bands_both_land_on_scale_four);
  RUN_TEST(test_qr_symbol_size_matches_the_version_formula);
  RUN_TEST(test_qr_has_finder_patterns_in_three_corners);
  RUN_TEST(test_qr_place_centres_and_reserves_the_quiet_zone);
  RUN_TEST(test_qr_place_uses_whole_pixel_modules);
  RUN_TEST(test_qr_place_reports_when_it_cannot_fit);

  return UNITY_END();
}
