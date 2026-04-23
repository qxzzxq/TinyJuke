// PN532 HSU demo for Lolin D32 Pro
//
// Wiring (remember UART must cross):
//   PN532 SDA (acts as RX) <- ESP32 TX  (GPIO 33)
//   PN532 SCL (acts as TX) -> ESP32 RX  (GPIO 32)
//   PN532 VCC              <-  3V3
//   PN532 GND              <-  GND
//
// Elechouse PN532 DIP switches: BOTH OFF for HSU mode.
// GPIO16/17 are reserved for PSRAM on the D32 Pro, hence using 32/33.

#include <Arduino.h>
#include <PN532_HSU.h>
#include <PN532.h>

#define PN532_TX 33  // ESP32 receives on this pin
#define PN532_RX 32  // ESP32 transmits on this pin

PN532_HSU pn532hsu(Serial2);
PN532 nfc(pn532hsu);

static void printHex(const uint8_t *data, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        if (data[i] < 0x10) Serial.print('0');
        Serial.print(data[i], HEX);
        if (i < len - 1) Serial.print(':');
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    delay(200);
    Serial.println();
    Serial.println("PN532 HSU demo starting...");

    // Explicitly bring up UART2 on our remapped pins before the PN532 lib touches it.
    Serial2.begin(115200, SERIAL_8N1, PN532_TX, PN532_RX);

    nfc.begin();

    uint32_t versiondata = nfc.getFirmwareVersion();
    if (!versiondata) {
        Serial.println("Did not find PN532.");
        Serial.println("Running raw-byte diagnostic: sending wake-up + firmware-version frame...");
        // Wake-up preamble
        const uint8_t wake[] = {0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        Serial2.write(wake, sizeof(wake));
        // GetFirmwareVersion command frame
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
        if (bytesIn == 0) {
            Serial.println("No bytes received. Likely: DIP not both OFF, wrong wiring, or no power.");
        } else {
            Serial.println("Bytes received -> UART link is alive. Check protocol/baud or retry.");
        }
        while (true) delay(1000);
    }

    Serial.print("Found chip PN5");
    Serial.println((versiondata >> 24) & 0xFF, HEX);
    Serial.print("Firmware version: ");
    Serial.print((versiondata >> 16) & 0xFF, DEC);
    Serial.print('.');
    Serial.println((versiondata >> 8) & 0xFF, DEC);

    nfc.SAMConfig();
    Serial.println("Waiting for an ISO14443A card...");
}

void loop() {
    uint8_t uid[7] = {0};
    uint8_t uidLength = 0;

    bool found = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 1000);
    if (found) {
        Serial.print("Card detected. UID (");
        Serial.print(uidLength, DEC);
        Serial.print(" bytes): ");
        printHex(uid, uidLength);
        Serial.println();
        delay(500);
    }
}
