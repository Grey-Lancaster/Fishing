# Fishing (CYD Games)

Touchscreen games for the CYD family of ESP32 boards: a **fishing game** and a **rainbow tic-tac-toe**, with a spinning
rainbow-spiral splash and celebrations.

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
| `cyd40` (default) | Freenove ESP32 Display 4.0" (ESP32-32E, no PSRAM), ST7796 480×320, XPT2046 touch on the display SPI bus |
| `cyd28` | CYD 2.8" ESP32-2432S028, ILI9341 320×240, one USB port |
| `cyd28usb2` | CYD 2.8" with two USB ports, ST7789 |

Pins come from [CYD-and-Freenove-Fish-Tank](https://github.com/Grey-Lancaster/CYD-and-Freenove-Fish-Tank):

| | cyd40 | cyd28 / cyd28usb2 |
| --- | --- | --- |
| Display SPI | MISO 12, MOSI 13, SCLK 14, CS 15, DC 2, RST -1 | same |
| Backlight | 27 | 21 |
| Touch (XPT2046) | shares the display bus, CS 33 | own pins, bit-banged: IRQ 36, MOSI 32, MISO 39, CLK 25, CS 33 |
| Other | speaker amp enable GPIO4 (active LOW, held off) | RGB LED 4/16/17 (active LOW), LDR 34, speaker DAC 26 |

On the 4" board the raw touch axes are the panel's native portrait ones: in landscape they are swapped and reversed.
That mapping is the built-in default; calibration overrides it.

## Build
```bash
pio run -e cyd40
```
Pushing to GitHub builds all three boards (`.github/workflows/build.yml`). Pushing an annotated tag `vX.Y.Z` also
publishes a release with `Fishing-<env>-factory.bin` (flash at 0x0) and `Fishing-<env>-app.bin` (flash at 0x10000);
the tag message is the "What's new" line.
