# Fishing (CYD Games)

Touchscreen games for CYD and Freenove ESP32 display boards: a **fishing game** and a **rainbow tic-tac-toe**, with a
spinning rainbow-spiral splash and celebrations.

**Built by James with his new buddy Claude.**

## Games
- **Fishing.** Tap the water: the boat sails there and drops the hook to that depth. When a fish bites, **hold** to reel
  it in and let go to ease the line; at 100% tension the line snaps. Small (10), medium (25), big (60) and the rare
  rainbow fish (150). Jellyfish cut the line and cost 5 seconds. 90-second rounds; the best score is saved.
- **Tic Tac Toe.** You are X against a beatable CPU, or play 2 players. The grid and marks cycle through the rainbow.
- **BOOT button:** short press = back to the menu; hold 2 s = 4-corner touch calibration (saved).

## Boards (PlatformIO envs)
| Env | Board |
| --- | --- |
| `cyd40` (default) | Freenove ESP32 Display 4.0" (ESP32-32E, no PSRAM), ST7796 480×320, XPT2046 resistive touch on the display SPI bus |
| `freenove4` | Freenove FNK0104S 4.0" (ESP32-S3 N16R8), ST7796 480×320, FT6336U capacitive touch |
| `freenove28` | Freenove FNK0104B/AB 2.8" (ESP32-S3 N16R8), ILI9341 320×240, FT6336U capacitive touch |
| `cyd28` | CYD 2.8" ESP32-2432S028, ILI9341 320×240, one USB port |
| `cyd28usb2` | CYD 2.8" with two USB ports, ST7789 |

Pins come from [CYD-and-Freenove-Fish-Tank](https://github.com/Grey-Lancaster/CYD-and-Freenove-Fish-Tank):

| | freenove4 / freenove28 | cyd40 | cyd28 / cyd28usb2 |
| --- | --- | --- | --- |
| Display SPI | MOSI 11, SCLK 12, CS 10, DC 46, RST -1 (freenove28 also MISO 13) | MISO 12, MOSI 13, SCLK 14, CS 15, DC 2, RST -1 | same as cyd40 |
| Backlight | 45 | 27 | 21 |
| Touch | FT6336U on I2C: SDA 16, SCL 15, RST 18, INT 17 | XPT2046 on the display bus, CS 33 | XPT2046 on its own pins, bit-banged: IRQ 36, MOSI 32, MISO 39, CLK 25, CS 33 |
| Other | speaker amp enable GPIO1 (LOW = on, held off) | speaker amp enable GPIO4 (LOW = on, held off) | RGB LED 4/16/17 (active LOW), LDR 34, speaker DAC 26 |

Touch notes: on the cyd40 the raw touch axes are the panel's native portrait ones, so in landscape they are swapped and
reversed. On the Freenove S3 boards, screen x = touch y and screen y = height − touch x. Both are built-in defaults;
calibration overrides them.

The FT6336U driver in `lib/` is [aselectroworks' FT6336U CTP Controller](https://github.com/aselectroworks/Arduino-FT6336U)
(MIT), the copy Fish Tank uses.

## Build
```bash
pio run -e cyd40
```
Use `-e freenove4`, `-e freenove28`, `-e cyd28` or `-e cyd28usb2` for the other boards. Pushing to GitHub builds all
five (`.github/workflows/build.yml`). Pushing an annotated tag `vX.Y.Z` also publishes a release with
`Fishing-<env>-factory.bin` (flash at 0x0) and `Fishing-<env>-app.bin` (flash at 0x10000); the tag message is the
"What's new" line.

Hardware status: `cyd40` is tested on a real board. The Freenove S3 builds and the 2.8" CYD builds compile but have not
been tried on hardware yet.
