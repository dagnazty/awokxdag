# Classic CYD and HaleHound board profiles

The four experimental 4 MB ESP32 builds are:

| Hardware | Build target | Panel |
| --- | --- | --- |
| ESP32-2432S028R CYD | `cyd-2432s028` | ILI9341, 240 × 320 |
| QDtech E32R28T | `halehound-e32r28t` | ILI9341, 240 × 320 |
| QDtech E32R35T | `halehound-e32r35t` | ST7796, 320 × 480 |
| ESP32-2432S028R with NM-RF-Hat | `halehound-nm-rf-hat` | ILI9341, 240 × 320 |

These are AxD firmware images for the hardware HaleHound supports; they do not
include HaleHound firmware or its external-radio features. The NM-RF-Hat profile
uses the base CYD touchscreen, SD, and Wi-Fi/BLE hardware. It does not drive
the Hat's CC1101, NRF24, or PN532 modules.

Build, then select the matching merged image for a first flash:

```sh
python3 scripts/build_firmware.py cyd-2432s028
python3 scripts/build_firmware.py halehound-e32r28t
python3 scripts/build_firmware.py halehound-e32r35t
python3 scripts/build_firmware.py halehound-nm-rf-hat
```

The TFT uses the ESP32 HSPI controller (GPIO 14/12/13; CS 15, DC 2). SD uses
VSPI (GPIO 18/19/23; CS 5). The 2.8-inch boards use software SPI for their
separately wired XPT2046 (GPIO 25/39/32; CS 33). E32R35T shares the TFT SPI
bus with touch. Backlight is GPIO 21 on 2.8-inch boards and GPIO 27 on E32R35T.
The three 2.8-inch builds address their panels as 320 × 240 with MADCTL MV
clear. Their 240 × 320 portrait menu is quarter-turned in software into those
native coordinates, without scaling, and their touch coordinates follow the
same orientation. On the ESP32-2432S028R, this replaces the earlier scaled
mapping that drew only into the top 240 rows and left the bottom noisy in a
physical photo. Full-screen coverage and touch alignment have been confirmed
on that CYD. The E32R28T and NM-RF-Hat still need their own panel and touch
checks; matching controller families do not prove matching orientation.

The 3.5-inch E32R35T uses its ST7796 in 320 × 480 portrait rotation 0, a
180-degree change from the previous rotation 2 build, with touch axes restored
to match. Display color inversion is enabled for this profile. The 240 × 320
UI continues to scale to that full panel. Its new orientation, colors, and
four touch corners still need physical verification.

On these boards the exposed GPS pins overlap the USB serial UART, so onboard GPS
is disabled in the default profile. In Fleet Wardrive, use a GPS-equipped AxD
coordinator; it stamps rows from these GPS-less workers with its fresh GPS fix.
Rows wait for a fresh coordinator fix instead of being written without a
location.

The ESP32-2432S028R's panel orientation and touch controls have been confirmed
on one physical board. The remaining hardware checks, and all checks on the
other HaleHound variants, are still pending. Before treating a build as
field-ready, verify boot, panel orientation/color, all four touch corners in
**Settings → Screen test**, microSD mount, Wi-Fi/BLE scans, and a Fleet session
with a GPS-equipped coordinator. The ESP32-2432S028**C** has
capacitive touch and needs a different input driver; these builds target the
resistive **R** variant.

Pin maps: [QDtech E32R28T specification](https://www.elecrow.com/download/product/DHO26028B/2.8inch_ESP32-32E_Display_Specification_V1.0.pdf),
[LCDWIKI E32R35T ESP-IDF documentation](https://www.lcdwiki.com/res/E32R35T/3.5inch_E32R35T_E32N35T_ESP-IDF%E7%A4%BA%E4%BE%8B%E7%A8%8B%E5%BA%8F%E8%AF%B4%E6%98%8E.pdf),
[HaleHound supported boards](https://github.com/JesseCHale/HaleHound-CYD#supported-boards), and
[NM-RF-Hat hardware](https://github.com/RockBase-iot/NM-RF-HAT).
