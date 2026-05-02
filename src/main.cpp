// ESP Jukebox — NFC tag reader + ST7735 TFT + WAV audio player
//
// TFT (ST7735S, 128x160) and SD card share the VSPI bus (GPIO 18/19/23).
// Arduino_ESP32SPI and the SD library both use bare-metal SPI, so they
// coexist naturally — only one CS is active at a time.

#include <Arduino.h>
#include <PN532_HSU.h>
#include <PN532.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>

#include <Arduino_GFX_Library.h>

// --- PN532 (HSU / UART) ---
#define PN532_TX 33
#define PN532_RX 32

PN532_HSU pn532hsu(Serial2);
PN532 nfc(pn532hsu);

// --- TFT (ST7735S, 128x160, standard SPI) ---
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
#define TFT_DC   21
#define TFT_RST  14
#define TFT_BL   13
#define TFT_SCK  18
#define TFT_MOSI 23
#define TFT_MISO 19

Arduino_ESP32SPI bus(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO, VSPI);
Arduino_ST7735 gfx(&bus, TFT_RST, 0, false, 128, 160,
                   0, 0, 0, 0);

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
static const uint16_t BG_COLOR    = 0x2104;
static const uint16_t TEXT_COLOR   = 0xFFFF;
static const uint16_t ACCENT_COLOR = 0x07E0;
static const uint16_t DIM_COLOR    = 0x8410;
static const uint16_t RED_COLOR    = 0xF800;

// ================================================================
//  Helpers
// ================================================================

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

static void uidToStrCompact(const uint8_t *uid, uint8_t len, char *buf) {
  for (uint8_t i = 0; i < len; i++) {
    buf[i * 2]     = "0123456789ABCDEF"[uid[i] >> 4];
    buf[i * 2 + 1] = "0123456789ABCDEF"[uid[i] & 0x0F];
  }
  buf[len * 2] = '\0';
}

// ================================================================
//  Display screens (128x160)
// ================================================================

static void drawWaitingScreen() {
  gfx.fillScreen(BG_COLOR);
  centerText("Jukebox", 20, TEXT_COLOR, 2);
  centerText("Waiting for", 80, DIM_COLOR, 1);
  centerText("tag...", 95, DIM_COLOR, 1);
}

static void drawTagScreen(const uint8_t *uid, uint8_t uidLen) {
  gfx.fillScreen(BG_COLOR);
  centerText("TAG SCANNED", 15, ACCENT_COLOR, 2);

  char uidStr[64];
  uidToStr(uid, uidLen, uidStr);

  if (textWidth(uidStr) > 110) {
    if (uidLen > 4) {
      int split = (uidLen + 1) / 2;
      char lineBuf[32];
      uidToStr(uid, split, lineBuf);
      centerText(lineBuf, 55, TEXT_COLOR, 1);
      uidToStr(uid + split, uidLen - split, lineBuf);
      centerText(lineBuf, 70, TEXT_COLOR, 1);
    } else {
      centerText(uidStr, 60, TEXT_COLOR, 1);
    }
  } else {
    centerText(uidStr, 60, TEXT_COLOR, 2);
  }
}

static void drawUnknownTagScreen(const uint8_t *uid, uint8_t uidLen) {
  gfx.fillScreen(BG_COLOR);
  centerText("UNKNOWN", 20, RED_COLOR, 2);
  char uidStr[64];
  uidToStr(uid, uidLen, uidStr);
  centerText(uidStr, 70, TEXT_COLOR, 1);
  centerText("Remove...", 120, DIM_COLOR, 1);
}

static void drawNowPlayingScreen(const char *filepath) {
  gfx.fillScreen(BG_COLOR);
  centerText("PLAYING", 15, ACCENT_COLOR, 2);

  const char *name = strrchr(filepath, '/');
  if (name) name++; else name = filepath;

  gfx.setTextColor(TEXT_COLOR);
  gfx.setTextSize(1);

  int16_t w = textWidth(name);
  if (w > 120) {
    char trunc[20];
    int maxLen = 16;
    strncpy(trunc, name, maxLen);
    trunc[maxLen] = '\0';
    strcat(trunc, "...");
    centerText(trunc, 70, TEXT_COLOR, 1);
  } else {
    centerText(name, 70, TEXT_COLOR, 1);
  }

  centerText("Remove to stop", 130, DIM_COLOR, 1);
}

static void drawSDErrorScreen() {
  gfx.fillScreen(BG_COLOR);
  centerText("SD CARD", 50, RED_COLOR, 2);
  centerText("NOT FOUND", 80, RED_COLOR, 1);
}

// ================================================================
//  Tag lookup
// ================================================================

static const char *lookupTag(const uint8_t *uid, uint8_t uidLen) {
  char key[32];
  uidToStrCompact(uid, uidLen, key);
  if (!tagDoc[key].isNull())
    return tagDoc[key]["file"].as<const char *>();
  return nullptr;
}

// ================================================================
//  WAV player
// ================================================================

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

static void playWav(const char *filepath) {
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

  while (remaining > 0 && !stopRequested) {
    size_t toRead = (remaining < CHUNK) ? (size_t)remaining : CHUNK;
    size_t bytesRead = f.read(buf, toRead);
    if (bytesRead == 0) break;

    size_t bytesWritten;
    if (i2s_write(I2S_NUM_0, buf, bytesRead, &bytesWritten, pdMS_TO_TICKS(100)) != ESP_OK)
      break;
    remaining -= bytesRead;

    if (millis() - lastNfcCheck >= 150) {
      lastNfcCheck = millis();
      uint8_t u[7]; uint8_t uLen;
      if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, u, &uLen, 30)) {
        if (++tagAbsentCount >= 3) stopRequested = true;
      } else {
        tagAbsentCount = 0;
      }
    }
  }

  i2s_zero_dma_buffer(I2S_NUM_0);
  i2s_stop(I2S_NUM_0);
  free(buf);
  f.close();
  audioPlaying = false;
}

static void stopPlayback() { stopRequested = true; }

// ================================================================
//  Setup & Loop
// ================================================================

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  delay(200);
  Serial.println("\nESP Jukebox starting...");

  // 1. Init TFT first (initializes VSPI via bare-metal SPI)
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  gfx.begin();

  // 2. Init SD card (VSPI was reset by TFT, re-init for SD)
  //    After this, TFT operations call beginWrite() which reconfigures
  //    VSPI back for the display. SD operations do the same via
  //    beginTransaction(). Both share the bus through CS pins.
  Serial.print("Mounting SD... ");
  if (SD.begin(SD_CS)) {
    sdReady = true;
    Serial.println("OK");

    File f = SD.open("/tags.json", FILE_READ);
    if (f) {
      DeserializationError err = deserializeJson(tagDoc, f);
      f.close();
      if (err) {
        Serial.print("tags.json err: ");
        Serial.println(err.c_str());
        tagDoc.clear();
      } else {
        Serial.print(tagDoc.size());
        Serial.println(" tags loaded.");
      }
    } else {
      Serial.println("tags.json not found.");
    }
  } else {
    sdReady = false;
    Serial.println("FAILED");
  }

  // 3. Draw boot screen (TFT reconfigures VSPI via beginWrite)
  if (!sdReady) {
    drawSDErrorScreen();
    delay(3000);
  } else {
    gfx.fillScreen(BG_COLOR);
    centerText("Jukebox", 20, TEXT_COLOR, 2);
    centerText("Starting...", 90, DIM_COLOR, 1);
    delay(500);
  }

  // 3. Init PN532
  Serial2.begin(115200, SERIAL_8N1, PN532_TX, PN532_RX);
  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("PN532 not found.");
    if (sdReady) {
      gfx.fillScreen(BG_COLOR);
      centerText("PN532", 50, RED_COLOR, 2);
      centerText("NOT FOUND", 80, RED_COLOR, 1);
    }
    const uint8_t wake[] = {0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    Serial2.write(wake, sizeof(wake));
    const uint8_t gfv[] = {0x00, 0x00, 0xFF, 0x02, 0xFE, 0xD4, 0x02, 0x2A, 0x00};
    Serial2.write(gfv, sizeof(gfv));
    Serial2.flush();
    uint32_t t0 = millis();
    Serial.print("RX: ");
    while (millis() - t0 < 500)
      while (Serial2.available()) {
        uint8_t b = Serial2.read();
        if (b < 0x10) Serial.print('0');
        Serial.print(b, HEX);
        Serial.print(' ');
      }
    Serial.println();
    while (true) delay(1000);
  }

  Serial.printf("Found PN532 fw %d.%d\n",
                (versiondata >> 16) & 0xFF,
                (versiondata >> 8) & 0xFF);

  nfc.SAMConfig();
  Serial.println("Ready.");
  drawWaitingScreen();
}

void loop() {
  uint8_t uid[7] = {0};
  uint8_t uidLength = 0;

  bool found = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 300);

  if (found && !tagPresent) {
    tagPresent = true;
    Serial.print("Tag UID("); Serial.print(uidLength); Serial.print("): ");
    printHex(uid, uidLength); Serial.println();

    if (sdReady) {
      const char *filepath = lookupTag(uid, uidLength);
      if (filepath) {
        Serial.print("Playing: "); Serial.println(filepath);
        drawNowPlayingScreen(filepath);
        playWav(filepath);
        drawWaitingScreen();
      } else {
        Serial.println("Unknown tag.");
        drawUnknownTagScreen(uid, uidLength);
      }
    } else {
      drawTagScreen(uid, uidLength);
    }
  } else if (!found && tagPresent) {
    tagPresent = false;
    Serial.println("Tag removed.");
    if (audioPlaying) stopPlayback();
    drawWaitingScreen();
  }
}
