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

MAX98357A configuration pins:
- **GAIN** — TBD (leaving floating gives 9 dB; tie to GND for 12 dB, Vin for 3 dB). Start floating and adjust if the speaker is too quiet or clipping.
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
│   ├── <uid>.mp3                 # filename = NFC tag UID in hex
│   └── ...
└── tags.json                     # UID → action mapping (see below)
```

`tags.json` maps each tag UID to a music file:

```json
{
  "04A224B2C38081": { "file": "music/dragon.mp3" },
  "04B1C3D4E5F6A0": { "file": "music/beethoven.mp3" }
}
```

## Building & flashing

**Toolchain:** Arduino framework on ESP32 (via Arduino IDE or PlatformIO).

**Libraries:**
- `https://github.com/elechouse/PN532` — NFC reader (HSU mode)
- `ESP32-audioI2S` (schreibfaul1) — MP3/WAV decoding over I²S
- `ArduinoJson` — parsing `tags.json`
- `SD` (built-in) — SD card access

**Pins** are defined in a single `config.h` header so they can be changed in one place if the wiring evolves.

**Flash steps:**
1. Format SD card as FAT32, copy `music/` and `tags.json` to the root
2. Open the sketch, select board "LOLIN D32 PRO"
3. Build and upload over USB
4. Open the serial monitor at 115200 baud to see boot diagnostics and scanned UIDs

## Troubleshooting

- **SD card not detected** — confirm FAT32 (not exFAT), re-seat the card, check CS pin conflicts. GPIO 4 is also a strapping-adjacent pin on some ESP32 boards; if SD init is unreliable, try a different CS pin.
- **PN532 not responding** — verify HSU mode DIP switch setting, confirm TX↔RX are crossed (PN532 TX → ESP32 RX), check 3V3 power.
- **Weak NFC read range** — keep the antenna away from metal and from the speaker magnet. Target ≤ 2 cm through the enclosure wall (1.2–1.6 mm PLA is fine).
- **Audio distortion / clipping** — try tying MAX98357A GAIN to 100 kΩ→GND (6 dB) or GND (12 dB only if you need more volume and your supply can handle it).
- **Audio whine synced with activity** — usually a power or ground routing issue; add bulk capacitance on the MAX98357A Vin and keep audio ground separate from SD/NFC digital ground where possible.

## Status

Work in progress.