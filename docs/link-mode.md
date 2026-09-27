# Link Mode

Two AxD units pair over ESP-NOW and can run **Split Wardrive**: they
divide the channel list so each board scans a different part of the spectrum.

Reached from **GPS → Drive modes → Split**, or serial `n`. Wi-Fi-only in this release; BLE is
kept off so ESP-NOW is not torn down on Mini boards.

## Pairing

1. Open Split on both units and select **Pair boards**.
2. Each board broadcasts a HELLO on channel 1. Discovery is plaintext; ESP-NOW
   cannot encrypt broadcast.
3. Both screens show the same 4-digit confirm code, derived from the unordered
   pair of MAC addresses. Nothing is typed.
4. Tap **Confirm** on each unit. The lower MAC becomes master and owns the
   session id and clock.
5. After pairing, SYNC/TELEM move to an encrypted unicast peer (PMK + per-pair
   LMK baked into the firmware). Anyone with the binary can derive those keys;
   encryption only stops casual sniffing.

Cancel pairing from the Link screen, or serial **h** from Home, to stop HELLO
broadcasts. Unpair from the paired idle screen. Serial **h** does not unpair a
finished session.

## Protocol

Control frames use the `LinkPacket` POD in `link_protocol.h` (36 bytes on every
supported ABI); file and dashboard messages use their own bounded structs. Magic `AWKL`, **protocol version 2** on all boards. Version 1
C5 units from 1.2.0 will not pair with 1.3.0+; reflash both sides.

HELLO flags advertise dual-band capability (`kLinkFlagDualBand`). Band
capability is *not* encoded in the version number; 1.3.0 made the version
uniform so a C5 and a classic 2.4 GHz board can pair.

## Split Wardrive

- Two matching units **alternate-deal** the shared plan (even/odd indices).
  Two C5s split the full dual-band list; two classic boards split 2.4 GHz.
- A mixed C5 + 2.4 GHz pair **splits by band**: the C5 takes all of 5 GHz, the
  classic board takes all of 2.4 GHz, with no overlap.
- A one-second rendezvous on channel 1 exchanges telemetry. Each screen shows
  its own session counts and recording/GPS health, with partner AP count,
  link RSSI, and a partner-lost alert on Touch. Mini keeps a compact summary.
- Each board writes a new WiGLE `wardrive-NNNN.csv` per Start and geotags from
  its own GPS.
- Started unpaired, Link Mode wardrives every assigned channel solo.

## Fleet Wardrive

Open **GPS → Drive modes → Fleet**, then choose **Start as coordinator** or
**Join as worker**. Reopening a running mode returns to its dashboard. The
mode picker requires stopping/leaving another active session (or cancelling
pairing) before changing modes; it never silently replaces one. A stopped
Split pair must be unpaired before starting a Fleet.

- Exactly one member is dedicated to BLE scanning.
- Classic v1-v3 Touch/Mini WROOM workers own and evenly split 2.4 GHz.
- When classic workers are present, C5 Wi-Fi workers own and evenly split 5 GHz.
- In an all-C5 fleet, the Wi-Fi workers evenly split the full dual-band plan.
- Band pools use independent modulo slices, so no two Wi-Fi workers receive the
  same channel.
- Fleet workers can scan Wi-Fi or BLE without local GPS. A worker includes its
  own fresh coordinates when it has them; otherwise the coordinator stamps its
  row with the coordinator's fresh GPS fix. If the coordinator has no fix,
  GPS-less worker rows stay unacknowledged and are retried from the worker's
  bounded queue; the coordinator also pauses its own row logging. No row is
  written with blank or false `0,0`
  coordinates. Keep the nodes close together when sharing the coordinator's
  position. Solo and Split still require each scanning board's own GPS fix.
  Update every fleet node to this firmware before relying on GPS-less scanning.

See `AWOKxDAG/link.ino` for the state machine.

## Dashboard and verified SD transfers

`AxdWardriveStatusMsg` carries versioned `$WDSTAT,1,...` telemetry (source,
session, active state, elapsed seconds, metres, GPS fix coverage, Wi-Fi/BLE
counts, recent rate, satellites/HDOP, storage state, rows/bytes, flush age,
local time/zone, filename, mode, current fix/speed, node count). The bridge
forwards it as BLE result source 10. Existing status/rendezvous opportunities
send it without changing scanner channels. Storage state reports SD API results;
it is not a guarantee against power loss.

Verified Bluetooth file requests use opcode 70: op/index/target followed by
little-endian uint32 token, start sequence, snapshot byte length, and CRC32
(19 bytes). Sequence zero requests a new snapshot. Resume uses the first
missing sequence and original size/CRC; sequence total+1 requests completion
only. The relayed command stores these fields in sessionId, masterMillis,
networks, and bleCount respectively. Both chips and the web client need support.

File chunks retain the existing token/sequence/browser-ACK protocol. Kind 3 is
the manifest (sequence 0xffffffff), kind 5 is hash progress (0xfffffffe), and
kind 4 is verified completion (total+1). Manifest/completion data is eight
hexadecimal CRC32 digits followed by a comma and filename. Kind 0 remains data
and kind 2 remains an error. CRC32 checks accidental corruption, not authenticity.
The browser retains partial chunks only while its tab remains open.
