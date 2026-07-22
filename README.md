# TinyJuke

An RFID-driven jukebox built around an ESP32. Scan an NFC tag to play the linked track. Tag stickers are embedded in 3D-printed objects themed around the music they trigger.

## Hardware

| Part              | Model                            | Notes                                |
|-------------------|----------------------------------|--------------------------------------|
| Microcontroller   | Lolin D32 Pro (ESP32)            | Onboard microSD (TF) card slot       |
| NFC reader        | Elechouse PN532 (red board)      | Configured in HSU/UART mode          |
| Audio amplifier   | MAX98357A (I²S class-D)          | Mono                                 |
| Speaker           | 4 Ω, 3 W, mono                   | Driven directly from MAX98357A       |
| Storage           | microSD card (FAT32)             | In the D32 Pro's onboard slot        |
| Display           | 2.0" TFT, 240×320                | ST7789V driver, SPI                  |

## Wiring

### microSD card (onboard, SPI)

| SD pin   | ESP32 GPIO |
|----------|------------|
| SD_MISO  | 19         |
| SD_MOSI  | 23         |
| SD_SCK   | 18         |
| SD_CS    | 4          |

### PN532 (HSU / UART mode)

Set the PN532's DIP switches to HSU mode (SEL0 = 0, SEL1 = 0). Note that TX on one side connects to RX on the other.

| PN532 pin | ESP32 GPIO     |
|-----------|----------------|
| TX        | 22 (ESP32 RX)  |
| RX        | 13 (ESP32 TX)  |
| VCC       | 3V3            |
| GND       | GND            |

### MAX98357A (I²S)

| MAX98357A pin | ESP32 GPIO |
|---------------|------------|
| BCLK          | 21         |
| LRC           | 26         |
| DIN           | 25         |
| Vin           | 5V (VUSB)  |
| GND           | GND        |

### TFT display (SPI, on-board TFT port)

The ST7789V display connects via the D32 Pro's on-board 10-pin SH 1.0 TFT port. It shares the VSPI bus with the onboard microSD card. Both use Arduino's bare-metal SPI (same `_spi_bus_array`), separated by their CS pins.

| TFT pin | ESP32 GPIO | Notes                         |
|---------|------------|-------------------------------|
| SDA     | 23         | MOSI, shared with SD_MOSI     |
| SCL     | 18         | SCK, shared with SD_SCK       |
| CS      | 14         |                               |
| DC      | 27         | Data/Command                  |
| RST     | 33         |                               |
| BLK     | 32         | Backlight                     |
| VDD     | 3.3V       |                               |
| GND     | GND        |                               |

The SD card MISO line (GPIO 19) is not connected to the display, since the ST7789V does not output data.

### Rotary encoder (KY-040)

Used for volume adjustment and menu navigation.

| KY-040 pin | ESP32 GPIO | Notes                      |
|------------|------------|----------------------------|
| CLK (A)    | 36         | Rotation pulse             |
| DT  (B)    | 5          | Direction                  |
| SW         | 34         | Push button (input-only)   |
| +          | 3.3V       |                            |
| GND        | GND        |                            |

The KY-040 module has built-in 10k pull-up resistors. GPIO 34 and 36 are input-only on the ESP32, but this works because the module handles the pull-up.

**Encoder controls:**
- **Rotate** to adjust volume (jukebox mode) or navigate menus (management mode)
- **Click** (short press) to save volume (jukebox) or select/confirm (menu)
- **Hold** (long press, >600ms) to enter the management menu (jukebox) or go back (menu)

The menu provides access to Web Server, Bluetooth, Volume, Brightness, Power Saving, Sleep Timer, Version (firmware version plus build mode), and Reboot. Reboot shows a confirmation screen: hold the encoder to reboot, click or rotate to cancel. The Volume screen has two parameters, Volume and Max Volume. In jukebox mode Max Volume is a scale factor: the volume bar keeps its full 0-100% range and the actual loudness is volume × max volume (so 80% volume at 50% max gives 40%). In Bluetooth mode it acts as a hard cap on the volume, including the phone's volume slider. Rotate to adjust the highlighted parameter, click to switch between them, hold to save both (`/volume.cfg`, `/maxvolume.cfg`). Brightness uses a white bar with the same layout as volume and is persisted to `/brightness.cfg`. Power Saving turns off the display after a configurable idle period (Off / 5 / 15 / 30 / 60 minutes; the 1-minute option only appears in `DEV_MODE` builds) and is persisted to `/powersave.cfg`. Sleep Timer stops audio playback after the configured duration (Off / 15 / 30 / 60 / 120 minutes; the 1-minute option only appears in `DEV_MODE` builds) and is persisted to `/sleeptimer.cfg`.

MAX98357A configuration pins:
- **GAIN**: tie to GND for 12 dB and control volume in software. Leaving the pin floating is unreliable (it is a high-impedance input, so noise can produce random gain at power-up).
- **SD / Mode**: leave floating for an (L+R)/2 mono mix, or tie to GND for left-channel only.

## How it works

Each NFC tag UID maps to a single audio file on the SD card. When a tag is scanned:

1. Any current playback stops immediately
2. The file mapped to the scanned UID begins playback from the start
3. The track loops as long as the same tag stays on the reader
4. Replacing the tag with a different known tag during playback switches to the new track (hot-swap)
5. Removing the tag stops playback
6. Rotate the encoder to adjust volume (overlay bar appears, auto-saves after 5 seconds of inactivity)
7. When idle on the waiting screen, the display turns off after the configured Power Saving timeout (default 15 min). Twist/press the encoder or scan a tag to wake.
8. If a Sleep Timer is configured, an on-screen countdown is shown during playback and audio stops when the timer reaches zero (the tag must be removed/replaced before a new track will play).

Unknown tags are displayed on screen for 10 seconds with their UID. Click or hold the encoder to dismiss, or remove/replace the tag.

## Bluetooth speaker mode

Hold the encoder to enter the menu and select **Bluetooth**. The ESP32 starts an A2DP sink and advertises itself as `TinyJuke-XXXX` (unique per device — the base name plus the last 4 hex digits of the MAC). Pair from a phone and play music from any app. While in BT mode:

- **Rotate** the encoder to adjust volume (saved to `/volume.cfg`)
- **Hold** the encoder to stop Bluetooth and return to the menu
- **Tap an RFID tag** and a prompt appears asking to switch to jukebox mode. Click the encoder to switch, hold to dismiss, or lift the tag to cancel.
- AVRCP track title/artist are displayed when the phone reports them in printable ASCII; otherwise the screen falls back to "Bluetooth".
- The configured **Sleep Timer** stops audio and exits BT mode when it fires (same one-shot semantics as WAV playback).
- The **Power Saving** screen blank applies while the phone is paused; the display wakes on the next audio frame or encoder event.

Bluetooth and the Web Server cannot run at the same time, so the menu items are mutually exclusive.

## Web server & tag management

Hold the encoder button (>600ms) to enter the management menu, then select "Web Server" to start a WiFi access point. Connect a phone or laptop to the **TinyJuke-Setup** network (password: `12345678`) and open `http://192.168.4.1` in a browser.

The web interface provides:
- **Tag grid**: browse all registered tags with album art, title, and artist
- **Add / Edit / Remove tags**: link any NFC tag UID to a WAV file on the SD card with optional metadata. In the Add Tag dialog you can type the UID by hand or fill it automatically by tapping the tag on the device's reader (`GET /api/scan`, polled while the dialog is open; the last tag scanned wins, and a scanned or typed UID that is already registered shows a warning and disables saving)
- **Audio upload**: upload WAV/MP3/M4A/AAC/OGG/FLAC. Non-WAV files are decoded and resampled in the browser to 44.1 kHz 16-bit mono WAV, then uploaded to `/music/` (`POST /upload`). The progress bar shows the decode → resample → encode → upload stages.
- **Embedded album art**: when an uploaded MP3 / M4A / FLAC carries embedded cover art (ID3v2 APIC, MP4 `covr`, or FLAC PICTURE block), it is auto-extracted, centre-cropped, scaled to 300×300, written as 24-bit BMP, and uploaded to `/img/` alongside the audio. The resulting `.bmp` shares the audio file's basename so it can be picked in the tag editor.
- **Image upload**: manually upload BMP/JPG/PNG album art to `/img/` (`POST /upload-img`)
- **Album art picker**: choose any image in `/img/` when editing a tag (served via `GET /img?name=...`)
- **Music management**: the Music tab lists every file in `/music/` with size, duration, and embedded title/artist (`GET /api/music`). Edit metadata (written into the WAV's LIST INFO chunk via `POST /api/file/meta`) or delete a file (`DELETE /api/file?name=...`). Deleting also removes any tags that reference it, after a confirmation listing them. Files uploaded through the web UI carry a fixed-size LIST INFO chunk so metadata edits are instant; older files are rewritten once on first edit, which can take tens of seconds for long tracks.
- **Firmware update (OTA)**: the System tab shows the running firmware version and accepts a `.bin` image (`POST /update`), protected by a 4-digit PIN displayed on the device's web server screen (so joining the WiFi alone is not enough to flash the device). The image is written to the inactive OTA slot with progress on the device screen, and on success the device reboots into the new firmware. Build the image with `pio run` and find it at `.pio/build/lolin_d32_pro/firmware.bin`. Note that after switching to the OTA partition table (v1.6.0), the first flash must be done over USB, and there is no automatic rollback if an update misbehaves.

Changes are written to `/tags.json` on the SD card immediately.

## SD card layout

```
/
├── img/                          # Album art (BMP, 24-bit, auto-scaled to 240×240)
│   └── album1.bmp
├── music/                        # WAV audio files
│   ├── sample-12s.wav
│   └── gc_22k.wav
├── tags.json                     # UID → file + metadata mapping
├── volume.cfg                    # Persisted volume level (plain text, 0-100)
├── maxvolume.cfg                 # Persisted max-volume ceiling (plain text, 0-100)
├── brightness.cfg                # Persisted brightness level (plain text, 0-100)
├── powersave.cfg                  # Persisted power save timeout (plain text, minutes)
└── sleeptimer.cfg                # Persisted audio sleep timer (plain text, minutes)
```

`tags.json` maps each tag UID to a music file. Optional fields provide album art and metadata:

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

All fields except `file` are optional. `img` paths are relative to `/img/` on the SD card. Metadata priority: tags.json fields → WAV LIST INFO metadata → filename without `.wav`.

## Building & flashing

**Toolchain:** PlatformIO with Arduino framework on ESP32.

**Libraries:**
- PN532 + PN532_HSU for the NFC reader (bundled in `lib/`, from `https://github.com/elechouse/PN532`)
- `ArduinoJson` (bblanchon) for parsing `tags.json`
- `Arduino_GFX` (moononournation) for the TFT display driver (ST7789)
- `ESP32-A2DP` (pschatzmann) for the Bluetooth A2DP sink (BT speaker mode), built with `-DA2DP_LEGACY_I2S_SUPPORT=1` so it shares the legacy I2S driver path we use for WAV playback
- WAV audio uses the ESP32's built-in I2S driver (no extra library needed)
- SD (built-in, Arduino ESP32 framework) for SD card access via SPI
- WiFi + WebServer (built-in, Arduino ESP32 framework) for AP mode and the REST API

**Platform:** PlatformIO `espressif32` 7.x (via pioarduino), shipping arduino-esp32 3.x. The 3.x core is required for `ledcAttach` and the 3.x `i2s_config_t` field names.

**Pins** are defined in `src/config.h`. The code is split into modules under `src/`: `config.h`, `audio.cpp`, `wav_parser.cpp`, `screen.cpp`, `tags.cpp`, `tag_utils.cpp`, `encoder.cpp`, `encoder_gray.h`, `value_array.h`, `gui.cpp`, `web.cpp`, `bluetooth.cpp`, `main.cpp`.

**Flash steps:**
1. Format SD card as FAT32, copy `music/`, `img/` (optional), and `tags.json` to the root
2. Run `~/.platformio/penv/bin/pio run -t upload` to build and flash over USB. The D32 Pro ships in 4 MB and 16 MB flash variants with identical markings (check with `esptool.py flash_id`). Use `-e lolin_d32_pro-4mb` for a 4 MB board, since the default 16 MB partition table boot-loops on a 4 MB chip. For the custom WROVER-E PCB, build with `-e wrover_e`.
3. Run `~/.platformio/penv/bin/pio device monitor` to see boot diagnostics and scanned UIDs at 115200 baud

**Tests:** Pure logic (WAV header/metadata parsing, UID formatting, tag lookup, encoder gray-code, value-array helpers) has host-side Unity tests. Run them with `~/.platformio/penv/bin/pio test -e native`; no board required.

## Troubleshooting

- **SD card not detected**: confirm FAT32 (not exFAT), re-seat the card, and check the CS pin (GPIO 4). If SD init is unreliable, try a different CS pin.
- **PN532 not responding**: verify the HSU mode DIP switch setting, confirm TX↔RX are crossed (PN532 TX → ESP32 RX), and check 3V3 power.
- **Weak NFC read range**: keep the antenna away from metal and from the speaker magnet. Target ≤ 2 cm through the enclosure wall (1.2 to 1.6 mm PLA is fine).
- **Audio distortion / clipping**: reduce software volume first. If it is still distorted, check that your WAV files are normalized to a consistent loudness.
- **Audio whine synced with activity**: usually a power or ground routing issue. Add bulk capacitance on the MAX98357A Vin and keep audio ground separate from the SD/NFC digital ground where possible.

## Status

Milestone 4 in progress. Added: Bluetooth A2DP sink (speaker) mode with AVRCP metadata display, encoder volume control, sleep-timer + power-save integration, an RFID tag-detected prompt that hands off to jukebox playback, web-based music management (list / edit WAV metadata / delete with tag cascade), OTA firmware updates from the web UI (16 MB partition table with dual app slots), and a max-volume setting configurable from the Volume screen (loudness scale factor in jukebox mode, hard cap in Bluetooth mode).

Prior milestones: web-based tag management, WAV + image upload, browser-side MP3/M4A/AAC/OGG/FLAC → WAV conversion with embedded-art extraction, runtime volume control, encoder-driven GUI with Brightness / Power Saving / Sleep Timer / Version screens, BMP album art (240×240, scaled in PSRAM), tag hot-swap detection, and amp-touch-noise mitigation via I2S priming.

## License

This is an open-hardware project, so the code, the hardware design, and the documentation are licensed separately. All three licenses are copyleft: you can use, modify, and redistribute the project, but anything you build from it has to stay open under the same license.

| Part | Covers | License |
|------|--------|---------|
| Firmware / source code | `src/`, `test/`, `platformio.ini`, partition tables, build config | [GPL-3.0-or-later](LICENSE) |
| Hardware design | PCB schematics & board layout in `docs/` | [CERN-OHL-S-2.0](LICENSES/CERN-OHL-S-2.0.txt) |
| Documentation & enclosure | `README.md`, `docs/*.md`, design notes, 3D-printable enclosure files | [CC-BY-SA-4.0](LICENSES/CC-BY-SA-4.0.txt) |

Copyright © 2026 qxzzxq.

Third-party components have their own licenses and are not covered by the above:

- PN532 NFC library (`lib/PN532*`): BSD 3-Clause (Adafruit / Seeed), see `lib/PN532/license.txt`
- Arduino_GFX: BSD (Adafruit-derived)
- ArduinoJson: MIT
- ESP32-A2DP: Apache-2.0
- arduino-esp32 core (SD, WiFi, WebServer, SPI, I2S): LGPL-2.1

These are all GPL-compatible, so the firmware as a whole can ship under GPL-3.0-or-later.
