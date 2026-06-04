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
#include "jukebox_state.cpp"

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

void test_fsm_tag_removal_requires_three_misses() {
  JukeboxState s = jukeboxInitialState(0);
  s.tagPresent = true;
  s.lastActivityMs = 0;

  for (int miss = 1; miss <= 2; miss++) {
    TickResult r = jukeboxStep(s, baseInput(miss * 100));
    s = r.state;
    TEST_ASSERT_FALSE(hasAction(r.actions, Action::ConfirmTagRemoved));
    TEST_ASSERT_TRUE(s.tagPresent);
    TEST_ASSERT_EQUAL_UINT8(miss, s.tagAbsentCount);
  }
  // Third consecutive miss confirms removal.
  TickResult r3 = jukeboxStep(s, baseInput(300));
  TEST_ASSERT_TRUE(hasAction(r3.actions, Action::ConfirmTagRemoved));
  TEST_ASSERT_FALSE(r3.state.tagPresent);
  TEST_ASSERT_FALSE(r3.state.sleepStopped);
  TEST_ASSERT_EQUAL_UINT8(0, r3.state.tagAbsentCount);
}

void test_fsm_intermittent_glitch_does_not_remove_tag() {
  // miss, miss, found, miss, miss — only resets to 1 after the found, never reaches 3.
  JukeboxState s = jukeboxInitialState(0);
  s.tagPresent = true;
  TickResult r;
  r = jukeboxStep(s, baseInput(10));                                s = r.state;
  r = jukeboxStep(s, baseInput(20));                                s = r.state;
  TickInput inFound = baseInput(30); inFound.nfcFound = true;
  r = jukeboxStep(s, inFound);                                      s = r.state;
  TEST_ASSERT_EQUAL_UINT8(0, s.tagAbsentCount);
  r = jukeboxStep(s, baseInput(40));                                s = r.state;
  r = jukeboxStep(s, baseInput(50));                                s = r.state;
  TEST_ASSERT_FALSE(hasAction(r.actions, Action::ConfirmTagRemoved));
  TEST_ASSERT_TRUE(s.tagPresent);
}

void test_fsm_removal_clears_sleep_stopped() {
  // After sleep timer fired, tag removal should clear sleepStopped so the
  // next arrival can trigger playback normally.
  JukeboxState s = jukeboxInitialState(0);
  s.tagPresent = true;
  s.sleepStopped = true;
  s.tagAbsentCount = 2;
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

  // 8) Tag removed for 3 consecutive misses.
  for (int i = 0; i < 3; i++) {
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
//  Unity entry point
// ----------------------------------------------------------------

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();

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

  RUN_TEST(test_fsm_initial_state_is_waiting_idle);
  RUN_TEST(test_fsm_tag_arrival_triggers_playback);
  RUN_TEST(test_fsm_no_retrigger_while_tag_present);
  RUN_TEST(test_fsm_arrival_suppressed_when_sleep_stopped);
  RUN_TEST(test_fsm_tag_removal_requires_three_misses);
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
  RUN_TEST(test_fsm_encoder_rotation_resets_idle_clock);
  RUN_TEST(test_fsm_full_sleep_timer_recovery_scenario);

  return UNITY_END();
}
