# CLAUDE.md — TinyJuke

## Project overview

RFID-driven audio player. Scan an NFC tag → lookup UID in `tags.json` on SD card → play the mapped WAV file through a MAX98357A I²S amplifier. Built on a Lolin D32 Pro (ESP32) with Arduino framework on PlatformIO. Display: ST7789V 240×320.

**Status:** Milestone 4 in progress — Bluetooth A2DP sink mode reachable from the menu, with AVRCP metadata display (ASCII-only fallback to "Bluetooth"), encoder volume control via the ESP32-A2DP library's internal volume, sleep-timer + power-save integration, and an RFID tag-detected prompt that hands off to jukebox playback. WiFi (Web Server) and Bluetooth are mutually exclusive in the UI. A Color Theme menu item swaps the whole UI palette at runtime among several dark themes (persisted to `/theme.cfg`). Web UI has a Music tab: list `/music/` files (size, duration, embedded title/artist, linked-tag count), edit metadata (written into the WAV's LIST INFO chunk), delete with tag-cascade. OTA firmware updates via the System tab (16 MB partition table, two 6 MB app slots; first flash after the table switch must be over USB).

Earlier (Milestone 3): tag hot-swap detection, brightness / power-save / sleep-timer settings persisted to SD, BMP album art (240×240, scaled in PSRAM), version screen, image upload, browser-side MP3/M4A/AAC/OGG/FLAC → WAV conversion with embedded-art extraction, and amp-touch-noise mitigation via I2S priming.

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

MAX98357A config: GAIN → GND (12 dB), SD/Mode → float (mono mix). Volume is software-controlled via encoder (runtime adjustable, persisted to `/volume.cfg` on SD). A separate max-volume setting (persisted to `/maxvolume.cfg`) caps loudness with mode-dependent semantics: in jukebox (WAV) playback it is a **scale factor** — the volume bar keeps its full 0–100 range and the effective output level is `effectiveVolume(vol, maxVol) = vol × maxVol / 100`; in Bluetooth mode it is a **clamp** — `volumeLevel` is limited to `maxVolumeLevel` on BT entry, encoder turns, and phone-pushed AVRCP volume (the value handed to the A2DP stack is echoed to the phone's slider, so scaling there would feedback-fight the phone). The Volume screen shows both parameters: rotate adjusts the active one, CLICK toggles Volume/Max Volume, HOLD saves both and returns to the menu. Brightness is also software-controlled via encoder (persisted to `/brightness.cfg` on SD). Power saving (persisted to `/powersave.cfg`) turns off the display after idle timeout. Audio sleep timer (persisted to `/sleeptimer.cfg`) stops playback after the configured duration. The UI color theme (persisted to `/theme.cfg`) selects one of several dark palettes (defined in `theme.cpp`); the whole `C_` color set is swapped at runtime by `applyTheme()`, and the Color Theme screen rotates through palettes with live preview (CLICK/HOLD saves, default = Bamboo Moss, index `THEME_DEFAULT`).

KY-040 encoder: CLK→GPIO36, DT→GPIO5, SW→GPIO34, +→3.3V, GND→GND. GPIO 34 and 36 are input-only but the module's external 10k pull-up resistors make them work.

## VSPI bus sharing

**Critical lesson:** The TFT and SD card share the VSPI bus (GPIO 18/19/23). Both must use the same SPI driver path.

- `Arduino_ESP32SPI` (TFT) and `SD.h` (SD card) both use **bare-metal SPI** via `_spi_bus_array[VSPI]` — direct register writes.
- `Arduino_ESP32QSPI` (old round display) used the **ESP-IDF SPI driver** (`spi_bus_initialize` / `spi_device_transmit`), which is incompatible with bare-metal SPI on the same host.
- Init order matters: `gfx.begin()` zeroes VSPI registers via `spiInitBus()`. SD must be initialized **after** TFT so it reconfigures VSPI. During normal operation, each device reconfigures the bus before use (TFT via `beginWrite()`, SD via `beginTransaction()`).

## Source files

```
src/
├── config.h          — Pin definitions, D32 Pro macro fix, brightness/volume/sleep/BT/theme defaults (includes theme.h for the C_ color globals)
├── theme.h/.cpp      — Runtime-switchable UI color themes: C_ RGB565 globals, dark palette table, rgb565hex() helper, applyTheme()/loadTheme()/saveTheme() (/theme.cfg)
├── audio.h/.cpp      — playWav(), stopPlayback(), parseWavMeta(), i2sPrime()/i2sDeinit()
├── wav_parser.h/.cpp — Pure buffer-based WAV header + LIST/INFO metadata parsers, canonical LIST INFO chunk builders/detectors for in-place metadata editing, duration helper (testable on native)
├── screen.h/.cpp     — TFT draw functions for 240×320 (extern gfx, uses the C_ color globals from theme.h)
├── tags.h            — TagInfo struct + declarations
├── tags.cpp          — sdReady, printHex (Arduino/Serial-coupled)
├── tag_utils.cpp     — tagDoc, uidToStr(), lookupTag() (pure, testable on native)
├── storage.h         — sdReady flag + sdOpenRead(): open-for-read that treats a missing file as a normal outcome (no VFS error log)
├── tag_presence.h    — Pure NFC presence policy: poll periods, removal-confirm windows, tagAbsentMisses(), tagMissTick() landing rule (testable on native)
├── encoder.h/.cpp    — Rotary encoder (ISR quadrature, button state machine, encHoldMs()/encPressActive(), volume/max-volume/brightness/power-save/sleep-timer save/load)
├── encoder_gray.h    — Pure gray-code transition helper used by the encoder ISR (testable on native)
├── value_array.h     — Pure helpers for "fixed list of option values" lookups (power-save/sleep-timer)
├── volume_logic.h    — Pure volume policy: volumeAdjust() (independent 0–100 params) + effectiveVolume() scale factor (testable on native)
├── timer_logic.h     — Pure timer policy: sleepTimerShouldFire / powerSaveShouldSleep / timerRemainingMs / formatCountdownMMSS
├── anim.h            — Pure animation policy: animProgress / easeOutCubic / animLerp, the retargetable AnimI32, holdProgressPct (testable on native)
├── qr_layout.h       — Pure QR placement: quiet-zone-aware integer scaling + centring for the Version screen's release QR (testable on native)
├── jukebox_state.h/.cpp — Pure top-level FSM (Waiting / Sleeping + sleepStopped / tagPresent flags) driving loop()'s decisions
├── gui.h/.cpp        — Management mode (menu + volume/brightness/theme/powersave/sleeptimer/version/web/bluetooth screens)
├── web.h/.cpp        — WiFi AP, REST API, file upload, SPA HTML page
├── bluetooth.h/.cpp  — A2DP sink wrapper: lifecycle, AVRCP metadata (ASCII-validated), NFC poll for tag-switch prompt, sleep-timer integration
└── main.cpp          — Peripherals (bus, gfx, nfc), setup(), loop(), sleep/wake logic
test/test_pure/       — Unity tests for pure logic; runs on host via `pio test -e native`
platformio.ini        — PlatformIO config + deps; board/flash mixins → envs: lolin_d32_pro (default), -4mb, -debug, wrover_e, wrover_e-debug, lolin_d32 (WROOM-32, no PSRAM), native
README.md             — User-facing docs (wiring, build steps, SD layout)
```

`playWav(filepath, nfc, tagUid, tagUidLen)` takes the active tag's UID so the per-150ms NFC poll can distinguish "tag absent" (3 misses → stop) from "tag swapped" (different UID → stop). Globals in audio.cpp: `audioPlaying`, `stopRequested`, `sleepTimerFired`, `audioStartTime`. `handleWebClient()` is not called during playback; the web server only runs while the GUI is on the WEB screen.

## Libraries (platformio.ini)

| Library          | Source                                        | Purpose              |
|------------------|-----------------------------------------------|----------------------|
| Arduino_GFX      | `moononournation/Arduino_GFX.git`             | TFT display (ST7789) |
| ArduinoJson      | `bblanchon/ArduinoJson @ ^7`                  | Parse `/tags.json`   |
| ESP32-A2DP       | `pschatzmann/ESP32-A2DP.git#v1.8.11`          | Bluetooth A2DP sink  |
| PN532 + PN532_HSU| Bundled in `lib/`                             | NFC reader           |
| SD               | Built-in (Arduino ESP32 framework)            | SD card access       |
| WiFi + WebServer | Built-in (Arduino ESP32 framework)            | AP mode + REST API   |

WAV audio uses the ESP32's built-in I2S driver (`driver/i2s.h` — legacy API, deprecated but functional). ESP32-A2DP is built with `-DA2DP_LEGACY_I2S_SUPPORT=1` so it uses the same legacy I2S path; `initBluetoothMode()` calls `i2sDeinit()` to release our driver before A2DP installs its own, and `stopBluetoothMode()` calls `i2sPrime()` afterward to restore amp pin drive.

**Platform:** pinned to `pioarduino/platform-espressif32@54.03.20` (arduino-esp32 3.x). 3.x is required for `ledcAttach`, the renamed `i2s_config_t` fields (`dma_desc_num`/`dma_frame_num`), and an in-framework Bluetooth Classic + Bluedroid build.

## Encoder events

`readEncoder()` returns:
- `0` (`ENC_NONE`) — no event
- `±N` — N full detents clockwise (positive) or counter-clockwise (negative)
- `100` (`ENC_CLICK`) — short press (<600ms release)
- `101` (`ENC_HOLD`) — long press (>600ms, fires on hold, not on release)

## main.cpp architecture

**setup() flow:**
1. LEDC attach + backlight off, then TFT init (`gfx.begin()` — initializes VSPI via bare-metal SPI). Backlight is held at 0 during boot draw to suppress power-on noise.
2. SD mount (`SD.begin(SD_CS, SPI, 20000000)` — 20 MHz, with a 4 MHz fallback if the card won't mount; creates `/music` and `/img` if missing so a fresh card doesn't fault the listing/upload paths; reads `/tags.json` into `JsonDocument tagDoc`)
3. Boot screen on TFT (SD error or waiting screen), then turn backlight on (`ledcWrite(TFT_BL, 255)`).
4. `i2sPrime()` — install I2S driver with a default config so BCLK/LRC/DOUT are actively driven; otherwise the MAX98357A picks up touch-coupled noise.
5. PN532 init with firmware version check + raw-byte diagnostic on failure (currently `while(true) delay(1000)` on failure — known unrecoverable hang).
6. `nfc.SAMConfig()`
7. `initEncoder()` — loads saved volume from `/volume.cfg` and max volume from `/maxvolume.cfg`, attaches ISR interrupts for quadrature decoding
8. `loadBrightness()` + `applyBrightness()` — load saved brightness, apply via LEDC PWM
9. `loadPowerSave()` + `loadSleepTimer()` + `resetActivityTimer()` — load power save and audio sleep timeouts, init activity tracking

**loop() state machine:**
- Sleep check: if idle on waiting screen > `powerSaveMinutes`, enter sleep (display off, backlight off). When `sleepStopped` is set (sleep timer fired with tag still present), the tag is treated as absent for sleep purposes. The menu screen also honours power-save: idling on `Screen::MENU` for `powerSaveMinutes` exits the GUI to the waiting screen, and the FSM's next tick enters sleep via the same path.
- Sleep state: poll encoder + NFC for wake; on wake restore display and re-sync. NFC wake is suppressed while `sleepStopped` is true so the still-present tag doesn't immediately re-trigger.
- Management mode active (`guiActive()`) → delegate to `guiLoop()` (menu, volume, brightness, power saving, sleep timer, version, web server, bluetooth, reboot). Menu items: **Web Management, Bluetooth Mode, Volume, Brightness, Color Theme, Power Saving, Sleep Timer, Version, Reboot** (9 items; menu `itemH` is 28 px to fit them all above the hint bar). Reboot shows a confirmation screen — hold = `ESP.restart()`, click/rotate = cancel back to menu.
- Jukebox mode: read encoder; HOLD enters menu (saves volume first). Rotation/click during playback are handled inside `playWav()`, not in the main loop.
- `!tagPresent && found && !sleepStopped` → tag arrived: lookup UID → enter `while (tagPresent)` replay loop: draw now-playing, optionally draw sleep-timer countdown, run `playWav()`, then quick NFC re-poll. Same UID = replay; different UID = exit (next iteration handles new arrival); no tag (single miss in this quick check) = exit.
- Sleep timer firing during playback sets `sleepTimerFired` → `sleepStopped`, breaks out of the replay loop and blocks re-trigger until the tag is physically removed.
- `!found && tagPresent` → tag removed: `TAG_ABSENT_CONFIRM`-miss debounce (derived in `tag_presence.h`), then clear `tagPresent`/`sleepStopped`, stop playback, draw waiting.
- Unknown tag: 10-second dismiss screen with click/hold to dismiss, or tag removal/swap to exit.
- During playback: NFC polling happens inside `playWav()` every ~150ms; a different UID stops the current track for tag-swap handling.
- NFC poll cadence: the awake waiting-screen poll blocks for `NFC_POLL_MS` (100 ms) per iteration, which sets the loop cadence. `TAG_ABSENT_CONFIRM` is now *derived* from it via `tagAbsentMisses(TAG_ABSENT_IDLE_MS, NFC_POLL_MS)`, so the two can no longer drift apart. While a button press is being tracked on the idle waiting screen (`encPressActive() && !tagPresent`) the poll is skipped entirely, so the loop free-runs and the hold indicator animates smoothly; that's safe precisely because no tag is present, so the removal debounce isn't armed and a skipped poll can't read as a tag disappearing.

**playWav() flow:**
1. Open WAV file via `SD.open()`, parse header (RIFF/fmt/data chunks; `data` chunk must lie within first 4 KB of file)
2. Configure I2S to match file's sample rate / bits / channels (16-bit and 24-bit nominally supported; **24-bit volume scaling and 24→32-bit unpacking are not implemented** — 24-bit playback is currently broken)
3. Stream PCM in 2 KB chunks: `f.read()` → volume-scale 16-bit samples (float, `effectiveVolume(volumeLevel, maxVolumeLevel)`) → mono-to-stereo duplication → `i2s_write()`. `remaining` is decremented by `bytesWritten` (mono-adjusted) so partial DMA writes don't drop data.
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
├── maxvolume.cfg       # Persisted max-volume ceiling 0–100 (plain text)
├── brightness.cfg      # Persisted brightness level 0–100 (plain text)
├── powersave.cfg        # Persisted power save timeout in minutes (0=off, plain text)
├── sleeptimer.cfg      # Persisted audio sleep timer in minutes (0=off, plain text)
└── theme.cfg           # Persisted UI color-theme index (plain text)
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
~/.platformio/penv/bin/pio run                       # build default (env:lolin_d32_pro)
~/.platformio/penv/bin/pio run -e lolin_d32_pro-debug # build with -DDEV_MODE
~/.platformio/penv/bin/pio run -t upload             # flash the default board
~/.platformio/penv/bin/pio run -e lolin_d32_pro-4mb -t upload  # flash the 4 MB D32 Pro
~/.platformio/penv/bin/pio run -e wrover_e -t upload # build/flash the custom WROVER-E PCB
~/.platformio/penv/bin/pio device monitor            # serial (115200 baud)
~/.platformio/penv/bin/pio test -e native            # run host-side unit tests
```

Framework: `arduino`, CPU: 240 MHz, PSRAM (`BOARD_HAS_PSRAM`) enabled on the PSRAM modules (D32 Pro / WROVER-E) and used by the BMP loader. **The D32 Pro ships in 4 MB and 16 MB flash variants with identical markings** — verify with `esptool.py flash_id` (16 MB main board vs. a 4 MB second board, GD25LQ32). Partition tables: `partitions_16mb_ota.csv` (two 6 MB OTA app slots; the board def claims 4 MB so `board_upload.flash_size`/`maximum_size` are overridden in platformio.ini), `partitions_8mb_ota.csv` (WROVER-E N8R8 — two ~3.94 MB OTA slots, no SPIFFS), and `partitions_4mb_ota.csv` (two ~1.94 MB OTA slots, no SPIFFS — flashing the 16 MB table onto a 4 MB chip boot-loops with "load partition table error").

**Build profiles.** `platformio.ini` factors axes into reusable mixin sections composed with `extends`: a **board** axis (`[board_d32pro]` → `lolin_d32_pro`; `[board_wrover_e]` → generic `esp32dev` + `-DBOARD_WROVER_E`; `[board_lolin_d32]` → plain `lolin_d32`/WROOM-32 + `-DBOARD_WROVER_E`) and a **flash** axis (`[flash_16mb]` / `[flash_8mb]` / `[flash_4mb]`). PSRAM compile flags live in a `psram_flags` fragment in `[common]`, interpolated **only** by the modules that have PSRAM (D32 Pro, WROVER-E) — the WROOM-32 omits them (defining `BOARD_HAS_PSRAM` with no PSRAM present makes the core try to init absent RAM). Pins are board-specific: `config.h` keys off `BOARD_WROVER_E` (`#if`/`#else`, D32 Pro is the default branch). Concrete environments: `lolin_d32_pro` (16 MB), `lolin_d32_pro-4mb`, `lolin_d32_pro-debug` (`-DDEV_MODE` exposes extra short-timeout options on Power Saving and Sleep Timer screens for testing), `wrover_e` (**default**, custom PCB, WROVER-E N8R8 — **8 MB**), `wrover_e-debug`, `lolin_d32` (WROOM-32, 4 MB, **no PSRAM** — a prototype running the new WROVER-E pin map on a plain Lolin D32; large album-art BMPs won't fit in DRAM without PSRAM, so art falls back/skips), and `native` (host-only Unity tests in `test/test_pure/`, no Arduino/ESP-IDF). `default_envs = lolin_d32_pro`, so a bare `pio run` builds the current hardware. The WROVER-E pin map lives in `docs/esp32_wrover_e_pin_map.md`.

## Testing

Pure logic (anything not coupled to SD/I2S/Serial/PN532) is covered by Unity tests in `test/test_pure/test_main.cpp`. The test program `#include`s the pure source files directly (`wav_parser.cpp`, `tag_utils.cpp`, `encoder_gray.h`, `value_array.h`) so the native env doesn't need Arduino stubs. When adding a new pure helper, prefer putting it in a header or pure-only `.cpp` so it stays testable.

What's covered: `uidToStr`, `lookupTag`, `parseWavHeaderBuffer`, `parseWavMetaBuffer`, `findCanonicalListInfo`, `canonicalListFieldOffsets`, `buildCanonicalListInfo`, `writeCanonicalField`, `wavDurationSeconds`, `grayStep`, `valueToIndex`, `indexToValue`, `sleepTimerShouldFire`, `powerSaveShouldSleep`, `timerRemainingMs`, `formatCountdownMMSS`, `volumeAdjust`, `effectiveVolume`, `rgb565hex` (theme color conversion), the **animation helpers** (`animProgress`, `easeOutCubic`, `animLerp`, `AnimI32` start/value/done including mid-flight retarget and `millis()` wrap, `holdProgressPct`), the **tag-presence policy** (`tagAbsentMisses` rounding/floor/saturation, `tagMissTick` landing rule + discard semantics), the **QR placement and capacity** (`qrPlace` centring/quiet zone/integer scale, plus the encoder's real 62-byte limit and symbol structure exercised natively), and the **top-level tag/sleep/powersave FSM** (`jukeboxStep`). The FSM tests cover tag arrival, removal debounce (asserted against `TAG_ABSENT_CONFIRM`, not a literal, so the count stays tied to the poll period), sleep-timer-fired suppression, power-save entry, NFC-suppressed-wake, and a full sleep-timer recovery scenario end-to-end. What's NOT covered (and needs on-target verification): the I2S setup, the SD-backed `playWav` streaming loop, the encoder ISR + button debounce state machine, the management GUI, the web server, and all actual drawing (the animation tests cover the interpolation math, not the pixels).

### State machine architecture

`main.cpp` `loop()` is a thin dispatcher: read inputs (encoder, NFC, audio flags) → call `jukeboxStep(state, input)` → execute the returned `Action`s (EnterSleep / WakeFromSleep / EnterMenu / TriggerPlayback / ConfirmTagRemoved). All decisions about *when* to sleep, *when* to trigger playback, and *when* to debounce a tag removal live in `jukebox_state.cpp`. The `TriggerPlayback` action invokes the (still-blocking) play loop, which mutates `s_state.tagPresent` / `s_state.lastActivityMs` directly when it exits so the next tick observes the new state. The GUI is handled externally — main.cpp short-circuits to `guiLoop()` while `guiActive()`.

## Dev workflow

1. **Create a branch** with a descriptive name (e.g., `feat/i2s-audio`, `fix/sd-mount-race`).

2. **Make code changes.** After *every* change:
   - **Verify it compiles.** Run `~/.platformio/penv/bin/pio run` and fix all syntax or compile-time errors before moving on. Never leave the project in a non-building state.
   - **Cross-validate docs.** Reconcile `CLAUDE.md` and `README.md` against the actual source. Update anything stale — pin maps, source file tree, architecture/data flow, current status, constraints, SD card layout, and TODOs. These docs are the source of truth for future agents and contributors; drift compounds quickly.

3. **Write tests** — unit tests for new logic, integration tests for cross-module behavior. Run the full suite locally.

4. **Bump the version** (follow semver: patch for fixes, minor for features, major for breaking changes).

5. **Commit and push** once all tests pass. Use clear, imperative commit messages (e.g., `Add I2S audio output via MAX98357A`).

6. **Open a PR** with a summary of changes, testing notes, and links to any related issues.

7. **Return to `main`** (`git checkout main && git pull`) once the PR is merged or handed off.

## Known constraints

- **D32 Pro `SS` macro conflict:** `pins_arduino.h` defines `#define SS TF_CS` (→ `#define SS 4`). Libraries that use `SS` as a parameter name will fail to compile. Avoid libraries affected by this, or `#undef SS` before including them.
- **TFT macros conflict:** D32 Pro variant pre-defines `TFT_CS=14`, `TFT_DC=27`, `TFT_RST=33`.
- **I2S uses legacy driver:** The `driver/i2s.h` API is deprecated in ESP-IDF 5.x. It works but emits warnings. Migration to `i2s_std.h` is a future task.
- **Single audio track at a time:** No crossfade or queue. Tag swaps mid-playback are detected (in both `playWav()` and the main loop) — a different UID stops the current track and the new tag is picked up on the next loop iteration. The same tag left on the reader replays the track in a `while (tagPresent)` loop.
- **WAV only:** Standard PCM WAV (16/24-bit, mono/stereo, any sample rate). No MP3/FLAC support.
- **Web server uses AP mode:** `WIFI_SSID` / `WIFI_PASSWORD` from config.h. Exposes REST API: `/api/tags` (list), `/api/tag` (POST upsert / DELETE / OPTIONS), `/api/files`, `/api/images`, `/api/music` (list with size/duration/title/artist from a 4 KB head scan), `/api/file/meta` (POST `{name,title,artist}` — write WAV LIST INFO), `/api/file` (DELETE `?name=` — delete file + cascade-remove referencing tags, returns `removed` UIDs), `/img?name=` (serve BMP), `/upload` (WAV multipart), `/upload-img` (image multipart), `/api/version` (firmware version), `/api/scan` (GET — on-demand single-shot PN532 read, 50 ms timeout; returns `{"ok":true,"uid":"AA:BB:..."}` or `uid:null`; no background scan state, safe because the GUI loop owns the PN532 on the WEB screen; the SPA's Add Tag modal polls it every 500 ms to auto-fill the UID field — last scanned tag wins, same-tag repeats don't clobber hand edits, never polled in edit mode, and a generation counter discards in-flight responses after the modal closes so a stale poll can't overwrite a later-opened Edit dialog; a scanned or hand-typed UID that's already registered shows a red "Already registered" warning and disables Save), `/api/verify-pin` (GET `?pin=` — validates the OTA PIN so the SPA can reject a wrong PIN before uploading; shares the `/update` lockout counter and returns 403/429 on bad PIN/lockout), `/update?size=&pin=` (OTA firmware multipart — gated by a per-session 4-digit PIN shown on the device's web screen so AP access alone can't flash the device, with a 5-failure lockout per session to block online brute force; the SPA calls `/api/verify-pin` first so a wrong PIN fails fast instead of after the whole image streams over WiFi, and `/update` still re-checks the PIN server-side at `UPLOAD_FILE_START` — `Update.begin()` never runs on a bad PIN so no firmware is written; `size` is mandatory, exact-size `begin()` with completeness enforced via `end(false)` at END; the Update library rejects non-firmware uploads on the first block via the image magic byte; TFT progress via `drawWebProgress`, reboots on success; **no rollback** — a bad-but-bootable firmware needs USB reflash). Serves a single-page web app at `/` with three tabs: Tags, Music, and System (version + firmware update). The SPA itself accepts WAV/MP3/M4A/AAC/OGG/FLAC for audio upload — non-WAV inputs are decoded via `AudioContext.decodeAudioData` and resampled to 44.1 kHz 16-bit mono via `OfflineAudioContext` entirely client-side, then uploaded as WAV. Embedded cover art (ID3v2 APIC, MP4 `covr`, FLAC PICTURE) is extracted in JS, centre-cropped to 300×300 24-bit BMP via a canvas, and uploaded to `/img/` with the same basename. The firmware-side `/upload` and `/upload-img` handlers remain WAV/BMP-only — no server-side decoding. Web server only runs while the GUI is on the WEB screen — leaving the WEB screen calls `stopWebServer()`. Lifecycle gotchas (learned the hard way): serve `PAGE_HTML` with `server.send_P(..., sizeof(PAGE_HTML)-1)` — the plain `send(const char*)` overload copies the whole page into a heap `String` and silently serves an empty page when that allocation fails; register routes only once (`server.on()` appends to the handler list — re-registering per start/stop cycle leaks heap); tear down as `server.stop()` → `WiFi.softAPdisconnect(false)` → `WiFi.mode(WIFI_OFF)` (the combined `softAPdisconnect(true)` transition races the WiFi stop state, `ESP_ERR_WIFI_STOP_STATE` 12308). `initWebServer()`/`stopWebServer()` log free heap + largest free block for diagnosing fragmentation.
- **WAV metadata editing (canonical LIST INFO chunk):** `pcmToWavBlob()` in the SPA emits a fixed-size LIST INFO chunk ("LIST"|148|"INFO"|"INAM"|64|title|"IART"|64|artist — 156 bytes, between `fmt ` and `data`), so converted uploads support instant in-place metadata patches (`writeWavMeta()` fast path in web.cpp, opens `"r+"` and overwrites the two 64-byte fields found via `findCanonicalListInfo`). Files without the canonical chunk (passthrough `.wav` uploads, third-party files) take a one-time slow path: streaming rewrite to `<path>.tmp` through a 256 KB PSRAM buffer (fallback: the static 4 KB `wavScanBuf`; existing front LIST INFO dropped, canonical chunk inserted, RIFF size patched from actual bytes written), then `SD.remove` + `SD.rename` — the original is never destroyed before a complete rewrite. Slow path blocks the synchronous web server for the duration (measured ~623 KB/s with the 256 KB PSRAM buffer at a 20 MHz SD clock — ~63 s for a 38 MB track; was ~146 KB/s with the 4 KB buffer at 4 MHz). Progress is drawn on the device's web screen (`drawWebWriteProgress`) and the SPA disables the Save button with a status note meanwhile. `.tmp` leftovers from interrupted rewrites are hidden from `/api/files` and `/api/music` and replaced on the next rewrite attempt. All three listing endpoints (`/api/files`, `/api/images`, `/api/music`) also hide dotfiles — any basename starting with `.` (e.g. macOS `._` resource forks, `.DS_Store`) via `isHiddenName()`. The playback fallback chain (tags.json → WAV LIST INFO → filename) is unchanged.
- **Bluetooth A2DP sink (`bluetooth.cpp`):** wraps `pschatzmann/ESP32-A2DP`. Menu CLICK on "Bluetooth" → `initBluetoothMode(nfc)` releases the legacy I2S driver, configures the same MAX98357A pins, registers AVRCP / connection / audio-state / stream callbacks, and calls `a2dp_sink.start(btDeviceName())` (advertised name `TinyJuke-XXXX` — base prefix + last 4 hex of the BT MAC, built from efuse so it's valid on the pairing screen before the stack starts). `handleBluetoothLoop()` runs every iteration on the BT screen: syncs `volumeLevel` into the A2DP stack via `set_volume`, resets the activity timer while streaming, checks `sleepTimerShouldFire(...)` (one-shot — clears `sleepTimerMinutes` and saves), and polls PN532 at ~300 ms cadence. Tag detection raises a modal prompt screen — click exits GUI (main loop's NFC poll picks up the still-present tag and triggers playback); hold dismisses; tag lift auto-dismisses. AVRCP `title`/`artist` are filtered through a printable-ASCII check (0x20..0x7E only) — anything outside falls back to "Bluetooth" to avoid encoding garbage on the 7-bit font. BT and WiFi (web server) are mutually exclusive in the menu UI.
- **UI animation (`anim.h` + `guiAnimTick()`):** motion is only used where it carries information. All interpolation math is pure and host-tested in `anim.h`; the drivers live in `gui.cpp` (menu/volume/brightness) and `audio.cpp` (playback overlay). Three rules the implementation depends on:
  1. **The true value is applied immediately; only its on-screen representation is interpolated.** `volumeLevel`, `brightnessLevel` + `applyBrightness()`, and `menuSel` all update on the encoder event — nothing ever waits on an animation. Percentages and labels are painted from the true value (`updateVolumeText` / `updateBrightnessText`); only the bar fills are animated (`updateVolumeBars` / `updateBrightnessBar`), which is why those pairs are split.
  2. **`guiAnimTick()` must be called before `guiLoop()`'s `if (ev == ENC_NONE) return;`** — that early return is what otherwise makes idle-time rendering impossible. It also runs immediately after `readEncoder()`, so the button state `encHoldMs()` reads is fresh. Frame-gated to `ANIM_FRAME_MS` (~60 fps).
  3. **`redraw()` calls `animReset()`**, settling every animation at the value the new screen was just painted with, so a leftover frame can't paint over a fresh screen.
  The menu **selector bar** is a 4 px accent marker at `x=4..7` — deliberately in the left margin, clear of the row highlight (`x=10`) and the label (`x=18`). Because it never overlaps text it animates with two small `fillRect`s and **no framebuffer**, which matters: `Arduino_Canvas` allocates via `aligned_alloc` (internal DRAM only, not PSRAM) and `env:lolin_d32` has no PSRAM at all. Wrap-around (item 8 → 0) needs no special case since the whole left margin is `C_BG`. The **hold indicator** is a 4 px line at `y=315..318`, below the hint-bar text (`y=305..312`); every screen that accepts a hold has `C_BG` there, which is what lets `clearHoldProgress()` erase unconditionally. It fills **linearly** (it reports real elapsed time — easing it would misreport how much longer to hold) from `HOLD_HINT_DELAY_MS` to `ENC_HOLD_MS`, and `encHoldMs()` returns 0 once `ENC_HOLD` has fired so the bar clears itself. Full-screen slide transitions were rejected: at the library's default 40 MHz SPI a 240×320 repaint is ~31 ms of bus time, i.e. under 20 fps.
- **Encoder button FSM runs on every poll:** `readEncoder()` calls `pollButton()` *before* returning rotation, latching any click/hold in `pendingBtn`. Previously rotation returned early and starved the button machine, so turning the shaft while pressed froze hold detection and could silently drop a click. Two consequences for the per-screen `for(;;)` drain loops, since events can now arrive there that previously could not: **(1)** a click can be delivered in the same burst as a rotation, so every post-drain paint is guarded on the current screen (`scr == Screen::BRIGHTNESS` and the matching checks on Volume, Power Saving, Sleep Timer and Bluetooth) — without it the click redraws the menu and the settings text is then painted over it. **(2)** a drain loop that reaches `ENC_HOLD` must handle it, because the global hold handler has already run; swallowing it forces the user to release and hold again. Every screen's drain loop now does.
- **Tag presence is debounced, and a fresh tag gets a settle window (`tag_presence.h`):** placing a tag quickly makes it skim the edge of the reader's field and keep moving, while the firmware is busy loading album art off SD and starting I2S. Two symptoms came from treating those transient misses as real: a **single** missed read used to confirm removal in the between-repeats re-poll and on the unknown-tag screen (→ "Tag removed." then an immediate re-detect), and `playWav()`'s own 3-miss run could stop a track that then restarted from zero (observed as one detection followed by two `Playing:` lines). Policy now lives in one pure header: removal is expressed as a **duration** (`TAG_ABSENT_IDLE_MS` 900 ms idle, `TAG_ABSENT_PLAYING_MS` 450 ms while audio is sounding — shorter because the sound keeps going until we accept it), and each polling site derives its own count via `tagAbsentMisses(confirmMs, pollPeriodMs)`, which rounds up and **never returns less than 2**. `tagMissTick()` owns the in-playback counter and adds a landing rule: until the tag has been **successfully read once** since playback began, misses mean "hasn't landed yet" and are **discarded** — not merely ignored. (Banking them was a real bug: the counter then sat at the threshold the moment the window closed, so the next single miss stopped playback and the grace period achieved nothing. It presented as a track that still restarted, just less often, and more the faster the tag was inserted.) `TAG_SETTLE_MS` (1 s) is only a backstop so a tag waved past the reader still stops playback; once the tag is seen, normal debouncing resumes immediately so lifting it is still responsive. `audioStartTime` is stamped at detection, so it doubles as that clock. `lastNfcCheck` is seeded with `millis()` rather than 0, so the first poll waits a full interval: at 0 it fired after a single 2 KB write with only ~11.6 ms of audio queued, and a blocking read there underran audibly at the start of every track. The hold indicator on the waiting screen is gated on `mode == Waiting` **alone**, not on `!tagPresent`: after the sleep timer stops playback the tag is still on the reader but the waiting screen is up and HOLD still opens the menu, so the gesture needs feedback there. Only the NFC-poll skip keeps the stricter `!tagPresent` condition, because that is what makes skipping safe. The poll is also skipped on the tick `ENC_HOLD` fires: that same poll moves the button to its HOLD state so `encPressActive()` is already false, and without the extra check the menu would open a blocking `NFC_POLL_MS` after the indicator completed. It is safe because `jukeboxStep()` returns on `Hold` before consulting `nfcFound` — an invariant pinned by `test_fsm_hold_outcome_ignores_nfc_state`. Separately, the in-playback read timeout is `NFC_PLAYBACK_READ_MS` (50 ms, raised from 30 ms): a field log showed `playWav()` calling a stationary tag absent on 30 ms reads while the re-poll found it immediately on 50 ms — 30 ms is not long enough for the PN532 to complete anticollision. The extra stall is well inside the ~185 ms of I2S DMA buffering. **Do not reintroduce bare miss counts**: deriving them is what keeps a cadence change from silently altering the physical behaviour.
- **Version screen QR code (`RELEASE_URL` + `drawQrCode()`):** the Version screen shows a QR pointing at `https://github.com/qxzzxq/TinyJuke/releases/latest` — the `/latest` alias rather than a pinned tag, so the code stays correct as versions ship and the device never needs to be online to know what the newest release is. Encoded with `ricmoo/QRCode` (pure C, no display coupling, so it composes with Arduino_GFX; the display-bound QR libraries on the registry do not). Three things here are load-bearing:
  1. **Drawn black-on-white, never themed.** The format assumes dark modules on light, and enough scanners reject an inverted symbol that theming it would leave it looking right but unscannable. The 4-module quiet zone is part of that white patch — borrowing it from the dark UI behind would break lock-on just as thoroughly.
  2. **`ricmoo/QRCode`'s `ECC_*` constants do not match its own table order.** `ECC_LOW` is 0, which indexes the row holding the *Medium* codeword counts — so we get Medium correction and Medium's **62-byte** capacity, not the 78 a version-4/ECC-L table implies. That is why `QR_CAPACITY` is 62.
  3. **`qrcode_initBytes()` never range-checks the payload.** Over-long input overruns the buffer rather than returning an error (it segfaults on the host), so the `static_assert` on `RELEASE_URL` is the *only* guard against memory corruption — not a nicety. Never weaken it, and never rely on `qrcode_initText()`'s return value to catch an oversized string.
  Placement lives in the pure `qr_layout.h` so the quiet zone and integer module scaling are host-tested; a fractional scale would make some modules a pixel wider than others and smear the edges scanners measure.
- **Missing files are not errors — use `sdOpenRead()`:** `SD.open()` on an absent path makes the ESP32 VFS layer log `E (...) vfs_api.cpp:99 open(): /sd/<name> does not exist, no permits for creation` at ERROR level. Several paths reach that legitimately: a fresh card has no `/tags.json`, a tag may name album art that was never uploaded, and a tag can outlive the WAV it points at. All of them already handle the miss, so the log line is noise that reads like a fault. `sdOpenRead()` in `storage.h` checks `sdReady` + `SD.exists()` first and returns a falsy `File`, so existing `if (!f)` handling is unchanged. Use it for any read where absence is an expected outcome; keep plain `SD.open()` where a missing file really is an error worth logging (e.g. `writeWavMeta()` in web.cpp, which reports it to the client). The `.cfg` readers in `encoder.cpp`/`theme.cpp` predate the helper and guard inline with `SD.exists()` — same effect, could adopt the helper later. This is the same class of problem the `SD.mkdir("/music")`/`"/img"` calls in `setup()` solve for directories.
- **Heap is tight:** BMP loader allocates a raw image buffer (PSRAM preferred via `heap_caps_malloc(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`, DRAM fallback) plus a 240×240×2 = 115 KB scaled buffer in `drawNowPlayingScreen`. Source BMPs up to 600×600 24-bit are accepted; larger or non-24-bit BMPs are rejected. `playWav()` allocates a 2 KB chunk buffer. Avoid additional large heap allocations; prefer stack or static buffers where possible.

## Other important remarks

- **Keep code modular and testable.** Each module should have a single, clear responsibility and minimal coupling to others — favor pure functions and dependency injection over hidden state.
- **Don't repeat yourself.** If the same logic appears in two or more places, extract it into a function (or class/module if it carries state). Apply the rule of three pragmatically: duplicating once is fine, twice is a smell, three times is a refactor.
- **Prefer small functions over long ones.** If a function doesn't fit on one screen or needs comments to explain its sections, it's probably doing too much.
- **Name things for what they do, not how.** `readSensor()` ages better than `readMAX98357AOverI2S()`; the latter leaks implementation details into every call site.

- **Display artifacts on encoder adjustments** — in incremental update functions (updateVolumeDisplay, updateBrightnessDisplay, updatePowerSaveDisplay, updateSleepTimerDisplay), always clear the full text area before drawing the new value. Arduino_GFX `setCursor` positions the top-left of the character cell (NOT the baseline), so text at y=140 with size=3 occupies y=140..163 (24 px). The `fillRect` eraser must span from a few px above the text's y to a few px below y + 8*size. Use generous margins: for size=3 at y, clear from y-8 to y+30.
- **visual feedback** is important: every interaction (rotating, clicking encoder, insert/remove tag) must have visual feedback.