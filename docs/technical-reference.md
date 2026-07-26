# TinyJuke — Technical Reference

Hardware, wiring, storage format, build profiles, and the HTTP API. This is the
single source of truth for these details; `README.md` (user guide) and
`CLAUDE.md` / `AGENTS.md` (agent instructions) link here rather than repeating
them.

For the firmware's internal architecture, state machines, and the reasoning
behind the tricky parts, see `CLAUDE.md`.

---

## 1. Hardware

### 1.1 Bill of materials

| Part            | Model                            | Notes                            |
|-----------------|----------------------------------|----------------------------------|
| Mainboard       | TinyJuke PCB — ESP32-WROVER-E N8R8 | 8 MB flash, 8 MB PSRAM. The default build target. Carries the MAX98357A onboard. |
| NFC reader      | Elechouse PN532 (red board)      | HSU / UART mode                  |
| Speaker         | 4 Ω, 3 W, mono                   | Driven directly from the amp     |
| Storage         | microSD card, FAT32              |                                  |
| Display         | 2.0" TFT, 240×320                | ST7789V driver, SPI              |
| Input           | KY-040 rotary encoder            | Rotate / click / hold            |
| Audio amplifier | MAX98357A (I²S class-D)          | Mono. **Dev-board builds only** — onboard on the TinyJuke PCB. |

Any other ESP32-WROVER board works — the Lolin D32 Pro has its own build
environment and was the original development target. On a dev board the
amplifier is a separate MAX98357A breakout, wired per [§1.4](#14-wiring).

**PSRAM is a hard requirement for album art.** The BMP loader allocates the
source image plus a 240×240×2 = 115 KB scaled buffer, which does not fit in
internal DRAM alongside the rest of the firmware. Every WROVER module has PSRAM;
the otherwise similar WROOM-32 does not. On a board without it (`env:lolin_d32`)
the firmware still boots and plays audio, but art loading falls back or is
skipped. See [§6](#6-known-limitations) on heap pressure.

### 1.2 Pin map — TinyJuke mainboard (ESP32-WROVER-E)

The default target. Selected by `-DBOARD_WROVER_E`, which is set by the
`wrover_e`, `wrover_e-debug`, and `lolin_d32` environments.

| GPIO | Function          | Notes                                       |
|------|-------------------|---------------------------------------------|
| 5    | SD_CS             | Native VSPI CS; idles high (strapping pin)  |
| 22   | TFT_CS            |                                             |
| 18   | SCK (VSPI)        | Shared: TFT SCL + SD_SCK                    |
| 19   | MISO (VSPI)       | SD only — the ST7789V is write-only         |
| 23   | MOSI (VSPI)       | Shared: TFT SDA + SD_MOSI                   |
| 21   | TFT_DC            |                                             |
| 13   | TFT_BL            | LEDC PWM (brightness control)               |
| 4    | TFT_RST           |                                             |
| 26   | PN532_RX (ESP TX) | ESP32 TX → PN532 RX                         |
| 27   | PN532_TX (ESP RX) | PN532 TX → ESP32 RX (UART2)                 |
| 32   | I2S_BCLK          |                                             |
| 25   | I2S_DOUT          |                                             |
| 33   | I2S_LRC           |                                             |
| 36   | ENC_CLK           | Input-only (external pull-up on the module) |
| 39   | ENC_DT            | Input-only (external pull-up on the module) |
| 34   | ENC_SW            | Input-only — needs an external ~10 kΩ pull-up |
| 35   | VBAT_SENSE        | ADC1 — battery divider. Routed on the PCB; no firmware define yet. |

ADC1 is deliberate: ADC2 pins cannot be sampled while WiFi is active.

Full allocation table, strapping-pin constraints, and PCB layout notes:
**[`esp32_wrover_e_pin_map.md`](esp32_wrover_e_pin_map.md)**. The board schematic
is [`TinyJuke_mainboard_v2_1-P1_2026-06-11.png`](TinyJuke_mainboard_v2_1-P1_2026-06-11.png).

The v3 revision's renders and power-path review live in `docs/v3/`, which is not
committed — ask for them if you need them.

### 1.3 Pin map — Lolin D32 Pro

The default branch in `src/config.h`, selected when `BOARD_WROVER_E` is *not*
defined. Used by the `lolin_d32_pro*` environments.

| GPIO | Function          | Notes                                         |
|------|-------------------|-----------------------------------------------|
| 4    | SD_CS             |                                               |
| 14   | TFT_CS            |                                               |
| 18   | SCK (VSPI)        | Shared: TFT SCL + SD_SCK                      |
| 19   | MISO (VSPI)       | SD only — the ST7789V is write-only           |
| 23   | MOSI (VSPI)       | Shared: TFT SDA + SD_MOSI                     |
| 27   | TFT_DC            |                                               |
| 32   | TFT_BL            | LEDC PWM (brightness control)                 |
| 33   | TFT_RST           |                                               |
| 13   | PN532_RX (ESP TX) | ESP32 TX → PN532 RX                           |
| 22   | PN532_TX (ESP RX) | PN532 TX → ESP32 GPIO 22 (UART2 RX)           |
| 21   | I2S_BCLK          |                                               |
| 25   | I2S_DOUT          |                                               |
| 26   | I2S_LRC           |                                               |
| 36   | ENC_CLK           | Input-only (external pull-up on the module)   |
| 5    | ENC_DT            |                                               |
| 34   | ENC_SW            | Input-only (external pull-up on the module)   |
| 35   | VBAT              | Unused                                        |

GPIO 34 and 36 are input-only and have no internal pull-ups; the KY-040 module's
own 10 kΩ pull-ups are what make them usable.

### 1.4 Wiring

On the TinyJuke PCB this is already routed; these tables are for building on a
dev board. **The GPIO numbers below are the Lolin D32 Pro map** ([§1.3](#13-pin-map--lolin-d32-pro)) —
for the WROVER-E map, substitute from [§1.2](#12-pin-map--tinyjuke-mainboard-esp32-wrover-e).
The peripheral side of each connection is the same either way.

#### microSD card (onboard, SPI)

| SD pin  | ESP32 GPIO |
|---------|------------|
| SD_MISO | 19         |
| SD_MOSI | 23         |
| SD_SCK  | 18         |
| SD_CS   | 4          |

#### PN532 (HSU / UART mode)

Set the PN532's DIP switches to HSU mode (SEL0 = 0, SEL1 = 0). TX on one side
goes to RX on the other.

| PN532 pin | ESP32 GPIO    |
|-----------|---------------|
| TX        | 22 (ESP32 RX) |
| RX        | 13 (ESP32 TX) |
| VCC       | 3V3           |
| GND       | GND           |

#### MAX98357A (I²S)

Dev-board builds only — the TinyJuke PCB has the amplifier onboard, so there is
nothing to wire but the speaker.

| MAX98357A pin | ESP32 GPIO |
|---------------|------------|
| BCLK          | 21         |
| LRC           | 26         |
| DIN           | 25         |
| Vin           | 5V (VUSB)  |
| GND           | GND        |

Configuration pins on the breakout:

- **GAIN** → tie to GND for 12 dB, and control volume in software. Do not leave
  it floating: it is a high-impedance input, so noise can select a random gain
  at power-up.
- **SD / Mode** → leave floating for an (L+R)/2 mono mix, or tie to GND for
  left-channel only.

#### TFT display (SPI, on-board TFT port)

The ST7789V connects via the D32 Pro's on-board 10-pin SH 1.0 TFT port.

| TFT pin | ESP32 GPIO | Notes                     |
|---------|------------|---------------------------|
| SDA     | 23         | MOSI, shared with SD_MOSI |
| SCL     | 18         | SCK, shared with SD_SCK   |
| CS      | 14         |                           |
| DC      | 27         | Data/Command              |
| RST     | 33         |                           |
| BLK     | 32         | Backlight (PWM)           |
| VDD     | 3.3V       |                           |
| GND     | GND        |                           |

GPIO 19 (MISO) is not connected to the display — the ST7789V does not output
data.

#### Rotary encoder (KY-040)

| KY-040 pin | ESP32 GPIO | Notes                    |
|------------|------------|--------------------------|
| CLK (A)    | 36         | Rotation pulse           |
| DT  (B)    | 5          | Direction                |
| SW         | 34         | Push button (input-only) |
| +          | 3.3V       |                          |
| GND        | GND        |                          |

### 1.5 Shared VSPI bus

The TFT and the SD card share VSPI (GPIO 18/19/23) and are separated only by
their CS pins. Both must use the *same* SPI driver path — bare-metal register
writes via `_spi_bus_array[VSPI]`. Mixing a bare-metal device with an ESP-IDF
`spi_bus_initialize` device on the same host does not work. Init order matters:
`gfx.begin()` zeroes the VSPI registers, so SD must be initialized **after** the
TFT. See the "VSPI bus sharing" section of `CLAUDE.md` for the full account.

---

## 2. SD card layout

```
/
├── img/              # Album art (BMP, 24-bit, auto-scaled to 240×240)
│   └── album1.bmp
├── music/            # WAV files (standard PCM, any sample rate)
│   └── song.wav
├── tags.json         # UID → file + metadata mapping (managed by the web UI)
├── volume.cfg        # Volume level 0–100 (plain text)
├── maxvolume.cfg     # Max-volume ceiling 0–100 (plain text)
├── brightness.cfg    # Backlight level 0–100 (plain text)
├── powersave.cfg     # Display-off idle timeout in minutes (0 = off)
├── sleeptimer.cfg    # Audio sleep timer in minutes (0 = off)
└── theme.cfg         # UI color-theme index (plain text)
```

`/music` and `/img` are created automatically on first boot if missing, so a
blank FAT32 card works.

### 2.1 `tags.json`

```json
{
  "81:0C:2B:07": {
    "file": "music/sample-12s.wav",
    "img": "album1.bmp",
    "title": "My Song",
    "artist": "Artist Name",
    "album": "Album Title"
  },
  "52:F4:13:07": { "file": "music/gc_22k.wav" }
}
```

- Keys are UIDs as colon-separated uppercase hex (`uidToStr()` format, e.g.
  `04:A2:24:B2:C3:80:81`).
- Only `file` is required. Paths may start with `/` or not — `playWav()`
  prepends it if missing.
- `img` is relative to `/img/`.
- Metadata display priority: `tags.json` fields → the WAV's LIST INFO chunk →
  the filename without `.wav`.

### 2.2 Audio format

Standard PCM WAV only — no MP3/FLAC decoding on the device. Mono or stereo, any
sample rate. 16-bit is fully supported; **24-bit playback is currently broken**
(volume scaling and 24→32-bit unpacking are not implemented). The `data` chunk
must begin within the first 4 KB of the file.

Non-WAV files are converted in the *browser* before upload — see
[§4.4](#44-browser-side-audio-conversion).

---

## 3. Building and flashing

### 3.1 Toolchain

PlatformIO with the Arduino framework, pinned to
`pioarduino/platform-espressif32@54.03.20` (arduino-esp32 3.x). The 3.x core is
required for `ledcAttach`, the renamed `i2s_config_t` fields
(`dma_desc_num` / `dma_frame_num`), and an in-framework Bluetooth Classic +
Bluedroid build. CPU runs at 240 MHz.

### 3.2 Build profiles

`platformio.ini` composes two mixin axes with `extends`:

**Board axis** — selects the pin map and PSRAM flags:

| Section            | PIO board       | Flags                   | PSRAM |
|--------------------|-----------------|-------------------------|-------|
| `[board_d32pro]`   | `lolin_d32_pro` | (default pin branch)    | yes   |
| `[board_wrover_e]` | `esp32dev`      | `-DBOARD_WROVER_E`      | yes   |
| `[board_lolin_d32]`| `lolin_d32`     | `-DBOARD_WROVER_E -DDEV_MODE` | **no** |

PSRAM flags (`-DBOARD_HAS_PSRAM -mfix-esp32-psram-cache-issue`) live in a
`psram_flags` fragment and are interpolated **only** by boards that actually
have PSRAM. Defining `BOARD_HAS_PSRAM` on a WROOM-32 makes the core try to
initialize RAM that isn't there.

**Flash axis** — selects the partition table and size overrides:
`[flash_16mb]`, `[flash_8mb]`, `[flash_4mb]`.

**Concrete environments:**

| Environment           | Board                        | Flash | PSRAM | Notes                                     |
|-----------------------|------------------------------|-------|-------|-------------------------------------------|
| `wrover_e`            | TinyJuke PCB (WROVER-E N8R8) | 8 MB  | 8 MB  | **`default_envs`** — a bare `pio run` builds this |
| `wrover_e-debug`      | TinyJuke PCB (WROVER-E N8R8) | 8 MB  | 8 MB  | `-DDEV_MODE`                              |
| `lolin_d32_pro`       | Lolin D32 Pro                | 16 MB | yes   |                                           |
| `lolin_d32_pro-4mb`   | Lolin D32 Pro                | 4 MB  | yes   | For the 4 MB flash variant                |
| `lolin_d32_pro-debug` | Lolin D32 Pro                | 16 MB | yes   | `-DDEV_MODE`                              |
| `lolin_d32`           | Lolin D32 (WROOM-32)         | 4 MB  | **no** | Uses the WROVER-E pin map. Album art falls back/skips — see [§1.1](#11-bill-of-materials) |
| `native`              | host                         | —     | —     | Unity tests in `test/test_pure/`          |

`-DDEV_MODE` adds extra short-timeout options to the Power Saving and Sleep
Timer screens for testing.

### 3.3 Flash size and partition tables

> **The Lolin D32 Pro ships in 4 MB and 16 MB variants with identical
> markings.** Check yours with `esptool.py flash_id` before flashing. Writing
> the 16 MB table to a 4 MB chip boot-loops with `load partition table error`.

| Table                     | Chip  | App slots     | Used by                              |
|---------------------------|-------|---------------|--------------------------------------|
| `partitions_16mb_ota.csv` | 16 MB | 2 × 6 MB      | `lolin_d32_pro`, `lolin_d32_pro-debug` |
| `partitions_8mb_ota.csv`  | 8 MB  | 2 × ~3.94 MB  | `wrover_e`, `wrover_e-debug`         |
| `partitions_4mb_ota.csv`  | 4 MB  | 2 × ~1.94 MB  | `lolin_d32_pro-4mb`, `lolin_d32`     |

None of them carry a SPIFFS partition. The `lolin_d32_pro` board definition
claims 4 MB, so `board_upload.flash_size` / `maximum_size` are overridden in
`platformio.ini`.

Switching a device onto the OTA partition table (v1.6.0 and later) requires one
flash over USB. After that, updates can go over the air. **There is no automatic
rollback** — firmware that boots but misbehaves needs a USB reflash.

### 3.4 Commands

```bash
~/.platformio/penv/bin/pio run                                # build default (wrover_e)
~/.platformio/penv/bin/pio run -t upload                      # flash the default board
~/.platformio/penv/bin/pio run -e lolin_d32_pro -t upload     # 16 MB D32 Pro
~/.platformio/penv/bin/pio run -e lolin_d32_pro-4mb -t upload # 4 MB D32 Pro
~/.platformio/penv/bin/pio run -e wrover_e-debug              # build with -DDEV_MODE
~/.platformio/penv/bin/pio device monitor                     # serial, 115200 baud
~/.platformio/penv/bin/pio test -e native                     # host-side unit tests
```

The OTA image for a given environment is at `.pio/build/<env>/firmware.bin`.

### 3.5 Tests

`pio test -e native` runs the Unity suite in `test/test_pure/` on the host — no
board required. It covers the pure logic only (WAV header/metadata parsing, UID
formatting and tag lookup, encoder gray-code, value arrays, volume and timer
policy, animation math, QR placement, tag-presence policy, and the top-level
FSM). See the "Testing" section of `CLAUDE.md` for what is deliberately *not*
covered and needs on-target verification.

### 3.6 Libraries

| Library           | Source                                | Purpose                    |
|-------------------|---------------------------------------|----------------------------|
| Arduino_GFX       | `moononournation/Arduino_GFX.git`     | TFT display (ST7789)       |
| ArduinoJson       | `bblanchon/ArduinoJson @ ^7`          | Parse `/tags.json`         |
| ESP32-A2DP        | `pschatzmann/ESP32-A2DP.git#v1.8.11`  | Bluetooth A2DP sink        |
| QRCode            | `ricmoo/QRCode @ ^0.0.1`              | Version-screen release QR  |
| PN532 + PN532_HSU | Bundled in `lib/`                     | NFC reader                 |
| SD                | Built-in (arduino-esp32)              | SD card access over SPI    |
| WiFi + WebServer  | Built-in (arduino-esp32)              | AP mode + REST API         |

WAV playback uses the ESP32's built-in I2S driver (`driver/i2s.h`, the legacy
API — deprecated but functional), so no audio library is needed. ESP32-A2DP is
built with `-DA2DP_LEGACY_I2S_SUPPORT=1` so it shares that same driver path.

ESP32-A2DP is pinned to v1.8.11 deliberately: a later git HEAD regressed to a
bare `min()` in `BluetoothA2DPOutput.h` that fails to compile under the pinned
gcc 14.2.0 toolchain.

---

## 4. Web API

The web server runs **only** while the device's GUI is on the Web Management
screen; leaving that screen tears the AP down. It runs in AP mode using
`WIFI_SSID` / `WIFI_PASSWORD` from `config.h` (default: `TinyJuke-Setup` /
`12345678`), serving a single-page app at `http://192.168.4.1/`.

### 4.1 Endpoints

| Method   | Path              | Purpose                                                                 |
|----------|-------------------|-------------------------------------------------------------------------|
| `GET`    | `/`               | The SPA (Tags / Music / System tabs)                                    |
| `GET`    | `/api/tags`       | All registered tags from `tags.json`                                    |
| `POST`   | `/api/tag`        | Upsert one tag mapping                                                  |
| `DELETE` | `/api/tag`        | Remove one tag mapping                                                  |
| `GET`    | `/api/files`      | Files in `/music/` (hides dotfiles and `.tmp` leftovers)                |
| `GET`    | `/api/images`     | Files in `/img/` (hides dotfiles)                                       |
| `GET`    | `/api/music`      | `/music/` with size, duration, and embedded title/artist from a 4 KB head scan |
| `POST`   | `/api/file/meta`  | `{name,title,artist}` — write the WAV's LIST INFO chunk                 |
| `DELETE` | `/api/file?name=` | Delete a file and cascade-remove tags referencing it; returns `removed` UIDs |
| `GET`    | `/img?name=`      | Serve a BMP from `/img/`                                                |
| `POST`   | `/upload`         | WAV upload (multipart) into `/music/`                                   |
| `POST`   | `/upload-img`     | BMP upload (multipart) into `/img/`                                     |
| `GET`    | `/api/version`    | Running firmware version                                                |
| `GET`    | `/api/scan`       | One-shot PN532 read (50 ms). `{"ok":true,"uid":"AA:BB:…"}` or `uid:null` |
| `GET`    | `/api/verify-pin?pin=` | Validate the OTA PIN before uploading. 403 bad PIN, 429 locked out  |
| `POST`   | `/update?size=&pin=`   | OTA firmware image (multipart)                                     |

`/api/tag`, `/api/music`, `/api/file/meta`, and `/api/file` also answer
`OPTIONS` for CORS preflight.

The firmware-side `/upload` and `/upload-img` handlers accept **WAV and BMP
only** — there is no server-side decoding.

### 4.2 Tag scanning from the browser

The Add Tag dialog polls `/api/scan` every 500 ms to auto-fill the UID field.
This is safe because the GUI loop owns the PN532 while on the web screen, and
the endpoint keeps no background scan state. Last tag scanned wins; repeats of
the same tag don't clobber hand edits; it is never polled in edit mode; and a
generation counter discards in-flight responses after the modal closes. A UID
that is already registered shows a warning and disables Save.

### 4.3 OTA firmware update

`/update` is gated by a **4-digit PIN regenerated each session** and displayed
on the device's web screen, so joining the AP is not by itself enough to flash
the device. Five failures lock the session out. The SPA calls
`/api/verify-pin` first so a wrong PIN fails immediately rather than after the
whole image has streamed over WiFi; `/update` re-checks it server-side at
`UPLOAD_FILE_START`, so `Update.begin()` never runs on a bad PIN and no
firmware is written.

`size` is mandatory — the exact-size `begin()` plus `end(false)` at END is what
enforces completeness. The Update library rejects non-firmware uploads on the
first block via the image magic byte. Progress is drawn on the TFT; the device
reboots on success.

### 4.4 Browser-side audio conversion

The SPA accepts WAV/MP3/M4A/AAC/OGG/FLAC. Non-WAV input is decoded with
`AudioContext.decodeAudioData` and resampled to 44.1 kHz 16-bit mono via
`OfflineAudioContext`, entirely client-side, then uploaded as WAV.

Embedded cover art (ID3v2 APIC, MP4 `covr`, FLAC PICTURE) is extracted in JS,
centre-cropped and scaled to 300×300, written as 24-bit BMP, and uploaded to
`/img/` under the audio file's basename.

### 4.5 WAV metadata editing

Files uploaded through the web UI carry a fixed-size canonical LIST INFO chunk
(`"LIST"|148|"INFO"|"INAM"|64|title|"IART"|64|artist` — 156 bytes, between
`fmt ` and `data`), so metadata edits are an in-place patch of two 64-byte
fields.

Files without that chunk — passthrough `.wav` uploads and third-party files —
take a one-time slow path on first edit: a streaming rewrite through a 256 KB
PSRAM buffer (falling back to a static 4 KB buffer), then replace. Measured
~623 KB/s, so roughly a minute for a 38 MB track. This blocks the synchronous
web server for the duration; progress is shown on the device screen and the SPA
disables Save meanwhile. The original file is never destroyed before a complete
rewrite succeeds.

---

## 5. Troubleshooting

| Symptom | Things to check |
|---|---|
| SD card not detected | FAT32 (not exFAT); re-seat the card; confirm CS on GPIO 4. Boot logs whether the 20 MHz mount or the 4 MHz fallback succeeded. |
| PN532 not responding | HSU DIP switches (SEL0=0, SEL1=0); TX↔RX crossed; 3V3 present. Boot prints a firmware-version check and a raw-byte dump on failure. |
| Weak NFC read range | Keep the antenna away from metal and the speaker magnet. Target ≤ 2 cm through the enclosure wall; 1.2–1.6 mm PLA is fine. |
| Audio distortion / clipping | Lower the software volume first, then check that your WAVs are normalized to a consistent loudness. |
| Whine that tracks activity | Power or ground routing. Add bulk capacitance on the MAX98357A Vin and keep audio ground away from the SD/NFC digital ground. |
| Boot-loops after flashing | Flash-size mismatch. Run `esptool.py flash_id` and pick the matching environment ([§3.3](#33-flash-size-and-partition-tables)). |
| `E (…) vfs_api.cpp:99 open(): … does not exist` | Expected for a missing optional file. Use `sdOpenRead()` for reads where absence is normal. |

---

## 6. Known limitations

- **24-bit WAV playback is broken** — volume scaling and 24→32-bit unpacking are
  not implemented.
- **One track at a time.** No queue, no crossfade. Swapping tags mid-playback
  stops the current track and starts the new one.
- **No OTA rollback.** A bad-but-bootable image needs a USB reflash.
- **Bluetooth and the web server are mutually exclusive** — the menu enforces it.
- **Legacy I2S driver.** `driver/i2s.h` is deprecated in ESP-IDF 5.x; it works
  but emits warnings. Migration to `i2s_std.h` is future work.
- **Heap is tight.** The BMP loader takes a source buffer (PSRAM preferred) plus
  a 115 KB scaled buffer. Source BMPs up to 600×600 24-bit are accepted; larger
  or non-24-bit files are rejected.
- **D32 Pro macro conflicts.** Its `pins_arduino.h` defines `SS` as `TF_CS`, and
  pre-defines `TFT_CS`/`TFT_DC`/`TFT_RST`. `config.h` `#undef`s the TFT ones;
  libraries that use `SS` as a parameter name will not compile.
