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
#include <mbedtls/md.h>
#include <string.h>

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



static const uint8_t KNOWN_KEYS[][6] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},  // factory default
    {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7},  // NDEF data sectors
    {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5},  // MAD key (sector 0 after NDEF format)
    {0x4D, 0x3A, 0x99, 0xC3, 0x51, 0xDD},  // commonly published key
    {0x1A, 0x98, 0x2C, 0x7E, 0x45, 0x9A},  // commonly published key
    {0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5},  // MAD KEYB variant
};

// create a list of known key names for easier debugging
static const char *KNOWN_KEY_NAMES[] = {
    "Factory default",
    "NDEF data sectors",
    "MAD key (sector 0 after NDEF format)",
    "Commonly published key #1",
    "Commonly published key #2",
    "MAD KEYB variant",
};

static const uint8_t NUM_KEYS = sizeof(KNOWN_KEYS) / 6;

static void dumpCard() {
    Serial.println();
    Serial.println("=== MIFARE Classic 1K full dump ===");

    for (uint8_t sector = 0; sector < 16; sector++) {
        uint8_t trailerBlock = sector * 4 + 3;
        int8_t keyIdx = -1;

        // A failed auth deselects the card on the PN532, so re-select before each key try.
        for (uint8_t k = 0; k < NUM_KEYS; k++) {
            uint8_t u[7];
            uint8_t uLen = 0;
            if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, u, &uLen, 200)) continue;
            if (nfc.mifareclassic_AuthenticateBlock(u, uLen, trailerBlock, 0,
                                                   (uint8_t *)KNOWN_KEYS[k])) {
                keyIdx = k;
                break;
            }
        }

        Serial.print("Sector ");
        if (sector < 10) Serial.print(' ');
        Serial.print(sector);
        if (keyIdx < 0) {
            Serial.println("  [all known keys failed]");
            continue;
        }
        Serial.print("  KEYA=");
        printHex(KNOWN_KEYS[keyIdx], 6);
        Serial.print("  (");
        Serial.print(KNOWN_KEY_NAMES[keyIdx]);
        Serial.println(")");

        for (uint8_t b = 0; b < 4; b++) {
            uint8_t block = sector * 4 + b;
            uint8_t data[16];
            if (!nfc.mifareclassic_ReadDataBlock(block, data)) {
                Serial.print("  blk ");
                if (block < 10) Serial.print(' ');
                Serial.print(block);
                Serial.println(" | read failed");
                continue;
            }
            Serial.print("  blk ");
            if (block < 10) Serial.print(' ');
            Serial.print(block);
            Serial.print(" | ");
            printHex(data, 16);
            Serial.print(" | ");
            for (uint8_t i = 0; i < 16; i++) {
                char c = (data[i] >= 0x20 && data[i] < 0x7F) ? (char)data[i] : '.';
                Serial.print(c);
            }
            if (b == 3) Serial.print("  <- trailer");
            else if (sector == 0 && b == 0) Serial.print("  <- manufacturer");
            Serial.println();
        }
    }
    Serial.println("=== End dump ===");
    Serial.println();
}

static void hmacSha256(const uint8_t *key, size_t keyLen,
                       const uint8_t *data, size_t dataLen,
                       uint8_t out[32]) {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    // Second arg = 1 selects HMAC mode rather than plain hash mode.
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, key, keyLen);
    mbedtls_md_hmac_update(&ctx, data, dataLen);
    mbedtls_md_hmac_finish(&ctx, out);
    mbedtls_md_free(&ctx);
}

// HKDF-SHA256 reproducing the Bambu RFID-Tag-Guide kdf(): salt = master constant,
// IKM = card UID, info = "RFID-A\0", output = 16 keys * 6 bytes = 96 bytes total.
static void deriveBambuKeys(const uint8_t *uid, uint8_t uidLen, uint8_t keys[16][6]) {
    static const uint8_t SALT[16] = {
        0x9a, 0x75, 0x9c, 0xf2, 0xc4, 0xf7, 0xca, 0xff,
        0x22, 0x2c, 0xb9, 0x76, 0x9b, 0x41, 0xbc, 0x96
    };
    static const uint8_t INFO[7] = { 'R', 'F', 'I', 'D', '-', 'A', 0x00 };

    // HKDF-Extract: PRK = HMAC-SHA256(salt, IKM)
    uint8_t prk[32];
    hmacSha256(SALT, sizeof(SALT), uid, uidLen, prk);

    // HKDF-Expand: T(i) = HMAC-SHA256(PRK, T(i-1) || info || i), 3 rounds give 96 bytes.
    uint8_t okm[96];
    uint8_t prev[32];
    size_t prevLen = 0;
    for (uint8_t i = 1; i <= 3; i++) {
        uint8_t buf[32 + sizeof(INFO) + 1];
        size_t off = 0;
        memcpy(buf + off, prev, prevLen); off += prevLen;
        memcpy(buf + off, INFO, sizeof(INFO)); off += sizeof(INFO);
        buf[off++] = i;

        uint8_t t[32];
        hmacSha256(prk, sizeof(prk), buf, off, t);
        memcpy(okm + (i - 1) * 32, t, 32);
        memcpy(prev, t, 32);
        prevLen = 32;
    }

    for (uint8_t k = 0; k < 16; k++) {
        memcpy(keys[k], okm + k * 6, 6);
    }
}

static void dumpBambuCard(const uint8_t *uid, uint8_t uidLen) {
    if (uidLen != 4) {
        Serial.println("Bambu tags use 4-byte UIDs; got a different length, skipping.");
        return;
    }

    uint8_t keys[16][6];
    deriveBambuKeys(uid, uidLen, keys);

    Serial.println();
    Serial.println("=== Bambu filament tag dump (HKDF-derived keys) ===");
    Serial.println("Derived per-sector KEYA values:");
    for (uint8_t s = 0; s < 16; s++) {
        Serial.print("  sector ");
        if (s < 10) Serial.print(' ');
        Serial.print(s);
        Serial.print(" KEYA = ");
        printHex(keys[s], 6);
        Serial.println();
    }
    Serial.println();

    for (uint8_t sector = 0; sector < 16; sector++) {
        uint8_t trailerBlock = sector * 4 + 3;

        // Re-select the card before each sector to keep state predictable across the dump.
        uint8_t u[7];
        uint8_t uLen = 0;
        if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, u, &uLen, 200)) {
            Serial.print("Sector ");
            Serial.print(sector);
            Serial.println(": card lost, aborting.");
            return;
        }

        bool authed = nfc.mifareclassic_AuthenticateBlock(u, uLen, trailerBlock, 0, keys[sector]);

        Serial.print("Sector ");
        if (sector < 10) Serial.print(' ');
        Serial.print(sector);
        if (!authed) {
            Serial.println("  [auth FAILED with derived key]");
            continue;
        }
        Serial.println();

        for (uint8_t b = 0; b < 4; b++) {
            uint8_t block = sector * 4 + b;
            uint8_t data[16];
            if (!nfc.mifareclassic_ReadDataBlock(block, data)) {
                Serial.print("  blk ");
                if (block < 10) Serial.print(' ');
                Serial.print(block);
                Serial.println(" | read failed");
                continue;
            }
            Serial.print("  blk ");
            if (block < 10) Serial.print(' ');
            Serial.print(block);
            Serial.print(" | ");
            printHex(data, 16);
            Serial.print(" | ");
            for (uint8_t i = 0; i < 16; i++) {
                char c = (data[i] >= 0x20 && data[i] < 0x7F) ? (char)data[i] : '.';
                Serial.print(c);
            }
            if (b == 3) Serial.print("  <- trailer");
            else if (sector == 0 && b == 0) Serial.print("  <- manufacturer");
            Serial.println();
        }
    }
    Serial.println("=== End dump ===");
    Serial.println();
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

        dumpBambuCard(uid, uidLength);

        // Wait for removal so we don't re-dump in a tight loop while the card sits on the reader.
        Serial.println("Remove card to dump the next one...");
        uint8_t u[7];
        uint8_t uLen;
        while (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, u, &uLen, 200)) delay(100);
        Serial.println("Card removed.");
    }
}
