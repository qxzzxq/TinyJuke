# CLAUDE.md — ESP Jukebox

## Project overview

RFID-driven audio player. Scan an NFC tag → lookup UID in `tags.json` on SD card → play the mapped WAV file through a MAX98357A I²S amplifier. Built on a Lolin D32 Pro (ESP32) with Arduino framework on PlatformIO.

**Status:** Milestone 2 complete — web-based tag management, runtime volume control, encoder-driven GUI all functional.

## Pin map

| GPIO | Function          | Notes                                    |
|------|-------------------|------------------------------------------|
| 4    | SD_CS             |                                          |
| 14   | TFT_CS            |                                          |
| 18   | SCK (VSPI)        | Shared: TFT SCL + SD_SCK                 |
| 19   | MISO (VSPI)       | SD only (ST7735S is write-only)          |
| 23   | MOSI (VSPI)       | Shared: TFT SDA + SD_MOSI                |
| 27   | TFT_DC            |                                          |
| 32   | TFT_BL            |                                          |
| 33   | TFT_RST           |                                          |
| 13   | PN532_RX (ESP TX) | ESP32 TX → PN532 RX                     |
| 22   | PN532_TX (ESP RX) | PN532 TX → ESP32 GPIO 22 (UART2 RX)     |
| 21   | I2S_BCLK          |                                          |
| 25   | I2S_DOUT          |                                          |
| 26   | I2S_LRC           |                                          |
| 36   | ENC_CLK           |                                          |
| 5    | ENC_DT            |                                          |
| 34   | ENC_SW            | Input-only (external pull-up on module)  |
| 35   | VBAT              | Unused                                   |

MAX98357A config: GAIN → GND (12 dB), SD/Mode → float (mono mix). Volume is software-controlled via encoder (runtime adjustable, persisted to `/volume.cfg` on SD).

KY-040 encoder: CLK→GPIO36, DT→GPIO5, SW→GPIO34, +→3.3V, GND→GND. GPIO 34 and 36 are input-only but the module's external 10k pull-up resistors make them work.

## VSPI bus sharing

**Critical lesson:** The TFT and SD card share the VSPI bus (GPIO 18/19/23). Both must use the same SPI driver path.

- `Arduino_ESP32SPI` (TFT) and `SD.h` (SD card) both use **bare-metal SPI** via `_spi_bus_array[VSPI]` — direct register writes.
- `Arduino_ESP32QSPI` (old round display) used the **ESP-IDF SPI driver** (`spi_bus_initialize` / `spi_device_transmit`), which is incompatible with bare-metal SPI on the same host.
- Init order matters: `gfx.begin()` zeroes VSPI registers via `spiInitBus()`. SD must be initialized **after** TFT so it reconfigures VSPI. During normal operation, each device reconfigures the bus before use (TFT via `beginWrite()`, SD via `beginTransaction()`).

## Source files

```
src/
├── config.h          — Pin definitions, colors, D32 Pro macro fix
├── audio.h/.cpp      — WavHeader, WavMeta, playWav(), stopPlayback(), parseWavMeta()
├── screen.h/.cpp     — TFT draw functions (extern gfx, uses C_ color constants)
├── tags.h/.cpp       — tagDoc (JsonDocument), uid formatting, lookupTag()
├── encoder.h/.cpp    — Rotary encoder (ISR quadrature, button state machine, volume save/load)
├── gui.h/.cpp        — Management mode (menu, volume, web server screens)
├── web.h/.cpp        — WiFi AP, REST API, file upload, SPA HTML page
└── main.cpp          — Peripherals (bus, gfx, nfc), setup(), loop()
platformio.ini        — PlatformIO project config + library dependencies
README.md             — User-facing docs (wiring, build steps, SD layout)
```

`playWav()` accepts a `PN532 &nfc` reference parameter so audio.cpp doesn't depend on a global NFC object. `stopRequested` and `audioPlaying` are global flags in audio.cpp. `handleWebClient()` is called during playback to service HTTP requests.

## Libraries (platformio.ini)

| Library          | Source                                        | Purpose              |
|------------------|-----------------------------------------------|----------------------|
| Arduino_GFX      | `moononournation/Arduino_GFX.git`             | TFT display (ST7735) |
| ArduinoJson      | `bblanchon/ArduinoJson @ ^7`                  | Parse `/tags.json`   |
| PN532 + PN532_HSU| Bundled in `lib/`                             | NFC reader           |
| SD               | Built-in (Arduino ESP32 framework)            | SD card access       |
| WiFi + WebServer | Built-in (Arduino ESP32 framework)            | AP mode + REST API   |

WAV audio uses the ESP32's built-in I2S driver (`driver/i2s.h` — legacy API, deprecated but functional). No external audio library is needed.

## Encoder events

`readEncoder()` returns:
- `0` — no event
- `±N` — N full detents clockwise (positive) or counter-clockwise (negative)
- `100` (`ENC_CLICK`) — short press (<600ms release)
- `101` (`ENC_HOLD`) — long press (>600ms, fires on hold, not on release)

## main.cpp architecture

**setup() flow:**
1. TFT init (`gfx.begin()` — initializes VSPI via bare-metal SPI)
2. SD mount (`SD.begin(4)`, reads `/tags.json` into `JsonDocument tagDoc`)
3. Boot screen on TFT (SD error or waiting screen)
4. PN532 init with firmware version check + raw-byte diagnostic on failure
5. `nfc.SAMConfig()`, draw waiting screen
6. `initEncoder()` — loads saved volume from `/volume.cfg`, attaches ISR interrupts for quadrature decoding

**loop() state machine:**
- Management mode active (`guiActive()`) → delegate to `guiLoop()` (menu, volume, web server)
- Jukebox mode: read encoder for volume adjustment (rotation) or save (click) or enter menu (hold)
- `!tagPresent && found` → tag arrived: lookup UID → draw now-playing → `playWav()` → draw waiting
- `tagPresent && !found` → tag removed: stop playback → draw waiting
- Unknown tag: 10-second dismiss screen with click/hold to dismiss or tag removal
- No tag while audio plays: NFC polling happens inside `playWav()` every ~150ms

**playWav() flow:**
1. Open WAV file via `SD.open()`, parse header (RIFF/fmt/data chunks)
2. Configure I2S to match file's sample rate / bits / channels
3. Stream PCM in 2KB chunks: `f.read()` → volume-scale 16-bit samples → `i2s_write()`
4. Per-chunk: service web client, check encoder for volume changes (draw overlay), poll NFC for tag removal every ~150ms (3 consecutive misses → stop)
5. Teardown I2S on exit

**UID matching:** `uidToStr()` produces colon-separated hex (`04:A2:24:B2:C3:80:81`) to match keys in `tags.json`. `lookupTag()` uses ArduinoJson's `tagDoc[key].isNull()` check.

## SD card layout

```
/
├── img/                # Album art (BMP, 24-bit, auto-scaled to 128×128)
│   └── album1.bmp
├── music/              # WAV files (standard PCM, any sample rate)
│   └── song.wav
├── tags.json           # UID → file + metadata mapping (managed by web UI)
└── volume.cfg          # Persisted volume level 0–100 (plain text)
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

## Dev workflow

**After every code change:**
1. **Verify it compiles** — run `~/.platformio/penv/bin/pio run` and fix any syntax or compile-time errors before considering the change complete. Never leave the project in a state that fails to build.
2. **Cross-validate docs** — check CLAUDE.md and README.md against the actual source files. Update any stale descriptions — pin maps, source file trees, architecture flows, status, constraints, SD card layout, and TODO lists. These documents are the source of truth for future agents and contributors; drift between docs and code compounds over time.

## Known constraints

- **D32 Pro `SS` macro conflict:** `pins_arduino.h` defines `#define SS TF_CS` (→ `#define SS 4`). Libraries that use `SS` as a parameter name will fail to compile. Avoid libraries affected by this, or `#undef SS` before including them.
- **TFT macros conflict:** D32 Pro variant pre-defines `TFT_CS=14`, `TFT_DC=27`, `TFT_RST=33`. `config.h` includes `<Arduino.h>` first, then `#undef`s the variant values, then redefines ours.
- **I2S uses legacy driver:** The `driver/i2s.h` API is deprecated in ESP-IDF 5.x. It works but emits warnings. Migration to `i2s_std.h` is a future task.
- **Single audio track at a time:** No crossfade or queue. Scanning a new tag stops the current track. Tag must be removed before a new tag is accepted.
- **WAV only:** Standard PCM WAV (16/24-bit, mono/stereo, any sample rate). No MP3/FLAC support.
- **Web server uses AP mode:** `WIFI_SSID` / `WIFI_PASSWORD` from config.h. Exposes REST API (`/api/tags`, `/api/files`, `/api/images`, `/upload`) and serves a single-page web app for tag management.
- **Heap is tight:** BMP loader allocates `128×128×2` bytes (32 KB), `playWav()` allocates a 2 KB DMA buffer. Avoid additional large heap allocations; prefer stack or static buffers where possible.
