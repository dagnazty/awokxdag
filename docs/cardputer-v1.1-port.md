# Cardputer v1.1 port

Target: **M5Stack Cardputer v1.1 (K132-V11)** with Stamp-S3A / ESP32-S3FN8.
This profile is distinct from Cardputer-Adv: v1.1 has a 74HC138 keyboard
matrix. Its ST7789V2 screen is 240 × 135, flash is 8 MB, microSD is onboard,
and Wi-Fi is 2.4 GHz only. [M5Stack's hardware page][m5] documents the pin
map. A connected v1.1 reported no PSRAM.

## Build and controls

```sh
python3 scripts/build_firmware.py cardputer-v11
```

The profile uses the compact Mini menu model rendered across the Cardputer's
full 240 × 135 screen. Outside text entry, **;** moves up, **,** jumps left,
**.** moves down, **/** jumps right, and **Enter** selects. Shifted versions of
those punctuation keys have the same navigation action. The top-left **~**
key goes back without **Fn**: it follows the current screen's Back or Cancel
action, or Stop when a running tool needs to stop before returning. On the
first Home page it does nothing. In the Wi-Fi SSID and password editors, type
normally, including punctuation; hold **Fn** with the navigation keys to move
among Cancel, Delete, and Save. **Enter** saves, **Tab** or **Fn+~** cancels,
**Backspace** deletes, and **Fn+Enter** activates the selected action. The
plain **~** key types punctuation in the editor.

The Cardputer profile keeps the original ESP32's 32-result and 2.4 GHz feature
limits. GPS is optional on the Grove UART (GPS TX to GPIO1, GPS RX to GPIO2).
The board has no verified second radio chip; ESP-NOW bridge features need a
separate compatible AxD node. The experimental
[LILYGO T-Dongle-C5 bridge](t-dongle-c5-bridge.md) can provide the phone's BLE
connection and relay screen-chip commands to the Cardputer.

## Hardware observations (2026-09-27)

- Full 8 MB flash backup saved at
  `device-backups/cardputer-v11-2026-09-27-full-8MB.bin` (ignored by Git),
  with an adjacent SHA-256 file.
- The independent probe in `tests/hardware/cardputer_v11_probe/` initialized
  the display, read keyboard input, mounted SD, and found eight nearby APs. It
  measured 280,060 bytes free internal RAM and a 225,268-byte largest block.
- AxD booted, mounted SD, initialized Wi-Fi and ESP-NOW, and reported about
  116 KB free internal RAM after boot with the full-width UI. A continuous
  Wi-Fi scan saved 11 results to `/awokxdag/latest_wifi_scan.csv`; Wi-Fi 6
  Intel observed 2.4 GHz APs and exported a CSV.

This remains an **experimental port**. The full set of BLE, capture, portal,
GPS, and bridge flows has not been checked on Cardputer hardware. Screen
legibility and the requested key mapping require final physical confirmation.

## Recovery

The original flash image can be restored with `esptool` while the Cardputer is
in download mode. The command below overwrites the entire 8 MB flash, including
settings and saved files, with the backed-up state:

```sh
esptool --port PORT write-flash 0x0 device-backups/cardputer-v11-2026-09-27-full-8MB.bin
```

Replace `PORT` with the Cardputer port from `arduino-cli board list`.

[m5]: https://docs.m5stack.com/en/core/Cardputer%20V1.1
