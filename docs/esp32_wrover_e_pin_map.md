# RFID Jukebox Mainboard — Pin Allocation

**MCU module:** ESP32-WROVER-E
**Purpose:** revised GPIO map grouping each peripheral onto a single side of the module for easier PCB routing.
**Date:** 2026-06-08

> This is a proposed starting map. Verify the left/right pin columns against the WROVER-E mechanical drawing before committing, and confirm the strapping-pin notes below.

---

## Right side — SPI cluster (SD card + TFT display)

| Signal | GPIO | Bus / function | Direction | Notes |
|---|---|---|---|---|
| SCK | GPIO18 | SPI (VSPI) | out | Shared SD + TFT. Native pin → fast SPI. |
| MISO | GPIO19 | SPI (VSPI) | in | Native pin. Series 33 Ω placed at the SD end. |
| MOSI | GPIO23 | SPI (VSPI) | out | Shared SD + TFT. Native pin. |
| TF_CS | GPIO5 | SPI chip select (SD) | out | Native CS. Idles high — matches IO5 boot requirement. |
| TFT_CS | GPIO22 | SPI chip select (TFT) | out | Any GPIO. |
| TFT_DC | GPIO21 | TFT data/command | out | Any GPIO. |
| TFT_RST | GPIO4 | TFT reset | out | Any GPIO. |
| TFT_BL | GPIO13 | TFT backlight (PWM) | out | Non-strapping; LEDC PWM. Physically module pin 16 (left side) — the BL trace crosses to the right-edge TFT connector (fine for a slow PWM line). |

## Left side — audio, encoder, NFC, battery sense

| Signal | GPIO | Bus / function | Direction | Notes |
|---|---|---|---|---|
| ENC_CLK | GPIO36 | Rotary encoder A | in | **Input-only.** KY-040 module provides the pull-up. |
| ENC_DT | GPIO39 | Rotary encoder B | in | **Input-only.** KY-040 module provides the pull-up. |
| ENC_SW | GPIO34 | Encoder push switch | in | **Input-only, no internal pull-up — add an external ~10 kΩ to 3V3.** |
| VBAT_SENSE | GPIO35 | Battery voltage (ADC1) | in | Must be an ADC1 pin (ADC2 is unusable with Wi-Fi on). Reads the R16/R17 divider. |
| I2S_BCLK | GPIO32 | I²S bit clock | out | To MAX98357A. |
| I2S_LRCLK | GPIO33 | I²S word/LR clock | out | To MAX98357A. |
| I2S_DOUT | GPIO25 | I²S data out | out | To MAX98357A. |
| PN532_TX | GPIO27 | UART2 RX (ESP receives) | in | PN532 TX → ESP RX. |
| PN532_RX | GPIO26 | UART2 TX (ESP transmits) | out | ESP TX → PN532 RX. |

## Bottom edge — USB, programming, boot (fixed)

| Signal | GPIO | Bus / function | Notes |
|---|---|---|---|
| TXD0 | GPIO1 | UART0 TX | To CH340C. Programming / console — do not reassign. |
| RXD0 | GPIO3 | UART0 RX | To CH340C. Do not reassign. |
| IO0 | GPIO0 | Boot select / auto-flash | **Strapping pin.** To the UMH3N auto-reset transistor. |
| EN | EN | Chip reset / auto-flash | To UMH3N + RC + reset button. |

## Spare / reserved

| GPIO | Status |
|---|---|
| GPIO2 | Free (right side). **Strapping pin** (boot mode, default pull-down). Fine as a spare output provided nothing pulls it high at reset. |
| GPIO14 | Free (left side). |
| GPIO15 | Free (right side). **Strapping pin** (MTDO, default pull-up). Spare; if reused, keep it from being forced low at reset (suppresses boot log). |
| GPIO12 | Leave unconnected — flash-voltage strapping pin (handled internally on WROVER). |
| GPIO16 / GPIO17 | Not available — used internally by the WROVER PSRAM. |

---

## Constraints honored

- **Input-only pins (GPIO34–39):** used only for inputs (encoder, ADC). They have no internal pull-ups, hence the external pull-up note on ENC_SW.
- **ADC with Wi-Fi:** battery sense is on ADC1 (GPIO35). ADC2 pins cannot be sampled while Wi-Fi is active.
- **Native VSPI pins (18/19/23/5):** used for the shared SPI bus so the TFT can run at full SPI speed. If fast SPI is not needed, these can be remapped freely via the GPIO matrix.
- **Strapping pins:** GPIO0, GPIO2, GPIO5, GPIO12, GPIO15. Ensure nothing external forces a bad logic level at reset. Only GPIO5 (SD_CS) carries a peripheral and it idles high; the backlight moved off the straps entirely, so GPIO2 and GPIO15 are spare.
- **UART0 (GPIO1/GPIO3) + IO0/EN** are reserved for the CH340C programming path and the auto-reset circuit.

## Firmware pin defines (Arduino / ESP-IDF style)

```cpp
// ---- Shared SPI bus (VSPI): SD card + TFT ----
#define SCK   18       // core also defines these; keep values = ESP32 VSPI defaults
#define MISO  19
#define MOSI  23

// ---- TFT (ST7789V) + SD chip selects ----
#define SD_CS      5
#define TFT_CS    22
#define TFT_DC    21
#define TFT_RST    4
#define TFT_BL    13   // backlight (PWM) — non-strapping (module pin 16, left side)

// ---- Rotary encoder (KY-040) ----
#define ENC_CLK   36   // input-only
#define ENC_DT    39   // input-only
#define ENC_SW    34   // input-only, external pull-up

// ---- Battery monitor (new) ----
#define VBAT_SENSE 35  // ADC1

// ---- I2S audio (MAX98357A) ----
#define I2S_BCLK  32
#define I2S_LRC   33
#define I2S_DOUT  25

// ---- PN532 NFC over UART2 ----
#define PN532_TX  27   // PN532 TX → ESP32 RX
#define PN532_RX  26   // ESP32 TX → PN532 RX

// Bring-up:
//   SPI.begin(SCK, MISO, MOSI, SD_CS);
//   Serial2.begin(115200, SERIAL_8N1, PN532_TX, PN532_RX);
```

## Layout grouping summary

- **Right edge:** SD card socket + TFT connector (SPI + display control).
- **Left edge:** MAX98357A + speaker, PN532 connector, encoder connector, battery-sense trace.
- **Bottom edge:** USB-C + ESD array + CH340C, power path, charger, regulator, battery connector.
- Route signals first, then pour ground last on the bottom layer.