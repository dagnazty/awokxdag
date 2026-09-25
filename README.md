# AxD

**Dual-band Wi-Fi / BLE penetration-testing toolkit for the ESP32-C5** (AWOK Dual
C5, white-USB screen board with an ILI9341 touchscreen).

- **Version:** 1.7.4
- **Author:** dag nazty
- **Target:** ESP32-C5 Dev Module, 8 MB flash, PSRAM, microSD
- **Changelog:** [CHANGELOG.md](CHANGELOG.md)

Experimental **original Dual ESP32 Touch v1/v2/v3** profiles are also available (2.4 GHz only).

Original **Dual ESP32 Mini v1/v2/v3** builds are available as well.

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
- **Wi-Fi Scan** — dual-band discovery for up to 64 APs: SSID, BSSID, RSSI,
  channel, band, and advertised auth mode, with Prev/Next paging after ten.
  Scans **continuously**, merging by BSSID so the list accumulates every AP seen
  (RSSI refreshed in place) until you select one or leave. Tap a result for a
  passive audit; **Track** graphs its RSSI; **Deauth** targets it; **Grab** jumps
  straight to handshake capture.
- **Channel Map** — 2.4 GHz and detected 5 GHz channel occupancy chart.
- **Spectrogram** — dual-band RF waterfall and channel duty cycle analyzer. Samples on-air
  energy, frame rates, byte volume, and noise floor per channel (2.4 GHz and 5 GHz). Renders
  an on-device instantaneous spectrum bar chart with peak hold indicators and a real-time
  thermal scrolling 2D waterfall heat map. Supports All-channel sweeps, 2.4 GHz only, 5 GHz only,
  or locked single-channel high-rate dwell. Streams live `$SPEC` telemetry and includes an
  interactive Web Bluetooth canvas waterfall.
- **BLE Scan** — up to 64 advertisers with Prev/Next paging and tap-to-inspect detail (address
  type, TX power, connectable/scannable, manufacturer data, service UUIDs).
  Advertised Flipper service UUIDs get an **[F?]** hint and CSV identification field;
  the hint does not verify device identity.
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
- **Security Audit** — passive beacon-IE posture report: parses each AP's RSN /
  WPA / WPS information elements and classifies it by encryption tier (Open /
  WEP / WPA / WPA2 / WPA2-TKIP / WPA2-WPA3 / WPA3 / OWE / Enterprise), 802.11w
  PMF (none / optional / required) and WPS (open / locked), scores each network's
  risk, and sorts the weakest to the top. Listen-only.
- **BLE Trackers** — flags personal item trackers: Apple Find My / AirTags in the
  separated ("offline finding") state, Tile tags, and Samsung SmartTags. Tracks
  each address over time and raises a **FOLLOW** alert when one persists across a
  long enough span with repeat sightings — the planted-tracker privacy case.
  Passive.
- **BLE Intel** — ecosystem intelligence and continuity decoder. Continuously
  decodes proprietary vendor advertisement payloads: Apple Continuity (AirPods/Beats
  model identification, Left/Right/Case battery percentages with real-time charging
  flags, AirDrop, Nearby Info, Find My), Google / Android Fast Pair (model ID and state),
  Microsoft Swift Pair, and Samsung Continuity / SmartThings. Supports on-device
  scrolling inspection, SD CSV logging to `/awokxdag/ble_intel.csv`, live `$BLEINTEL`
  serial streaming, and a dedicated Web Bluetooth dashboard tab with real-time battery indicators.
- **Harvester** — all-channel passive EAPOL / PMKID collector. Hops every channel
  recording WPA key frames already in the air (plus one beacon per BSSID for the
  ESSID) to `harvest.pcap`, and writes hashcat-ready PMKID lines. **No deauth is
  sent** — the quiet counterpart to the targeted Grab.
- **Probe Intel** — aggregates directed probe requests by the SSID they name,
  ranked by probe count and distinct devices, revealing the preferred-network
  lists leaking from nearby devices. Passive.
- **Topology Map** — Swarm Mesh Topology Graph mapping client ↔ AP associations
  and directed probe request leaks across 2.4 GHz and 5 GHz channels. Flags unencrypted
  open networks, streams live `$TOPO` telemetry, shares links across worker nodes via
  ESP-NOW, and powers an interactive HTML5 force-directed physics graph in the Web
  Bluetooth dashboard.
- **Fleet Hunter** — multi-node radio direction-finding (RDF) and trilateration
  engine. Aggregates multi-node target observations over ESP-NOW, computes estimated
  GPS coordinates, geodesic range, confidence radius, and heading bearing, visualized
  via a tactical radar scope on device and in the web dashboard.
- **Wi-Fi 6 Intel** — passive 802.11ax High Efficiency (HE) capability and operation
  inspector. Decodes BSS Color (1–63) collision parameters, color disabled flags,
  channel widths (20/40/80/160 MHz), and generational classifications (Wi-Fi 4/5/6)
  across 2.4 GHz and 5 GHz bands. Features on-device touch/mini inspection, SD CSV
  logging to `/awokxdag/wifi6_intel.csv`, live `$AXINTEL` telemetry, and an interactive
  Web Bluetooth BSS color collision matrix.
- **Saved** — up to 10 access points kept in NVS across reboots.

### Network Tools (connected LAN)

Open **Recon → Network Tools**. These tools send discovery/service
queries on the Wi-Fi network you join; they require a normal network connection.
The overview separates **Connection**, **Hosts**, **Services**, and
**Results & Upload**. Connection/IP status stays visible while browsing. Touch
uses larger cards; Mini uses selectable, scrolling entries with the same actions.

- **Connection** — choose an AP from the last scan (or rescan), enter its
  password with the on-device phone keypad, then Join. Both Touch and Mini
  use a three-column T9-style **multi-tap** layout (no dictionary): repeat a key
  to cycle its letters, then wait one second or press **# Next** for another
  letter on the same key. **\* Mode** cycles abc / ABC / 123 / symbols; **0**
  enters space in letter mode. All printable ASCII characters are available,
  with separate Delete, Cancel and Done buttons. Mini uses all four directions
  to move between keys and Center to select. Credentials stay in RAM; the password entry is
  masked and cleared after a connection attempt. Leaving Network Tools disconnects.
- **Hosts → Discover hosts** — ARP discovery with IP/MAC results and subnet-mask
  handling. Select a host to check its ports, cameras, printers, or SIP services.
  Service results return to that host; the host list keeps its page. The separate
  **Services** menu checks all discovered hosts (UPnP queries the gateway).
- **TCP Ports** — checks 19 common TCP ports with one nonblocking connection at a
  time. Results establish an open TCP port, not a verified service or vulnerability.
- **LAN Cameras** — checks RTSP OPTIONS on 554/8554 and ONVIF device-information
  responses on HTTP 80/8000/8080/8899. Shows RTSP candidates, ONVIF manufacturer/model
  when returned, or ONVIF authentication requirements. Complements the existing
  passive Cameras tool; RTSP alone does not prove a device is a camera.
- **Printers** — checks raw-print (9100), IPP (631), and LPD (515) ports; labels
  responses as printer candidates. Does not submit print jobs.
- **SIP Services** — sends UDP OPTIONS on 5060 and matches responses to the queried
  host and request. Includes authentication/error responses as service evidence.
- **UPnP Mappings** (Services, second page) — discovers a compatible gateway using SSDP,
  reads its IGD service description, and lists existing port mappings. It does
  not create, delete, or change mappings.
- **Results / exports** — three result cards per page, detail inspection, a large
  **Stop scan** action while busy, and **Actions / Save CSV** after stopping.
  Actions switches between retained hosts/services, saves a snapshot, restarts
  discovery, or opens Connection. **Results & Upload** also opens the last host
  and service lists directly. Completed and cancelled scans automatically save to SD when available;
  CSVs retain partial-result flags, network context, and timeout/error counts.
  Serial **h** exits and disconnects.

Network Tools retain up to 128 hosts and 128 service results on C5, or 32 each on
original ESP32 boards. Subnets of at most 1,024 usable addresses are scanned in
full; larger subnets scan only the local /24 intersection and show a partial-scope
notice. IPv4 /31 and /32 networks are unsupported. ARP observes the local broadcast
domain; isolation, sleeping devices and packet loss can hide hosts. Timeouts are
inconclusive. UPnP accepts HTTP URLs using the gateway's literal IPv4 address,
limits responses to 8 KiB and mappings to 64 (32 on original ESP32), and reports
unsupported or oversized responses. HTTPS camera endpoints and SIP over TCP/TLS
are outside this first implementation. The character picker supports printable
ASCII (SSID up to 32 bytes, WPA password 8–63 characters or empty for open Wi-Fi);
enterprise authentication and raw 64-digit PSKs are not supported.

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

Open **Monitor → Wi-Fi / Bluetooth / Advanced**. Each detector has a description
and running/stopped state, with three large cards per page on Touch and selectable
scrolling entries on Mini. **Stop** ends the detector and returns to its group and
page; an already stopped detector shows **Back**. **Groups** returns to the picker.

- **Deauth Watch** — hops 2.4/5 GHz counting deauth/disassoc frames; logs the
  last offender.
- **Rogue Watch** — flags evil-twin APs: one SSID on multiple BSSIDs, or a saved
  SSID appearing on a new BSSID.
- **BLE Spam Watch** — flags BLE advertisement floods (Apple continuity, Swift
  Pair, Samsung, Fast Pair) by rate + vendor payload. Passive.
- **Karma Watch** — the inverse of Rogue Watch: flags a single BSSID that beacons
  or probe-responds for *many* different SSIDs — the signature of a WiFi
  Pineapple / Karma / MANA rogue AP answering every network a victim looks for.
  Passive.
- **Beacon Watch** — counts distinct BSSIDs beaconing per short window and alerts
  on a spike — beacon-flood / fake-AP spam (mdk4, or this firmware's own Beacon
  Flood). Passive.
- **Auth Flood** — detects authentication / association-request floods against an
  AP (mdk4 `a` / connection-flood DoS) and names the targeted AP. Passive.
- **Advanced Watch** — runs a shared Wi-Fi/BLE anomaly pipeline that monitors
  beacon/RSN/PMF/WPS integrity, saved-network security downgrades, grouped
  disconnect reason storms, channel-switch announcements, EAPOL and association
  spikes, RF noise-floor changes, and rapid BLE address churn. Alerts are
  thresholded and GPS logged. Passive; RF and BLE churn results are heuristics.
- **Deauth Forensics** — targeted deauthentication & disassociation frame forensic
  analyzer and attribution engine. Differentiates shotgun broadcast floods from
  targeted unicast victim station attacks, detects transmitter 802.11 sequence number
  jumps indicating forged/spoofed attack frames, decodes 802.11 reason codes, logs
  forensic audit trails to `/awokxdag/deauth_forensics.csv`, and streams live `$DEAUTH`
  telemetry to Web Bluetooth with real-time alert banners.
- **SD Card File Manager & Remote Transfer** — browse, preview, and download SD card captures
  directly to your phone or computer over Web Bluetooth or Web Serial without removing the SD card.
  Streams base64 chunks on-the-fly (`$FILEDATA`), supports PCAP handshakes and wardrive CSVs,
  provides an in-browser preview drawer with one-click copy, and automatically bridges requests
  between Screen Chip and Bridge Chip over ESP-NOW.
  For verified previews/downloads, update both chips with this firmware and reload
  the matching control page. BLE transfers wait for the bridge to finish its channel sweep,
  then require a browser acknowledgment for each chunk and retry missing chunks.
  Bridge scanning pauses during the transfer. Bluetooth downloads can pause and
  resume after reconnecting to the same bridge, starting at the first missing
  chunk. Keep the browser tab open: partial downloads are held in tab memory.
  The device checks the original snapshot and the browser checks the complete
  file with CRC32 before preview/save. Appended CSV rows do not change an existing
  snapshot; changed or truncated contents require Restart. USB retains its
  existing stream and size check.
  On the board, Captures shows four larger rows per page with file details and
  type filters. It retains the newest 64 files by SD modification time (filename
  order breaks ties); the header discloses larger directories. Filters apply to
  those 64 entries. Deleting a file requires a separate confirmation, and an open
  wardrive CSV cannot be deleted from the board menu.

### GPS
- **GPS status** — fix, satellites, coordinates, speed, HDOP, plus a baud cycler
  and NMEA link diagnostics. GPS coordinates automatically select the local
  timezone offline; the current date selects daylight saving or standard time.
  GPS/Wardrive screens show local time and the offset/DST state. Logs, WiGLE
  CSV FirstSeen values, and SD file timestamps use local time too. The last zone
  is remembered through fix loss and reboots, and refreshed from a valid fix
  every 30 seconds. Before the clock and any zone are known, logs use explicit
  `uptime+` markers. The bundled map is approximate near timezone borders;
  [data sources and update instructions](third_party/timezones/README.md) describe
  its limits. Existing files are not rewritten.
- **Wardrive** — logs each Wi-Fi BSSID and BLE device once to a WiGLE-compatible
  CSV while moving. A GPS-fix indicator sits in the header on every screen, and
  scan/deauth/client/portal/handshake logs are geotagged with the current fix.
  Touch, Mini, and website dashboards show device Wi-Fi/BLE counts, elapsed
  time, estimated distance, recent discovery rate, GPS quality/fix coverage, local
  time, and SD row/byte/flush status. The website marks stale telemetry and keeps
  device session totals separate from its best-effort live CSV rows. Fleet
  workers identify the coordinator as the owner of the merged CSV.

### Link (two-unit)
- **Link Mode** — pairs two AxD units (any mix of C5 and original 2.4 GHz
  boards, Touch or Mini) over an ESP-NOW back-channel using a display-and-confirm
  4-digit code — no typing on either board. Powers **Split Wardrive**: the pair
  divides the channels so each unit scans a different part of the spectrum and the
  combined logs cover it all. Two matching units **alternate-deal** their shared
  plan (two C5s split the full dual-band list; two 2.4 GHz units split 2.4 GHz),
  while a **mixed C5 + 2.4 GHz pair splits by band** — the C5 takes all of 5 GHz
  and the 2.4 GHz unit takes all of 2.4 GHz, with no overlap. A one-second
  time-synced rendezvous on channel 1 swaps telemetry, so each screen shows its
  own, the partner's, and the combined AP count, the partner's link RSSI, and a
  partner-lost alert. Each board logs its own session WiGLE CSV and uses its
  own GPS. The unpaired option covers every channel with Wi-Fi only. Reached
  from GPS → Drive modes → Split; Solo mode remains the Wi-Fi + BLE entry.
- **Fleet Wardrive** — links up to six C5 or classic ESP32 Touch/Mini boards in an explicit
  coordinator/worker topology. Press **Start** on one chip to make it the stable
  coordinator, then **Join** on each worker. The coordinator assigns Wi-Fi/BLE
  roles, maintains a live node count with heartbeats, and merges every worker's
  rows into one WiGLE CSV. Exactly one node is BLE-only. Classic WROOM workers
  take precedence on 2.4 GHz and split its channels without overlap; C5 workers
  split 5 GHz without overlap. In an all-C5 fleet, the C5 Wi-Fi workers split
  the full dual-band plan. The coordinator/node ESP-NOW design was informed by
  **[Piglet](https://github.com/Hamspiced/piglet)** by **Hamspiced**, whose open
  mesh-node implementation provided the reference for keeping one Core
  authoritative while nodes discover, join, and reconnect to it.
- **Fleet Hunter** — multi-node radio direction-finding and target trilateration.
  Selected access points are tracked across the fleet; worker nodes transmit
  observation frames (type 11) over ESP-NOW, and the coordinator solves target GPS
  coordinates, distance, confidence radius, and compass bearing using Weighted
  Centroid Localization. Includes an interactive directional radar compass on-device
  and in the Web Bluetooth interface.

### Status / utility
- **Status** — uptime, free heap, chip temp, SD used/total, GPS fix, Wi-Fi MAC,
  battery (set `kBatteryAdc` in `board_pins.h` to enable).
- **Settings** (Home page 2, serial `t`) — sleep timeout (off / 15s / 30s /
  1m / 2m / 5m), brightness, GPS baud, boot splash, two-tap confirm before
  active tests, NMEA echo, and screen test. Stored in NVS. Footer **Defaults**
  restores those prefs. A sleeping screen wakes on the first tap or button
  without triggering an action. Baud from the GPS screen or serial `u` is
  remembered across reboots.
- **Screen test** (Settings → Screen test) — full-bleed color bars, checkerboard,
  rotation marks, backlight sweep, and touch or five-button probe. Mini draws
  the panel directly, then again through the firmware canvas, so a dead ST7735
  can be told apart from a MiniLayout bug. Serial `h` aborts to Home.
- **Capture manager** (Home page 1 → Files, also Status → Files) — browse `/awokxdag/` files with sizes;
  delete behind a two-tap confirm.
- **Direct wardrive upload** (Recon → Network Tools → Results & Upload → Wardrive Upload) — choose
  and join an access point, choose a `wardrive-*.csv` file from SD, select
  **WiGLE** or **WDGWars**, and explicitly press Upload. Put the relevant API
  credentials in one `wardrive_upload.txt` file on the SD card (root or
  `/awokxdag/`) before opening the page:

  ```ini
  wigle_api_name=YOUR_WIGLE_API_NAME
  wigle_api_token=YOUR_WIGLE_API_TOKEN
  wdgwars_api_key=YOUR_64_CHARACTER_HEX_KEY
  ```

  Copy [`wardrive_upload.example.txt`](wardrive_upload.example.txt), fill in
  your own values, and rename the SD-card copy to `wardrive_upload.txt`.

  Credentials are read only for the request, wiped from RAM afterward, and
  never printed or written to the audit log. Uploads are streamed from SD over
  certificate-verified HTTPS. GPS supplies the absolute device clock; log times and
  SD file timestamps are converted to the GPS-selected local timezone; NTP is used only as a fallback when an upload needs TLS
  before GPS time is available. The files remain on SD until you remove them.
  New wardrive files use WiGLE 1.6 so both destinations receive the documented
  multipart CSV.

---

## Menu map

```
Home  page 1: Recon | Attacks | Monitor | GPS            (footer: version | Next)
      page 2: Files | Settings | Status | About          (footer: Prev | version)
      About  overlay: name, version, board, authorized-use notice (any tap = back)
      (Card menu with descriptions; Files/Settings open straight from Home)

Recon:        Wi-Fi | Bluetooth | RF & Packets | Field Tools | Network Tools
  Wi-Fi:      Wi-Fi Scan | Saved | WPS Scan | Hidden SSID | Security Audit | Wi-Fi 6 Intel
  Bluetooth:  BLE Scan | BLE Trackers | BLE Intel
  RF & Packets: Channel Map | Spectrogram | Packet Mon
  Field Tools: Clients | Cameras | Harvester | Probe Intel | Fleet Hunter | Topology Map
  (tool Back returns to its group; Groups returns to the Recon picker)

Network Tools: Connection | Hosts | Services | Results & Upload
  Connection: SSID / password (phone keypad) / scanned AP / Join; explicit Cancel
  Hosts:      Discover hosts -> select host -> TCP / cameras / printers / SIP
  Services:   TCP Ports | LAN Cameras | Printers | SIP Services | UPnP Mappings
  Results:    three cards/page -> detail; Stop scan while busy
  Actions:    switch hosts/services | Save CSV | Discover hosts | Connection
  Results & Upload: Last hosts | Last services | Wardrive Upload
  (host/service pages are retained; leaving Network Tools disconnects)

Attacks:      Beacon Flood | Evil Portal | Evil Twin | Probe Lure
              (Deauth / Handshake launch from a scanned Wi-Fi result)

Monitor:      Wi-Fi | Bluetooth | Advanced
  Wi-Fi:      Deauth Watch | Rogue Watch | Karma Watch | Beacon Watch | Auth Flood
  Bluetooth:  BLE Spam Watch
  Advanced:   Advanced Watch | Deauth Forensics
  (Stop/Back returns to the tool's group/page; Groups returns to the picker)

GPS:          overview -> Diagnostics / Drive modes
  Diagnostics: Baud / Raw NMEA / receiver and wiring status
  Drive modes: Solo (Wi-Fi + BLE) | Split (two boards) | Fleet (multiple boards)
  Split:      Pair boards / Wi-Fi-only unpaired; paired -> Start / Unpair
  Fleet:      Start as coordinator / Join as worker
Status:       health -> Home / Settings / Files
Files:        newest captures -> details; All / Wardrive / PCAP / Logs filters
              Filter & refresh -> Refresh SD list; details -> Delete confirmation
Settings:     Display | GPS & Time | Behavior | Diagnostics
  Display:    Screen sleep | Brightness | Boot splash
  GPS & Time: GPS baud | automatic local time / timezone / DST status
  Behavior:   Active-tool confirmation
  Diagnostics: GPS receiver | Raw NMEA | Screen / input test
  Editors:    explicit value choices -> Cancel / Save
  Defaults:   separate confirmation; failed persistence shows Retry save
```

## Serial commands (115200 baud)

`w` Wi-Fi scan · `c` channel map · `b` BLE scan · `p` clients · `k` packet
monitor · `m` deauth watch · `g` GPS screen · `n` Link mode · `u` cycle GPS baud
· `r` toggle raw NMEA echo · `s` saved · `t` settings · `d` retry SD · `h` home
(also stops any running tool).

## SD-card output (`/awokxdag/`)

Insert a FAT32 microSD before boot. Saved networks live in NVS so the device
works without a card, and readable snapshots mirror to:

| File | Source |
| --- | --- |
| `firmware_audit.csv` | Rotating operational audit trail (boot, active tests, configuration and file changes) |
| `firmware_audit.previous.csv` | Previous audit segment after the live log reaches 256 KB |
| `saved_networks.csv` | saved AP list |
| `latest_wifi_scan.csv` | last Wi-Fi scan |
| `latest_ble_scan.csv` | last BLE scan |
| `latest_wifi_signal.csv` | signal monitor |
| `latest_lan_hosts.csv` | Last LAN host scan, including partial-scan status |
| `latest_network_services.csv` | Last ports/camera/printer/SIP/UPnP scan |
| `latest_clients.csv` | client sniffer |
| `latest_deauth_log.csv` | Deauth Watch |
| `rogue_log.csv` | Rogue Watch |
| `<ESSID>_<BSSID>.pcap` | WPA handshake capture; hidden SSIDs use `<BSSID>.pcap` (link type 105) |
| `latest_handshake_loc.csv` | Sidecar for each handshake capture: target, channel, EAPOL/PMKID flags, GPS |
| `pmkid.txt` | captured PMKID (hashcat-ready) |
| `portal_creds.csv` | evil portal / evil twin |
| `pktmon.pcap` | packet monitor |
| `wardrive-NNNN.csv` | One new WiGLE 1.6 wardrive file per Start (Wi-Fi + BLE) |
| `security_audit.csv` | Security Audit posture report |
| `ble_trackers.csv` | BLE Trackers scan |
| `harvest.pcap` | Harvester capture (link type 105) |
| `harvest_pmkid.txt` | Harvester PMKIDs (hashcat-ready) |
| `probe_intel.csv` | Probe Intel SSID map |
| `karma_log.csv` | Karma Watch alerts |
| `beacon_flood_log.csv` | Beacon Watch flood windows |
| `auth_flood_log.csv` | Auth Flood alerts |
| `advanced_watch.csv` | Combined beacon, downgrade, disconnect, CSA, EAPOL, association, RF and BLE-churn alerts |

The camera and BLE-spam watches are live-view only.

The firmware audit trail includes a per-boot session id, firmware version, GPS-derived
local time when available (otherwise uptime), result, non-secret details, and GPS
position. It intentionally does not copy portal submissions, packet payloads,
or credentials. The live segment rotates at 256 KB and retains one previous
segment.

---

## Build & flash (Arduino IDE)

1. Arduino IDE 2.x, board-manager URL
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`, install
   **esp32 by Espressif** 3.3.x. Then run `python3 scripts/setup_arduino_ide.py`
   (and again after any core update) so Verify gets `-Wl,-z,muldefs` and
   `-Wl,--wrap=esp_wifi_init -Wl,--wrap=esp_bt_controller_init`. Without muldefs the sketch's
   `ieee80211_raw_frame_sanity_check` collides with Espressif's copy in
   `libnet80211.a`. The wrap shrinks Arduino's default STA RX/TX buffers so
   Dual C5 Touch can keep BLE up beside Wi-Fi.
2. Libraries: **Adafruit GFX**, **Adafruit ILI9341**, **NimBLE-Arduino 2.5.1 or newer**,
   **XPT2046_Touchscreen**, **TinyGPSPlus**.
   NimBLE 2.5.0 can crash when stopping wardriving or another BLE tool because
   it destroys the scan-response timer after host teardown. Update it through
   Library Manager, or run `arduino-cli lib install "NimBLE-Arduino@2.5.1"`.
   The firmware rejects older versions; release builds pin 2.5.1.
3. Open `AWOKxDAG.ino`, select **ESP32C5 Dev Module** with:
   - Flash Size **8 MB**, Partition Scheme **8M with spiffs (3MB APP)**,
     PSRAM **Enabled**, USB CDC On Boot **Disabled**.
4. Plug the USB cable into the port whose ESP32-C5 you want to flash (the AWOK
   Dual C5 has a white and an orange port, one chip each). The connected port
   enumerates as `/dev/ttyUSB*` (Linux), `/dev/cu.usbserial-*` (macOS), or
   `COM*` (Windows). It auto-resets into download mode; if a board does not,
   hold SCREEN BOOT while applying power, then release.

The sketch is currently configured for **Touch** in Arduino IDE
(`AWOK_DUAL_C5_TOUCH`). The packaging script selects the requested board
explicitly, independent of that default. To package the Touch profile:

```bash
python3 scripts/build_firmware.py dual-c5-touch
```

### Flash from the terminal (no IDE)

`scripts/flash_firmware.py` writes a packaged image to the board over the
CP2102 USB port with esptool — no Arduino IDE needed. It builds the image
first if it is missing.

```bash
python3 scripts/flash_firmware.py dual-c5-touch            # autodetect the port
python3 scripts/flash_firmware.py dual-c5-touch --port /dev/ttyUSB0
python3 scripts/flash_firmware.py dual-c5-touch --build    # (re)build, then flash
```

The port is autodetected when only one is present; pass `--port` if several
serial devices are attached. Any board profile `build_firmware.py` accepts
works here too.

### Phone control over BLE (both chips)

Dual C5 and original Dual ESP32 boards can run the full firmware on both chips.
Flash the **screen** chip (white port) with its Touch/Mini profile, then flash
the **bridge** chip (orange port) with the matching headless bridge profile. The
bridge is the *same* firmware built without a screen and with its BLE GATT server
enabled:

```bash
python3 scripts/flash_firmware.py dual-c5-touch    # (or dual-c5-mini) white port
python3 scripts/flash_firmware.py dual-c5-bridge   # cable in the orange port

# Original ESP32 example; use the profile matching Touch/Mini and v1/v2/v3
python3 scripts/flash_firmware.py dual-esp32-touch-v1
python3 scripts/flash_firmware.py dual-esp32-touch-bridge-v1
```

The bridge advertises as **`AxD-Bridge`** over BLE. Open the control page —
`https://dagnazty.github.io/awokxdag/control.html` (or the local
`website/public/control.html`) — in **Bluefy** on iOS or **Chrome** on Android
(Safari has no Web Bluetooth), tap **Connect**, then pick a **Target chip**:

- **This bridge** — the orange chip runs the tool *locally* (Wi-Fi/BLE scan,
  monitors, attacks). GPS-wardrive and Files/SD are screen-chip only for now.
- **Screen chip** — the command is relayed over ESP-NOW to the white chip, which
  runs it.

Either way the network list, per-AP actions (Deauth / Grab / Track / Evil Twin /
Probe Lure) and live counts stream back to the phone, tagged with the chip that
produced them. The bridge sweeps every channel when relaying so a command reaches
the screen chip even while it is hopping. The two chips talk over ESP-NOW; the shared wire format is
`AWOKxDAG/link_protocol.h`.

### Dual C5 Mini

Install **Adafruit ST7735 and ST7789 Library** in addition to the libraries above,
then build and package the Mini profile:

```bash
python3 scripts/build_firmware.py dual-c5-mini
```

Outputs are in `build/dual-c5-mini-1.6.1/`, with explicit board names and
`SHA256SUMS`. This command only compiles and packages; it does not flash.
The Mini uses a native 128 × 128 layout with readable text, highlighted menu
rows, wrapped details, and compact charts. Up/down moves through rows, center
selects, right jumps to actions, and left returns to the top of the screen. At
boot it shows the same AWOK logo as the Touch board, downscaled to 96 × 128 by
`scripts/gen_mini_boot.py` (re-run with `--threshold` to retune the 1-bit art).
Use `dual-c5-touch` with the same script to package the default Touch build.

### Original ESP32 boards (2.4 GHz)

The original **Dual ESP32 Touch v1/v2/v3** and **Dual ESP32 Mini v1/v2/v3**
profiles target the classic **ESP32 Dev Module** (4 MB flash, `huge_app`
partition, PSRAM disabled) and add the **Adafruit ST7735 and ST7789 Library** for
the Mini panels. Package any of them with the same script:

```bash
python3 scripts/build_firmware.py dual-esp32-touch-v1   # or -v2 / -v3
python3 scripts/build_firmware.py dual-esp32-mini-v1    # or -v2 / -v3
python3 scripts/build_firmware.py dual-esp32-touch-bridge-v1
python3 scripts/build_firmware.py dual-esp32-mini-bridge-v1
```

Each classic Touch/Mini revision has a matching `*-bridge-v1`, `*-bridge-v2`,
or `*-bridge-v3` artifact. These headless builds preserve the selected board's
GPS/SD wiring, expose the same `AxD-Bridge` BLE service as the C5 bridge, and
relay only across the classic ESP32's supported 2.4 GHz channels.

## Hardware map (Dual C5 Touch)

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

## Hardware map (Dual C5 Mini)

| Function | GPIO |
| --- | ---: |
| SPI SCK / MISO / MOSI | 6 / 2 / 7 |
| ST7735 CS / DC / Reset | 23 / 24 / (none) |
| Backlight | 5 (active low) |
| Buttons Left / Center / Up / Right / Down | 0 / 1 / 4 / 8 / 9 |
| SD card CS | 10 |
| GPS UART1 RX / TX | 14 / 13 @ 115200 NMEA |
| Battery ADC | unset (`kBatteryAdc = -1`) |

The Mini shares the SPI bus, display CS/DC, SD, and GPS wiring with the Touch
board; it swaps the ILI9341 + XPT2046 touchscreen for a 128 × 128 ST7735 driven
by five buttons, and its backlight is active-low on GPIO 5. Selected by building
with `AWOK_DUAL_C5_MINI` (via `scripts/build_firmware.py dual-c5-mini`). This
mapping was recovered from the bundled Mini firmware (see the comments in
`board_pins.h`) and is confirmed working on hardware.

## Hardware map (original ESP32 Touch v1/v2/v3)

| Function | GPIO |
| --- | ---: |
| SPI SCK / MISO / MOSI | 18 / 19 / 23 |
| ILI9341 CS / DC / Reset | 17 / 16 / 5 |
| Backlight | 32 (active high) |
| XPT2046 touch CS | 21 |
| SD card CS | 12 (v1) · 14 (v2, v3) |
| GPS UART2 RX / TX | 4 / 13 @ 115200 NMEA (selectable) |
| Battery ADC | unset (`kBatteryAdc = -1`) |

## Hardware map (original ESP32 Mini v1/v2/v3)

| Function | GPIO |
| --- | ---: |
| SPI SCK / MISO / MOSI | 18 / 19 / 23 |
| ST7735 CS / DC / Reset | 17 / 16 / 5 |
| Backlight | 32 (active low) |
| Buttons Left / Center / Up / Right / Down | 13 / 34 / 36 / 39 / 35 |
| SD card CS | 4 |
| GPS UART2 RX / TX | 21 / 22 @ 9600 NMEA (selectable) |
| Battery ADC | unset (`kBatteryAdc = -1`) |

These original 2.4 GHz-only ESP32 profiles share the classic display bus and are
selected with `AWOK_DUAL_ESP32_TOUCH_V<n>` / `AWOK_DUAL_ESP32_MINI_V<n>`. On the
Mini, GPIO34–39 are **input-only with no internal pull-ups** (Center / Up / Right
/ Down rely on the board's external biasing; Left on GPIO13 uses `INPUT_PULLUP`).
Pin sources and validation status: [original Touch](docs/dual-esp32-touch.md),
[original Mini](docs/dual-esp32-mini.md). Link pairing: [Link Mode](docs/link-mode.md).

## Collection capacity

The capacities below describe the C5 profiles. Original ESP32 Touch profiles
retain 32 results per table and 128 Wardrive deduplication addresses; combined
views use Wi-Fi only. See [original Touch](docs/dual-esp32-touch.md) and
[original Mini](docs/dual-esp32-mini.md).

Wi-Fi and BLE scans retain up to **64 results** each (dual-band C5). BLE results use ten rows
per page, with Prev/Next controls and detail inspection on every page.
Clients, Security Audit, BLE Trackers, Cameras, WPS, Hidden SSID, Harvester,
Probe Intel, and Karma Watch each retain up to **64 entries** (Probe Intel counts
SSIDs). Live summary screens keep their existing row limits; available CSV
exports include the full collected table. Continuous BLE scans use callbacks
without retaining a NimBLE result list, so the regular scan's 64-result cap does
not stop their incoming observations. Their feature tables remain bounded.

On Mini, the Wi-Fi, BLE, Clients, Probe Intel, and Karma Watch result tables
are allocated in PSRAM at boot, preserving their 64-entry capacities and moving
**47 KiB** of table storage out of internal RAM. Strings are constructed normally;
radio buffers and callback queues remain internal. A table falls back to internal
RAM if its PSRAM allocation fails; failure of both allocations stops startup.
Touch keeps its static result tables.

Mini Wardrive, Cameras, and Advanced Watch now attempt Wi-Fi + BLE when at least
32 KiB of tables were placed in PSRAM and, after stopping previous radios, at
least 110 KiB of internal RAM is free with a 36 KiB contiguous block. BLE starts
first, then Wi-Fi. If the budget check, radio initialization, or BLE scan start
fails, the view retains Wi-Fi-only operation and displays BLE off. These memory
thresholds are estimates; simultaneous scanning and repeated tool switching still
need hardware validation under load. See Serial Monitor for table placement and
internal free/largest-block readings before and after radio initialization.

## Source layout

Single Arduino sketch split into feature tabs (one translation unit):
`AWOKxDAG.ino` (globals, UI, scanning, menus, setup/loop) + `awok_common.h`
(types/enums/constants) + `board_pins.h`, and per-feature tabs: `gps`,
`deauth`, `handshake`, `sniffer`, `beacon`, `portal`, `wardrive` (in gps),
`pktmon`, `cameras`, `wps`, `hidden`, `roguewatch`, `bledetect`, `probelure`,
`securityaudit`, `settings`, `screentest`, `tracker`, `harvester`, `probeintel`, `karmawatch`,
`beaconwatch`, `authflood`, `advancedwatch`, `locator`, `link` (Link Mode +
Split Wardrive; [docs/link-mode.md](docs/link-mode.md)), `auditlog`, `status`,
`files`, `input`, `networktools`. `network_parse.h` contains bounded
network-response parsers shared with the host tests in `tests/host/` (run
`bash tests/host/run.sh`).

## Recovery

Flashing replaces the app on whichever ESP32-C5 the USB cable is plugged into. Keep an official
Marauder `_v8.bin` and its C5 bootloader/partition files so factory firmware
can be restored.

## Credits & license

AxD is original firmware, but it stands on prior work and would not exist
without it. Thanks to:

- **[Evil-M5Project](https://github.com/7h30th3r0n3/Evil-M5Project)** by
  **7h30th3r0n3** — thanks for the work behind the LAN host/port scanning,
  CCTV, printer, SIP OPTIONS, UPnP mapping, and Wall of Flippers features used
  as references for our Network Tools and BLE identification hints. We adapted
  those ideas to AxD's radio lifecycle, bounded scan engine, and Touch/Mini
  controls. The linked Evil-Cardputer source carries an MIT notice; some sections
  credit other projects, including Bruce. See [third-party notices](THIRD_PARTY_NOTICES.md).
- **[ESP32 Marauder](https://github.com/justcallmekoko/ESP32Marauder)** by
  **justcallmekoko (Justin Hazard)** — the reference for the original ESP32
  board pin maps, display/SPI setup and touch-calibration constants (see
  [dual-esp32-touch.md](docs/dual-esp32-touch.md) and
  [dual-esp32-mini.md](docs/dual-esp32-mini.md) for exact upstream sources),
  the AWOK white-port board compatibility mapping, and the radio-lifecycle
  approach that `shutdownWiFi()` / `shutdownBLE()` follow. Marauder's documented
  direct-upload workflow also informed the SD credential-file convention and
  select-network/select-file/select-service user flow; AxD's uploader and UI are
  newly implemented here.
- **[FZEasyMarauderFlash](https://github.com/SkeletonMan03/FZEasyMarauderFlash)**
  by **SkeletonMan03** — flashing reference used for the original Mini pin
  sourcing.
- **[Piglet](https://github.com/Hamspiced/piglet)** by **Hamspiced** — the
  coordinator/node ESP-NOW reference for Fleet Wardrive. Piglet's Core/Node
  model informed AxD's explicit Start/Join roles, coordinator pinning, worker
  discovery/heartbeats, and reconnect behavior. AxD uses its own packet format,
  roster, channel-dealing, aggregation, display, and web integration.
- The libraries this firmware builds on: **Adafruit GFX**, **Adafruit ILI9341**,
  **Adafruit ST7735 and ST7789**, **Adafruit BusIO**, **NimBLE-Arduino**,
  **XPT2046_Touchscreen**, and **TinyGPSPlus** — each under its own license.

**License:** AxD's own code is released under the **MIT License** (see
[LICENSE](LICENSE)). Hardware pin numbers and calibration constants are factual
board-interface values, and the radio-lifecycle behavior was reimplemented rather
than copied. ESP32 Marauder is licensed **GPL-3.0** and each referenced project
and library remains under its own license — consult those upstreams for their
terms before redistributing derived work.
