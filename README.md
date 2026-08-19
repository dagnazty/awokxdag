# AWOKxDAG

**Dual-band Wi-Fi / BLE penetration-testing toolkit for the ESP32-C5** (AWOK Dual
C5, white-USB screen board with an ILI9341 touchscreen).

- **Version:** 1.0.0
- **Author:** dag nazty
- **Target:** ESP32-C5 Dev Module, 8 MB flash, PSRAM, microSD

> ## Authorized use only
> This firmware transmits and disrupts networks (deauthentication, beacon
> flooding, captive/evil-twin portals, targeted probe lures). Use it **only**
> against networks and devices you own or are explicitly contracted to test.
> Deauthentication, credential capture, and impersonation of a network are
> illegal against third parties without consent. **You are responsible for how
> you use this.**

---

## Features

### Recon (passive)
- **Wi-Fi Scan** — dual-band AP discovery: SSID, BSSID, RSSI, channel, band,
  advertised auth mode. Tap a result for a passive audit; **Track** graphs its
  RSSI; **Deauth** targets it; **Grab** jumps straight to handshake capture.
- **Channel Map** — 2.4 GHz and detected 5 GHz channel occupancy chart.
- **BLE Scan** — passive advertisement scan with tap-to-inspect detail (address
  type, TX power, connectable/scannable, manufacturer data, service UUIDs).
- **Clients** — probe-request / station sniffer: client MACs, probed SSIDs,
  associated BSSIDs.
- **Packet Monitor** — promiscuous all-frame capture to pcap, hopping every
  channel, with a live management/data/control breakdown.
- **WPS Scan** — lists APs advertising WPS and whether setup is locked.
- **Hidden SSID** — reveals hidden network names from probe-response /
  association frames, with an optional deauth pulse.
- **Cameras** — continuous scanner that flags common surveillance cameras
  (Ring, Blink, Wyze, Nest, Arlo, Reolink, Eufy, Tapo, Hikvision, Dahua...) by
  vendor OUI (beaconing APs *and* connected clients), SSID/BLE name, and BLE
  manufacturer id.
- **Saved** — up to 10 access points kept in NVS across reboots.

### Attacks (active — authorized targets only)
- **Deauth** — per-network from a Wi-Fi result: single AP, or multi-select
  several BSSIDs (a network's 2.4 GHz + 5 GHz radios) and round-robin across
  their channels/bands.
- **Handshake / PMKID capture** — from the Deauth screen (**Capture**) or a
  Wi-Fi result (**Grab**): locks the target channel, pulses deauth to force
  reconnection, records the WPA 4-way EAPOL handshake to pcap, and writes a
  hashcat-ready **PMKID** line when seen.
- **Beacon Flood** — broadcasts fake, clearly-synthetic test SSIDs.
- **Evil Portal** — open SoftAP + captive portal, logging submitted form fields.
- **Evil Twin** — clones the last-selected network's SSID as an open twin +
  portal.
- **Probe Lure (PineAP-lite)** — scoped to the last-selected SSID: beacons that
  one network and counts probe requests naming it, logging the probing client.

### Monitor (defensive)
- **Deauth Watch** — hops 2.4/5 GHz counting deauth/disassoc frames; logs the
  last offender.
- **Rogue Watch** — flags evil-twin APs: one SSID on multiple BSSIDs, or a saved
  SSID appearing on a new BSSID.
- **BLE Spam Watch** — flags BLE advertisement floods (Apple continuity, Swift
  Pair, Samsung, Fast Pair) by rate + vendor payload. Passive.

### GPS
- **GPS status** — fix, satellites, coordinates, speed, HDOP, plus a baud cycler
  and NMEA link diagnostics.
- **Wardrive** — logs each Wi-Fi BSSID and BLE device once to a WiGLE-compatible
  CSV while moving. A GPS-fix indicator sits in the header on every screen, and
  scan/deauth/client/portal/handshake logs are geotagged with the current fix.

### Status / utility
- **Status** — uptime, free heap, chip temp, SD used/total, GPS fix, Wi-Fi MAC,
  battery (set `kBatteryAdc` in `board_pins.h` to enable).
- **Capture manager** (Status → Files) — browse `/awokxdag/` files with sizes;
  delete behind a two-tap confirm.

---

## Menu map

```
Home  page 1: Recon | Attacks | Monitor | GPS | Status      (footer: About >)
      page 2: About  (name, version, board, authorized-use notice)

Recon page 1: Wi-Fi Scan | Channel Map | BLE Scan | Clients | Packet Mon | WPS Scan
      page 2: Hidden SSID | Saved | Cameras

Attacks:      Beacon Flood | Evil Portal | Evil Twin | Probe Lure
              (Deauth / Handshake launch from a scanned Wi-Fi result)

Monitor:      Deauth Watch | Rogue Watch | BLE Spam Watch

GPS:          status screen -> Baud / Wardrive
```

## Serial commands (115200 baud)

`w` Wi-Fi scan · `c` channel map · `b` BLE scan · `p` clients · `k` packet
monitor · `m` deauth watch · `g` GPS screen · `u` cycle GPS baud · `r` toggle
raw NMEA echo · `s` saved · `d` retry SD · `h` home (also stops any running
tool).

## SD-card output (`/awokxdag/`)

Insert a FAT32 microSD before boot. Saved networks live in NVS so the device
works without a card, and readable snapshots mirror to:

| File | Source |
| --- | --- |
| `saved_networks.csv` | saved AP list |
| `latest_wifi_scan.csv` | last Wi-Fi scan |
| `latest_ble_scan.csv` | last BLE scan |
| `latest_wifi_signal.csv` | signal monitor |
| `latest_clients.csv` | client sniffer |
| `latest_deauth_log.csv` | Deauth Watch |
| `rogue_log.csv` | Rogue Watch |
| `latest_handshake.pcap` | WPA handshake capture (link type 105) |
| `pmkid.txt` | captured PMKID (hashcat-ready) |
| `portal_creds.csv` | evil portal / evil twin |
| `pktmon.pcap` | packet monitor |
| `wardrive.csv` | WiGLE 1.4 wardrive (Wi-Fi + BLE) |

The camera and BLE-spam watches are live-view only.

---

## Build & flash (Arduino IDE)

1. Arduino IDE 2.x, board-manager URL
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`, install
   **esp32 by Espressif** 3.3.x.
2. Libraries: **Adafruit GFX**, **Adafruit ILI9341**, **NimBLE-Arduino**,
   **XPT2046_Touchscreen**, **TinyGPSPlus**.
3. Open `AWOKxDAG.ino`, select **ESP32C5 Dev Module** with:
   - Flash Size **8 MB**, Partition Scheme **8M with spiffs (3MB APP)**,
     PSRAM **Enabled**, USB CDC On Boot **Disabled**.
4. Upload to the **white USB** port only (hold SCREEN BOOT, apply power, release).

CLI equivalent:

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32c5:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=enabled" AWOKxDAG/AWOKxDAG.ino
```

## Hardware map

| Function | GPIO |
| --- | ---: |
| SPI SCK / MISO / MOSI | 6 / 2 / 7 |
| ILI9341 CS / DC / Reset | 23 / 24 / (none) |
| Backlight | 8 (active high) |
| XPT2046 touch CS | 3 |
| SD card CS | 10 |
| GPS UART1 RX / TX | 14 / 13 @ 115200 NMEA |
| Battery ADC | unset (`kBatteryAdc = -1`) |

Pins and touch calibration live in `board_pins.h`.

## Source layout

Single Arduino sketch split into feature tabs (one translation unit):
`AWOKxDAG.ino` (globals, UI, scanning, menus, setup/loop) + `awok_common.h`
(types/enums/constants) + `board_pins.h`, and per-feature tabs: `gps`,
`deauth`, `handshake`, `sniffer`, `beacon`, `portal`, `wardrive` (in gps),
`pktmon`, `cameras`, `wps`, `hidden`, `roguewatch`, `bledetect`, `probelure`,
`status`, `files`, `input`.

## Recovery

Flashing replaces the app on the white-port ESP32. Keep an official Marauder
`_v8.bin` and its C5 bootloader/partition files so factory firmware can be
restored.
