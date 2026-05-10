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

  return UNITY_END();
}
