## Install

Plug the board in by USB and flash the **factory** file for your board at address **0x0**, for example with the
browser tool [ESP Tool](https://espressif.github.io/esptool-js/) (Chrome or Edge) or from a computer:

`esptool.py write_flash 0x0 Fishing-<build>-factory.bin`

Freenove ESP32-S3 boards: if the board isn't found, hold BOOT, tap RESET, release BOOT, then flash; press RESET afterwards.

| Build file | Board |
| --- | --- |
| `freenove4` | Freenove FNK0104S 4.0" (ESP32-S3), ST7796 480×320, capacitive touch |
| `freenove28` | Freenove FNK0104B/AB 2.8" (ESP32-S3), ILI9341 320×240, capacitive touch |
| `cyd40` | Freenove ESP32 Display 4.0" (ESP32-32E), ST7796 480×320, resistive touch |
| `cyd28` | CYD 2.8" ESP32-2432S028 with an ILI9341 display (one USB port) |
| `cyd28usb2` | the same CYD with two USB ports and an ST7789 display |

Colours wrong on a CYD? You most likely picked the wrong CYD build; try the other one. Nothing is damaged.

## Which file do I want?
| File | Flash at | Saved best score and touch calibration |
| --- | --- | --- |
| `Fishing-<build>-factory.bin` | `0x0` | **wiped** |
| `Fishing-<build>-app.bin` | `0x10000` | **kept** |

Touch off? Hold the **BOOT** button for 2 seconds to calibrate. A short press of BOOT always returns to the menu.
