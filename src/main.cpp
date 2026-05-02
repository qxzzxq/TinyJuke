// ESP Jukebox
//
// TFT (ST7735S, 128x160) and SD card share the VSPI bus (GPIO 18/19/23).
// Arduino_ESP32SPI and the SD library both use bare-metal SPI — only one CS
// is active at a time.

#include "config.h"
#include "tags.h"
#include "screen.h"
#include "audio.h"
#include "encoder.h"

#include <PN532_HSU.h>
#include <PN532.h>
#include <SD.h>
#include <Arduino_GFX_Library.h>

// --- Peripherals ---
PN532_HSU pn532hsu(Serial2);
PN532      nfc(pn532hsu);

Arduino_ESP32SPI bus(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO, VSPI);
Arduino_ST7735  gfx(&bus, TFT_RST, 0, false, 128, 160, 0, 0, 0, 0);

// --- State ---
static bool tagPresent = false;

// ================================================================

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  delay(200);
  Serial.println("\nESP Jukebox starting...");

  // 1. Init TFT (initializes VSPI via bare-metal SPI)
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  gfx.begin();

  // 2. Init SD (VSPI reconfigures for each device via CS)
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

  // 3. Boot screen
  if (!sdReady) {
    drawSDErrorScreen();
    delay(3000);
  } else {
    gfx.fillScreen(C_BG);
    drawWaitingScreen();
  }

  // 4. Init PN532
  Serial2.begin(115200, SERIAL_8N1, PN532_TX, PN532_RX);
  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("PN532 not found.");
    if (sdReady) {
      gfx.fillScreen(C_BG);
      gfx.setTextColor(C_RED);
      gfx.setTextSize(2);
      gfx.setCursor(10, 50); gfx.print("PN532");
      gfx.setTextSize(1);
      gfx.setCursor(10, 80); gfx.print("NOT FOUND");
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
        Serial.print(b, HEX); Serial.print(' ');
      }
    Serial.println();
    while (true) delay(1000);
  }

  Serial.printf("Found PN532 fw %d.%d\n",
                (versiondata >> 16) & 0xFF,
                (versiondata >> 8) & 0xFF);

  nfc.SAMConfig();
  Serial.println("Ready.");

  // 5. Init encoder (placeholder)
  initEncoder();
}

// ================================================================

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
        playWav(filepath, nfc);
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
