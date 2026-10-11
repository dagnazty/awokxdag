#pragma once
// Shared ESP-NOW wire format for AxD, included by BOTH the main Touch/Mini
// firmware (via awok_common.h) and the headless orange bridge (AxDBridge.ino),
// so the on-air format can never drift between the two chips. POD + constants
// only -- no Arduino/display/NimBLE dependencies.
#include <stdint.h>

// ---- Link Mode / bridge ESP-NOW protocol -------------------------------
constexpr uint32_t kLinkMagic = 0x41574B4C;   // "AWKL"
constexpr uint8_t kLinkProtoVersion = 2;      // one value across all boards
constexpr uint8_t kLinkChannel = 1;           // rendezvous + pairing channel
constexpr int kLinkPacketQueueSlots = 16;
constexpr uint8_t kLinkBroadcastAddr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ESP-NOW encryption keys for the post-pairing unicast link. Broadcast HELLO
// stays in the clear (ESP-NOW cannot encrypt broadcast); paired unicast uses an
// LMK that also mixes in the confirm code. These ship in the binary, so they
// stop casual sniffing but are not secret from anyone holding the build.
constexpr uint8_t kLinkPmk[16] = {0x9E, 0x1C, 0x74, 0xB3, 0x2A, 0xF5, 0x60, 0xD8,
                                  0x4B, 0x07, 0xC9, 0x3E, 0xA1, 0x52, 0x8F, 0x66};
constexpr uint8_t kLinkLmkBase[16] = {0x51, 0xE4, 0x0B, 0x9A, 0x7D, 0x38, 0xC2,
                                      0x6F, 0x14, 0xBE, 0x05, 0xA7, 0x3C, 0xD0,
                                      0x89, 0x22};

enum LinkMsgType : uint8_t {
  kLinkMsgHello = 1,       // identity + confirm code + confirmed flag
  kLinkMsgSync = 2,        // master millis for clock sync
  kLinkMsgTelem = 3,       // running counts + channel + session id (status back)
  kLinkMsgCommand = 4,     // bridge -> screen: run a tool (opcode in `reserved`)
  kLinkMsgWifiResult = 5,  // screen -> bridge: one scanned AP (AxdWifiResult)
  // Fleet Wardrive (N linked nodes splitting the channel plan into one CSV):
  kLinkMsgFleetInvite = 6,    // coordinator -> all: join my session (LinkPacket)
  kLinkMsgFleetJoin = 7,      // member -> coordinator: joining (caps in flags)
  kLinkMsgFleetRoster = 8,    // coordinator -> all: the roster (FleetRoster)
  kLinkMsgFleetWardriveRow = 9,  // member -> coordinator: one WiGLE row
  kLinkMsgFleetAck = 10,      // coordinator -> member: rows up to seq received
  kLinkMsgFleetHuntObservation = 11,  // member -> coordinator: target hunt observation
  kLinkMsgFleetHuntResult = 12,       // coordinator/screen -> bridge: hunt solution
  kLinkMsgFleetTopology = 13,         // swarm node -> coordinator: client/AP/probe link
  kLinkMsgFileEntry = 14,             // screen -> bridge: one file listing entry
  kLinkMsgFileData = 15,              // screen -> bridge: one chunk of file payload
  kLinkMsgFileDone = 16,              // screen -> bridge: file operation complete / status
  kLinkMsgFileChunk = 18,             // reliable file data/completion (browser ACK)
  kLinkMsgFileChunkAck = 19,          // browser -> bridge -> screen
  kLinkMsgWardriveStatus = 20,        // screen -> bridge: versioned session dashboard
  kLinkMsgFileReady = 17,             // request/ACK: bridge is parked for file replies
};

// File commands from a supporting bridge wait for its receiver-ready ACK.
// sessionId carries a fresh nonce; existing unflagged commands keep their behavior.
constexpr uint8_t kLinkCommandWaitFileReady = 0x80;
constexpr uint8_t kLinkFileReadyRequest = 0;
constexpr uint8_t kLinkFileReadyAck = 1;
constexpr uint32_t kLinkFileReadyTimeoutMs = 2000;
constexpr uint32_t kLinkFileReadyRetryMs = 30;

// One ESP-NOW frame. POD, 36 bytes on every supported ABI, copied verbatim.
// Command frames reuse `reserved` (low byte = AxdCommand opcode, high byte =
// optional arg); the pairing/telemetry fields keep their meaning.
struct LinkPacket {
  uint32_t magic = kLinkMagic;
  uint8_t version = kLinkProtoVersion;
  uint8_t type = 0;      // LinkMsgType
  uint8_t flags = 0;     // bit0 = confirmed
  uint8_t role = 0;      // sender's computed role: 0 master, 1 slave
  uint16_t code = 0;     // 4-digit confirm code
  uint16_t reserved = 0; // COMMAND: low byte opcode, high byte arg
  uint32_t sessionId = 0;
  uint32_t masterMillis = 0;  // SYNC: master clock
  uint32_t networks = 0;      // TELEM: sender Wi-Fi count
  uint32_t bleCount = 0;      // TELEM: sender BLE count
  uint8_t channel = 0;        // TELEM: sender's current channel / tool id
  uint8_t srcMac[6] = {0};    // sender MAC
};
// 35 payload bytes plus 1 tail pad on both Xtensa ESP32 and RISC-V C5. Do not
// pack this: changing the on-air size breaks pairing with existing units.
static_assert(sizeof(LinkPacket) == 36,
              "LinkPacket must stay 36 bytes on every board ABI");

constexpr uint8_t kLinkFlagConfirmed = 0x01;
constexpr uint8_t kLinkFlagDualBand = 0x02;  // sender's radio covers 5 GHz too

// kLinkMsgTelem status flags. Message types give `flags` separate semantics,
// so these may overlap the HELLO-only flags above without changing the wire ABI.
constexpr uint8_t kLinkTelemStatus = 0x80;       // full remote status payload
constexpr uint8_t kLinkTelemFleetActive = 0x01;
constexpr uint8_t kLinkTelemFleetCoordinator = 0x02;
constexpr uint8_t kLinkTelemFleetListening = 0x04;
constexpr uint8_t kLinkTelemFleetRunning = 0x08;
constexpr uint8_t kLinkTelemGpsFix = 0x10;

// One scanned Wi-Fi AP, streamed screen -> bridge -> phone so the phone can show
// a list and pick a target. Shares the magic/version/type prefix with LinkPacket
// so the bridge can tell frames apart by type; it is a different size (~50 B),
// which is fine over ESP-NOW (250 B max). The phone selects by `index`.
struct AxdWifiResult {
  uint32_t magic = kLinkMagic;
  uint8_t version = kLinkProtoVersion;
  uint8_t type = kLinkMsgWifiResult;
  uint8_t index = 0;    // position in the screen chip's wifiEntries[]
  uint8_t count = 0;    // total APs in the list
  uint8_t bssid[6] = {0};
  int8_t rssi = -127;
  uint8_t channel = 0;
  uint8_t auth = 0;     // wifi_auth_mode_t
  char ssid[33] = {0};  // null-terminated (empty = hidden)
};

// ---- Fleet Wardrive -----------------------------------------------------
// Up to this many linked chips split the channel plan and merge into one CSV.
constexpr int kFleetMaxNodes = 6;

// Member capability bits (FleetJoin flags / FleetRoster caps byte).
constexpr uint8_t kFleetCapDualBand = 0x01;  // covers 5 GHz too
constexpr uint8_t kFleetCapGps = 0x02;
constexpr uint8_t kFleetCapSd = 0x04;
constexpr uint8_t kFleetCapBle = 0x08;

// Coordinator's roster broadcast: every node finds its own MAC to learn its
// index, and derives its role (BLE node vs Wi-Fi slice = its rank among the
// non-BLE members). One frame carries the whole fleet (~54 B for 6 nodes).
struct FleetRoster {
  uint32_t magic = kLinkMagic;
  uint8_t version = kLinkProtoVersion;
  uint8_t type = kLinkMsgFleetRoster;
  uint32_t sessionId = 0;
  uint16_t code = 0;         // short join code
  uint8_t memberCount = 0;
  uint8_t bleNodeIndex = 0xFF;  // member that scans BLE (0xFF = none)
  uint8_t sinkNodeIndex = 0xFF; // SD-sink member (0xFF = none)
  uint8_t coordIndex = 0;       // aggregator member index
  uint8_t wardriveOn = 0;       // 1 = members should be running fleet wardrive
  struct Member {
    uint8_t mac[6] = {0};
    uint8_t caps = 0;
  } members[kFleetMaxNodes];
};

// One WiGLE row shipped worker -> coordinator during a rendezvous window.
struct FleetWardriveRow {
  uint32_t magic = kLinkMagic;
  uint8_t version = kLinkProtoVersion;
  uint8_t type = kLinkMsgFleetWardriveRow;
  uint32_t sessionId = 0;
  uint32_t seq = 0;      // per-sender sequence (coordinator ACKs a high-water)
  uint8_t src[6] = {0};  // sender MAC (so the coordinator can ACK per member)
  uint8_t id[6] = {0};   // BSSID (Wi-Fi) or address (BLE)
  int8_t rssi = -127;
  uint8_t channel = 0;
  uint8_t auth = 0;      // wifi_auth_mode_t
  uint8_t isBle = 0;
  float lat = 0.0f;  // NaN when sender has no fresh fix
  float lon = 0.0f;  // NaN when sender has no fresh fix
  int16_t alt = 0;
  char name[33] = {0};   // SSID or BLE name (null-terminated)
};

// Coordinator -> member: rows up to `seq` were stored, so retransmit only newer.
struct FleetAck {
  uint32_t magic = kLinkMagic;
  uint8_t version = kLinkProtoVersion;
  uint8_t type = kLinkMsgFleetAck;
  uint32_t sessionId = 0;
  uint8_t targetMac[6] = {0};
  uint32_t seq = 0;
};

// Canonical channel plan — identical on every board (Split Wardrive deals from
// it, and the bridge sweeps it to deliver commands to whatever channel the
// screen chip is currently hopping on).
constexpr uint8_t kLink24Channels[] = {1, 2, 3,  4,  5,  6, 7,
                                       8, 9, 10, 11, 12, 13};
constexpr int kLink24ChannelCount =
    static_cast<int>(sizeof(kLink24Channels) / sizeof(kLink24Channels[0]));
constexpr uint8_t kLink5Channels[] = {
    36,  40,  44,  48,  52,  56,  60,  64,  100, 104, 108, 112, 116,
    120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165};
constexpr int kLink5ChannelCount =
    static_cast<int>(sizeof(kLink5Channels) / sizeof(kLink5Channels[0]));

// Remote-control opcodes carried in a kLinkMsgCommand frame's `reserved` low
// byte. Mirrors the on-device serial shortcuts (handleSerial) so the phone,
// the bridge, and the screen chip all agree. Also mirrored in the web app.
// Command target: which chip should run the tool. Carried in the phone's BLE
// write ([op, arg, target]); does NOT ride ESP-NOW, so LinkPacket is unchanged.
enum AxdTarget : uint8_t {
  kTargetBridge = 0,  // run on the orange bridge chip itself (local dispatch)
  kTargetScreen = 1,  // relay over ESP-NOW to the white screen chip
};

// Source tag prefixed to status/result notifications so the phone knows which
// chip produced them.
enum AxdSource : uint8_t {
  kSourceBridge = 0,
  kSourceScreen = 1,
  kSourceWardrive = 2,  // results char carries a WiGLE CSV text row (bridge)
  kSourceHunt = 3,      // results char carries a Fleet Hunter text row
  kSourceTopo = 4,      // results char carries a Topology Map text row
  kSourceBleIntel = 5,  // results char carries a BLE Intel telemetry row
  kSourceSpectrogram = 6, // results char carries a Spectrogram telemetry row
  kSourceWifi6Intel = 7,  // results char carries a Wi-Fi 6 Intel telemetry row
  kSourceDeauthForensics = 8, // results char carries a Deauth Forensics telemetry row
  kSourceWardriveStatus = 10, // $WDSTAT dashboard; separate from CSV row stream
  kSourceFiles = 9,           // results char carries SD file manager telemetry/data
};

// Compact text telemetry shares the 250-byte ESP-NOW budget. The source column
// distinguishes the bridge session from the screen session in a multi-node UI.
struct AxdWardriveStatusMsg {
  uint32_t magic = kLinkMagic;
  uint8_t version = kLinkProtoVersion;
  uint8_t type = kLinkMsgWardriveStatus;
  char data[232] = {};
};
static_assert(sizeof(AxdWardriveStatusMsg) <= 250, "Wardrive status must fit ESP-NOW");

// SD File Manager entry frame (ESP-NOW screen -> bridge)
struct AxdFileEntryMsg {
  uint32_t magic = kLinkMagic;
  uint8_t version = kLinkProtoVersion;
  uint8_t type = kLinkMsgFileEntry;
  uint8_t index = 0;
  uint8_t count = 0;
  uint32_t size = 0;
  char name[36] = {0};
};

// SD File Manager chunk data frame (ESP-NOW screen -> bridge)
struct AxdFileDataMsg {
  uint32_t magic = kLinkMagic;
  uint8_t version = kLinkProtoVersion;
  uint8_t type = kLinkMsgFileData;
  uint16_t chunkSeq = 0;
  uint16_t totalChunks = 0;
  uint8_t dataLen = 0;
  uint8_t pad = 0;
  char data[136] = {0};       // Base64 chunk string
};

// SD File Manager status/completion frame (ESP-NOW screen -> bridge)
struct AxdFileDoneMsg {
  uint32_t magic = kLinkMagic;
  uint8_t version = kLinkProtoVersion;
  uint8_t type = kLinkMsgFileDone;
  uint8_t status = 0;         // 0 = OK, 1 = Error, 2 = Aborted
  uint32_t totalBytes = 0;
  char name[36] = {0};
};

// One chunk in flight, retried until the browser acknowledges token + sequence.
// 32-bit sequences avoid the legacy 65535-chunk (~6 MB) size limit.
constexpr uint32_t kFileChunkBytes = 96;
constexpr uint32_t kFileChunkRetryMs = 750;
constexpr int kFileChunkAttempts = 10;
struct AxdFileChunkMsg {
  uint32_t magic = kLinkMagic;
  uint8_t version = kLinkProtoVersion;
  uint8_t type = kLinkMsgFileChunk;
  uint8_t kind = 0;  // 0 data, 1 legacy done, 2 error, 3 manifest, 4 verified done, 5 hashing
  uint8_t pad = 0;
  uint32_t token = 0;
  uint32_t seq = 0;
  uint32_t totalBytes = 0;
  char data[136] = {};
};
struct AxdFileChunkAck {
  uint32_t magic = kLinkMagic;
  uint8_t version = kLinkProtoVersion;
  uint8_t type = kLinkMsgFileChunkAck;
  uint16_t pad = 0;
  uint32_t token = 0;
  uint32_t seq = 0;
};
static_assert(sizeof(AxdFileChunkMsg) <= 250, "File chunk must fit ESP-NOW");

// Multi-node Fleet Hunter observation frame (ESP-NOW)
struct FleetHuntObservation {
  uint32_t magic = kLinkMagic;
  uint8_t version = kLinkProtoVersion;
  uint8_t type = kLinkMsgFleetHuntObservation;
  uint8_t channel = 0;
  int8_t rssi = -127;
  uint8_t bssid[6] = {0};
  float lat = 0.0f;
  float lon = 0.0f;
  uint32_t timestampMs = 0;
  uint8_t nodeIndex = 0;
  uint8_t reserved = 0;
};

// Solved Fleet Hunter telemetry frame (ESP-NOW screen -> bridge)
struct FleetHuntResult {
  uint32_t magic = kLinkMagic;
  uint8_t version = kLinkProtoVersion;
  uint8_t type = kLinkMsgFleetHuntResult;
  uint8_t bssid[6] = {0};
  int8_t rssi = -127;
  uint8_t points = 0;
  float lat = 0.0f;
  float lon = 0.0f;
  float distanceM = 0.0f;
  float bearingDeg = 0.0f;
  float confidenceM = 0.0f;
  char ssid[33] = {0};
};

// Multi-node Swarm Topology link frame (ESP-NOW)
struct FleetTopologyLink {
  uint32_t magic = kLinkMagic;
  uint8_t version = kLinkProtoVersion;
  uint8_t type = kLinkMsgFleetTopology;
  uint8_t linkType = 0;   // 0 = Client->AP, 1 = Client->Probe
  int8_t rssi = -127;
  uint8_t channel = 0;
  uint8_t clientMac[6] = {0};
  uint8_t targetMac[6] = {0};  // BSSID for AP
  char targetName[33] = {0};   // SSID for Probe / AP
};

enum AxdCommand : uint8_t {
  kAxdCmdNone = 0,
  // Recon
  kAxdCmdWifiScan = 1,
  kAxdCmdBleScan = 2,
  kAxdCmdChannelMap = 3,
  kAxdCmdPacketMon = 4,
  kAxdCmdClients = 6,
  kAxdCmdWpsScan = 10,
  kAxdCmdHiddenSsid = 11,
  kAxdCmdCameras = 12,
  kAxdCmdSecurityAudit = 13,
  kAxdCmdTrackers = 14,
  kAxdCmdHarvester = 15,
  kAxdCmdProbeIntel = 16,
  kAxdCmdSaved = 17,
  // Attacks (authorized targets only; target-specific ones use the on-device
  // last selection)
  kAxdCmdBeaconFlood = 20,
  kAxdCmdEvilPortal = 21,
  kAxdCmdEvilTwin = 22,
  kAxdCmdProbeLure = 23,
  kAxdCmdAuthFlood = 24,
  // Monitor (defensive)
  kAxdCmdDeauthWatch = 5,
  kAxdCmdRogueWatch = 30,
  kAxdCmdBleSpamWatch = 31,
  kAxdCmdKarmaWatch = 32,
  kAxdCmdBeaconWatch = 33,
  kAxdCmdAdvancedWatch = 34,
  // GPS / wardrive / misc
  kAxdCmdWardriveStart = 7,
  kAxdCmdWardriveStop = 8,
  kAxdCmdGps = 9,
  kAxdCmdLocator = 40,
  kAxdCmdStatus = 41,
  kAxdCmdFiles = 42,
  // Network selection + per-target actions (act on the screen chip's
  // selectedWifi). SelectWifi carries the list index in the command arg
  // (reserved high byte).
  kAxdCmdListWifi = 50,     // stream the current Wi-Fi list to the phone
  kAxdCmdSelectWifi = 51,   // arg = index into wifiEntries[]
  kAxdCmdDeauthSel = 52,    // deauth the selected AP
  kAxdCmdGrabSel = 53,      // handshake/PMKID grab on the selected AP
  kAxdCmdTrackSel = 54,     // RSSI-track the selected AP
  kAxdCmdFleetHunt = 55,    // multi-node target hunt / trilateration on selected AP
  kAxdCmdTopology = 56,     // live swarm mesh topology mapping
  kAxdCmdBleIntel = 57,     // BLE ecosystem intel & continuity decoder
  kAxdCmdSpectrogram = 58,  // dual-band RF spectrogram & waterfall analyzer
  kAxdCmdWifi6Intel = 59,   // Wi-Fi 6 / 802.11ax OFDMA & BSS color intelligence
  // Fleet Wardrive control (multi-node; joining is always deliberate).
  kAxdCmdFleetStart = 60,   // become coordinator + start the fleet wardrive
  kAxdCmdFleetJoin = 61,    // arm this chip to auto-join a coordinator's fleet
  kAxdCmdFleetStop = 62,    // leave the fleet (coordinator ends it for everyone)
  kAxdCmdDeauthForensics = 63, // targeted deauth & disassociation forensic analyzer
  kAxdCmdFileList = 64,        // stream list of files in /awokxdag to phone/PC
  kAxdCmdFileGet = 65,         // download file by index (arg = index)
  kAxdCmdFileDelete = 66,      // delete file by index (arg = index)
  kAxdCmdFileAbort = 67,       // cancel active file streaming
  kAxdCmdFileGetReliable = 68, // BLE: opcode, index, target, LE uint32 request token
  kAxdCmdFileChunkAck = 69,    // BLE write: opcode + LE uint32 token + LE uint32 seq
  // 19-byte BLE request: op/index/target + LE token/startSeq/snapshotBytes/CRC32.
  // startSeq=0 starts a new snapshot; >0 resumes the matching snapshot.
  // COMMAND reuses masterMillis/networks/bleCount for these three fields.
  kAxdCmdFileGetVerified = 70,
  kAxdCmdStopHome = 255,
};
