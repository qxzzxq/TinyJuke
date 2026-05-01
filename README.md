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
| Display           | 1.53" round TFT, 360×360         | ST77916 driver, QSPI                |

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

### TFT display (QSPI)

The ST77916 display uses Quad SPI (4 data lines). It is wired to VSPI which is shared with the onboard microSD card — only one can be active at a time.

| TFT pin | ESP32 GPIO | Notes                         |
|---------|------------|-------------------------------|
| SDA     | 23         | QSPI IO0, shared with SD_MOSI |         
| IO1     | 19         | QSPI IO1, shared with SD_MISO |
| IO2     | 21         | QSPI IO2                      |
| IO3     | 22         | QSPI IO3                      |
| SCL     | 18         | Shared with SD_SCK            |
| CS      | 5          |                               |
| RST     | 14         |                               |
| BL      | 13         | Backlight                      |
| TE      | —          | Leave unconnected             |
| VCC     | 5V (VUSB)  |                               |
| GND     | GND        |                               |

MAX98357A configuration pins:
- **GAIN** — tie to GND for 12 dB and control volume in software. Leaving the pin floating is unreliable (high-impedance input, noise can produce random gain at power-up).
- **SD / Mode** — leave floating for (L+R)/2 mono mix, or tie to GND for left-channel only.

## How it works

Each NFC tag UID maps to a single audio file on the SD card. When a tag is scanned:

1. The current track (if any) fades out over ~200 ms
2. The file mapped to the scanned UID begins playback from the start
3. Playback continues until the file ends, a new tag is scanned, or the track is stopped

Unknown tags are logged over serial and ignored (or play a short "unknown tag" chirp — TBD).

## SD card layout

```
/
├── music/                        # Audio files
│   ├── <uid>.wav                 # filename = NFC tag UID in hex
│   └── ...
└── tags.json                     # UID → action mapping (see below)
```

`tags.json` maps each tag UID to a music file:

```json
{
  "04A224B2C38081": { "file": "music/dragon.wav" },
  "04B1C3D4E5F6A0": { "file": "music/beethoven.wav" }
}
```

## Building & flashing

**Toolchain:** Arduino framework on ESP32 (via Arduino IDE or PlatformIO).

**Libraries:**
- `https://github.com/elechouse/PN532` — NFC reader (HSU mode)
- `ESP32-audioI2S` (schreibfaul1) — MP3/WAV decoding over I²S
- `ArduinoJson` — parsing `tags.json`
- `Arduino_GFX` (moononournation) — TFT display driver (ST77916)
- `SD` (built-in) — SD card access

**Pins** are defined in a single `config.h` header so they can be changed in one place if the wiring evolves.

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

Work in progress.