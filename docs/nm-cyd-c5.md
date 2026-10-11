# NM-CYD-C5 port

Status: **experimental / compile-tested; physical validation required**.

This profile ports the full 240 × 320 AxD Touch interface to the RockBase IoT /
NMTech NM-CYD-C5. The board uses an ESP32-C5-WROOM-1 with 16 MB flash and 8 MB
PSRAM, a 2.8-inch ST7789 display, XPT2046 resistive touch, and microSD on one
shared SPI bus.

Hardware sources:

- [Official NM-CYD-C5 repository](https://github.com/RockBase-iot/NM-CYD-C5)
- [Official RockBase IoT product documentation](https://wiki.rockbaseiot.com/docs/products/nm-cyd-c5/)
- [CYM working-hardware documentation](https://github.com/JimGat/CYM)

## Pin map

| Function | GPIO | Notes |
| --- | ---: | --- |
| SPI SCK | 6 | Shared by display, touch, and SD; do not use as battery ADC |
| SPI MISO | 2 | Shared bus |
| SPI MOSI | 7 | Shared bus |
| ST7789 CS | 23 | Active low |
| ST7789 DC | 24 | Data/command |
| LCD backlight | 25 | Active high; strapping pin, driven only after boot |
| XPT2046 CS | 1 | Active low; IRQ is not connected |
| microSD CS | 10 | Active low |
| GPS UART1 RX | 4 | ESP RX from GPS TX |
| GPS UART1 TX | 5 | ESP TX to GPS RX |
| GPS baud | — | 9600 NMEA |

GPIO 16–20 and GPIO22 belong to the module's flash/PSRAM interface and must not
be repurposed. Battery sensing remains disabled because the documented ADC pin
is GPIO6, which is also the live SPI clock.

## Build and flash

Use Espressif Arduino core 3.3.5 or newer and install the dependencies listed in
the main README, including **Adafruit ST7735 and ST7789 Library**.

```bash
python3 scripts/build_firmware.py nm-cyd-c5
python3 scripts/flash_firmware.py nm-cyd-c5 --port PORT
```

The profile selects ESP32-C5, 16 MB flash, the Arduino 16 MB 3 MB-app/FAT
partition layout, and PSRAM. The merged image is written to:

```text
build/nm-cyd-c5-<version>/awokxdag-<version>-nm-cyd-c5-merged.bin
```

Write the merged image at address `0x0`. For a board that does not auto-enter
download mode, hold **BOOT**, connect or reset the board, release **BOOT**, and
retry. Either USB-C connector may expose a serial path depending on whether the
native USB or CH340 connection is used; pass the exact port when both appear.

## Display and touch choices

AxD uses the Adafruit ST7789 driver directly; it does not require patching
TFT_eSPI. The profile initializes a native 240 × 320 panel, disables display
inversion, selects BGR color order, and retains the PSRAM back buffer used by
other C5 Touch builds.

The XPT2046 fallback assumes native portrait with both axes inverted, matching
working-hardware reports. Individual resistive panels vary, so the constants in
`AwokTouchCalibration` are intentionally considered baseline values until four
corners are checked on the target device.

## Physical validation checklist

- Boot artwork fills the portrait panel with correct red/blue colors.
- Settings → Screen test passes color bars, orientation, all four touch corners,
  backlight dimming, and wake-on-first-tap.
- The SD card mounts, creates `/awokxdag`, writes a capture, and reads it back.
- An optional 3.3 V NMEA GPS on P5 receives at 9600 baud and sets system UTC.
- Wi-Fi scans both 2.4 and 5 GHz; BLE starts and stops repeatedly without a
  controller allocation failure.
- Solo wardrive writes a new WiGLE 1.6 CSV, and an HTTPS upload completes.
- Fleet Start/Join and leave/rejoin behavior work with another AxD node.

Do not mark the port hardware-validated until these checks pass on an actual
NM-CYD-C5. If touch is mirrored or offset, capture the raw values shown by the
screen test before changing the profile calibration.
