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
| Display           | 1.8" TFT, 128×160                | ST7735S driver, SPI               |

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
| TX        | 33 (ESP32 RX)  |
| RX        | 32 (ESP32 TX)  |
| VCC       | 3V3            |
| GND       | GND            |

### MAX98357A (I²S)

| MAX98357A pin | ESP32 GPIO |
|---------------|------------|
| BCLK          | 27         |
| LRC           | 26         |
| DIN           | 25         |
| Vin           | 5V (VUSB)  |
| GND           | GND        |

### TFT display (SPI)

The ST7735S display uses standard SPI. It shares the VSPI bus with the onboard microSD card — both use Arduino's bare-metal SPI (same `_spi_bus_array`), separated by their CS pins.

| TFT pin | ESP32 GPIO | Notes                         |
|---------|------------|-------------------------------|
| SDA     | 23         | MOSI, shared with SD_MOSI     |
| SCL     | 18         | SCK, shared with SD_SCK       |
| CS      | 5          |                               |
| DC      | 21         | Data/Command                  |
| RST     | 14         |                               |
| BLK     | 13         | Backlight                     |
| VDD     | 3.3V       |                               |
| GND     | GND        |                               |

The SD card MISO line (GPIO 19) is not connected to the display — the ST7735S does not output data.

### Rotary encoder (KY-040)

Planned for tag management GUI. Not yet wired.

| KY-040 pin | ESP32 GPIO | Notes                      |
|------------|------------|----------------------------|
| CLK (A)    | 2          | Rotation pulse             |
| DT  (B)    | 15         | Direction                  |
| SW         | 34         | Push button (input-only)   |
| +          | 3.3V       |                            |
| GND        | GND        |                            |

The KY-040 module has built-in 10k pull-up resistors. GPIO 34 is input-only on ESP32 — this works because the module handles the pull-up.

MAX98357A configuration pins:
- **GAIN** — tie to GND for 12 dB and control volume in software. Leaving the pin floating is unreliable (high-impedance input, noise can produce random gain at power-up).
- **SD / Mode** — leave floating for (L+R)/2 mono mix, or tie to GND for left-channel only.

## How it works

Each NFC tag UID maps to a single audio file on the SD card. When a tag is scanned:

1. Any current playback stops immediately
2. The file mapped to the scanned UID begins playback from the start
3. Playback continues until the file ends or the tag is removed

Unknown tags are logged over serial and displayed on screen. The tag must be removed before a new tag is accepted — hot-swapping tags mid-playback is not yet supported.

## SD card layout

```
/
├── music/                        # WAV audio files (any name)
│   ├── sample-12s.wav
│   └── gc_22k.wav
└── tags.json                     # UID → file mapping (see below)
```

`tags.json` maps each tag UID to a music file:

```json
{
  "810C2B07": { "file": "music/sample-12s.wav" },
  "52F41307": { "file": "music/gc_22k.wav" }
}
```

## Building & flashing

**Toolchain:** Arduino framework on ESP32 (via Arduino IDE or PlatformIO).

**Libraries:**
- PN532 + PN532_HSU (bundled in `lib/`, `https://github.com/elechouse/PN532`) — NFC reader
- `ArduinoJson` (bblanchon) — parsing `tags.json`
- `Arduino_GFX` (moononournation) — TFT display driver (ST7735)
- WAV audio uses the ESP32's built-in I2S driver (no extra library needed)
- SD (built-in, Arduino ESP32 framework) — SD card access via SPI

**Pins** are defined in `src/config.h`. The code is split into modules under `src/`: `config.h`, `audio.cpp`, `screen.cpp`, `tags.cpp`, `encoder.cpp`, `main.cpp`.

**Flash steps:**
1. Format SD card as FAT32, copy `music/` and `tags.json` to the root
2. Open the sketch, select board "LOLIN D32 PRO"
3. Build and upload over USB
4. Open the serial monitor at 115200 baud to see boot diagnostics and scanned UIDs

## Troubleshooting

- **SD card not detected** — confirm FAT32 (not exFAT), re-seat the card, and check CS pin (GPIO 4). If SD init is unreliable, try a different CS pin.
- **PN532 not responding** — verify HSU mode DIP switch setting, confirm TX↔RX are crossed (PN532 TX → ESP32 RX), check 3V3 power.
- **Weak NFC read range** — keep the antenna away from metal and from the speaker magnet. Target ≤ 2 cm through the enclosure wall (1.2–1.6 mm PLA is fine).
- **Audio distortion / clipping** — reduce software volume first. If still distorted, check your WAV files are normalized to a consistent loudness.
- **Audio whine synced with activity** — usually a power or ground routing issue; add bulk capacitance on the MAX98357A Vin and keep audio ground separate from SD/NFC digital ground where possible.

## Status

Milestone 1 complete — NFC tag reading, SD card WAV playback, and TFT display are all functional.

## TODO

- **Tag management GUI** — on-device interface to link/unlink music files with NFC tags, no computer needed
- Audio fade-out on track stop
- "Unknown tag" audio chirp
- Software volume control at runtime

