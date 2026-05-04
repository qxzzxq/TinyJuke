# CLAUDE.md — ESP Jukebox

## Project overview

RFID-driven audio player. Scan an NFC tag → lookup UID in `tags.json` on SD card → play the mapped WAV file through a MAX98357A I²S amplifier. Built on a Lolin D32 Pro (ESP32) with Arduino framework on PlatformIO.

**Status:** Milestone 1 complete — NFC tag reading, SD card WAV playback, TFT display all functional.

## Pin map

| GPIO | Function          | Notes                                    |
|------|-------------------|------------------------------------------|
| 4    | SD_CS             | Onboard microSD slot                     |
| 14   | TFT_CS            | Display chip select (on-board TFT port)  |
| 18   | SCK (VSPI)        | Shared: TFT SCL + SD_SCK                 |
| 19   | MISO (VSPI)       | SD only (ST7735S is write-only)          |
| 23   | MOSI (VSPI)       | Shared: TFT SDA + SD_MOSI                |
| 27   | TFT_DC            | Display data/command (on-board TFT port) |
| 32   | TFT_BL            | Display backlight (on-board TFT port)    |
| 33   | TFT_RST           | Display reset (on-board TFT port)        |
| 13   | PN532_RX (ESP TX) | ESP32 TX → PN532 RX                     |
| 22   | PN532_TX (ESP RX) | PN532 TX → ESP32 GPIO 22 (UART2 RX)     |
| 21   | I2S_BCLK          | MAX98357A BCLK                           |
| 25   | I2S_DOUT          | MAX98357A DIN                            |
| 26   | I2S_LRC           | MAX98357A LRC                            |
| 36   | ENC_CLK           | Rotary encoder (KY-040)                  |
| 5    | ENC_DT            | Rotary encoder direction                 |
| 34   | ENC_SW            | Rotary encoder button (input-only)       |
| 35   | VBAT              | Battery voltage sense (unused)           |

MAX98357A config: GAIN → GND (12 dB), SD/Mode → float (mono mix). Volume is software-controlled in the WAV player (not yet adjustable at runtime).

KY-040 encoder: CLK→GPIO36, DT→GPIO5, SW→GPIO34, +→3.3V, GND→GND. GPIO 34 and 36 are input-only but the module's external 10k pull-up resistors make them work.

## Hardware

| Part            | Model                     |
|-----------------|---------------------------|
| Microcontroller | Lolin D32 Pro (ESP32)     |
| NFC reader      | Elechouse PN532 (HSU)     |
| Audio amp       | MAX98357A (I²S, mono)     |
| Display         | 1.8" ST7735S, 128×160 SPI |
| Storage         | microSD (FAT32) onboard   |

## VSPI bus sharing

**Critical lesson:** The TFT and SD card share the VSPI bus (GPIO 18/19/23). Both must use the same SPI driver path.

- `Arduino_ESP32SPI` (TFT) and `SD.h` (SD card) both use **bare-metal SPI** via `_spi_bus_array[VSPI]` — direct register writes.
- `Arduino_ESP32QSPI` (old round display) used the **ESP-IDF SPI driver** (`spi_bus_initialize` / `spi_device_transmit`), which is incompatible with bare-metal SPI on the same host.
- Init order matters: `gfx.begin()` zeroes VSPI registers via `spiInitBus()`. SD must be initialized **after** TFT so it reconfigures VSPI. During normal operation, each device reconfigures the bus before use (TFT via `beginWrite()`, SD via `beginTransaction()`).
- If switching back to a QSPI display, both TFT and SD must use the ESP-IDF driver path. The Arduino SD library (`SD.h`) will not work alongside an ESP-IDF-managed bus.

## Source files

```
src/
├── config.h          — Pin definitions, colors, D32 Pro macro fix
├── audio.h/.cpp      — WavHeader, parseWavHeader, i2sInit, playWav(), stopPlayback()
├── screen.h/.cpp     — TFT draw functions (extern gfx, uses C_ color constants)
├── tags.h/.cpp       — tagDoc (JsonDocument), uid formatting, lookupTag()
├── encoder.h/.cpp    — Rotary encoder reader (placeholder, not yet wired)
└── main.cpp          — Peripherals (bus, gfx, nfc), setup(), loop(), tagPresent
platformio.ini        — PlatformIO project config + library dependencies
README.md             — User-facing docs (wiring, build steps, SD layout)
```

`playWav()` accepts a `PN532 &nfc` reference parameter so audio.cpp doesn't depend on a global NFC object. `stopRequested` and `audioPlaying` are global flags in audio.cpp, checked by main.cpp's loop.

## Libraries (platformio.ini)

| Library          | Source                                        | Purpose              |
|------------------|-----------------------------------------------|----------------------|
| Arduino_GFX      | `moononournation/Arduino_GFX.git`             | TFT display (ST7735) |
| ArduinoJson      | `bblanchon/ArduinoJson @ ^7`                  | Parse `/tags.json`   |
| PN532 + PN532_HSU| Bundled in `lib/`                             | NFC reader           |
| SD               | Built-in (Arduino ESP32 framework)            | SD card access       |

WAV audio uses the ESP32's built-in I2S driver (`driver/i2s.h` — legacy API, deprecated but functional). No external audio library is needed.

## main.cpp architecture

**setup() flow:**
1. TFT init (`gfx.begin()` — initializes VSPI via bare-metal SPI)
2. SD mount (`SD.begin(4)`, reads `/tags.json` into `JsonDocument tagDoc`)
3. Boot screen on TFT (SD error or waiting screen)
4. PN532 init with firmware version check + raw-byte diagnostic on failure
5. `nfc.SAMConfig()`, draw waiting screen
6. `initEncoder()` — placeholder, no-op until encoder is wired

**loop() state machine:**
- `!tagPresent && found` → tag arrived: lookup UID → draw now-playing → `playWav()` → draw waiting
- `tagPresent && !found` → tag removed: stop playback → draw waiting
- No tag while audio plays: NFC polling happens inside `playWav()` every ~150ms

**playWav() flow:**
1. Open WAV file via `SD.open()`, parse header (RIFF/fmt/data chunks)
2. Configure I2S to match file's sample rate / bits / channels
3. Stream PCM in 2KB chunks: `f.read()` → `i2s_write()`
4. Poll NFC for tag removal every ~150ms (3 consecutive misses → stop)
5. Teardown I2S on exit

**UID matching:** `uidToStr()` produces colon-separated hex (`04:A2:24:B2:C3:80:81`) to match keys in `tags.json`. `lookupTag()` uses ArduinoJson's `tagDoc[key].isNull()` check.

## SD card layout

```
/
├── img/                # Album art (128×160 BMP, 24-bit)
│   └── album1.bmp
├── music/              # WAV files (standard PCM, any sample rate)
│   └── song.wav
└── tags.json           # UID → file + metadata mapping
```

`tags.json` format (only `file` is required):
```json
{
  "81:0C:2B:07": {
    "file": "music/sample-12s.wav",
    "img": "album1.bmp",
    "title": "My Song",
    "artist": "Artist Name",
    "album": "Album Title"
  }
}
```

Paths may or may not start with `/` — `playWav()` prepends it if missing.

## Build & flash

```bash
~/.platformio/penv/bin/pio run              # build
~/.platformio/penv/bin/pio run -t upload    # flash
~/.platformio/penv/bin/pio device monitor   # serial (115200 baud)
```

Board: `lolin_d32_pro`, framework: `arduino`, CPU: 240 MHz.

## Known constraints

- **D32 Pro `SS` macro conflict:** `pins_arduino.h` defines `#define SS TF_CS` (→ `#define SS 4`). Libraries that use `SS` as a parameter name will fail to compile. Avoid libraries affected by this, or `#undef SS` before including them.
- **TFT macros conflict:** D32 Pro variant pre-defines `TFT_CS=14`, `TFT_DC=27`, `TFT_RST=33`. `config.h` includes `<Arduino.h>` first, then `#undef`s the variant values, then redefines ours.
- **I2S uses legacy driver:** The `driver/i2s.h` API is deprecated in ESP-IDF 5.x. It works but emits warnings. Migration to `i2s_std.h` is a future task.
- **Single audio track at a time:** No crossfade or queue. Scanning a new tag stops the current track. Tag must be removed before a new tag is accepted.
- **WAV only:** Standard PCM WAV (16/24-bit, mono/stereo, any sample rate). No MP3/FLAC support.
