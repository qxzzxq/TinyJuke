// ESP Jukebox — NFC tag reader + TFT display + WAV audio player
//
// Scans NFC tags, looks up the UID in /tags.json on the SD card,
// and plays the mapped WAV file through a MAX98357A over I2S.
//
// The QSPI display and SD card share the VSPI bus (GPIO 18/19/23).
// Only one device drives the bus at a time — guarded by CS pins.
//
// Audio uses the ESP32's I2S peripheral directly with a built-in
// WAV parser, avoiding library conflicts.

#include <Arduino.h>
#include <PN532_HSU.h>
#include <PN532.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>

// QSPI display uses VSPI (SPI3) — must be defined before the GFX include.
#define ESP32QSPI_SPI_HOST SPI3_HOST
#include <Arduino_GFX_Library.h>

// --- PN532 (HSU / UART) ---
#define PN532_TX 33  // ESP32 RX (receives from PN532)
#define PN532_RX 32  // ESP32 TX (transmits to PN532)

PN532_HSU pn532hsu(Serial2);
PN532 nfc(pn532hsu);

// --- TFT display (QSPI) ---
// The D32 Pro variant header pre-defines conflicting TFT pin macros.
#ifdef TFT_CS
#undef TFT_CS
#endif
#ifdef TFT_DC
#undef TFT_DC
#endif
#ifdef TFT_RST
#undef TFT_RST
#endif

#define TFT_CS   5
#define TFT_SCK 18
#define TFT_IO0 23
#define TFT_IO1 19
#define TFT_IO2 21
#define TFT_IO3 22
#define TFT_RST 14
#define TFT_BL  13

// is_shared_interface=true: the QSPI driver acquires the bus before each
// batch of TFT operations and releases it afterward, so the SD card can
// use the same VSPI bus between TFT updates.
Arduino_ESP32QSPI bus(TFT_CS, TFT_SCK, TFT_IO0, TFT_IO1, TFT_IO2, TFT_IO3, true);
Arduino_ST77916 gfx(&bus, TFT_RST, 0 /* rotation */, false /* ips */,
                    360, 360,
                    0, 0, 0, 0,
                    st77916_150_init_operations, sizeof(st77916_150_init_operations));

// --- SD card ---
#define SD_CS 4

// --- MAX98357A (I2S) ---
#define I2S_BCLK 27
#define I2S_LRC  26
#define I2S_DOUT 25

// --- Tag mapping ---
static JsonDocument tagDoc;
static bool sdReady = false;

// --- Playback state ---
static bool tagPresent = false;
static bool audioPlaying = false;
static bool stopRequested = false;

// --- Colors ---
static const uint16_t BG_COLOR    = 0x2104;  // dark navy
static const uint16_t TEXT_COLOR   = 0xFFFF;  // white
static const uint16_t ACCENT_COLOR = 0x07E0;  // green
static const uint16_t DIM_COLOR    = 0x8410;  // dim gray
static const uint16_t RED_COLOR    = 0xF800;  // red

// --- Helpers ---

static int16_t textWidth(const char *str) {
  int16_t x1, y1;
  uint16_t w, h;
  gfx.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return (int16_t)w;
}

static void centerText(const char *str, int16_t y, uint16_t color, uint8_t size) {
  gfx.setTextColor(color);
  gfx.setTextSize(size);
  int16_t w = textWidth(str);
  gfx.setCursor((gfx.width() - w) / 2, y);
  gfx.print(str);
}

static void printHex(const uint8_t *data, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    if (data[i] < 0x10) Serial.print('0');
    Serial.print(data[i], HEX);
    if (i < len - 1) Serial.print(':');
  }
}

static void uidToStr(const uint8_t *uid, uint8_t len, char *buf) {
  uint8_t pos = 0;
  for (uint8_t i = 0; i < len; i++) {
    if (uid[i] < 0x10) buf[pos++] = '0';
    else buf[pos++] = "0123456789ABCDEF"[(uid[i] >> 4) & 0x0F];
    buf[pos++] = "0123456789ABCDEF"[uid[i] & 0x0F];
    if (i < len - 1) buf[pos++] = ':';
  }
  buf[pos] = '\0';
}

// Compact hex string (no colons) for tags.json key lookup.
static void uidToStrCompact(const uint8_t *uid, uint8_t len, char *buf) {
  for (uint8_t i = 0; i < len; i++) {
    buf[i * 2]     = "0123456789ABCDEF"[uid[i] >> 4];
    buf[i * 2 + 1] = "0123456789ABCDEF"[uid[i] & 0x0F];
  }
  buf[len * 2] = '\0';
}

// --- Display screens ---

static void drawWaitingScreen() {
  gfx.fillScreen(BG_COLOR);
  centerText("ESP Jukebox", 40, TEXT_COLOR, 2);
  centerText("Waiting for", 160, DIM_COLOR, 2);
  centerText("tag...", 190, DIM_COLOR, 2);
}

static void drawTagScreen(const uint8_t *uid, uint8_t uidLen) {
  gfx.fillScreen(BG_COLOR);

  centerText("TAG DETECTED", 40, ACCENT_COLOR, 2);

  char lenStr[16];
  snprintf(lenStr, sizeof(lenStr), "UID (%d bytes)", uidLen);
  centerText(lenStr, 90, TEXT_COLOR, 1);

  char uidStr[64];
  uidToStr(uid, uidLen, uidStr);

  if (uidLen > 4) {
    gfx.setTextSize(2);
    if (textWidth(uidStr) > 280) {
      int split = (uidLen + 1) / 2;
      char lineBuf[32];
      uidToStr(uid, split, lineBuf);
      centerText(lineBuf, 140, TEXT_COLOR, 2);
      uidToStr(uid + split, uidLen - split, lineBuf);
      centerText(lineBuf, 170, TEXT_COLOR, 2);
    } else {
      centerText(uidStr, 160, TEXT_COLOR, 2);
    }
  } else {
    centerText(uidStr, 160, TEXT_COLOR, 3);
  }
}

static void drawUnknownTagScreen(const uint8_t *uid, uint8_t uidLen) {
  gfx.fillScreen(BG_COLOR);
  centerText("UNKNOWN TAG", 40, RED_COLOR, 2);

  char uidStr[64];
  uidToStr(uid, uidLen, uidStr);
  centerText(uidStr, 160, TEXT_COLOR, 2);

  centerText("Remove tag...", 300, DIM_COLOR, 1);
}

static void drawNowPlayingScreen(const char *filepath) {
  gfx.fillScreen(BG_COLOR);
  centerText("NOW PLAYING", 40, ACCENT_COLOR, 2);

  // Show filename (strip directories)
  const char *name = strrchr(filepath, '/');
  if (name) name++; else name = filepath;

  gfx.setTextColor(TEXT_COLOR);
  gfx.setTextSize(2);

  int16_t w = textWidth(name);
  if (w > 280) {
    char trunc[24];
    int maxLen = 18;
    strncpy(trunc, name, maxLen);
    trunc[maxLen] = '\0';
    strcat(trunc, "...");
    centerText(trunc, 140, TEXT_COLOR, 2);
  } else {
    centerText(name, 140, TEXT_COLOR, 2);
  }

  centerText("Remove to stop", 300, DIM_COLOR, 1);
}

static void drawSDErrorScreen() {
  gfx.fillScreen(BG_COLOR);
  centerText("SD CARD", 130, RED_COLOR, 2);
  centerText("NOT FOUND", 160, RED_COLOR, 2);
  centerText("Check card & FAT32", 220, DIM_COLOR, 1);
}

// --- Tag lookup ---

static const char *lookupTag(const uint8_t *uid, uint8_t uidLen) {
  char key[32];
  uidToStrCompact(uid, uidLen, key);

  if (!tagDoc[key].isNull()) {
    return tagDoc[key]["file"].as<const char *>();
  }
  return nullptr;
}

// --- WAV player ---

struct WavHeader {
  uint16_t channels;
  uint32_t sampleRate;
  uint16_t bitsPerSample;
  uint32_t dataSize;
  uint32_t dataOffset;
};

static bool parseWavHeader(File &f, WavHeader &hdr) {
  uint8_t buf[44];
  if (f.read(buf, 44) != 44) return false;

  // RIFF chunk
  if (memcmp(buf, "RIFF", 4) != 0) return false;
  if (memcmp(buf + 8, "WAVE", 4) != 0) return false;

  // Find fmt chunk (skip any extra chunks)
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
    uint32_t chunkSize = buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24);
    pos += 8 + chunkSize;
  }

  if (pos >= 256) return false;

  // Find data chunk
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

static bool i2sConfigured = false;

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
  cfg.dma_desc_num          = 8;
  cfg.dma_frame_num         = 1024;
  cfg.use_apll             = false;
  cfg.tx_desc_auto_clear   = true;

  esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("i2s_driver_install failed: %d\n", err);
    return false;
  }

  i2s_pin_config_t pins = {};
  pins.bck_io_num     = I2S_BCLK;
  pins.ws_io_num      = I2S_LRC;
  pins.data_out_num   = I2S_DOUT;
  pins.data_in_num    = I2S_PIN_NO_CHANGE;

  err = i2s_set_pin(I2S_NUM_0, &pins);
  if (err != ESP_OK) {
    Serial.printf("i2s_set_pin failed: %d\n", err);
    i2s_driver_uninstall(I2S_NUM_0);
    return false;
  }

  i2sConfigured = true;
  return true;
}

// Play a WAV file. Returns when the file ends or the tag is removed.
// Polls NFC between chunks so tag removal is detected while audio plays.
static void playWav(const char *filepath) {
  // SD VFS requires an absolute path starting with /
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
  if (!f) {
    Serial.print("Failed to open: ");
    Serial.println(filepath);
    return;
  }

  WavHeader hdr;
  if (!parseWavHeader(f, hdr)) {
    Serial.println("Invalid WAV header.");
    f.close();
    return;
  }

  Serial.printf("WAV: %d Hz, %d-bit, %d ch, %u bytes PCM\n",
                hdr.sampleRate, hdr.bitsPerSample, hdr.channels, hdr.dataSize);

  if (!i2sInit(hdr)) {
    f.close();
    return;
  }

  audioPlaying = true;
  stopRequested = false;

  const size_t CHUNK = 2048;
  uint8_t *buf = (uint8_t *)malloc(CHUNK);
  if (!buf) {
    Serial.println("Failed to allocate playback buffer.");
    f.close();
    return;
  }

  f.seek(hdr.dataOffset);
  uint32_t remaining = hdr.dataSize;
  i2s_start(I2S_NUM_0);

  uint32_t lastNfcCheck = 0;
  uint8_t  tagAbsentCount = 0;    // debounce: N consecutive misses = removed

  while (remaining > 0 && !stopRequested) {
    size_t toRead = (remaining < CHUNK) ? (size_t)remaining : CHUNK;
    size_t bytesRead = f.read(buf, toRead);

    if (bytesRead == 0) break;

    size_t bytesWritten;
    esp_err_t err = i2s_write(I2S_NUM_0, buf, bytesRead, &bytesWritten,
                              pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
      Serial.printf("i2s_write error: %d\n", err);
      break;
    }

    remaining -= bytesRead;

    // Poll NFC for tag removal every ~150 ms.
    // Uses a short timeout so the DMA buffer doesn't drain.
    if (millis() - lastNfcCheck >= 150) {
      lastNfcCheck = millis();

      uint8_t u[7];
      uint8_t uLen;
      bool stillThere = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, u, &uLen, 30);

      if (!stillThere) {
        if (++tagAbsentCount >= 3) {
          stopRequested = true;   // tag removed, exit after this chunk
        }
      } else {
        tagAbsentCount = 0;       // tag still present, reset counter
      }
    }
  }

  i2s_zero_dma_buffer(I2S_NUM_0);
  i2s_stop(I2S_NUM_0);
  free(buf);
  f.close();
  audioPlaying = false;
}

static void stopPlayback() {
  stopRequested = true;
  // playWav() will exit its loop and clean up
}

// --- Setup ---

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  delay(200);
  Serial.println();
  Serial.println("ESP Jukebox starting...");

  // --- TFT init ---
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  gfx.begin();
  gfx.fillScreen(BG_COLOR);
  centerText("ESP Jukebox", 40, TEXT_COLOR, 2);
  centerText("Starting...", 200, DIM_COLOR, 1);

  // --- SD card init ---
  // SD shares VSPI with the QSPI display. The display driver releases the
  // bus after each drawing batch (is_shared_interface=true), so the bus is
  // free for SD init at this point.
  Serial.print("Mounting SD card... ");
  if (SD.begin(SD_CS)) {
    sdReady = true;
    Serial.println("OK");

    // Read tags.json
    File f = SD.open("/tags.json", FILE_READ);
    if (f) {
      DeserializationError err = deserializeJson(tagDoc, f);
      f.close();

      if (err) {
        Serial.print("tags.json parse error: ");
        Serial.println(err.c_str());
        tagDoc.clear();
      } else {
        Serial.print("Loaded ");
        Serial.print(tagDoc.size());
        Serial.println(" tag mappings from tags.json");
      }
    } else {
      Serial.println("tags.json not found on SD card.");
    }
  } else {
    Serial.println("FAILED");
    sdReady = false;

    drawSDErrorScreen();
    delay(3000);
  }

  // --- PN532 init ---
  Serial2.begin(115200, SERIAL_8N1, PN532_TX, PN532_RX);
  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("Did not find PN532.");
    gfx.fillScreen(BG_COLOR);
    centerText("PN532", 140, RED_COLOR, 2);
    centerText("NOT FOUND", 170, RED_COLOR, 2);

    // Raw diagnostic
    Serial.println("Running raw-byte diagnostic...");
    const uint8_t wake[] = {0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    Serial2.write(wake, sizeof(wake));
    const uint8_t gfv[] = {0x00, 0x00, 0xFF, 0x02, 0xFE, 0xD4, 0x02, 0x2A, 0x00};
    Serial2.write(gfv, sizeof(gfv));
    Serial2.flush();

    uint32_t t0 = millis();
    size_t bytesIn = 0;
    Serial.print("RX bytes: ");
    while (millis() - t0 < 500) {
      while (Serial2.available()) {
        uint8_t b = Serial2.read();
        if (b < 0x10) Serial.print('0');
        Serial.print(b, HEX);
        Serial.print(' ');
        bytesIn++;
      }
    }
    Serial.println();
    if (bytesIn == 0)
      Serial.println("No bytes received. Check DIP switches, wiring, power.");
    else
      Serial.println("Bytes received -> UART link alive. Check protocol/baud.");
    while (true) delay(1000);
  }

  Serial.print("Found chip PN5");
  Serial.println((versiondata >> 24) & 0xFF, HEX);
  Serial.print("Firmware version: ");
  Serial.print((versiondata >> 16) & 0xFF, DEC);
  Serial.print('.');
  Serial.println((versiondata >> 8) & 0xFF, DEC);

  nfc.SAMConfig();
  Serial.println("Ready. Waiting for an ISO14443A card...");
  drawWaitingScreen();
}

// --- Loop ---

void loop() {
  uint8_t uid[7] = {0};
  uint8_t uidLength = 0;

  // Poll NFC for tag presence.
  uint16_t nfcTimeout = 300;
  bool found = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, nfcTimeout);

  // --- State machine ---
  if (found && !tagPresent) {
    // Tag arrived
    tagPresent = true;

    Serial.print("Card detected. UID (");
    Serial.print(uidLength, DEC);
    Serial.print(" bytes): ");
    printHex(uid, uidLength);
    Serial.println();

    if (sdReady) {
      const char *filepath = lookupTag(uid, uidLength);

      if (filepath) {
        Serial.print("Mapped to: ");
        Serial.println(filepath);

        // Draw now-playing screen BEFORE starting audio.
        // This ensures TFT access happens before SD access for audio.
        drawNowPlayingScreen(filepath);

        playWav(filepath);

        // Playback ended — show waiting screen
        drawWaitingScreen();
      } else {
        Serial.println("Unknown tag. Ignored.");
        drawUnknownTagScreen(uid, uidLength);
      }
    } else {
      // No SD card — just show the UID
      drawTagScreen(uid, uidLength);
    }
  } else if (!found && tagPresent) {
    // Tag removed
    tagPresent = false;

    Serial.println("Card removed.");

    if (audioPlaying) {
      stopPlayback();
    }

    drawWaitingScreen();
  }
}
