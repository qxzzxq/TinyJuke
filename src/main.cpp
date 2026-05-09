// ESP Jukebox
//
// TFT (ST7789V, 240x320) and SD card share the VSPI bus (GPIO 18/19/23).
// Arduino_ESP32SPI and the SD library both use bare-metal SPI — only one CS
// is active at a time.
//
// Jukebox mode: scan tag → play WAV, encoder adjusts volume, hold for menu.
// Menu mode: manage tags, start web server, adjust volume.

#include "config.h"
#include "tags.h"
#include "screen.h"
#include "audio.h"
#include "encoder.h"
#include "gui.h"
#include "web.h"

#include <PN532_HSU.h>
#include <PN532.h>
#include <SD.h>
#include <Arduino_GFX_Library.h>

// --- Peripherals ---
PN532_HSU pn532hsu(Serial2);
PN532      nfc(pn532hsu);

Arduino_ESP32SPI bus(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO, VSPI);
Arduino_ST7789  gfx(&bus, TFT_RST, 0, true, 240, 320, 0, 0, 0, 0);

// --- State ---
static bool tagPresent = false;
static char lastTagUid[32] = "";

// ================================================================

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  delay(200);
  Serial.println("\nESP Jukebox starting...");

  // 1. Init TFT (backlight off until first screen is drawn)
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);
  gfx.begin();

  // 2. Init SD
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

  // 3. Boot screen (drawn before backlight on to avoid init artifacts)
  if (!sdReady) {
    drawSDErrorScreen();
    delay(3000);
  } else {
    gfx.fillScreen(C_BG);
    drawWaitingScreen();
  }
  digitalWrite(TFT_BL, HIGH);

  // 3.5 Prime I2S pins so MAX98357A BCLK isn't floating (touch-sensitive)
  i2sPrime();

  // 4. Init PN532
  Serial2.begin(115200, SERIAL_8N1, PN532_TX, PN532_RX);
  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("PN532 not found.");
    if (sdReady) {
      gfx.fillScreen(C_BG);
      gfx.setTextColor(C_RED);  gfx.setTextSize(2);
      gfx.setCursor(30, 40); gfx.print("PN532");
      gfx.setTextSize(1);
      gfx.setCursor(30, 65); gfx.print("NOT FOUND");
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

  // 5. Init encoder (loads saved volume, sets up interrupts)
  initEncoder();
}

// ================================================================

void loop() {
  // --- Management mode ---
  if (guiActive()) {
    guiLoop();
    return;
  }

  // --- Jukebox mode: handle encoder for volume / menu entry ---
  int ev = readEncoder();

  if ((ev > 0 && ev < ENC_CLICK) || (ev < 0)) {
    int steps = (ev > 0) ? ev : -ev;
    int delta = (ev > 0) ? 1 : -1;
    while (steps-- > 0) {
      int next = volumeLevel + delta;
      if (next < 0 || next > 100) break;
      volumeLevel = next;
    }
  }
  if (ev == ENC_CLICK) {
    // Save volume on click when in jukebox mode
    saveVolume();
  }
  if (ev == ENC_HOLD) {
    saveVolume();
    guiEnter();
    return;
  }

  // --- NFC tag polling ---
  uint8_t uid[10] = {0};
  uint8_t uidLength = 0;
  bool found = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 300);
  if (uidLength > 10) uidLength = 10;

  if (found && !tagPresent) {
    tagPresent = true;
    uidToStr(uid, uidLength, lastTagUid);
    Serial.print("Tag UID("); Serial.print(uidLength); Serial.print("): ");
    printHex(uid, uidLength); Serial.println();

    if (sdReady) {
      TagInfo tag = lookupTag(uid, uidLength);
      if (tag.file) {
        Serial.print("Playing: "); Serial.println(tag.file);
        drawNowPlayingScreen(tag);
        playWav(tag.file, nfc, uid, uidLength);
        // Quick NFC check: detect tag removal or tag swap before redrawing
        {
          uint8_t u[10]; uint8_t uLen = 0;
          if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, u, &uLen, 50)) {
            tagPresent = false;
            lastTagUid[0] = '\0';
          } else if (uLen <= 10) {
            char currentUid[32];
            uidToStr(u, uLen, currentUid);
            if (strcmp(currentUid, lastTagUid) != 0)
              tagPresent = false;  // different tag → arrival on next loop
          }
        }
        drawWaitingScreen();
      } else {
        Serial.println("Unknown tag.");
        drawUnknownTagScreen(uid, uidLength);
        uidToStr(uid, uidLength, lastTagUid);
        uint32_t t = millis();
        while (millis() - t < 10000) { // 10s timeout
          int eu = readEncoder();
          if (eu == ENC_CLICK || eu == ENC_HOLD) {
            break; // dismiss
          }
          // Check for tag removal or swap
          uint8_t u[10]; uint8_t uLen = 0;
          if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, u, &uLen, 200)) {
            tagPresent = false;
            lastTagUid[0] = '\0';
            Serial.println("Tag removed.");
            drawWaitingScreen();
            break;
          }
          if (uLen <= 10) {
            char currentUid[32];
            uidToStr(u, uLen, currentUid);
            if (strcmp(currentUid, lastTagUid) != 0) {
              tagPresent = false;  // different tag → arrival on next loop
              break;
            }
          }
          delay(30);
        }
        if (tagPresent) drawWaitingScreen();
      }
    } else {
      drawTagScreen(uid, uidLength);
    }
  } else if (!found && tagPresent) {
    tagPresent = false;
    lastTagUid[0] = '\0';
    Serial.println("Tag removed.");
    if (audioPlaying) stopPlayback();
    drawWaitingScreen();
  }
}
