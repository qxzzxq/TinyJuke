#include "audio.h"
#include "screen.h"
#include "encoder.h"
#include "timer_logic.h"
#include "volume_logic.h"
#include <driver/i2s.h>

bool audioPlaying = false;
bool stopRequested = false;
bool sleepTimerFired = false;
uint32_t audioStartTime = 0;

static bool i2sConfigured = false;
static bool parseWavHeader(File &f, WavHeader &hdr);

// ----------------------------------------------------------------

void i2sPrime() {
  // Install I2S with a default config at boot so BCLK/LRC/DOUT are
  // actively driven and the MAX98357A doesn't pick up touch noise.
  i2s_config_t cfg = {};
  cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate          = 44100;
  cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_desc_num         = 8;
  cfg.dma_frame_num        = 1024;
  cfg.use_apll             = false;
  cfg.tx_desc_auto_clear   = true;

  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);

  i2s_pin_config_t pins = {};
  pins.bck_io_num   = I2S_BCLK;
  pins.ws_io_num    = I2S_LRC;
  pins.data_out_num = I2S_DOUT;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;
  i2s_set_pin(I2S_NUM_0, &pins);

  i2sConfigured = true;
}

void i2sDeinit() {
  if (i2sConfigured) {
    i2s_zero_dma_buffer(I2S_NUM_0);
    i2s_stop(I2S_NUM_0);
    i2s_driver_uninstall(I2S_NUM_0);
    i2sConfigured = false;
  }
}

static bool i2sInit(const WavHeader &hdr) {
  i2sDeinit();

  i2s_config_t cfg = {};
  cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate          = hdr.sampleRate;
  cfg.bits_per_sample      = (hdr.bitsPerSample == 16)
                                 ? I2S_BITS_PER_SAMPLE_16BIT
                                 : I2S_BITS_PER_SAMPLE_24BIT;
  cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_desc_num         = 8;
  cfg.dma_frame_num        = 1024;
  cfg.use_apll             = false;
  cfg.tx_desc_auto_clear   = true;

  esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  if (err != ESP_OK) { Serial.printf("i2s_driver_install: %d\n", err); return false; }

  i2s_pin_config_t pins = {};
  pins.bck_io_num   = I2S_BCLK;
  pins.ws_io_num    = I2S_LRC;
  pins.data_out_num = I2S_DOUT;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;

  err = i2s_set_pin(I2S_NUM_0, &pins);
  if (err != ESP_OK) { Serial.printf("i2s_set_pin: %d\n", err); i2s_driver_uninstall(I2S_NUM_0); return false; }

  i2sConfigured = true;
  return true;
}

static bool parseWavHeader(File &f, WavHeader &hdr) {
  // Read first 4 KB into a stack buffer; the data chunk must lie within
  // this window per the buffer parser's contract.
  uint8_t buf[4096];
  size_t toRead = f.size() < sizeof(buf) ? f.size() : sizeof(buf);
  f.seek(0);
  size_t n = f.read(buf, toRead);
  return parseWavHeaderBuffer(buf, n, hdr);
}

// ----------------------------------------------------------------

void playWav(const char *filepath, PN532 &nfc, const uint8_t *tagUid, uint8_t tagUidLen) {
  char path[128];
  if (filepath[0] != '/') {
    path[0] = '/';
    strncpy(path + 1, filepath, sizeof(path) - 2);
    path[sizeof(path) - 1] = '\0';
  } else {
    strncpy(path, filepath, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
  }

  File f = SD.open(path);
  if (!f) { Serial.printf("Open failed: %s\n", path); return; }

  WavHeader hdr;
  if (!parseWavHeader(f, hdr)) { Serial.println("Bad WAV header"); f.close(); return; }

  Serial.printf("WAV: %d Hz %d-bit %d ch, %u B\n",
                hdr.sampleRate, hdr.bitsPerSample, hdr.channels, hdr.dataSize);

  if (!i2sInit(hdr)) { f.close(); return; }

  audioPlaying = true;
  stopRequested = false;

  const size_t CHUNK = 2048;
  bool mono16 = (hdr.channels == 1 && hdr.bitsPerSample == 16);
  size_t readSize = mono16 ? CHUNK / 2 : CHUNK;
  uint8_t *buf = (uint8_t *)malloc(CHUNK);
  if (!buf) { Serial.println("OOM"); f.close(); return; }

  f.seek(hdr.dataOffset);
  uint32_t remaining = hdr.dataSize;
  i2s_start(I2S_NUM_0);

  uint32_t lastNfcCheck = 0;
  uint8_t  tagAbsentCount = 0;

  uint32_t volOverlayTimer = 0;
  bool     volOverlayVisible = false;

  uint32_t lastCountdownUpdate = 0;
  bool     countdownVisible = (sleepTimerMinutes > 0);

  while (remaining > 0 && !stopRequested) {
    size_t toRead = (remaining < readSize) ? (size_t)remaining : readSize;
    size_t bytesRead = f.read(buf, toRead);
    if (bytesRead == 0) break;

    // Apply volume scaling to 16-bit samples
    if (hdr.bitsPerSample == 16) {
      int16_t *samples = (int16_t *)buf;
      size_t count = bytesRead / 2;
      float scale = effectiveVolume(volumeLevel, maxVolumeLevel) / 100.0f;
      for (size_t i = 0; i < count; i++)
        samples[i] = (int16_t)(samples[i] * scale);
    }

    // Duplicate mono channel to both L+R so it matches stereo loudness
    size_t i2sBytes = bytesRead;
    if (mono16) {
      size_t n = bytesRead / 2;
      for (size_t i = n; i > 0; i--) {
        int16_t s = ((int16_t *)buf)[i - 1];
        ((int16_t *)buf)[(i - 1) * 2]     = s;
        ((int16_t *)buf)[(i - 1) * 2 + 1] = s;
      }
      i2sBytes = bytesRead * 2;
    }

    size_t bytesWritten;
    if (i2s_write(I2S_NUM_0, buf, i2sBytes, &bytesWritten, pdMS_TO_TICKS(100)) != ESP_OK)
      break;
    // For mono: i2s bytes are doubled, so file bytes consumed = i2s bytes / 2
    uint32_t consumed = mono16 ? (uint32_t)(bytesWritten / 2) : (uint32_t)bytesWritten;
    if (consumed > remaining) consumed = remaining;
    remaining -= consumed;

    // --- Encoder: volume adjustment during playback ---
    int enc = readEncoder();
    if ((enc > 0 && enc < ENC_CLICK) || (enc < 0)) {
      int steps = (enc > 0) ? enc : -enc;
      int delta = (enc > 0) ? 1 : -1;
      while (steps-- > 0) {
        int next = volumeLevel + delta;
        if (next < 0 || next > 100) break;
        volumeLevel = next;
      }
      drawPlaybackVolumeOverlay(volumeLevel);
      volOverlayTimer   = millis();
      volOverlayVisible = true;
    }
    if (volOverlayVisible && millis() - volOverlayTimer >= 5000) {
      saveVolume();
      clearPlaybackVolumeOverlay();
      volOverlayVisible = false;
      if (countdownVisible) {
        uint32_t remaining = timerRemainingMs(sleepTimerMinutes, millis() - audioStartTime);
        drawSleepTimerCountdown(remaining);
      }
    }

    // Update countdown every second (skip if volume overlay is active)
    if (countdownVisible && !volOverlayVisible) {
      uint32_t now = millis();
      if (now - lastCountdownUpdate >= 1000) {
        lastCountdownUpdate = now;
        uint32_t remaining = timerRemainingMs(sleepTimerMinutes, now - audioStartTime);
        updateSleepTimerCountdown(remaining);
      }
    }

    // Audio sleep timer: stop playback after configured duration (one-shot)
    if (sleepTimerShouldFire(sleepTimerMinutes, millis() - audioStartTime)) {
      sleepTimerMinutes = 0;
      saveSleepTimer();
      sleepTimerFired = true;
      stopRequested = true;
      break;
    }

    if (millis() - lastNfcCheck >= 150) {
      lastNfcCheck = millis();
      uint8_t u[10]; uint8_t uLen = 0;
      if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, u, &uLen, 30)) {
        if (++tagAbsentCount >= 3) stopRequested = true;
      } else {
        tagAbsentCount = 0;
        // Detect tag swap: different UID → stop and let main loop pick up new tag
        if (uLen != tagUidLen || memcmp(u, tagUid, uLen) != 0)
          stopRequested = true;
      }
    }
  }

  i2s_zero_dma_buffer(I2S_NUM_0);
  i2s_stop(I2S_NUM_0);
  free(buf);
  f.close();
  audioPlaying = false;
}

void stopPlayback() {
  stopRequested = true;
}

// ----------------------------------------------------------------
//  WAV metadata extraction (LIST INFO chunk)
// ----------------------------------------------------------------

void parseWavMeta(const char *filepath, WavMeta &meta) {
  memset(&meta, 0, sizeof(meta));

  char path[192];
  if (filepath[0] != '/') {
    path[0] = '/';
    strncpy(path + 1, filepath, sizeof(path) - 2);
    path[sizeof(path) - 1] = '\0';
  } else {
    strncpy(path, filepath, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
  }

  File f = SD.open(path);
  if (!f) return;

  // LIST INFO is conventionally near the front of the file; cap scan to 64 KB.
  const size_t MAX = 65536;
  size_t fileSize = (size_t)f.size();
  size_t toRead = fileSize < MAX ? fileSize : MAX;
  uint8_t *buf = (uint8_t *)malloc(toRead);
  if (!buf) { f.close(); return; }

  f.seek(0);
  size_t n = f.read(buf, toRead);
  parseWavMetaBuffer(buf, n, meta);

  free(buf);
  f.close();
}
