#include "audio.h"
#include "web.h"
#include "screen.h"
#include <driver/i2s.h>

bool audioPlaying = false;
bool stopRequested = false;

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

static void i2sDeinit() {
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
  cfg.channel_format       = (hdr.channels == 1)
                                 ? I2S_CHANNEL_FMT_ONLY_LEFT
                                 : I2S_CHANNEL_FMT_RIGHT_LEFT;
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
  uint8_t buf[64];
  if (f.read(buf, 44) != 44) return false;
  if (memcmp(buf, "RIFF", 4) != 0) return false;
  if (memcmp(buf + 8, "WAVE", 4) != 0) return false;

  uint32_t pos = 12;
  while (pos < 256) {
    f.seek(pos);
    if (f.read(buf, 8) != 8) return false;
    if (memcmp(buf, "fmt ", 4) == 0) {
      uint32_t fmtSize = buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24);
      if (fmtSize < 16 || fmtSize > 64) return false;
      if (f.read(buf, fmtSize) != fmtSize) return false;
      hdr.channels      = buf[2]  | (buf[3] << 8);
      hdr.sampleRate    = buf[4]  | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24);
      hdr.bitsPerSample = buf[14] | (buf[15] << 8);
      pos = 12 + 8 + fmtSize;
      break;
    }
    pos += 8 + (buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24));
  }
  if (pos >= 256) return false;

  while (pos < 4096) {
    f.seek(pos);
    if (f.read(buf, 8) != 8) return false;
    uint32_t chunkSize = buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24);
    if (memcmp(buf, "data", 4) == 0) {
      hdr.dataSize   = chunkSize;
      hdr.dataOffset = pos + 8;
      return true;
    }
    pos += 8 + chunkSize;
  }
  return false;
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
  uint8_t *buf = (uint8_t *)malloc(CHUNK);
  if (!buf) { Serial.println("OOM"); f.close(); return; }

  f.seek(hdr.dataOffset);
  uint32_t remaining = hdr.dataSize;
  i2s_start(I2S_NUM_0);

  uint32_t lastNfcCheck = 0;
  uint8_t  tagAbsentCount = 0;

  uint32_t volOverlayTimer = 0;
  bool     volOverlayVisible = false;

  while (remaining > 0 && !stopRequested) {
    size_t toRead = (remaining < CHUNK) ? (size_t)remaining : CHUNK;
    size_t bytesRead = f.read(buf, toRead);
    if (bytesRead == 0) break;

    // Apply volume scaling to 16-bit samples
    if (hdr.bitsPerSample == 16) {
      int16_t *samples = (int16_t *)buf;
      size_t count = bytesRead / 2;
      float scale = volumeLevel / 100.0f;
      for (size_t i = 0; i < count; i++)
        samples[i] = (int16_t)(samples[i] * scale);
    }

    size_t bytesWritten;
    if (i2s_write(I2S_NUM_0, buf, bytesRead, &bytesWritten, pdMS_TO_TICKS(100)) != ESP_OK)
      break;
    remaining -= bytesWritten;

    handleWebClient();

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

  uint8_t buf[12];
  if (f.read(buf, 12) != 12) { f.close(); return; }
  if (memcmp(buf, "RIFF", 4) != 0) { f.close(); return; }

  uint32_t riffSize = buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24);
  uint32_t pos = 12;
  uint32_t end = riffSize + 8;
  if (end > 500000) end = 500000;

  while (pos + 8 < end) {
    f.seek(pos);
    if (f.read(buf, 8) != 8) break;
    uint32_t size = buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24);

    if (memcmp(buf, "LIST", 4) == 0 && size >= 4) {
      if (f.read(buf, 4) != 4) break;
      if (memcmp(buf, "INFO", 4) != 0) { pos += 8 + size; continue; }

      uint32_t infoEnd = pos + 8 + size;
      pos += 12;

      while (pos + 8 < infoEnd) {
        f.seek(pos);
        if (f.read(buf, 8) != 8) break;
        uint32_t sub = buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24);

        char *dst = nullptr;
        int max = 0;
        if      (memcmp(buf, "INAM", 4) == 0) { dst = meta.title;  max = 63; }
        else if (memcmp(buf, "IART", 4) == 0) { dst = meta.artist; max = 63; }

        if (dst && sub > 0) {
          int n = (sub < max) ? sub : max;
          f.read((uint8_t *)dst, n);
          dst[n] = '\0';
          while (n > 0 && (dst[n-1] == 0 || dst[n-1] == ' ')) dst[--n] = '\0';
        }
        pos += 8 + sub + (sub & 1);
      }
      break;
    }
    pos += 8 + size;
  }
  f.close();
}
