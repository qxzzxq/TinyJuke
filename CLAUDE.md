# CLAUDE.md — ESP Jukebox

## Project overview

RFID-driven audio player. Scan an NFC tag → lookup UID in `tags.json` on SD card → play the mapped WAV file through a MAX98357A I²S amplifier. Built on a Lolin D32 Pro (ESP32) with Arduino framework on PlatformIO. Display: ST7789V 240×320.

**Status:** Milestone 3 in progress — tag hot-swap detection, brightness / power-save / sleep-timer settings persisted to SD, BMP album art (240×240, scaled in PSRAM), version screen, image upload, browser-side MP3/M4A/AAC/OGG/FLAC → WAV conversion with embedded-art extraction, and amp-touch-noise mitigation via I2S priming.

## Pin map

| GPIO | Function          | Notes                                    |
|------|-------------------|------------------------------------------|
| 4    | SD_CS             |                                          |
| 14   | TFT_CS            |                                          |
| 18   | SCK (VSPI)        | Shared: TFT SCL + SD_SCK                 |
| 19   | MISO (VSPI)       | SD only (ST7789V is write-only)          |
| 23   | MOSI (VSPI)       | Shared: TFT SDA + SD_MOSI                |
| 27   | TFT_DC            |                                          |
| 32   | TFT_BL            | LEDC PWM (brightness control)          |
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

MAX98357A config: GAIN → GND (12 dB), SD/Mode → float (mono mix). Volume is software-controlled via encoder (runtime adjustable, persisted to `/volume.cfg` on SD). Brightness is also software-controlled via encoder (persisted to `/brightness.cfg` on SD). Power saving (persisted to `/powersave.cfg`) turns off the display after idle timeout. Audio sleep timer (persisted to `/sleeptimer.cfg`) stops playback after the configured duration.

KY-040 encoder: CLK→GPIO36, DT→GPIO5, SW→GPIO34, +→3.3V, GND→GND. GPIO 34 and 36 are input-only but the module's external 10k pull-up resistors make them work.

## VSPI bus sharing

**Critical lesson:** The TFT and SD card share the VSPI bus (GPIO 18/19/23). Both must use the same SPI driver path.

- `Arduino_ESP32SPI` (TFT) and `SD.h` (SD card) both use **bare-metal SPI** via `_spi_bus_array[VSPI]` — direct register writes.
- `Arduino_ESP32QSPI` (old round display) used the **ESP-IDF SPI driver** (`spi_bus_initialize` / `spi_device_transmit`), which is incompatible with bare-metal SPI on the same host.
- Init order matters: `gfx.begin()` zeroes VSPI registers via `spiInitBus()`. SD must be initialized **after** TFT so it reconfigures VSPI. During normal operation, each device reconfigures the bus before use (TFT via `beginWrite()`, SD via `beginTransaction()`).

## Source files

```
src/
├── config.h          — Pin definitions, colors, D32 Pro macro fix, brightness/volume/sleep defaults
├── audio.h/.cpp      — playWav(), stopPlayback(), parseWavMeta() (SD-backed thin wrappers around wav_parser)
├── wav_parser.h/.cpp — Pure buffer-based WAV header + LIST/INFO metadata parsers (testable on native)
├── screen.h/.cpp     — TFT draw functions for 240×320 (extern gfx, uses C_ color constants)
├── tags.h            — TagInfo struct + declarations
├── tags.cpp          — sdReady, printHex (Arduino/Serial-coupled)
├── tag_utils.cpp     — tagDoc, uidToStr(), lookupTag() (pure, testable on native)
├── encoder.h/.cpp    — Rotary encoder (ISR quadrature, button state machine, volume/brightness/power-save/sleep-timer save/load)
├── encoder_gray.h    — Pure gray-code transition helper used by the encoder ISR (testable on native)
├── value_array.h     — Pure helpers for "fixed list of option values" lookups (power-save/sleep-timer)
├── timer_logic.h     — Pure timer policy: sleepTimerShouldFire / powerSaveShouldSleep / timerRemainingMs / formatCountdownMMSS
├── jukebox_state.h/.cpp — Pure top-level FSM (Waiting / Sleeping + sleepStopped / tagPresent flags) driving loop()'s decisions
├── gui.h/.cpp        — Management mode (menu, volume, brightness, power saving, sleep timer, version, web server screens)
├── web.h/.cpp        — WiFi AP, REST API, file upload, SPA HTML page
└── main.cpp          — Peripherals (bus, gfx, nfc), setup(), loop(), sleep/wake logic
test/test_pure/       — Unity tests for pure logic; runs on host via `pio test -e native`
platformio.ini        — PlatformIO project config + library dependencies (envs: release, debug, native)
README.md             — User-facing docs (wiring, build steps, SD layout)
```

`playWav(filepath, nfc, tagUid, tagUidLen)` takes the active tag's UID so the per-150ms NFC poll can distinguish "tag absent" (3 misses → stop) from "tag swapped" (different UID → stop). Globals in audio.cpp: `audioPlaying`, `stopRequested`, `sleepTimerFired`, `audioStartTime`. `handleWebClient()` is not called during playback; the web server only runs while the GUI is on the WEB screen.

## Libraries (platformio.ini)

| Library          | Source                                        | Purpose              |
|------------------|-----------------------------------------------|----------------------|
| Arduino_GFX      | `moononournation/Arduino_GFX.git`             | TFT display (ST7789) |
| ArduinoJson      | `bblanchon/ArduinoJson @ ^7`                  | Parse `/tags.json`   |
| PN532 + PN532_HSU| Bundled in `lib/`                             | NFC reader           |
| SD               | Built-in (Arduino ESP32 framework)            | SD card access       |
| WiFi + WebServer | Built-in (Arduino ESP32 framework)            | AP mode + REST API   |

WAV audio uses the ESP32's built-in I2S driver (`driver/i2s.h` — legacy API, deprecated but functional). No external audio library is needed.

## Encoder events

`readEncoder()` returns:
- `0` (`ENC_NONE`) — no event
- `±N` — N full detents clockwise (positive) or counter-clockwise (negative)
- `100` (`ENC_CLICK`) — short press (<600ms release)
- `101` (`ENC_HOLD`) — long press (>600ms, fires on hold, not on release)

## main.cpp architecture

**setup() flow:**
1. LEDC attach + backlight off, then TFT init (`gfx.begin()` — initializes VSPI via bare-metal SPI). Backlight is held at 0 during boot draw to suppress power-on noise.
2. SD mount (`SD.begin(4)`, reads `/tags.json` into `JsonDocument tagDoc`)
3. Boot screen on TFT (SD error or waiting screen), then turn backlight on (`ledcWrite(TFT_BL, 255)`).
4. `i2sPrime()` — install I2S driver with a default config so BCLK/LRC/DOUT are actively driven; otherwise the MAX98357A picks up touch-coupled noise.
5. PN532 init with firmware version check + raw-byte diagnostic on failure (currently `while(true) delay(1000)` on failure — known unrecoverable hang).
6. `nfc.SAMConfig()`
7. `initEncoder()` — loads saved volume from `/volume.cfg`, attaches ISR interrupts for quadrature decoding
8. `loadBrightness()` + `applyBrightness()` — load saved brightness, apply via LEDC PWM
9. `loadPowerSave()` + `loadSleepTimer()` + `resetActivityTimer()` — load power save and audio sleep timeouts, init activity tracking

**loop() state machine:**
- Sleep check: if idle on waiting screen > `powerSaveMinutes`, enter sleep (display off, backlight off). When `sleepStopped` is set (sleep timer fired with tag still present), the tag is treated as absent for sleep purposes.
- Sleep state: poll encoder + NFC for wake; on wake restore display and re-sync. NFC wake is suppressed while `sleepStopped` is true so the still-present tag doesn't immediately re-trigger.
- Management mode active (`guiActive()`) → delegate to `guiLoop()` (menu, volume, brightness, power saving, sleep timer, version, web server). Menu items: **Web Server, Volume, Brightness, Power Saving, Sleep Timer, Version**.
- Jukebox mode: read encoder; HOLD enters menu (saves volume first). Rotation/click during playback are handled inside `playWav()`, not in the main loop.
- `!tagPresent && found && !sleepStopped` → tag arrived: lookup UID → enter `while (tagPresent)` replay loop: draw now-playing, optionally draw sleep-timer countdown, run `playWav()`, then quick NFC re-poll. Same UID = replay; different UID = exit (next iteration handles new arrival); no tag (single miss in this quick check) = exit.
- Sleep timer firing during playback sets `sleepTimerFired` → `sleepStopped`, breaks out of the replay loop and blocks re-trigger until the tag is physically removed.
- `!found && tagPresent` → tag removed: 3-miss debounce, then clear `tagPresent`/`sleepStopped`, stop playback, draw waiting.
- Unknown tag: 10-second dismiss screen with click/hold to dismiss, or tag removal/swap to exit.
- During playback: NFC polling happens inside `playWav()` every ~150ms; a different UID stops the current track for tag-swap handling.

**playWav() flow:**
1. Open WAV file via `SD.open()`, parse header (RIFF/fmt/data chunks; `data` chunk must lie within first 4 KB of file)
2. Configure I2S to match file's sample rate / bits / channels (16-bit and 24-bit nominally supported; **24-bit volume scaling and 24→32-bit unpacking are not implemented** — 24-bit playback is currently broken)
3. Stream PCM in 2 KB chunks: `f.read()` → volume-scale 16-bit samples (float) → mono-to-stereo duplication → `i2s_write()`. `remaining` is decremented by `bytesWritten` (mono-adjusted) so partial DMA writes don't drop data.
4. Per-chunk: check encoder for volume changes (draws bottom-overlay bar; auto-saves volume after 5s idle), update sleep-timer countdown each second, poll NFC every ~150ms (3 consecutive misses → stop; different UID → stop for tag swap)
5. Sleep timer: when `sleepTimerMinutes > 0` and `audioStartTime + sleepTimerMinutes` has elapsed, persist `sleepTimerMinutes = 0`, set `sleepTimerFired`, and stop.
6. Teardown I2S on exit (zero DMA, stop, uninstall driver — `i2sPrime()` will be needed again before next playback to keep amp pins driven; `i2sInit()` does this implicitly).

**UID matching:** `uidToStr()` produces colon-separated hex (`04:A2:24:B2:C3:80:81`) to match keys in `tags.json`. `lookupTag()` uses ArduinoJson's `tagDoc[key].isNull()` check.

## SD card layout

```
/
├── img/                # Album art (BMP, 24-bit, auto-scaled to 240×240)
│   └── album1.bmp
├── music/              # WAV files (standard PCM, any sample rate)
│   └── song.wav
├── tags.json           # UID → file + metadata mapping (managed by web UI)
├── volume.cfg          # Persisted volume level 0–100 (plain text)
├── brightness.cfg      # Persisted brightness level 0–100 (plain text)
├── powersave.cfg        # Persisted power save timeout in minutes (0=off, plain text)
└── sleeptimer.cfg      # Persisted audio sleep timer in minutes (0=off, plain text)
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
~/.platformio/penv/bin/pio run              # build (env:release)
~/.platformio/penv/bin/pio run -e debug     # build with -DDEV_MODE
~/.platformio/penv/bin/pio run -t upload    # flash
~/.platformio/penv/bin/pio device monitor   # serial (115200 baud)
~/.platformio/penv/bin/pio test -e native   # run host-side unit tests
```

Board: `lolin_d32_pro`, framework: `arduino`, CPU: 240 MHz, partitions: `huge_app.csv`, `BOARD_HAS_PSRAM` enabled (used by BMP loader). Three PIO environments: `release` (default), `debug` (`-DDEV_MODE` exposes extra short-timeout options on Power Saving and Sleep Timer screens for testing), and `native` (host-only, runs Unity tests in `test/test_pure/` against pure-logic source files — no Arduino/ESP-IDF needed).

## Testing

Pure logic (anything not coupled to SD/I2S/Serial/PN532) is covered by Unity tests in `test/test_pure/test_main.cpp`. The test program `#include`s the pure source files directly (`wav_parser.cpp`, `tag_utils.cpp`, `encoder_gray.h`, `value_array.h`) so the native env doesn't need Arduino stubs. When adding a new pure helper, prefer putting it in a header or pure-only `.cpp` so it stays testable.

What's covered: `uidToStr`, `lookupTag`, `parseWavHeaderBuffer`, `parseWavMetaBuffer`, `grayStep`, `valueToIndex`, `indexToValue`, `sleepTimerShouldFire`, `powerSaveShouldSleep`, `timerRemainingMs`, `formatCountdownMMSS`, and the **top-level tag/sleep/powersave FSM** (`jukeboxStep`). The FSM tests cover tag arrival, removal debounce, sleep-timer-fired suppression, power-save entry, NFC-suppressed-wake, and a full sleep-timer recovery scenario end-to-end. What's NOT covered (and needs on-target verification): the I2S setup, the SD-backed `playWav` streaming loop, the encoder ISR + button debounce state machine, the management GUI, the web server.

### State machine architecture

`main.cpp` `loop()` is a thin dispatcher: read inputs (encoder, NFC, audio flags) → call `jukeboxStep(state, input)` → execute the returned `Action`s (EnterSleep / WakeFromSleep / EnterMenu / TriggerPlayback / ConfirmTagRemoved). All decisions about *when* to sleep, *when* to trigger playback, and *when* to debounce a tag removal live in `jukebox_state.cpp`. The `TriggerPlayback` action invokes the (still-blocking) play loop, which mutates `s_state.tagPresent` / `s_state.lastActivityMs` directly when it exits so the next tick observes the new state. The GUI is handled externally — main.cpp short-circuits to `guiLoop()` while `guiActive()`.

## Dev workflow
- create a new branch with appropriate name
- make code changes.

  **After every code change:**
  1. **Verify it compiles** — run `~/.platformio/penv/bin/pio run` and fix any syntax or compile-time errors before considering the change complete. Never leave the project in a state that fails to build.
  2. **Cross-validate docs** — check CLAUDE.md and README.md against the actual source files. Update any stale descriptions — pin maps, source file trees, architecture flows, status, constraints, SD card layout, and TODO lists. These documents are the source of truth for future agents and contributors; drift between docs and code compounds over time.

- increment version

## Known constraints

- **D32 Pro `SS` macro conflict:** `pins_arduino.h` defines `#define SS TF_CS` (→ `#define SS 4`). Libraries that use `SS` as a parameter name will fail to compile. Avoid libraries affected by this, or `#undef SS` before including them.
- **TFT macros conflict:** D32 Pro variant pre-defines `TFT_CS=14`, `TFT_DC=27`, `TFT_RST=33`.
- **I2S uses legacy driver:** The `driver/i2s.h` API is deprecated in ESP-IDF 5.x. It works but emits warnings. Migration to `i2s_std.h` is a future task.
- **Single audio track at a time:** No crossfade or queue. Tag swaps mid-playback are detected (in both `playWav()` and the main loop) — a different UID stops the current track and the new tag is picked up on the next loop iteration. The same tag left on the reader replays the track in a `while (tagPresent)` loop.
- **WAV only:** Standard PCM WAV (16/24-bit, mono/stereo, any sample rate). No MP3/FLAC support.
- **Web server uses AP mode:** `WIFI_SSID` / `WIFI_PASSWORD` from config.h. Exposes REST API: `/api/tags` (list), `/api/tag` (POST upsert / DELETE / OPTIONS), `/api/files`, `/api/images`, `/img?name=` (serve BMP), `/upload` (WAV multipart), `/upload-img` (image multipart). Serves a single-page web app at `/`. The SPA itself accepts WAV/MP3/M4A/AAC/OGG/FLAC for audio upload — non-WAV inputs are decoded via `AudioContext.decodeAudioData` and resampled to 44.1 kHz 16-bit mono via `OfflineAudioContext` entirely client-side, then uploaded as WAV. Embedded cover art (ID3v2 APIC, MP4 `covr`, FLAC PICTURE) is extracted in JS, centre-cropped to 300×300 24-bit BMP via a canvas, and uploaded to `/img/` with the same basename. The firmware-side `/upload` and `/upload-img` handlers remain WAV/BMP-only — no server-side decoding. Web server only runs while the GUI is on the WEB screen — leaving the WEB screen calls `stopWebServer()`.
- **Heap is tight:** BMP loader allocates a raw image buffer (PSRAM preferred via `heap_caps_malloc(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`, DRAM fallback) plus a 240×240×2 = 115 KB scaled buffer in `drawNowPlayingScreen`. Source BMPs up to 600×600 24-bit are accepted; larger or non-24-bit BMPs are rejected. `playWav()` allocates a 2 KB chunk buffer. Avoid additional large heap allocations; prefer stack or static buffers where possible.

## Other important remarks
- **Code should be modularized** and testable.
- If there are duplicated logic, turn it into a function.
- **Display artifacts on encoder adjustments** — in incremental update functions (updateVolumeDisplay, updateBrightnessDisplay, updatePowerSaveDisplay, updateSleepTimerDisplay), always clear the full text area before drawing the new value. Arduino_GFX `setCursor` positions the top-left of the character cell (NOT the baseline), so text at y=140 with size=3 occupies y=140..163 (24 px). The `fillRect` eraser must span from a few px above the text's y to a few px below y + 8*size. Use generous margins: for size=3 at y, clear from y-8 to y+30.
- **visual feedback** is important: every interaction (rotating, clicking encoder, insert/remove tag) must have visual feedback.