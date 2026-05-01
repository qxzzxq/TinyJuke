// ESP Jukebox — NFC tag reader + TFT display demo
//
// Displays scanned NFC tag UIDs on a 1.53" round TFT (ST77916, 360x360, QSPI).
//
// Wiring (UART must cross):
//   PN532 TX  -> ESP32 RX  (GPIO 33)
//   PN532 RX  <- ESP32 TX  (GPIO 32)
//   PN532 VCC              <- 3V3
//   PN532 GND              <- GND

#include <Arduino.h>
#include <PN532_HSU.h>
#include <PN532.h>

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

// QSPI bus: CS, SCK, IO0/MOSI, IO1/MISO, IO2/WP, IO3/HD
#define TFT_CS   5
#define TFT_SCK 18
#define TFT_IO0 23
#define TFT_IO1 19
#define TFT_IO2 21
#define TFT_IO3 22
#define TFT_RST 14
#define TFT_BL  13

Arduino_ESP32QSPI bus(TFT_CS, TFT_SCK, TFT_IO0, TFT_IO1, TFT_IO2, TFT_IO3);
Arduino_ST77916 gfx(&bus, TFT_RST, 0 /* rotation */, false /* ips */,
                    360, 360,
                    0, 0, 0, 0,
                    st77916_150_init_operations, sizeof(st77916_150_init_operations));

// --- Helpers ---

static const uint16_t BG_COLOR    = 0x2104;  // dark navy
static const uint16_t TEXT_COLOR   = 0xFFFF;  // white
static const uint16_t ACCENT_COLOR = 0x07E0;  // green
static const uint16_t DIM_COLOR    = 0x8410;  // dim gray

// Arduino_GFX does not have textWidth(); use getTextBounds() to measure.
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

static void drawWaitingScreen() {
    gfx.fillScreen(BG_COLOR);
    centerText("ESP Jukebox", 40, TEXT_COLOR, 2);
    centerText("Waiting for", 160, DIM_COLOR, 2);
    centerText("tag...", 190, DIM_COLOR, 2);
}

static void drawTagScreen(const uint8_t *uid, uint8_t uidLen) {
    gfx.fillScreen(BG_COLOR);

    centerText("TAG DETECTED", 40, ACCENT_COLOR, 2);

    // UID byte count
    char lenStr[16];
    snprintf(lenStr, sizeof(lenStr), "UID (%d bytes)", uidLen);
    centerText(lenStr, 90, TEXT_COLOR, 1);

    // UID hex string
    char uidStr[64];
    uidToStr(uid, uidLen, uidStr);

    // Pick font size that fits in the round display; split long UIDs to two lines
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

    centerText("Remove tag...", 300, DIM_COLOR, 1);
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
    drawWaitingScreen();

    // --- PN532 init ---
    Serial2.begin(115200, SERIAL_8N1, PN532_TX, PN532_RX);
    nfc.begin();

    uint32_t versiondata = nfc.getFirmwareVersion();
    if (!versiondata) {
        Serial.println("Did not find PN532.");
        gfx.fillScreen(BG_COLOR);
        centerText("PN532", 140, 0xF800, 2);      // red
        centerText("NOT FOUND", 170, 0xF800, 2);

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
    Serial.println("Waiting for an ISO14443A card...");
}

// --- Loop ---

void loop() {
    uint8_t uid[7] = {0};
    uint8_t uidLength = 0;

    bool found = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 500);

    if (found) {
        Serial.print("Card detected. UID (");
        Serial.print(uidLength, DEC);
        Serial.print(" bytes): ");
        printHex(uid, uidLength);
        Serial.println();

        drawTagScreen(uid, uidLength);

        // Wait for tag removal
        Serial.println("Remove tag...");
        uint8_t u[7];
        uint8_t uLen;
        while (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, u, &uLen, 200)) {
            delay(100);
        }
        Serial.println("Card removed.");

        drawWaitingScreen();
    }
}
