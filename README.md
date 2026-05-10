# ESP Jukebox

An RFID-driven jukebox built around an ESP32. Scan an NFC tag to play the linked track. Tag stickers are embedded in 3D-printed objects themed around the music they trigger.

## Hardware

| Part              | Model                            | Notes                                |
|-------------------|----------------------------------|--------------------------------------|
| Microcontroller   | Lolin D32 Pro (ESP32)            | Onboard microSD (TF) card slot       |
| NFC reader        | Elechouse PN532 (red board)      | Configured in HSU/UART mode          |
| Audio amplifier   | MAX98357A (I²S class-D)          | Mono                                 |
| Speaker           | 4 Ω, 3 W, mono                   | Driven directly from MAX98357A       |
| Storage           | microSD card (FAT32)             | In the D32 Pro's onboard slot        |
| Display           | 2.0" TFT, 240×320                | ST7789V driver, SPI               |

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

The ST7789V display connects via the D32 Pro's on-board 10-pin SH 1.0 TFT port. It shares the VSPI bus with the onboard microSD card — both use Arduino's bare-metal SPI (same `_spi_bus_array`), separated by their CS pins.

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

The SD card MISO line (GPIO 19) is not connected to the display — the ST7789V does not output data.

### Rotary encoder (KY-040)

Used for volume adjustment and menu navigation.

| KY-040 pin | ESP32 GPIO | Notes                      |
|------------|------------|----------------------------|
| CLK (A)    | 36         | Rotation pulse             |
| DT  (B)    | 5          | Direction                  |
| SW         | 34         | Push button (input-only)   |
| +          | 3.3V       |                            |
| GND        | GND        |                            |

The KY-040 module has built-in 10k pull-up resistors. GPIO 34 and 36 are input-only on ESP32 — this works because the module handles the pull-up.

**Encoder controls:**
- **Rotate** — adjust volume (jukebox mode) or navigate menus (management mode)
- **Click (short press)** — save volume (jukebox) or select/confirm (menu)
- **Hold (long press, >600ms)** — enter management menu (jukebox) or go back (menu)

The menu provides access to **Web Server**, **Volume**, **Brightness**, **Power Saving**, **Sleep Timer**, and **Version** (firmware version + build mode). Brightness uses a white bar (same layout as volume) and is persisted to `/brightness.cfg`. Power Saving turns off the display after a configurable idle period (Off / 5 / 15 / 30 / 60 minutes; the 1-minute option only appears in `DEV_MODE` builds) and is persisted to `/powersave.cfg`. Sleep Timer stops audio playback after the configured duration (Off / 15 / 30 / 60 / 120 minutes; the 1-minute option only appears in `DEV_MODE` builds) and is persisted to `/sleeptimer.cfg`.

MAX98357A configuration pins:
- **GAIN** — tie to GND for 12 dB and control volume in software. Leaving the pin floating is unreliable (high-impedance input, noise can produce random gain at power-up).
- **SD / Mode** — leave floating for (L+R)/2 mono mix, or tie to GND for left-channel only.

## How it works

Each NFC tag UID maps to a single audio file on the SD card. When a tag is scanned:

1. Any current playback stops immediately
2. The file mapped to the scanned UID begins playback from the start
3. The track loops as long as the same tag stays on the reader
4. Replacing the tag with a different known tag during playback switches to the new track (hot-swap)
5. Removing the tag stops playback
6. Rotate the encoder to adjust volume (overlay bar appears, auto-saves after 5 seconds of inactivity)
7. When idle on the waiting screen, the display turns off after the configured Power Saving timeout (default 15 min). Twist the encoder or scan a tag to wake.
8. If a Sleep Timer is configured, an on-screen countdown is shown during playback and audio stops when the timer reaches zero (the tag must be removed/replaced before a new track will play).

Unknown tags are displayed on screen for 10 seconds with their UID — click or hold the encoder to dismiss, or remove/replace the tag.

## Web server & tag management

Hold the encoder button (>600ms) to enter the management menu, then select "Web Server" to start a WiFi access point. Connect a phone or laptop to the **Jukebox-Setup** network (password: `12345678`) and open `http://192.168.4.1` in a browser.

The web interface provides:
- **Tag grid** — browse all registered tags with album art, title, and artist
- **Add / Edit / Remove tags** — link any NFC tag UID to a WAV file on the SD card with optional metadata
- **WAV upload** — upload WAV files directly to `/music/` over WiFi (`POST /upload`)
- **Image upload** — upload BMP/JPG/PNG album art to `/img/` (`POST /upload-img`)
- **Album art picker** — choose any image in `/img/` when editing a tag (served via `GET /img?name=...`)

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
├── volume.cfg                    # Persisted volume level (plain text, 0–100)
├── brightness.cfg                # Persisted brightness level (plain text, 0–100)
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
- PN532 + PN532_HSU (bundled in `lib/`, `https://github.com/elechouse/PN532`) — NFC reader
- `ArduinoJson` (bblanchon) — parsing `tags.json`
- `Arduino_GFX` (moononournation) — TFT display driver (ST7789)
- WAV audio uses the ESP32's built-in I2S driver (no extra library needed)
- SD (built-in, Arduino ESP32 framework) — SD card access via SPI
- WiFi + WebServer (built-in, Arduino ESP32 framework) — AP mode + REST API

**Pins** are defined in `src/config.h`. The code is split into modules under `src/`: `config.h`, `audio.cpp`, `wav_parser.cpp`, `screen.cpp`, `tags.cpp`, `tag_utils.cpp`, `encoder.cpp`, `encoder_gray.h`, `value_array.h`, `gui.cpp`, `web.cpp`, `main.cpp`.

**Flash steps:**
1. Format SD card as FAT32, copy `music/`, `img/` (optional), and `tags.json` to the root
2. Run `~/.platformio/penv/bin/pio run -t upload` to build and flash over USB
3. Run `~/.platformio/penv/bin/pio device monitor` to see boot diagnostics and scanned UIDs at 115200 baud

**Tests:** Pure logic (WAV header/metadata parsing, UID formatting, tag lookup, encoder gray-code, value-array helpers) has host-side Unity tests. Run them with `~/.platformio/penv/bin/pio test -e native` — no board required.

## Troubleshooting

- **SD card not detected** — confirm FAT32 (not exFAT), re-seat the card, and check CS pin (GPIO 4). If SD init is unreliable, try a different CS pin.
- **PN532 not responding** — verify HSU mode DIP switch setting, confirm TX↔RX are crossed (PN532 TX → ESP32 RX), check 3V3 power.
- **Weak NFC read range** — keep the antenna away from metal and from the speaker magnet. Target ≤ 2 cm through the enclosure wall (1.2–1.6 mm PLA is fine).
- **Audio distortion / clipping** — reduce software volume first. If still distorted, check your WAV files are normalized to a consistent loudness.
- **Audio whine synced with activity** — usually a power or ground routing issue; add bulk capacitance on the MAX98357A Vin and keep audio ground separate from SD/NFC digital ground where possible.

## Status

Milestone 3 in progress. Implemented: web-based tag management, WAV + image upload, runtime volume control, encoder-driven GUI with Brightness / Power Saving / Sleep Timer / Version screens, BMP album art (240×240, scaled in PSRAM), tag hot-swap detection, and amp-touch-noise mitigation via I2S priming.

## TODO

- Audio fade-out on track stop
- "Unknown tag" audio chirp
- Queue / crossfade between tracks

