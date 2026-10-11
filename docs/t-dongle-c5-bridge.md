# LILYGO T-Dongle-C5 bridge for Cardputer v1.1

This experimental profile runs the existing AxD BLE-to-ESP-NOW bridge on a
LILYGO T-Dongle-C5. The Cardputer v1.1 runs `cardputer-v11` as the screen chip.
The phone connects to the dongle's `AxD-Bridge` BLE service; the control page's
**This bridge** target runs tools on the dongle, while **Screen chip** relays
commands to the Cardputer over ESP-NOW. This relay does not use the separate
two-screen Link Mode pairing flow.

LILYGO documents an ESP32-C5, 16 MB flash, 8 MB PSRAM, dual-band Wi-Fi, and a
USB-A connector for the [T-Dongle-C5][lilygo]. The 80 × 160 LCD shows a compact
AxD logo in dark-on-light colors, BLE connection state, and microSD mount state;
tools are still controlled from the phone. The APA102 LED remains unused. The
profile uses the onboard SD card pins and does not start a GPS UART because the
dongle has no onboard GPS. File browsing and downloads from the phone are
routed to the Cardputer's SD card by the bridge protocol.

## Build and flash

```sh
python3 scripts/build_firmware.py lilygo-t-dongle-c5-bridge
python3 scripts/flash_firmware.py lilygo-t-dongle-c5-bridge --port PORT
```

The profile selects ESP32C5 Dev Module with 16 MB flash, the 3 MB app / FATFS
partition, PSRAM enabled, and USB CDC on boot. The build produces a merged image
and component images under `build/lilygo-t-dongle-c5-bridge-<version>/`.
The release artifact is `awokxdag-lilygo-t-dongle-c5-bridge-merged.bin`.

Before replacing factory firmware, back up the 16 MB flash if you want a full
recovery image:

```sh
esptool --chip esp32c5 --port PORT read-flash 0x0 0x1000000 t-dongle-c5-factory-16mb.bin
```

LILYGO says to hold **BOOT** while plugging in the USB-A dongle to enter
download mode. Identify its port with `arduino-cli board list`, flash, then
replug if it remains in download mode.

## Use with the Cardputer

1. Flash `cardputer-v11` on the Cardputer and this bridge profile on the dongle.
2. Power both units. The dongle should advertise **AxD-Bridge** over BLE.
3. Open the [AxD control page](https://dagnazty.github.io/awokxdag/control.html)
   in a Web Bluetooth browser and connect to **AxD-Bridge**.
4. Select **Screen chip** to control the Cardputer or **This bridge** to run a
   tool on the dongle. Both boards must be powered for screen-chip commands.

The Cardputer's ESP32-S3 receives ESP-NOW on 2.4 GHz; the C5 bridge sends on
the channel-1 rendezvous first and sweeps 2.4 GHz when needed. The dongle can
run its own dual-band tools, but the Cardputer remains 2.4 GHz only. With no
GPS on the dongle, Solo and Split wardrives cannot record geotagged rows on it.
In Fleet Wardrive, it can scan without GPS; the co-located coordinator stamps
its sightings with the coordinator's fresh GPS fix in the merged CSV. If that
fix drops, the dongle retries its queued rows when it returns.
For this setup, start Fleet on the GPS-equipped coordinator, then select
**This bridge** on the control page and tap **Fleet → Join Fleet** for the
T-Dongle. The coordinator stores the merged CSV on its SD card.

## Board wiring used by this profile

| Function | GPIO |
| --- | ---: |
| SD SPI clock / MISO / MOSI / CS | 6 / 7 / 2 / 23 |
| LCD CS / DC / reset / active-low backlight | 10 / 3 / 1 / 0 |
| USB data pins (left untouched) | 13 / 14 |

## Validation on a T-Dongle-C5

Built with Arduino ESP32 core 3.3.10 and flashed to a connected ESP32-C5 rev
v1.0 with 16 MB flash. Before flashing, a full 16 MB factory image was saved
locally under `device-backups/` (SHA-256
`b8da8e5ba205a0ed4ba58775b93a5b11c5ce6bf6e0a7e33d9dac9b231187c3d7`).
The serial log reached `[memory] ready` with about 8.35 MB PSRAM free. It also
reported LCD initialization at 160 × 80 pixels, and the inverted AxD logo and
status were visually confirmed on the device. A macOS Bluetooth scan found the
`AxD-Bridge` name and service UUID; a GATT client connected and read the status
characteristic again after the LCD build. With a microSD inserted, the boot log
reported `[sd] card ready at /awokxdag`, then successfully wrote the firmware
audit and saved-network CSV. A GATT client sent a harmless **Status** command
targeted to the screen chip; the BLE write was accepted and the bridge reported
its own status, but no Cardputer status came back in that trial. The ESP-NOW
relay still needs an end-to-end check with both boards powered.

[lilygo]: https://wiki.lilygo.cc/products/t-dongle-series/t-dongle-c5/
