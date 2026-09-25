# Changelog

All notable changes to AxD are documented here. This project follows
[Semantic Versioning](https://semver.org/).

## [Unreleased]

## [1.7.5] - 2026-09-25

### Changed

- Unified the scan-result screens with the shared card layout (title + one-line
  detail, 4 per page): Wi-Fi results, BLE Scan, WPS Scan, Hidden SSID, Security
  Audit, Wi-Fi 6 Intel, BLE Trackers, BLE Intel, Clients, Cameras, and Probe
  Intel. Card outline conveys state per screen (saved/revealed green, open-WPS /
  Flipper / camera / following red, risk tiers for the audit, ecosystem color for
  BLE Intel, generation for Wi-Fi 6). Every result screen now pages with
  Back/Prev/Next and a page indicator in the header; existing per-screen actions
  (Save/Export/Reset) and BLE Scan's tap-to-detail are preserved.

## [1.7.4] - 2026-09-24

### Changed

- Home is now a paginated single-column tile list: page 1 Recon / Attacks /
  Monitor / GPS / Files, page 2 Settings / Status / About, with Prev/Next paging
  and the firmware version shown in the unused footer slot. **Files** and
  **Settings** open directly from Home instead of only through the device-health
  Status screen. Back from the Files list and from the Settings root now returns
  to Home. About and the memory/radio error screens share one "any tap returns to
  the tiles" gesture.
- Navigation labels standardized to plain **Back** / **Prev** / **Next** (the
  decorated `< Back`, `< Prev`, `Next >`, `About >` variants were removed) across
  Home, Recon, Deauth Forensics, and Wi-Fi 6 Intel; Home and Stop are unchanged.
- Unified menu styling: Home, the Recon groups/tools (Wi-Fi, Bluetooth, RF &
  Packets, Field Tools), the Attacks menu, and the Monitor menu now render as the
  same Network-Tools-style cards (bold title + one-line description, 4 per page,
  `Back / Prev / Next` pager) via a shared `drawMenuCard`. Monitor cards keep
  their running state as a green outline; Attacks cards are danger-red. Tool
  routing, view IDs, and pagination are unchanged.

## [1.7.3] - 2026-09-24

### Changed

- Network Tools now separates Connection, Hosts, Services, and Results & Upload.
  Larger cards, visible connection/IP state, and selected-host/page return replace
  the flat tool list. Results use three cards per page, a large Stop action while
  scanning, and a separate Actions / Save CSV menu. Connection setup has explicit
  Cancel while joining; missing prerequisites, empty results, partial scans, and
  save failures remain visible. Mini retains scrolling position when connection
  state is unchanged. Existing probes, credentials, upload requests, and view IDs
  are preserved; stale host/result selections are guarded after memory release
  or connection loss.
- Monitor groups all eight detectors into Wi-Fi, Bluetooth, and Advanced, with
  descriptions and running/stopped state. Touch uses three large cards per page;
  Mini uses selectable scrolling entries. Stop/Back returns to the detector's
  group and page, including tools launched remotely. Opening a running detector
  preserves its results. Existing Reset, Clear, and Export actions remain available;
  detector algorithms, BLE participation, and radio start/stop routines are unchanged.
- Settings is grouped into Display, GPS & Time, Behavior, and Diagnostics.
  Value pickers show the current setting and require Save; Cancel leaves it
  unchanged. GPS & Time shows the automatic local zone/DST and receiver state.
  Defaults require a separate confirmation, failed writes report RAM-only
  changes with Retry save, and diagnostics return to their originating group.
  The existing NVS record, defaults, and view IDs are unchanged.
- GPS now separates the location/local-time overview from receiver Diagnostics
  (baud, raw NMEA, wiring and counters). Mini gets a native compact summary.
  Drive modes explicitly separate Solo Wi-Fi/BLE, two-board Split, and Fleet.
  Split has a reachable Pair action; Fleet has explicit coordinator/worker
  choices. Mode navigation protects active sessions and preserves the existing
  radio, pairing, and start/stop routines.
- Reworked the board Captures menu with four larger rows, newest-first ordering,
  All/Wardrive/PCAP/Logs filters, a named Refresh action, and file details.
  The bounded list retains the newest 64 files and discloses larger directories.
  Delete now has a separate confirmation target, reports failures, and blocks
  the open wardrive CSV. A remote refresh invalidates stale menu selections.
  Touch and Mini share the same actions; remote transfer indices are not filtered.

### Added

- Wardrive dashboards on Touch, Mini, and the control website: session counts,
  elapsed time, estimated distance, recent discovery rate, local time, GPS
  fix coverage/quality, and SD write/flush status. Website telemetry is scoped
  to the selected bridge/screen and marks stale updates; live received rows
  remain separate from device session totals. Radio scheduling is unchanged.
- Bluetooth preview/download Pause, Resume, and Restart. Resume keeps received
  chunks in the open tab, reconnects to the same bridge, and starts at the first
  missing chunk after the device verifies the original SD snapshot. Whole-file
  CRC32 verification blocks corrupt or incomplete previews/saves. Appended CSVs
  can resume their original prefix; changed or truncated files require Restart.
  Requires matching updates on both chips and the website; USB streaming is unchanged.

## [1.7.2] - 2026-09-23

### Changed

- Replaced the credential editor with a three-column phone keypad: large Touch
  targets, abc/ABC/123/symbol modes, repeat-tap letter cycling, a one-second
  timeout, and explicit Next/Delete/Cancel/Done. Mini uses a native keypad grid
  with four-direction joystick navigation. All printable ASCII is available;
  passwords stay masked and keyboard touch coordinates are not logged. The
  character currently being cycled shows in the clear until it is committed so
  repeat-tap letter selection is usable while entering a masked password.
- GPS coordinates now select a local timezone offline. Local time and DST status
  appear on GPS/Wardrive screens; log timestamps, WiGLE FirstSeen, and filesystem
  timestamps use local time. The last zone persists through fix loss/reboots;
  valid fixes refresh the choice every 30 seconds. Flash-only map/rule tables
  cover 2020–2099 using IANA 2026d, including non-DST and irregular DST regions.
  Compact geographic boundaries are approximate; future legal changes require
  refreshing the bundled data. Absolute system time remains correct for TLS/NTP.

## [1.7.1] - 2026-09-22

### Fixed

- Require NimBLE-Arduino 2.5.1 or newer for its scan-response-timer shutdown
  fix; reject older libraries at compile time and pin release builds to 2.5.1.
  NimBLE 2.5.0 deleted the scan timer after host teardown, exposing a crash on
  wardriving/BLE-tool exit. Wardriving now closes its CSV before radio shutdown,
  logs shutdown stages, ignores duplicate stops, and closes its CSV if radio
  startup fails. Dual-radio scheduling and BLE participation are unchanged.

### Changed

- Fleet row rings, Wi-Fi 6 Intel, topology, BLE Intel, and deauth-forensics
  buffers now allocate on tool start and release on stop. Loop-owned result
  tables prefer PSRAM; callback queues remain in internal RAM. Fleet allocates
  only the active role's ring. Failed allocations unwind and report a memory
  error. Callback queues reject late writes across stop/restart; topology peer
  merges and forensic sequence history now run in the main loop. Tools drain
  accepted observations and attempt CSV export before freeing their results.

## [1.7.0] - 2026-09-22

### Fixed

- Bridge BLE results now send an explicit payload per notification instead of
  scheduling an update to a shared characteristic value, preventing concurrent
  telemetry/file writes from replacing pending data. Transfer logs now identify
  ESP-NOW enqueue failures, BLE enqueue failures and MTU, and the exact chunk
  whose browser ACK exhausted its retries.
- BLE file preview/download now uses browser acknowledgments for every chunk
  and completion message, with up to 10 attempts per chunk. Lost ESP-NOW packets,
  BLE notifications, and ACKs are retried without duplicating file bytes. Transfer
  tokens reject stale packets; 32-bit sequences support captures above 6 MB.
  Reliable file notifications are queued outside the Wi-Fi callback, and bridge
  scanning pauses during the transfer. Requires both chips and the updated control
  page; legacy USB file streaming remains available.
- Remote file preview/download now waits for a receiver-ready handshake after
  the bridge finishes its command channel sweep. Previously the screen started
  sending immediately while the bridge hopped away, consistently losing the
  first chunks containing the wardrive CSV header. Update both chips for this
  handshake; USB transfers are unchanged.
- Wardrive SD sessions now verify the complete WiGLE metadata and column-header
  write before accepting rows, including Split/Fleet Link sessions. Failed header
  writes disable SD logging and remove the incomplete new file.
- Remote SD downloads reject missing, malformed, or short chunks instead of
  silently saving partial files (which could omit the wardrive header). Reading
  an active wardrive file flushes its buffered SD data first.

### Changed

- Replaced Recon's four mixed pages with Wi-Fi, Bluetooth, RF & Packets, and
  Field Tools groups, plus direct access to Network Tools. Each group fits on
  one menu page; returning from a tool preserves its group on Touch and Mini.

## [1.6.5] - 2026-09-22

### Added

- **Wi-Fi 6 / 802.11ax OFDMA & BSS Color Intelligence (`AWOKxDAG/wifi6intel.ino`):**
  - **Passive HE Beacon & Probe Inspector:** Promiscuous management frame parser extracting 802.11ax High Efficiency capabilities and operation elements (Extended Tag 255 with Ext IDs 35 and 36) without transmitting.
  - **BSS Color Collision Analysis:** Extracts 6-bit BSS Color codes (1–63) and BSS Color Disabled flags to map spatial reuse channel congestion and co-channel interference.
  - **Channel Width & Generation Classification:** Identifies channel operating widths (20, 40, 80, 160 MHz) and classifies networks into Wi-Fi 4 (802.11n), Wi-Fi 5 (802.11ac), and Wi-Fi 6 (802.11ax).
  - **On-Device UI (`View::kWifi6Intel`):** Touch (240×320) and Mini (128×128) scrollable list showing generational badges, color pills, channel widths, BSSID, and RSSI with on-device SD CSV export.
  - **SD Card CSV Logging:** Exports observed Wi-Fi 6 parameters and collision states to `/awokxdag/wifi6_intel.csv` with GPS coordinates.
  - **Telemetry Streaming:** Emits `$AXINTEL,bssid,ssid,channel,generation,bssColor,channelWidth,rssi` over Serial and Web Bluetooth (`kSourceWifi6Intel = 7`, opcode 59 `kAxdCmdWifi6Intel`).
  - **Remote Dashboard Tab (`control.html`):** Added dedicated "⚡ Wi-Fi 6" tab with live interactive 64-cell BSS Color collision matrix, generational breakdown stats, band filters, and CSV export.

- **Targeted Deauth & Disassociation Forensic Analyzer (`AWOKxDAG/deauthforensics.ino`):**
  - **Promiscuous Forensic Attribution Engine:** Passive sniffer for 802.11 deauthentication (subtype 12) and disassociation (subtype 10) frames.
  - **Attack Classification:** Differentiates shotgun broadcast floods (`ff:ff:ff:ff:ff:ff`) from targeted unicast victim station attacks.
  - **Transmitter Sequence Number Anomaly Detection:** Tracks per-transmitter 802.11 sequence counters and flags sudden sequence number jumps ($|\Delta| > 10$) indicating forged/spoofed transmitter MAC addresses.
  - **Reason Code Decoding:** Decodes standard 802.11 reason codes (1 Unspecified, 2 Prev Auth Invalid, 3 Station Leaving, 6 Class 2 Nonauth, 7 Class 3 Nonassoc, 8 Station Disassoc, 15 4-Way Handshake Timeout, etc.).
  - **On-Device UI (`View::kDeauthForensics`):** Touch and Mini screens with live incident counters, attack classification badges, and victim MAC tracking.
  - **SD Card CSV Logging:** Logs complete forensic event audits to `/awokxdag/deauth_forensics.csv` with timestamps and GPS geotags.
  - **Telemetry & Real-Time Alerts:** Emits `$DEAUTH,type,targetMac,sourceMac,bssid,reason,seqJump,rssi,channel` over Serial and Web Bluetooth (`kSourceDeauthForensics = 8`, opcode 63 `kAxdCmdDeauthForensics`).
  - **Remote Dashboard Tab (`control.html`):** Added dedicated "🛡️ Deauth Forensics" tab with real-time attack alert banners, live forensic event table, filter segments, and CSV export.

- **Remote SD Card File Manager & Web Serial Transfer (`files.ino`, `link.ino`, `control.html`):**
  - **In-Browser File Manager Tab (`tab-files`):** Dedicated "📁 SD Files" dashboard tab in the WebUI to browse, preview, and download capture files, wardrive CSVs, and logs directly from `/awokxdag` to your phone or PC.
  - **Binary-Safe Base64 Chunked Streaming:** Emits on-the-fly Base64 encoded chunks (`$FILEDATA,<seq>,<total>,<data>`) over Web Bluetooth and Web Serial, ensuring binary safe packet capture (`.pcap`) and text log transfers without delimiter collisions or control character corruption.
  - **Dual-Board ESP-NOW Relay:** File listing (`kAxdCmdFileList = 64`), download (`kAxdCmdFileGet = 65`), delete (`kAxdCmdFileDelete = 66`), and abort (`kAxdCmdFileAbort = 67`) requests are seamlessly bridged between the Screen Chip (holding the physical SD card) and the Orange Bridge Chip over ESP-NOW.
  - **Web Serial Integration:** Added native Web Serial (`navigator.serial`) connection engine at 115200 baud to the WebUI header, enabling direct cable plug-and-play on desktop PCs and Android USB-OTG in addition to Web Bluetooth.
  - **In-Browser Preview Drawer:** Instant in-browser preview with copy-to-clipboard for wardrive CSVs, handshake hashcat files, and text logs.
  - **Live Progress & Throughput Tracker:** Shows animated progress bar, percentage completion, transfer speed in KB/s, and byte-level verification with automatic browser file saving.

## [1.6.4] - 2026-09-21

### Added

- **Dual-Band RF Spectrogram & Waterfall Analyzer (`AWOKxDAG/spectrogram.ino`):**
  - **Promiscuous RF Monitor:** Continuous passive channel dwell measuring frame arrival rate, byte volume, peak RSSI, and noise floor across 2.4 GHz (channels 1–13) and 5 GHz (channels 36–165).
  - **Duty Cycle & Airtime Saturation Metric:** Computes estimated physical on-air channel occupancy percentage (0–100%) and categorizes Management, Control, and Data frame distributions.
  - **On-Device Thermal Waterfall Spectrogram (`View::kSpectrogram`):** Touch (240×320) interface displaying live channel stats, instantaneous spectrum bar chart with decay peak-hold indicators, and a scrolling 28-row thermal 2D waterfall heat map. Mini (128×128) compact duty cycle and bar chart.
  - **Channel Sweeping & Single-Channel Lock Modes:** Supports cycling through All Channels, 2.4 GHz only, or locking onto a specific congested channel (with direct tap-to-lock on the bar chart).
  - **SD Card CSV Logging:** Exports full channel spectrum and duty cycle snapshots to `/awokxdag/spectrogram.csv` with GPS coordinates.
  - **Serial & BLE Telemetry Streaming:** Emits `$SPEC,ch,dutyPct,pkts,peakRssi,noise,mgmt,ctrl,data` over serial and Web Bluetooth (`kSourceSpectrogram = 6`).
  - **Remote Dashboard Tab (`control.html`):** Dedicated "🌈 Spectrogram" tab featuring real-time HTML5 Canvas spectrum bar chart with peak hold, high-fps scrolling thermal waterfall canvas, band filters, and CSV export. Opcode 58 (`kAxdCmdSpectrogram`) remote activation.

## [1.6.3] - 2026-09-20

### Added

- **BLE Ecosystem Intel & Continuity Decoder (`AWOKxDAG/bleintel.ino`):**
  - **Proprietary Vendor Payload Decoding:** Continuous passive BLE scan that parses manufacturer and service data payloads without active pairing:
    - **Apple Continuity (0x004C):** Decodes Proximity Pairing (`0x07`) identifying models (AirPods 1/2/3, AirPods Pro 1/2, AirPods Max, Powerbeats Pro, Beats Solo Pro, Studio Buds, Fit Pro) with exact Left, Right, and Case battery percentages and active charging states; parses AirDrop (`0x05`), Nearby Info (`0x10`), Find My (`0x12`), AirPlay Target (`0x09`), Apple Watch tethering (`0x0B`), and Handoff (`0x0C`).
    - **Google / Android Fast Pair (0xFE2C):** Matches Fast Pair Service Data (0xFE2C), decoding Model ID hex strings and pairing readiness.
    - **Microsoft Swift Pair (0x0006):** Identifies PC peripheral discovery advertisements.
    - **Samsung Continuity (0x0075 / 0xFD5A):** Detects Samsung Galaxy continuity beacons and SmartThings Find beacons.
  - **On-Device Interface (`View::kBleIntel`):** Touch (240×320) and Mini (128×128) scrollable list showing vendor ecosystem color-coded badges, device type, address, RSSI, and decoded battery/status details.
  - **SD Card CSV Logging:** Exports full telemetry to `/awokxdag/ble_intel.csv` with GPS coordinates, timestamps, battery levels, and sightings.
  - **Serial & BLE Telemetry Streaming:** Emits `$BLEINTEL,mac,ecosystem,deviceType,rssi,batteryL,batteryR,batteryCase,details` strings via serial monitor and over Web Bluetooth notifications (`kSourceBleIntel = 5`).
  - **Remote Control Dashboard (`control.html`):** Added dedicated "🎧 BLE Intel" tab with live ecosystem filtering (Apple, Google, Microsoft, Samsung), real-time battery pill indicators with charge indicators, CSV export, and opcode 57 (`kAxdCmdBleIntel`) activation.

## [1.6.1] - 2026-09-20

### Added

- **Swarm Mesh Topology Graph (Client & AP Relationship Map):**
  - **Promiscuous link sniffer (`AWOKxDAG/topologymap.ino`):** Channel-hopping 802.11 monitor
    capturing active associations from data frames (`toDs && !fromDs`) and directed probe
    request leaks (`0x40` subtype) across 2.4 GHz and 5 GHz bands. Detects unencrypted open
    networks directly from beacon/probe capability flags.
  - **Multi-node Swarm Protocol (`AWOKxDAG/link_protocol.h` & `link.ino`):**
    Introduced `kLinkMsgFleetTopology` (type 13, 48 bytes) to stream client-to-AP and client-to-probe
    links across fleet worker nodes to the coordinator. Opcode 56 (`kAxdCmdTopology`) provides
    remote activation over ESP-NOW and Web Bluetooth.
  - **On-device Cluster UI (`View::kTopologyMap`):** Hierarchical cluster matrix on Touch
    (240×320) and Mini (128×128) screens, displaying APs with channel, encryption badge, and
    indented connected client stations showing packet count and signal RSSI.
  - **Interactive Web Bluetooth Force-Directed Physics Graph (`control.html`):**
    Dedicated "🕸️ Topology" tab featuring real-time HTML5 Canvas particle physics
    (Coulomb repulsion, Hooke springs, centering gravity), interactive drag-and-drop,
    node inspector drawer, live metric counters (APs, Clients, Probes, Open Networks),
    probe leak toggles, and JSON/CSV export.
  - **Bridge & SD Telemetry (`AxDBridge/AxDBridge.ino` & `topologymap.ino`):**
    Streams `$TOPO,AP,...`, `$TOPO,CLI,...`, and `$TOPO,PRB,...` telemetry via `kSourceTopo` (4)
    over BLE and logs to `/awokxdag/topology_map.csv` with GPS coordinates.

## [1.6.0] - 2026-09-20

### Added

- **Fleet Hunter (multi-node target radio direction-finding & trilateration).**
  - **Trilateration engine (`AWOKxDAG/locator.ino`):** Computes estimated target GPS
    coordinates $(lat, lon)$, geodesic distance, and confidence radius using
    Weighted Centroid Localization (WCL) with log-distance path loss ($n = 2.5$).
    Calculates dynamic forward azimuth compass bearing ($0^\circ$–$360^\circ$) to guide
    operators directly to rogue or target transmitters on foot.
  - **Multi-node ESP-NOW protocol (`AWOKxDAG/link_protocol.h` & `link.ino`):**
    Introduced `kLinkMsgFleetHuntObservation` (type 11, 28 bytes) and
    `kLinkMsgFleetHuntResult` (type 12) frames. Fleet worker nodes send target
    sightings with their own GPS coordinates and RSSI readings to the coordinator,
    which aggregates observations across the fleet in a 16-point ring buffer.
  - **On-device Radar Compass display:** Added `View::kFleetHunt` screen for Touch
    (240×320) and Mini (128×128) devices, featuring a tactical circular radar scope
    with concentric range rings, cardinal compass headers (N/E/S/W), real-time
    target vector ray, animated pulsating target blip, confidence radius boundary,
    and numerical metrics (distance, bearing, coordinates, RSSI, point count).
  - **Interactive Web Bluetooth Radar Scope (`control.html`):**
    - Added `🎯 Fleet Hunter` action button (opcode 55) for selected target APs.
    - Added HTML5 Canvas radar scope visualizer with animated rotating sweep beam,
      target blip, distance readout, directional compass badge, and confidence indicator.
    - Direct "📍 Open in Maps" integration generating live Google Maps links from
      solved coordinates.
    - Added `$HUNT` telemetry parser (`parseHuntRow()`) and updated WiGLE CSV export
      release header to `1.6.0`.
  - **Bridge relay (`AxDBridge/AxDBridge.ino`):** Added headless ESP-NOW forwarding of
    `FleetHuntResult` frames directly to Web Bluetooth clients as `kSourceHunt` (3)
    notifications.

## [1.5.5] - 2026-09-20

### Fixed

- **Live Wi-Fi AP and BLE discovery counters in Web Bluetooth control.** Resolved an
  issue where scanned Wi-Fi APs and BLE devices were not displayed (showing `0` or
  `–`) in the control interface during wardriving and fleet multi-node wardriving
  for both the bridge chip and the screen chip.
  - **Fleet status telemetry (`AWOKxDAG/link.ino`):** Fixed `linkBroadcastStatus()`
    which previously only checked `wardriveActive`. During fleet wardriving,
    `linkWardriveActive` and `fleetWardriveOn` were active instead, causing discovery
    counts to fall back to unpopulated manual scan counters (`0`). It now checks all
    wardriving and fleet states and retains session totals upon stopping.
  - **Screen-to-bridge inter-pass channel rendezvous (`AWOKxDAG/gps.ino`):** While
    actively wardriving on the screen chip, the Wi-Fi radio continuously sweeps
    channels 1–14 (and 5 GHz). Between scan passes (right after `WiFi.scanDelete()`)
    and on BLE-to-Wi-Fi phase transitions, the screen chip now temporarily homes to
    `kLinkChannel` (channel 1) and calls `linkBroadcastStatus()`. This ensures the
    bridge chip reliably receives live updates every 1–2 seconds.
  - **Bridge protocol & self-status alignment (`AxDBridge/AxDBridge.ino`):** Added the
    missing source prefix byte (`blob[0] = 1`), populated the complete 25-byte status
    payload with GPS and fleet tails, prefixed Wi-Fi result frames, and added a 1 Hz
    self-status heartbeat for `kSourceBridge` (`0`).

### Added

- **Dual-target telemetry breakdown in web control app.**
  - **Telemetry tab:** Added target indicator badge (`Screen Chip` vs `Bridge Chip`)
    and live dual-target quick breakdown (`Screen: X APs · Y BLE | Bridge: A APs · B BLE`).
  - **Fleet tab:** Added dedicated breakdown stat cards for Screen Chip Scanned,
    Bridge Chip Scanned, and Fleet Total Sighted.
  - **Live node descriptions:** Enhanced `fleetDescription()` to dynamically append
    live AP and BLE counts per node (e.g. `coordinator · code 1234 · 3 nodes · logging · 142 APs · 38 BLE`).
  - **Connection list summary:** Updated `connSummary()` to report AP and BLE
    counts across both target chips.
  - **Tool opcode decoder:** Added friendly mapping for `View::kWardrive` (19) and
    active `Fleet Wardrive` state.

## [1.5.4] - 2026-09-20

### Added

- **Modernized Web Bluetooth remote control interface.** The web control app
  (`website/public/control.html` and `website/dist/control.html`) now features a
  streamlined tactical dark theme, tabbed navigation (Telemetry, Wi-Fi Scan,
  Wardrive, Tools & Ops, Fleet, and Terminal Log), real-time Wi-Fi network search
  and sorting (by signal strength or channel), dynamic 4-bar RSSI signal meters,
  and direct Google Maps link from live GPS coordinates.
- **Real-time WiGLE wardrive stream parsing.** Incoming WiGLE 1.6 CSV rows
  streamed from the ESP32 bridge over BLE are parsed live into a structured
  sightings table displaying Type (Wi-Fi/BLE), SSID, BSSID, RSSI, Channel, Auth,
  and GPS coordinates, alongside live counters for total sightings, Wi-Fi APs,
  BLE devices, and open networks.
- **Friendly tool opcode decoding.** Raw tool opcodes (`#1`, `#7`, `#50`, `#52`,
  etc.) displayed on the phone are automatically mapped to readable names and
  status badges (e.g. `#7 Wardrive`, `#1 Wi-Fi Scan`, `#52 Deauth`).

### Fixed

- **Fleet coordinator lockup under multi-node wardrive load.** Fixed an issue where
  the fleet coordinator froze shortly after wardriving began with more than one
  worker node connected.
  - **Batched SD writes:** Coordinator now buffers WiGLE CSV writes and batches SD
    card flushes (every 5 rows or 1000 ms) instead of executing a synchronous,
    blocking `flush()` on every single incoming row from worker nodes.
  - **Bounded ESP-NOW row queue processing:** Restricted ESP-NOW fleet row queue
    ingestion to a maximum of 4 rows per frame, increased queue capacity from 16 to
    32 entries, and added cooperative task yielding (`yield()` / `delay(1)`) every
    3 dispatches to avoid starving the coordinator main loop and watchdog timer.
  - **Non-blocking BLE relay buffer:** Replaced blocking GATT notification loops
    with a non-blocking ring buffer on the bridge chip, preventing worker node
    backpressure when streaming live rows to a connected phone.
- **Classic ESP32 DRAM overflow during compilation.** Resolved `region 'dram0_0_seg' overflowed by 8336 bytes` during `arduino-cli compile` on classic single-band ESP32 Touch targets (`build-esp32-touch-v1`, `v2`, `v3`). Tuned static queues and deduplication tables (`kFleetRowRingSlots`, `kWardriveBloomFilterBytes`, `kCaptureQueueSlots`, `kAdvancedHitQueueSlots`, `kAdvancedMaxAps`, etc.) proportionally for single-band operation, reclaiming >25 KB of statically allocated `.dram0.bss` memory.

## [1.5.3] - 2026-09-19

### Changed

- **Single wardrive credential file.** WiGLE and WDGWars credentials now share
  one `wardrive_upload.txt` file on SD, with a safe checked-in example that can
  be copied, completed, and renamed without committing live API credentials.
- **GPS-backed system time.** Fresh GPS UTC now sets and periodically corrects
  the ESP system clock used by TLS, logs, and SD/FAT timestamps. NTP remains an
  upload fallback when GPS time is unavailable, and synchronized UTC survives
  temporary GPS signal loss instead of reverting to an uptime-only timestamp.
- **Wardrive upload reliability.** WiGLE and WDGWars TLS trust now includes
  stable root certificates, connection and streaming timeouts are explicit,
  and failures identify the TLS, header, SD-read, body, or response stage
  instead of collapsing every problem into `connection/write failed`.
  Network discovery buffers and the SD file handle are now released before the
  TLS handshake, preventing mbedTLS `SSL_ALLOC_FAILED` (`-32512`) on RAM-tight
  boards; LAN-tool workspace is recreated lazily when it is next needed.
  On PSRAM-equipped C5 boards, large mbedTLS record allocations are routed to
  PSRAM while AES/key state stays in internal RAM, preserving enough internal
  heap for the ESP hardware AES backend to complete the handshake.

## [1.5.2] - 2026-09-19

### Added

- **Direct wardrive upload page.** Under Network Tools, users can select and
  join a Wi-Fi network, choose a `wardrive-*.csv` file from SD, select WiGLE or
  WDGWars, and explicitly upload it. Credentials come from one SD config file,
  `wardrive_upload.txt` (with a checked-in safe example), stay
  out of logs, and are wiped from RAM after a bounded multipart upload over
  certificate-verified HTTPS.
- **WiGLE 1.6 export.** New on-device and phone-downloaded wardrive CSVs include
  Frequency, RCOIs, and MfgrId columns for documented WDGWars compatibility.

### Changed

- **Touch controls improved.** Primary Touch buttons are wider, light taps are
  accepted at a lower pressure threshold, and the press debounce was reduced
  from 250 ms to 120 ms for faster navigation without repeat firing.

## [1.5.1] - 2026-09-18

### Added

- **Credit:** Fleet Wardrive's explicit coordinator/node topology was informed
  by **[Piglet](https://github.com/Hamspiced/piglet)** by **Hamspiced**. Piglet's
  Core/Node ESP-NOW implementation was the reference for keeping one coordinator
  authoritative while workers discover, join, heartbeat, and reconnect. AxD's
  wire protocol, roster/channel assignment, aggregation, UI, and web integration
  are implemented for this firmware.

- **Phase 1 (done): fleet session + N-way channel split.** New ESP-NOW protocol
  (`FleetInvite`/`FleetJoin`/`FleetRoster` in `link_protocol.h`) lets up to
  `kFleetMaxNodes` C5 or classic ESP32 chips form one session: a coordinator broadcasts an invite
  with a short code, other armed nodes **auto-join**, and the coordinator deals a
  **roster** so every node learns its index and role. The old 2-way channel deal
  (`linkNextAssignedChannel`) is generalized to **N-way** (`idx % M == mySlice`),
  and one member can be designated the BLE node. Testable over serial: `F` starts
  a fleet, `J` arms a node to auto-join; each prints its assignment.
- **Phase 2 (done): row aggregation into one CSV.** Workers queue each new WiGLE
  row and stream it to the coordinator in the rendezvous window (`FleetWardriveRow`
  frames, per-member contiguous ACK + retransmit); the coordinator dedups and
  writes it to SD and/or streams it to the phone via the existing
  `kSourceWardrive` path -- one merged CSV. Fleet nodes share the rendezvous
  window and sync their clocks to the coordinator (carried in the invite). `f`
  starts a fleet wardrive; joiners start automatically via the roster's
  `wardriveOn` flag. Coordinator prints a periodic aggregate/per-member status.
  Validated on hardware: a 2-node fleet forms and the coordinator aggregates.
- **Phase 3 (done): dedicated BLE node.** The roster designates one member as
  the fleet's BLE scanner; it is excluded
  from the N-way Wi-Fi split. Instead of hopping Wi-Fi channels it parks on the
  link channel and runs a continuous BLE observer scan (co-resident with
  Wi-Fi/ESP-NOW), reusing the wardrive BLE callbacks/queue. New devices are
  deduped by address and either logged locally (if the BLE node is also the
  coordinator) or streamed to the coordinator as `FleetWardriveRow` frames
  (`isBle=1`) in the same rendezvous window as Wi-Fi rows, landing in the one
  merged CSV. So a fleet covers Wi-Fi *and* BLE without cross-node duplicates.
- **Capability-aware, collision-free channel assignment.** Exactly one fleet
  member is BLE-only. Classic v1-v3 Touch/Mini WROOM workers take precedence on
  2.4 GHz and evenly split those channels only; when classics are present, C5
  workers stay on 5 GHz and evenly split that band only. With no classic Wi-Fi
  worker, C5 workers evenly split the full dual-band plan. BLE-node selection
  preserves the only C5 in a mixed fleet so 5 GHz coverage is not lost, and
  role changes stop the previous BLE scan before the node returns to Wi-Fi.
- **Phase 4 (done): fleet UI on-device and in the web app.** The Link screen's
  idle footer is now **Back / Fleet / Solo**; Fleet opens a menu with **Start**
  (become coordinator) and **Join** (arm to auto-join), replacing the serial
  `f`/`j` triggers with buttons. A live fleet view shows the session code, each
  member's role (coord / wifi / BLE) and row count, the aggregate Wi-Fi/BLE
  totals, and LOGGING/READY state, with **Go/Stop** (coordinator) and **Leave**
  controls. The web app (`control.html`) gains a **Fleet** card — Start/Join/Leave
  buttons plus per-target status (coordinator/member, code, live node count and
  logging state) parsed from the fleet tail on bridge and screen status blobs.
  The coordinator display shows `Nodes: N`; worker heartbeats and a six-second
  timeout keep that count current. New opcodes `kAxdCmdFleetStart/Join/
  Stop` (60–62) let the phone drive the fleet on either chip. So the flow is:
  Start Fleet on one chip, switch target and Join on the others, one merged CSV.
- **Multi-bridge web app.** `control.html` now manages several bridge
  connections at once: **Add ESP** connects each board's orange chip and they all
  stay live, so you switch the **active** bridge with a tap instead of
  disconnecting and reconnecting. Each bridge shows its own live Wi-Fi/BLE counts,
  GPS, and fleet role in the connection list; commands and the Wi-Fi list follow
  the active bridge, while **wardrive rows from every connected bridge merge into
  the one CSV**. Handy for a multi-board fleet where each board's coordinator is a
  separate BLE server.
- **Fixed coordinators "fighting."** Fleet roles now follow an explicit
  coordinator/worker model: the chip where **Start** was pressed remains the
  coordinator, while **Join** pins a worker to that coordinator's MAC and
  session until Leave. Coordinators ignore competing invites, joined workers
  ignore foreign invites/rosters, and unsolicited rosters cannot auto-enroll a
  chip. A malformed roster that omits the local worker is rejected instead of
  defaulting it to coordinator slot zero. Powering up never auto-links or
  promotes a chip into a fleet.
- **A new CSV per wardrive run.** Solo, link, and fleet wardrive now open a fresh
  `/awokxdag/wardrive-NNNN.csv` (first unused index) on every Start instead of
  appending to one ever-growing `wardrive.csv`, so each session is its own file.
  The wardrive/split screens show the current file name; a serial line reports it.
- **Classic ESP32 builds fixed.** The fleet feature's two `FleetWardriveRow` row
  rings (~76 B/slot × 96 × 2 ≈ 15 KB of static DRAM) overflowed the RAM-tight
  single-band ESP32's `dram0_0_seg`. `kFleetRowRingSlots` is now 96 on the
  dual-band C5 and 24 on the classic ESP32, so all nine board targets link again.
- **Classic bridges added.** Original Dual ESP32 Touch and Mini v1/v2/v3 now
  each have a matching headless BLE-to-ESP-NOW bridge profile and packaged
  build/flash target. Classic bridges retain the revision-specific GPS/SD pin
  map and restrict relay sweeps to supported 2.4 GHz channels.
- 1.5.1 Fleet Wardrive is feature-complete (Phases 1–4).

## [1.4.4] - 2026-09-17

### Changed

- **Bridge firmware optimized (RAM + BLE).** The bridge's five largest result
  tables (Wi-Fi, BLE, Clients, Probe Intel, Karma) now allocate in PSRAM via
  `MiniResultTable` (as the Mini already does), moving ~24 KB out of the DMA
  pool: free DMA with the GATT server + Wi-Fi both up rose from ~29 KB to
  **~53 KB** (measured). The headless display's `write()` is a true no-op so
  `print/printf` no longer rasterize glyphs to a screen that isn't there (CPU
  saved on every redraw), and the bridge requests a **247-byte BLE MTU** so
  result/CSV notifications send in fewer fragments.
- **ESP-NOW relay optimized.** Commands relayed to the white screen chip now
  **burst on the rendezvous channel first** (an idle screen chip parks there, so
  starting a tool / any command from Home or a results view lands instantly with
  no channel hopping), then fall back to a **shorter channel sweep** (2 passes
  instead of 3, 2 ms dwell instead of 3) only for a tool that is actively
  hopping. Roughly halves the relay's on-air time and radio churn while keeping
  Stop-mid-tool delivery reliable (per-command sequence number still de-dups).

## [1.4.3] - 2026-09-17

### Added

- **The orange bridge chip is now a full, phone-controllable node — both chips
  from one page.** The bridge stopped being a tiny relay sketch: it now runs the
  **same firmware as the white chip, headless** (new `AWOK_DUAL_C5_BRIDGE`
  profile with a no-op `Adafruit_GFX` display) with the BLE GATT server folded in
  (`bridge_ble.ino`). Every phone command carries a **target byte**: run the tool
  **locally on the bridge** (dispatch straight into `linkDispatchCommand`) or
  **relay to the white screen chip** over the existing ESP-NOW channel sweep. The
  bridge's own tool output notifies the phone directly; the white chip's relayed
  output is forwarded on, each tagged with its source chip. The web app gains a
  **"Target chip" selector** (Screen chip / This bridge); the network list,
  per-AP actions, and live status all follow the selection. This is possible
  because the coexistence work (v1.4.2) lets the bridge run Wi-Fi + BLE together
  — measured ~29 KB DMA free with the GATT server and a Wi-Fi scan both up.
- The bridge builds from `AWOKxDAG.ino` (not the retired `AxDBridge.ino`) with
  the main firmware's linker wraps and a NimBLE role set that keeps **Peripheral**
  (GATT server) **and Observer** (BLE-scan tools). On the bridge, `releaseBleMemory`
  never deinits — NimBLE stays resident so the phone connection is never dropped.
- GPS is wired to the orange chip and confirmed working (multi-constellation
  fix); the bridge streams **GPS fix/sats/lat/lon** to the phone in its status
  heartbeat and the app shows a live GPS readout.
- **Wardrive to phone (no SD needed).** The orange chip has GPS but no SD card,
  so wardrive there streams each WiGLE row (BSSID/SSID/auth/time/chan/RSSI/lat/
  lon/alt/accuracy/type) to the phone over BLE as it's logged; the app collects
  them and a **Download CSV** button saves a `WigleWifi_1.4` file straight to the
  phone. On the white chip each Start writes a new `wardrive-NNNN.csv` to SD.
  Files/SD browsing stays screen-chip only on the bridge.
- **Wi-Fi Scan is now continuous** on both chips. The tool async-rescans on a
  loop and **merges results by BSSID**, so the list accumulates every AP seen
  (not just the latest sweep) and refreshes RSSI in place; new APs stream to the
  phone as they appear. Selecting a network or leaving the view stops it. Channel
  Map and the network-connect flow keep the blocking one-shot scan.

### Fixed

- Bridge-local commands run on the main loop instead of inside the BLE write
  callback, so a phone-triggered Wi-Fi scan on the orange chip streams the *full*
  AP list (previously the blocked callback dropped most result notifications).
- Continuous Wi-Fi scan keeps the screen responsive (it no longer holds
  `scanInProgress`, which had frozen touch/serial) and shows a **"Scanning…"**
  indicator on start instead of an empty list — which also removes the stray
  extra tap that could land on Channel Map. SD writes are throttled to ~15 s.
- Wardrive **Download CSV** uses the Web Share sheet on iOS ("Save to Files");
  the plain `<a download>` it used before was a no-op in Bluefy/iOS.

### Changed

- Status screen shows **Power: ON** instead of "Power: USB". On this board a
  battery (when fitted) feeds the 5 V rail through the 5V/GND pins — a regulated
  rail that reads ~5 V regardless of charge — so there is no cell voltage to
  sense and no meaningful battery percentage to display.

## [1.4.2] - 2026-09-16

### Fixed

- **Wardrive now captures Wi-Fi + BLE for an unlimited session (was dying with
  "Radio initialization failed" after ~10 min).** Root cause, traced by
  instrumenting every allocation: the firmware tore the BLE controller down and
  back up every Wi-Fi/BLE window, and the ESP32-C5's closed BLE-controller blob
  leaks ~0.4 KB of DMA on each such init/deinit. Fixed by adopting the
  ESP32-Marauder model — initialize BLE **once** per session and keep **both
  radios resident**, alternating only the *scans* (never the controllers). With
  no per-cycle init/deinit there is nothing to leak. The enabler: the 128-entry
  result tables (`wifiEntries`, `bleEntries`, clients, trackers, …) were sitting
  in DMA-capable RAM and consuming ~50 KB, which is what forced the teardown
  scheme in the first place; `kResultCapacity` on dual-band C5 is now 64, leaving
  Wi-Fi (~34 KB) and BLE (~33 KB) both resident with ~28 KB DMA to spare
  (measured). Affects all RadioScheduler tools (wardrive, camera scan, advanced
  watch). Scan result lists now page at 64 entries instead of 128.

## [1.4.1] - 2026-09-15

### Added

- **Network selection + per-target actions from the phone.** A Wi-Fi scan streams
  the AP list (`AxdWifiResult` frames → a Results GATT characteristic) to the web
  app, which shows a tappable list (SSID / BSSID / RSSI / channel / auth). Tapping
  a network selects it on the screen chip (`selectedWifi`), unlocking per-target
  actions from the phone: **Deauth, Grab Handshake, Track, Evil Twin, Probe
  Lure** — the same actions the on-device audit screen offers, now remote. Deauth
  honors the two-tap confirm setting via a repeated tap. Both Touch and Mini
  screen chips participate.

### Fixed

- **Remote network list was incomplete.** `linkStreamWifiResults()` broadcast the
  scanned APs on whatever channel the scan left the radio on, but the bridge only
  listens on the rendezvous channel, so the phone saw a partial (often empty)
  list. It now homes to the rendezvous channel before streaming and paces each
  frame ~30 ms (with the bridge requesting a ~15 ms BLE connection interval) so
  notifications don't overflow the ATT queue and drop APs.

## [1.4.0] - 2026-09-15

### Added

- **Phone control over BLE (two-chip bridge).** The AWOK Dual C5 has two
  ESP32-C5 chips; the orange (headless) chip now runs a dedicated bridge
  firmware (`AxDBridge/AxDBridge.ino`) that a phone connects to over BLE
  (Bluefy on iOS, Chrome on Android) and drives the white (screen) chip — Touch
  or Mini, both listen for the bridge. The
  phone writes a command opcode to a GATT characteristic; the bridge forwards it
  over ESP-NOW to the screen chip, which runs the matching tool — the same
  entry points as the on-device serial shortcuts (Wi-Fi/BLE scan, channel map,
  packet monitor, deauth watch, clients, wardrive start/stop, GPS, stop/home).
  Live Wi-Fi/BLE counts and current view stream back to the phone as BLE
  notifications. A single-page Web Bluetooth control app ships at
  `website/public/control.html` (served from GitHub Pages at `/control.html`).
- Splitting BLE (bridge) and Wi-Fi (screen) across the two chips means the phone
  link is never dropped by the screen chip's channel hopping. Validated on
  hardware: with a phone connected while ESP-NOW is up, the bridge chip holds
  ~106 KB of free DMA (BLE + ESP-NOW coexist comfortably on one C5 because the
  bridge has no display/SD/GPS).
- Because the screen chip hops across every channel while a tool runs, the
  bridge **sweeps the whole channel plan** when sending a command (repeating a
  few times, tagged with a sequence number so the screen acts once) — so Stop
  and every other command land even mid-tool, not just from idle result views.
- `scripts/build_firmware.py dual-c5-bridge` and `scripts/flash_firmware.py
  dual-c5-bridge` build/flash the bridge; the shared ESP-NOW wire format lives
  in `AWOKxDAG/link_protocol.h`, included by both firmwares so it can't drift.

## [1.3.5] - 2026-09-15

### Added

- Flicker-free Touch UI. Live views (Status, Packet Monitor, Client Sniffer,
  Probe Lure, Wardrive, Cameras, and the rest) redraw on 0.5-1 s timers, and
  each starts by clearing the whole screen; drawing straight to the ILI9341
  made that clear-then-repaint visible as a flash every refresh. The Touch now
  renders into a ~150 KB off-screen back buffer in PSRAM (`touch_display.h`,
  `AwokTouchDisplay`) and blits it to the panel once per frame, dirty-gated, so
  a refresh appears atomically. Every existing `display.*` call is unchanged
  (the wrapper is a drop-in `Adafruit_GFX`); a single `present()` at the end of
  `loop()` pushes the frame, with explicit pushes where a screen is drawn right
  before a blocking operation (boot splash, "Scanning..."). Boards without
  PSRAM (classic ESP32 Touch, experimental) fall back to direct-to-panel
  drawing, exactly as before.
- Terminal flashing: `scripts/flash_firmware.py` writes a packaged image to a
  board over the CP2102 USB port (/dev/ttyUSB*) with esptool, so no Arduino IDE is
  needed to program a board. It autodetects the serial port, accepts `--port`
  and `--build`, and works for every profile `build_firmware.py` builds.

### Changed

- Rebranded the product name to **AxD** (pronounced "axed"): device screens
  (Home / About / Status / screen test / link-mode prose), the boot banner, the
  WiGLE CSV brand fields, the RTSP user-agent, and the README/docs title. Code
  identifiers, the sketch folder/filename, board macros, the SD path
  (`/awokxdag`), and release-artifact names are unchanged.
- Status screen shows **Power: USB** instead of the developer-facing
  "Battery: n/a (set kBatteryAdc)" when no battery-sense ADC is configured.

### Fixed

- Added a 0.75 s settle gap between Wi-Fi and BLE when the wardrive/cameras/
  advanced-watch scheduler switches radios. The controller/driver deinit
  returns before the hardware and its DMA are fully released; bringing the
  other radio up immediately overlapped that teardown and could re-trigger the
  "Memory Capacity Exceeded" fault. The scheduler now waits for one radio to
  fully release before the other claims the DMA.

## [1.3.4] - 2026-09-13

### Fixed

- BLE scanning failed every time on Dual C5 Touch in wardrive (and every other
  Wi-Fi+BLE view) with `NimBLEScan: Error starting scan; rc=519` — invisible
  until now because NimBLE-Arduino's own error logging is compiled out at the
  default `CORE_DEBUG_LEVEL=0`. `rc=519` decodes to `BLE_HS_HCI_ERR(7)`,
  Bluetooth HCI error `0x07` "Memory Capacity Exceeded". Per-capability heap
  instrumentation traced it to **DMA-capable SRAM exhaustion during Wi-Fi+BLE
  coexistence**, not the general heap: the C5 has a ~70 KB DMA pool, Wi-Fi's
  driver takes ~44 KB of it (fixed overhead — shrinking the buffer counts moved
  it <2 KB), and the BLE controller takes the remaining ~26 KB at init, leaving
  24 bytes — so scan-enable, which needs its own DMA buffer, is refused. The
  two radios simply do not fit in DMA at the same time, and neither pool is
  relocatable: Wi-Fi's is fixed driver/coex memory, and the BLE controller's
  bulk is the MSYS mbuf pool, which on the C5 is allocated inside the closed
  controller blob (`CONFIG_BT_LE_MSYS_INIT_IN_CONTROLLER=1`) and cannot be
  moved to PSRAM or shrunk by config. (This is why the 1.3.2/1.3.3 bring-up
  reordering and buffer trimming, and an attempted PSRAM offload of the NimBLE
  host heap, could not fix it — they tuned the wrong pool.)
- **Fix: dual-radio views now time-multiplex the radios** instead of running
  them at once. wardrive, Cameras, and Advanced Watch alternate Wi-Fi and BLE
  windows (~8 s / ~6 s) through a shared `RadioScheduler`; only one radio is
  DMA-resident per window, each brought up into a clean heap — the same
  single-radio condition that already works reliably. GPS logs continuously
  throughout (UART, unaffected), and both Wi-Fi and BLE totals accumulate
  across windows. Classic ESP32 boards stay a permanent Wi-Fi-only session.
- Supporting cleanup: NimBLE is compiled Observer-only
  (`CONFIG_BT_NIMBLE_ROLE_CENTRAL/PERIPHERAL/BROADCASTER_DISABLED`) since the
  sketch never connects or advertises; the BLE controller's 100-entry hardware
  duplicate lists drop to 16 (the app dedups scan hits itself);
  `build.code_debug=1` (Error-level) is set in every build path so the next
  radio failure prints its real error instead of being silently swallowed.

## [1.3.3] - 2026-09-13

### Fixed

- Dual C5 Touch wardrive no longer turns BLE off when NimBLE fails to start.
  Arduino's default STA pool is ~49 KB internal (`dynamic_rx/tx` = 32), which
  left no room for BLE after the display and SD. `wifi_init_wrap.c` is linked
  with `--wrap=esp_wifi_init` so dynamic RX/TX drop to 8, CSI/AMPDU are off,
  and static RX is 3. `--wrap=esp_bt_controller_init` drops the C5 controller
  reservation so NimBLE still fits beside STA. Arduino's SPIRAM cache TX
  count of 4 is left as-is. Both radios come up after the display and before
  SD. A failed BLE bring-up retries, then shows a radio error instead of a
  Wi-Fi-only session.

## [1.3.2] - 2026-09-12

### Added

- **Settings** under Status: compact rows that fit the button (Sleep Off / 15s /
  30s / 1m / 2m / 5m, Bright 20–100%, GPS baud, boot splash, two-tap confirm
  for active tests, NMEA echo, screen test). Values live in NVS on a blob
  separate from saved networks. Baud changes from the GPS screen or serial `u`
  persist across reboots. Serial `t` opens Settings. Footer **Defaults** restores
  factory prefs without touching saved networks. A dimmed screen wakes on the
  first tap or Mini button without firing that control.
- **Screen test** from Settings: solid colors, SMPTE-style bars, checkerboard,
  corner orientation marks, backlight PWM sweep, and a touch (corners + center,
  raw vs mapped) or Mini button probe. Mini writes the ST7735 directly, then
  blits the same checker through the firmware canvas so panel faults are not
  confused with MiniLayout bugs.

### Fixed

- Arduino IDE Verify failed with a multiple definition of
  `ieee80211_raw_frame_sanity_check` against ESP32 core 3.3.x. CI already
  passed `-Wl,-z,muldefs`; the IDE does not. `scripts/setup_arduino_ide.py`
  writes that flag into the core's `platform.local.txt`.
- Wardrive on Dual C5 Touch started BLE first, then `WiFi.mode(STA)` tried to
  deinit a driver NimBLE already claimed (`wifi_init` 0x3001) and aborted the
  session. Wi-Fi is brought up first; BLE failure now falls back to Wi-Fi-only
  logging instead of a radio-error screen. `shutdownWifi()` no longer double
  deinits after `WiFi.mode(WIFI_OFF)`.
- Touch Wi-Fi init after the SD mount left only a ~34 KB heap block, so
  `esp_wifi_init` failed and IDF logged 0x3001 on cleanup. STA now starts
  right after the display, before SD. Default full brightness uses GPIO
  instead of LEDC so PWM does not split that block.

### Validation

- Dual C5 Touch and Dual C5 Mini compiled and packaged.

## [1.3.1] - 2026-09-12

### Added

- Board and Link Mode notes that the README already pointed at:
  [dual-esp32-touch.md](docs/dual-esp32-touch.md),
  [dual-esp32-mini.md](docs/dual-esp32-mini.md), and
  [link-mode.md](docs/link-mode.md). The website now emits pages for them.
- Host-side `NetworkParse` tests under `tests/host/` (IPv4, subnet range, HTTP
  headers, XML/UPnP blocks, URLs, Content-Length and chunked bodies, Flipper
  UUID hints, malformed input) run with AddressSanitizer and
  UndefinedBehaviorSanitizer. `bash tests/host/run.sh` or the **Host tests**
  workflow.

### Fixed

- Deauth Watch read addr2/addr3 without a 24-byte frame-length check, the same
  class of bug Packet Monitor already guarded against.
- Serial **h** left the Locator running and kept Link Mode HELLO broadcasts
  going after the UI returned Home.
- Evil Portal CSV concatenated raw form fields, so a comma or quote in a
  submitted value broke `portal_creds.csv`. Fields are now a single escaped
  column; Serial logs the client and field count, not the values.
- README said the Arduino IDE sketch default was Mini; the code selects
  Dual C5 Touch. Original Mini button prose also swapped Left (GPIO13,
  `INPUT_PULLUP`) with Center (GPIO34, input-only).
- `scripts/build_firmware.py` omitted `-Wl,-z,muldefs`, so a local package
  could lose the raw-frame sanity-check override that CI always links.
- `scripts/gen_mini_boot.py` opened the source PNG relative to the working
  directory; it now resolves paths from the script location.

### Changed

- `LinkPacket` is `static_assert`ed at 36 bytes so a C5 / classic ESP32 ABI
  mismatch cannot silently drop every pairing frame.
- Channel-count constants use `sizeof array / sizeof element`.
- Packaging metadata marks original ESP32 profiles experimental and records
  C5 Touch/Mini as the hardware-tested maps.
- `.gitignore` covers `.DS_Store` and website build output.

### Validation

- Dual C5 Touch and Dual C5 Mini compiled and packaged. Host parser tests
  passed under AddressSanitizer and UndefinedBehaviorSanitizer. Website tests
  passed.

## [1.3.0] - 2026-09-11

### Added

- **Network Tools** under Recon: RAM-only Wi-Fi connection setup with a masked
  Touch/Mini character picker, subnet-aware ARP host discovery, common TCP port
  checks, RTSP/ONVIF camera-service discovery, printer-port candidates, SIP OPTIONS
  discovery, and a read-only UPnP gateway mapping viewer.
- Bounded, cancellable LAN scans with paginated host/service details, automatic
  CSV exports on completion/cancellation, manual snapshots, separate host/service
  summaries, and explicit scope/result limits and inconclusive timeout counts.
  Serial **h** also saves partial scan results before disconnecting and releasing
  the Network Tools resources.
- Flipper-like advertised-service hints in BLE results, details, and CSV exports.
  These hints do not authenticate device identity.
- README and third-party acknowledgement of **7h30th3r0n3 / Evil-M5Project**,
  whose scanning features informed these additions.
- Host-side subnet/URL/HTTP/XML/BLE parser tests, including malformed-input checks
  under AddressSanitizer and UndefinedBehaviorSanitizer, plus socket-state tests
  for scan progression, timeouts, oversized responses, and cleanup on exit.

- Original **Dual ESP32 Mini v1/v2/v3** screen-side profiles: shared native Mini
  UI, ST7735 and five-button wiring, SD/GPS pins and input-only GPIO handling.
  Original Mini GPS defaults to 9600 baud; Touch defaults to 115200.
  C5 and original Mini display selection now share `AWOK_MINI_DISPLAY` while
  retaining their different radio and memory policies.

- Experimental original **Dual ESP32 Touch v1/v2/v3** white-port build profiles,
  with revision-specific SD pins, ILI9341/XPT2046 and GPS wiring, and guards
  against selecting the wrong chip or conflicting profiles.
- Classic ESP32 uses 2.4 GHz channels, 32-entry result tables and 128 Wardrive
  deduplication addresses. BLE-only tools remain available; combined views use
  Wi-Fi only. C5 profiles retain their existing capacities and radio policies.
- **Link Mode now works across board families.** A C5 (dual-band) and a classic
  2.4 GHz unit pair and run Split Wardrive together: HELLO advertises each unit's
  band capability, and the pair splits by band — the C5 takes all of 5 GHz and
  the 2.4-only unit takes all of 2.4 GHz, for full coverage with no overlap. Two
  C5s still alternate-deal the full dual-band plan; two classics split 2.4 GHz.
- **Credits & license** section in the README crediting ESP32 Marauder
  (justcallmekoko), FZEasyMarauderFlash (SkeletonMan03), and the upstream
  libraries, and noting that AWOKxDAG's own code is MIT while referenced projects
  keep their own licenses (Marauder is GPL-3.0).

### Fixed

- Link Mode could not pair a C5 with a 2.4 GHz board: the ESP-NOW protocol
  version was band-dependent (C5 sent 1, classic sent 2) and the receive path
  drops version mismatches, so every cross-board frame was rejected. The version
  is now uniform across boards, and the Split Wardrive channel plan is shared and
  band-aware instead of each unit dealing from its own (differently sized) list.
  Reflash both units — an older 1.2.0 C5 (version 1) will not pair with a 1.3.0
  unit.

### Validation

- Builds passed for C5 Mini, C5 Touch, original ESP32 Touch v1, and original
  ESP32 Mini v3. Parser, socket-state, and existing result-memory tests passed.
  The new features have not yet been flashed or validated on hardware.

## [1.2.0] - 2026-09-10

### Added

- **Link Mode** — pairs two AWOKxDAG units (any mix of Touch and Mini) over an
  ESP-NOW back-channel, using a display-and-confirm 4-digit code derived from the
  two MAC addresses so neither board has to type anything. The lower MAC becomes
  master and owns the shared session id and clock. Discovery is plaintext
  broadcast (ESP-NOW cannot encrypt broadcast); once paired, telemetry moves to an
  encrypted unicast peer (PMK + per-pair LMK baked into the firmware).
- **Split Wardrive** over a link: the paired units alternate-deal the dual-band
  channel list (master even indices, slave odd), so each scans half the spectrum
  and halves its per-channel revisit interval. A one-second, time-synced
  rendezvous on channel 1 exchanges telemetry; each screen shows its own, the
  partner's, and the combined AP count, the partner's link RSSI, and a
  partner-lost alert. Each board logs its own WiGLE `wardrive.csv` and geotags
  from its own GPS. Starting it unpaired wardrives every channel solo. Reached
  from the GPS screen (`Home | Baud | Drive | Link`) or serial `n`. Wi-Fi-only in
  this release.
- **Mini boot logo** — the Dual C5 Mini now shows the AWOK logo at startup
  instead of a text placeholder. The 240×320 Touch splash is downscaled to a
  96×128 1-bit image (`scripts/gen_mini_boot.py`) and blitted straight to the
  ST7735 through a new `splash()` path, since the Mini's retained-mode renderer
  cannot draw arbitrary bitmaps through its normal layout document.
- **Firmware website** — responsive documentation generated from `README.md`,
  `CHANGELOG.md`, and Markdown files in `docs/`, with section navigation, a latest
  release summary, and links to [ESPTerminator](https://espterminator.com/) for
  web flashing. Includes dag's logo in the header and footer, browser
  favicons, and a mobile home-screen icon.
- **Website publishing workflow** — prepared GitHub Pages automation to rebuild
  and publish when documentation changes are pushed to `main`.

### Changed

- The WiGLE CSV `release=` metadata now derives from `kVersion` instead of a
  hardcoded string, so wardrive logs track the firmware version automatically.
- The 240×320 Touch boot bitmap is compiled only into Touch builds now (~9.6 KB
  of flash reclaimed on the Mini, which carries its own 96×128 splash instead).
- Bumped firmware, WiGLE metadata, documentation, and release-workflow defaults
  to 1.2.0 for both Mini and Touch builds.

### Validation

- Link Mode confirmed on hardware: two units pair via the display-and-confirm
  4-digit code and run the split-channel wardrive with encrypted telemetry,
  logging to WiGLE once each unit has a GPS fix (the AP count is fix-gated, as
  with solo Wardrive). A confirm-handshake bug found on-device — the first unit
  to pair went silent and starved the second, hanging it on the confirm screen —
  was fixed by having a paired unit answer the peer's lingering HELLOs so both
  latch.
- The Mini boot logo and split wardrive were exercised on-device; the Touch
  profile shares the same code paths but was not re-flashed this round.

## [1.1.4] - 2026-09-09

### Fixed

- Mini dual-radio views can now attempt Wi-Fi + BLE with the stock core by
  placing five application result tables (47 KiB) in PSRAM, retaining their
  128-entry capacities. Driver queues and DMA buffers stay internal; Touch
  retains its static tables.
- Replaced the Mini's permanent BLE disable with per-session heap admission,
  BLE-first startup, and Wi-Fi-only fallback on insufficient memory or reported
  radio/scan startup failure. Logs report actual table placement and heap use.
  Allocation failure is checked before tools can run. Hardware validation of
  coexistence and repeated tool switching is still pending.

### Changed

- Bumped firmware, WiGLE metadata, documentation, and release-workflow defaults
  to 1.1.4 for both Mini and Touch builds.

### Validation

- Both board profiles compile. Sanitized host checks cover result-table object
  lifetime, allocation fallback, memory admission, and radio-startup recovery.
  On-device testing of Mini coexistence and repeated tool switching is pending.

## [1.1.3] - 2026-09-09

### Added

- Dual-board release workflow: the Actions build now produces both Dual C5
  Touch (`awokxdag-touch-*`) and Dual C5 Mini (`awokxdag-mini-*`) firmware
  images from the same source in one release, with combined SHA-256 checksums.
- Mini dual-radio views degrade to Wi-Fi-only instead of failing: Wardrive
  logs Wi-Fi APs to WiGLE with GPS, Cameras flags camera-like Wi-Fi devices,
  and Advanced Watch monitors deauth/EAPOL/CSA/RF. Each notes on screen that
  BLE is off because the Mini has room for only one radio at a time.

### Changed

- Reworked the radio lifecycle to a reliable one-radio-at-a-time model on the
  Mini, where Wi-Fi (~49 KB) and BLE (~33 KB) cannot coexist in ~75 KB of free
  internal RAM: each radio is fully torn down before the other starts. Wi-Fi
  shutdown now fully deinitializes the driver (`esp_wifi_stop` +
  `esp_wifi_deinit`) and BLE frees its controller between uses. Wi-Fi-only and
  BLE-only tools switch cleanly; the Touch board still runs both radios.
- Bumped firmware, WiGLE metadata, and release-workflow defaults to 1.1.3.

### Fixed

- Fixed a Guru Meditation (load access fault) when tearing down a BLE scan. The
  release path called `NimBLEDevice::deinit(false)` then `deinit(true)`, and
  deleting the scan object ran its scan-response-timer callout deinit after the
  NimBLE port had already been freed. BLE now stops the scan, drains pending
  callbacks, and calls a single `NimBLEDevice::deinit()`.
- Fixed Wi-Fi failing to reinitialize (`ESP_ERR_WIFI_NOT_INIT`, 0x3001) after a
  radio switch; `WiFi.mode(WIFI_OFF)` alone left the driver half-initialized.
- Fixed "ble ll env init error code:-13" (out of memory) when opening a
  dual-radio view on the Mini after Wi-Fi was already up.

## [1.1.2] - 2026-09-09

### Added

- Experimental Dual C5 Mini build profile with a native 128 × 128 ST7735
  interface: highlighted joystick selection, wrapped details, compact charts,
  and quick access to screen actions. Physical validation is pending.
- Board-specific firmware packaging with SHA-256 checksums, plus a sanitized
  host regression test for Mini layout bounds, wrapping, selection, live
  redraws, and content capacity.
- BLE result paging with Prev/Next controls and detail inspection for every
  retained advertiser.
- Six host regression groups covering saved-network persistence, BLE scan
  transitions and paging, touchscreen contacts, GPS freshness, and packet
  monitor buffer handling, with address and undefined-behavior sanitizers.

### Changed

- Packaging and the Touch release workflow now select their board profile
  explicitly, independent of the sketch's Arduino IDE Mini selection.
- Increased BLE results from 24 to 128 and Wi-Fi results from 72 to 128.
- Increased Clients, Security Audit, BLE Trackers, Cameras, WPS, Hidden SSID,
  Harvester, Probe Intel, and Karma Watch tables from 24 to 128 entries, and
  Harvester beacon deduplication from 64 to 128 BSSIDs.
- Saved networks now use a checked, versioned NVS snapshot. Existing per-field
  records remain readable and migrate on the next successful save.
- Bumped firmware, WiGLE metadata, documentation, and release-workflow defaults
  to version 1.1.2.

### Fixed

- Reduced Mini display RAM from a 32 KB RGB565 canvas to an 8 KB indexed-color
  canvas, compacted layout records, and deferred BLE initialization until a BLE
  tool starts. BLE shutdown releases its allocations for subsequent Wi-Fi use.
- Wi-Fi initialization and scan errors now cancel the scan, preserve previous
  results, and report available internal RAM, largest free block, and PSRAM.
- An absent saved-network namespace now reports an empty first-run list;
  actual NVS failures include the Espressif error name and code.
- Packet Monitor no longer reads frame payloads from metadata-only Wi-Fi
  notifications, preventing an out-of-bounds read.
- Held touchscreen contacts no longer trigger repeated actions or bypass the
  file manager's two-tap delete confirmation.
- Failed saved-network writes no longer report success or erase the previous
  list before writing its replacement; failed edits restore the in-memory list.
- Continuous BLE scans now receive repeat advertisements without inheriting a
  previous scan's result limit. Regular scans reset the previous monitor's
  callback and restore bounded result retention.
- GPS timestamps now fall back to uptime when date or time updates are stale;
  the timestamp buffer also accommodates the full formatted value.

## [1.1.1] - 2026-08-24

### Added

- Rotating `firmware_audit.csv` operational trail with per-boot session IDs,
  firmware version, event outcomes, optional GPS context, and one retained
  256 KB previous segment.
- Audit events for active-test sessions, security-audit runs, saved-network
  changes, file deletion, portal-log clearing, and SD recovery.
- Paged Wi-Fi results with Home, Prev, Next, Deauth, and Scan controls for up to
  72 stored access points.
- Advanced Watch, a shared passive Wi-Fi/BLE monitoring pipeline for:
  - Beacon, RSN, PMF, WPS, channel, and beacon-interval integrity changes.
  - Saved-network security downgrades.
  - Grouped deauthentication/disassociation storms and reason codes.
  - Channel-switch announcement abuse.
  - EAPOL and authentication/association storms.
  - RF noise-floor anomalies against a learned baseline.
  - Rapid BLE random-address churn with a stable payload structure.
- GPS-tagged `advanced_watch.csv` alerts and matching firmware-audit events.

### Changed

- Bumped firmware, WiGLE metadata, documentation, and release-workflow defaults
  to version 1.1.1.
- Handshake captures now use readable `<ESSID>_<BSSID>.pcap` filenames, falling
  back to `<BSSID>.pcap` for hidden networks, instead of overwriting one
  `latest_handshake.pcap` across every target.
- Advanced Watch tables now evict stale entries in crowded environments instead
  of becoming permanently saturated.

### Privacy

- The firmware audit trail intentionally excludes captured credentials and
  packet payloads.

## [1.1.0] - 2026-08-22

### Added

- Passive Security Audit with encryption, PMF, WPS, and risk reporting.
- BLE tracker monitoring for AirTag/Find My, Tile, and Samsung SmartTag devices.
- Passive handshake and PMKID Harvester.
- Probe Intel directed-probe aggregation.
- Karma Watch, Beacon Watch, and authentication/association flood detection.
- CSV/pcap exports and menu integration for the new recon and monitor tools.

## [1.0.0] - 2026-08-19

### Added

- Initial ESP32-C5 firmware release.
- Wi-Fi and BLE reconnaissance, client and packet monitoring, WPS and hidden
  SSID tools, camera detection, GPS wardriving, and saved networks.
- Deauthentication testing, handshake/PMKID capture, beacon flooding, captive
  portal/evil-twin testing, and Probe Lure.
- Deauth, rogue-AP, and BLE-spam defensive monitors.
- Touchscreen UI, SD capture manager, status screens, serial controls, build
  workflow, and recovery documentation.

[Unreleased]: https://github.com/dagnazty/awokxdag/compare/v1.7.5...HEAD
[1.7.5]: https://github.com/dagnazty/awokxdag/compare/v1.7.4...v1.7.5
[1.7.4]: https://github.com/dagnazty/awokxdag/compare/v1.7.3...v1.7.4
[1.7.3]: https://github.com/dagnazty/awokxdag/compare/v1.7.2...v1.7.3
[1.7.2]: https://github.com/dagnazty/awokxdag/compare/v1.7.1...v1.7.2
[1.7.1]: https://github.com/dagnazty/awokxdag/compare/v1.7.0...v1.7.1
[1.7.0]: https://github.com/dagnazty/awokxdag/compare/v1.6.5...v1.7.0
[1.6.5]: https://github.com/dagnazty/awokxdag/compare/v1.6.4...v1.6.5
[1.6.4]: https://github.com/dagnazty/awokxdag/compare/v1.6.3...v1.6.4
[1.6.3]: https://github.com/dagnazty/awokxdag/compare/v1.6.1...v1.6.3
[1.6.1]: https://github.com/dagnazty/awokxdag/compare/v1.6.0...v1.6.1
[1.6.0]: https://github.com/dagnazty/awokxdag/compare/v1.5.5...v1.6.0
[1.5.5]: https://github.com/dagnazty/awokxdag/compare/v1.5.4...v1.5.5
[1.5.4]: https://github.com/dagnazty/awokxdag/compare/v1.5.3...v1.5.4
[1.5.3]: https://github.com/dagnazty/awokxdag/compare/v1.5.2...v1.5.3
[1.5.2]: https://github.com/dagnazty/awokxdag/compare/v1.5.1...v1.5.2
[1.5.1]: https://github.com/dagnazty/awokxdag/compare/v1.4.4...v1.5.1
[1.4.4]: https://github.com/dagnazty/awokxdag/compare/v1.4.3...v1.4.4
[1.4.3]: https://github.com/dagnazty/awokxdag/compare/v1.4.2...v1.4.3
[1.4.2]: https://github.com/dagnazty/awokxdag/compare/v1.4.1...v1.4.2
[1.4.1]: https://github.com/dagnazty/awokxdag/compare/v1.4.0...v1.4.1
[1.4.0]: https://github.com/dagnazty/awokxdag/compare/v1.3.5...v1.4.0
[1.3.5]: https://github.com/dagnazty/awokxdag/compare/v1.3.4...v1.3.5
[1.3.4]: https://github.com/dagnazty/awokxdag/compare/v1.3.3...v1.3.4
[1.3.3]: https://github.com/dagnazty/awokxdag/compare/v1.3.2...v1.3.3
[1.3.2]: https://github.com/dagnazty/awokxdag/compare/v1.3.1...v1.3.2
[1.3.1]: https://github.com/dagnazty/awokxdag/compare/v1.3.0...v1.3.1
[1.3.0]: https://github.com/dagnazty/awokxdag/compare/v1.2.0...v1.3.0
[1.2.0]: https://github.com/dagnazty/awokxdag/compare/v1.1.4...v1.2.0
[1.1.4]: https://github.com/dagnazty/awokxdag/compare/v1.1.3...v1.1.4
[1.1.3]: https://github.com/dagnazty/awokxdag/compare/v1.1.2...v1.1.3
[1.1.2]: https://github.com/dagnazty/awokxdag/compare/v1.1.1...v1.1.2
[1.1.1]: https://github.com/dagnazty/awokxdag/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/dagnazty/awokxdag/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/dagnazty/awokxdag/releases/tag/v1.0.0
