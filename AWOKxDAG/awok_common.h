// Shared declarations for the AWOKxDAG firmware. Included once by the main
// sketch; the feature tabs (deauth/handshake/sniffer/beacon/portal/gps/input)
// are concatenated into the same translation unit, so all globals defined in
// the main sketch and the tabs are visible everywhere without extern churn.
#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SD.h>
#include <NimBLEDevice.h>
// 2.5.0 destroys the scan-response timer after freeing the NimBLE port.
// Every scan tool can reach this teardown path; require the upstream fix.
#if !defined(NIMBLE_CPP_VERSION) || !defined(NIMBLE_CPP_VERSION_VAL)
#error "AWOKxDAG requires NimBLE-Arduino 2.5.1 or newer. Update it in Library Manager."
#elif NIMBLE_CPP_VERSION < NIMBLE_CPP_VERSION_VAL(2, 5, 1)
#error "NimBLE-Arduino 2.5.0 has a BLE shutdown crash. Install NimBLE-Arduino 2.5.1 or newer."
#endif
#include <Preferences.h>
#include <SPI.h>
#include <WiFi.h>
#include <XPT2046_Touchscreen.h>
#include <TinyGPSPlus.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include "tool_memory.h"
#include <nvs.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <string>
#include "network_parse.h"
#include <WiFiUdp.h>
#include <NetworkClientSecure.h>
#include <lwip/sockets.h>
#include <lwip/etharp.h>
#include <lwip/priv/tcpip_priv.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#include "board_pins.h"
#include "keyboard_layout.h"
#include "gps_timezone.h"
#include "file_crc.h"
#include "wardrive_stats.h"
#ifdef AWOK_MINI_DISPLAY
#include "mini_display.h"
#include "mini_boot_screen_data.h"
#include "result_memory.h"
// True when BLE participates in a dual-radio session. RadioScheduler alternates
// scan windows while keeping both controllers resident on supported boards.
// False keeps a view Wi-Fi-only. Set by radioSchedulerBegin.
bool radiosCoexist = false;
#elif defined(AWOK_HEADLESS)
#include "headless_display.h"  // orange bridge chip: no screen, BLE-driven
#include "result_memory.h"     // PSRAM-backed result tables (frees DMA)
bool radiosCoexist = true;     // C5: Wi-Fi + BLE run resident together
// Bridge BLE server hooks (defined in bridge_ble.ino); forward-declared so the
// main sketch and link.ino can notify the phone regardless of .ino tab order.
extern volatile bool g_bridgePhoneConnected;
void bridgeBleBegin();
void bridgeNotifyStatus(uint8_t source, const uint8_t* body, size_t len);
bool bridgeNotifyResult(uint8_t source, const uint8_t* body, size_t len);
void bridgeServiceCommand();
bool bridgeFileReceiverReady(uint32_t token);
#else
#ifdef AWOK_CLASSIC_ESP32
bool radiosCoexist = false;
#else
bool radiosCoexist = true;
#endif
#include "touch_display.h"     // buffered ILI9341 wrapper (kills refresh flicker)
#include "boot_screen_data.h"  // 240x320 Touch splash; unused on the Mini
#endif

constexpr int kScreenWidth = 240;
constexpr int kScreenHeight = 320;
constexpr int kHeaderHeight = 42;
constexpr int kFooterTop = 278;
// Classic ESP32 has a much smaller statically addressable DRAM segment and no
// verified PSRAM on these display pins. Keep bounded tables within its budget.
constexpr int kResultCapacity = AwokPins::kDualBand ? 64 : 32;
constexpr int kMaxWifiResults = kResultCapacity;
constexpr int kMaxBleResults = kResultCapacity;
// NimBLE reserves 255 for unlimited retention; keep snapshot scans bounded.
static_assert(kMaxBleResults > 0 && kMaxBleResults < 255, "Invalid BLE result limit");
constexpr int kVisibleRows = 10;
constexpr int kMaxSaved = 10;
constexpr uint32_t kBleScanMs = 5000;
constexpr uint32_t kBootScreenMs = 1800;
constexpr uint32_t kSdClockHz = 10000000;
constexpr char kSdDirectory[] = "/awokxdag";
constexpr char kFirmwareAuditCsvPath[] = "/awokxdag/firmware_audit.csv";
constexpr char kFirmwareAuditPreviousCsvPath[] =
    "/awokxdag/firmware_audit.previous.csv";
constexpr uint32_t kFirmwareAuditMaxBytes = 256 * 1024;
constexpr char kSavedCsvPath[] = "/awokxdag/saved_networks.csv";
constexpr char kScanCsvPath[] = "/awokxdag/latest_wifi_scan.csv";
constexpr char kBleScanCsvPath[] = "/awokxdag/latest_ble_scan.csv";
constexpr char kSignalCsvPath[] = "/awokxdag/latest_wifi_signal.csv";
constexpr int kSignalSampleCount = 30;
constexpr uint32_t kSignalSampleIntervalMs = 1500;
constexpr uint32_t kSignalPassiveDwellMs = 350;
constexpr char kDeauthLogCsvPath[] = "/awokxdag/latest_deauth_log.csv";
constexpr uint32_t kDeauthHopIntervalMs = 300;
constexpr uint32_t kDeauthMonitorRedrawMs = 500;
constexpr uint32_t kDeauthAttackBurstIntervalMs = 20;
constexpr uint32_t kDeauthAttackRedrawMs = 500;
// Detection hops every 2.4 GHz channel (1-13) and every standard 5 GHz 20 MHz
// channel, including the DFS block (52-64, 100-144) and 165 -- listening on DFS
// is allowed, only transmitting is restricted. On the C5, esp_wifi_set_channel
// switches band automatically from the channel number; if the region disallows
// a channel the call fails and the hop simply moves on.
constexpr uint8_t kDeauthHopChannels[] = {
    1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,
#ifndef AWOK_CLASSIC_ESP32
    36,  40,  44,  48,  52,  56,  60,  64,  100, 104, 108, 112, 116,
    120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165
#endif
};
constexpr int kDeauthHopChannelCount =
    static_cast<int>(sizeof(kDeauthHopChannels) / sizeof(kDeauthHopChannels[0]));
constexpr int kMaxDeauthTargets = 8;
constexpr char kVersion[] = "1.7.4";
constexpr char kAuthor[] = "dag nazty";
constexpr uint32_t kHandshakeRedrawMs = 500;
constexpr uint32_t kHandshakePulseMs = 2000;
constexpr int kCaptureSlotBytes = 256;
constexpr int kCaptureQueueSlots = AwokPins::kDualBand ? 24 : 12;
constexpr char kClientCsvPath[] = "/awokxdag/latest_clients.csv";
constexpr int kMaxClients = kResultCapacity;
constexpr int kSnifferQueueSlots = AwokPins::kDualBand ? 32 : 16;
constexpr uint32_t kClientHopIntervalMs = 300;
constexpr uint32_t kClientRedrawMs = 700;
constexpr uint32_t kBeaconBurstIntervalMs = 20;
constexpr uint32_t kBeaconRedrawMs = 500;
constexpr int kBeaconsPerBurst = 5;
constexpr uint8_t kBeaconChannels[] = {1, 6, 11};
constexpr int kBeaconChannelCount =
    static_cast<int>(sizeof(kBeaconChannels) / sizeof(kBeaconChannels[0]));
constexpr char kPortalSsid[] = "Free_WiFi";
constexpr char kPortalCredsPath[] = "/awokxdag/portal_creds.csv";
constexpr uint32_t kPortalRedrawMs = 1000;
constexpr size_t kWardriveBloomFilterBytes = AwokPins::kDualBand ? 4096 : 256;  // fast O(1) MAC deduplication bitset
constexpr int kMaxWardriveMacs = AwokPins::kDualBand ? 512 : 128;
constexpr uint32_t kWardriveRedrawMs = 800;
constexpr int kBleHitQueueSlots = AwokPins::kDualBand ? 24 : 12;

// Security Audit: passive beacon-IE posture report (encryption tier, PMF, WPS).
constexpr char kSecurityAuditCsvPath[] = "/awokxdag/security_audit.csv";
constexpr int kMaxAudit = kResultCapacity;
constexpr int kAuditHitQueueSlots = AwokPins::kDualBand ? 24 : 12;
constexpr uint32_t kAuditHopIntervalMs = 300;
constexpr uint32_t kAuditRedrawMs = 700;

// BLE Trackers: passive AirTag/Find My, Tile, Samsung SmartTag detection.
constexpr char kTrackerCsvPath[] = "/awokxdag/ble_trackers.csv";
constexpr int kMaxTrackers = kResultCapacity;
constexpr int kTrackerHitQueueSlots = AwokPins::kDualBand ? 32 : 16;
constexpr uint32_t kTrackerRedrawMs = 700;
// A tracker seen over a span longer than this (with repeat sightings) while you
// move is flagged as potentially following you.
constexpr uint32_t kTrackerFollowMs = 45000;
constexpr uint32_t kTrackerMinSightings = 4;

// BLE Intel: ecosystem decoder (Apple Continuity, Fast Pair, Swift Pair, Samsung).
constexpr char kBleIntelCsvPath[] = "/awokxdag/ble_intel.csv";
constexpr int kMaxBleIntel = kResultCapacity;
constexpr int kBleIntelHitQueueSlots = AwokPins::kDualBand ? 32 : 16;
constexpr uint32_t kBleIntelRedrawMs = 800;

enum BleIntelEcosystem : uint8_t {
  kBleEcoUnknown = 0,
  kBleEcoApple = 1,
  kBleEcoGoogle = 2,
  kBleEcoMicrosoft = 3,
  kBleEcoSamsung = 4,
};

struct BleIntelHit {
  char addr[18] = {0};
  int8_t rssi = -127;
  uint8_t ecosystem = 0;
  char deviceType[20] = {0};
  char details[36] = {0};
  int8_t batteryLeft = -1;
  int8_t batteryRight = -1;
  int8_t batteryCase = -1;
};

struct BleIntelEntry {
  String addr;
  int32_t rssi = -127;
  uint8_t ecosystem = 0;
  String deviceType;
  String details;
  int8_t batteryLeft = -1;
  int8_t batteryRight = -1;
  int8_t batteryCase = -1;
  uint32_t firstSeenMs = 0;
  uint32_t lastSeenMs = 0;
  uint32_t sightings = 0;
};

// Spectrogram: dual-band RF waterfall & channel duty cycle monitor.
constexpr char kSpectrogramCsvPath[] = "/awokxdag/spectrogram.csv";
constexpr uint32_t kSpectrogramDwellMs = 60;
constexpr uint32_t kSpectrogramRedrawMs = 120;
constexpr int kWaterfallHistoryRows = 28;
constexpr int kSpectrogramHitQueueSlots = AwokPins::kDualBand ? 32 : 16;

struct SpectrogramChannelStats {
  uint8_t channel = 0;
  uint8_t dutyPercent = 0;
  uint16_t packetCount = 0;
  uint32_t byteCount = 0;
  int8_t peakRssi = -127;
  int8_t avgNoise = -127;
  uint16_t mgmtCount = 0;
  uint16_t ctrlCount = 0;
  uint16_t dataCount = 0;
  uint32_t lastSeenMs = 0;
};

struct SpectrogramHit {
  uint8_t channel = 0;
  uint8_t dutyPercent = 0;
  uint16_t packetCount = 0;
  uint32_t byteCount = 0;
  int8_t peakRssi = -127;
  int8_t avgNoise = -127;
  uint16_t mgmtCount = 0;
  uint16_t ctrlCount = 0;
  uint16_t dataCount = 0;
};

// Wi-Fi 6 Intel: 802.11ax HE capabilities, BSS Color, and spatial reuse intelligence
constexpr char kWifi6IntelCsvPath[] = "/awokxdag/wifi6_intel.csv";
constexpr uint32_t kWifi6HopIntervalMs = 250;
constexpr uint32_t kWifi6RedrawMs = 600;
constexpr int kMaxWifi6Aps = kResultCapacity;

struct Wifi6ApEntry {
  uint8_t bssid[6] = {0};
  char ssid[33] = {0};
  uint8_t channel = 0;
  uint8_t generation = 4; // 4: 802.11n, 5: 802.11ac, 6: 802.11ax
  uint8_t bssColor = 0;   // 1..63 (0 = disabled/none)
  bool colorDisabled = false;
  uint16_t channelWidth = 20; // 20, 40, 80, 160 MHz
  int8_t rssi = -127;
  uint32_t lastSeenMs = 0;
};

struct Wifi6Hit {
  uint8_t bssid[6] = {0};
  char ssid[33] = {0};
  uint8_t channel = 1;
  uint8_t generation = 4;
  uint8_t bssColor = 0;
  bool colorDisabled = false;
  uint16_t channelWidth = 20;
  int8_t rssi = -127;
};

// Deauth Forensics: Passive attack attribution, sequence anomaly, and victim profiling
constexpr char kDeauthForensicsCsvPath[] = "/awokxdag/deauth_forensics.csv";
constexpr uint32_t kDeauthForensicsHopMs = 200;
constexpr uint32_t kDeauthForensicsRedrawMs = 500;
constexpr int kMaxDeauthForensicEvents = 24;

enum DeauthAttackType : uint8_t {
  kDeauthTypeNone = 0,
  kDeauthTypeBroadcast = 1,  // Shotgun flood to FF:FF:FF:FF:FF:FF
  kDeauthTypeTargeted = 2,   // Targeted unicast to specific victim station
  kDeauthTypeDisassoc = 3,   // Disassociation frame
  kDeauthTypeAnomaly = 4     // High sequence jump / forged transmitter
};

struct DeauthHit {
  uint8_t attackType = kDeauthTypeNone;
  uint8_t targetMac[6] = {0};
  uint8_t sourceMac[6] = {0};
  uint8_t bssid[6] = {0};
  uint16_t reasonCode = 0;
  uint16_t seqNum = 0;
  int16_t seqJump = 0;
  int8_t rssi = -127;
  uint8_t channel = 1;
};

struct DeauthForensicEvent {
  uint32_t timestampMs = 0;
  uint8_t attackType = kDeauthTypeNone;
  uint8_t targetMac[6] = {0};
  uint8_t sourceMac[6] = {0};
  uint8_t bssid[6] = {0};
  uint16_t reasonCode = 0;
  uint16_t seqNum = 0;
  int16_t seqJump = 0;
  int8_t rssi = -127;
  uint8_t channel = 0;
};

// Harvester: all-channel passive EAPOL/PMKID collection (no deauth).
constexpr char kHarvestPcapPath[] = "/awokxdag/harvest.pcap";
constexpr char kHarvestPmkidPath[] = "/awokxdag/harvest_pmkid.txt";
constexpr int kMaxHarvestAp = kResultCapacity;
constexpr int kMaxHarvestSeen = kResultCapacity;  // beacons written once per BSSID
constexpr uint32_t kHarvestHopIntervalMs = 300;
constexpr uint32_t kHarvestRedrawMs = 700;

// Probe Intel: directed probe-request SSID aggregation.
constexpr char kProbeIntelCsvPath[] = "/awokxdag/probe_intel.csv";
constexpr int kMaxProbeSsids = kResultCapacity;
constexpr int kProbeMacsPerSsid = 8;
constexpr int kProbeHitQueueSlots = AwokPins::kDualBand ? 32 : 16;
constexpr uint32_t kProbeHopIntervalMs = 300;
constexpr uint32_t kProbeRedrawMs = 700;

// Topology Map: Swarm client/AP association & probe graph
constexpr char kTopologyCsvPath[] = "/awokxdag/topology_map.csv";

// Karma Watch: one BSSID answering many SSIDs (WiFi Pineapple / Karma / MANA).
constexpr char kKarmaLogCsvPath[] = "/awokxdag/karma_log.csv";
constexpr int kMaxKarmaAps = kResultCapacity;
constexpr int kKarmaSsidsPerAp = 6;
constexpr int kKarmaHitQueueSlots = AwokPins::kDualBand ? 24 : 12;
constexpr int kKarmaSsidThreshold = 3;  // distinct SSIDs => suspicious
constexpr uint32_t kKarmaHopIntervalMs = 300;
constexpr uint32_t kKarmaRedrawMs = 700;

// Beacon Watch: beacon-flood / fake-AP detection by BSSID diversity per window.
constexpr char kBeaconWatchLogCsvPath[] = "/awokxdag/beacon_flood_log.csv";
constexpr int kBeaconWatchWindowSet = AwokPins::kDualBand ? 64 : 32;   // distinct BSSIDs counted per window
constexpr int kBeaconWatchHitQueueSlots = AwokPins::kDualBand ? 32 : 16;
constexpr uint32_t kBeaconWatchWindowMs = 2000;
constexpr uint32_t kBeaconWatchRedrawMs = 500;
constexpr uint32_t kBeaconWatchHopIntervalMs = 250;
constexpr uint32_t kBeaconFloodThreshold = 25;  // distinct BSSIDs / window

// Auth Flood Watch: authentication / association flood DoS against an AP.
constexpr char kAuthFloodLogCsvPath[] = "/awokxdag/auth_flood_log.csv";
constexpr uint32_t kAuthFloodWindowMs = 2000;
constexpr uint32_t kAuthFloodRedrawMs = 500;
constexpr uint32_t kAuthFloodHopIntervalMs = 250;
constexpr uint32_t kAuthFloodThreshold = 30;  // auth/assoc frames / window


// Advanced Watch: shared passive Wi-Fi/BLE anomaly detector.
constexpr char kAdvancedWatchLogCsvPath[] = "/awokxdag/advanced_watch.csv";
constexpr int kAdvancedHitQueueSlots = AwokPins::kDualBand ? 64 : 24;
constexpr int kAdvancedBleQueueSlots = AwokPins::kDualBand ? 32 : 16;
constexpr int kAdvancedMaxAps = AwokPins::kDualBand ? 32 : 16;
constexpr int kAdvancedMaxDisconnectGroups = 8;
constexpr int kAdvancedMaxBleFingerprints = AwokPins::kDualBand ? 16 : 8;
constexpr uint32_t kAdvancedWindowMs = 2000;
constexpr uint32_t kAdvancedRedrawMs = 500;
constexpr uint32_t kAdvancedHopIntervalMs = 300;
constexpr uint32_t kAdvancedDisconnectThreshold = 20;
constexpr uint32_t kAdvancedEapolThreshold = 16;
constexpr uint32_t kAdvancedAssocThreshold = 30;
constexpr uint32_t kAdvancedCsaThreshold = 8;
constexpr int kAdvancedNoiseRiseDb = 15;
constexpr int kAdvancedNoiseFloorMinDbm = -80;
constexpr uint32_t kAdvancedBleChurnWindowMs = 15000;
constexpr int kAdvancedBleChurnAddresses = 5;

enum AdvancedHitKind : uint8_t {
  kAdvancedBeaconHit = 1,
  kAdvancedDisconnectHit = 2,
  kAdvancedEapolHit = 3,
  kAdvancedAssocHit = 4
};

struct AdvancedHit {
  uint8_t kind = 0;
  uint8_t subtype = 0;
  uint8_t source[6] = {0};
  uint8_t target[6] = {0};
  uint8_t channel = 0;
  int8_t rssi = -127;
  int8_t noise = -127;
  uint16_t reason = 0;
  uint16_t beaconInterval = 0;
  uint32_t fingerprint = 0;
  uint8_t security = 0;
  uint8_t pmf = 0;
  uint8_t wps = 0;
  uint8_t csaChannel = 0;
  uint8_t ssidLen = 0;
  char ssid[33] = {0};
};

struct AdvancedApEntry {
  uint8_t bssid[6] = {0};
  String ssid;
  uint8_t channel = 0;
  int8_t rssi = -127;
  uint16_t beaconInterval = 0;
  uint32_t fingerprint = 0;
  uint8_t security = 0;
  uint8_t pmf = 0;
  uint8_t wps = 0;
  uint32_t lastSeenMs = 0;
  bool savedDowngradeLogged = false;
  uint32_t pendingKey = 0;
  uint8_t pendingCount = 0;
  uint8_t pendingChannel = 0;
  uint16_t pendingBeaconInterval = 0;
  uint32_t pendingFingerprint = 0;
  uint8_t pendingSecurity = 0;
  uint8_t pendingPmf = 0;
  uint8_t pendingWps = 0;
  char pendingSsid[33] = {0};
};

struct AdvancedDisconnectGroup {
  uint8_t source[6] = {0};
  uint8_t target[6] = {0};
  uint16_t reason = 0;
  uint32_t count = 0;
};

struct AdvancedBleHit {
  char address[18] = {0};
  uint32_t fingerprint = 0;
  int8_t rssi = -127;
};

struct AdvancedBleFingerprint {
  uint32_t fingerprint = 0;
  char addresses[kAdvancedBleChurnAddresses + 2][18] = {{0}};
  uint8_t addressCount = 0;
  int8_t lastRssi = -127;
  uint32_t firstSeenMs = 0;
  uint32_t lastSeenMs = 0;
  bool alerted = false;
};

constexpr uint16_t kBackground = ILI9341_BLACK;
constexpr uint16_t kForeground = ILI9341_WHITE;
constexpr uint16_t kPanel = 0x1082;
constexpr uint16_t kAccent = ILI9341_CYAN;
constexpr uint16_t kMuted = 0x7BEF;
constexpr uint16_t kGood = ILI9341_GREEN;
constexpr uint16_t kWarn = ILI9341_YELLOW;
constexpr uint16_t kBad = ILI9341_RED;

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Persistent operational audit trail. Details are CSV-escaped by the writer;
// callers must not put captured passwords or other secrets in this log.
bool initializeFirmwareAudit();
bool recordFirmwareAudit(const char* category, const char* action,
                         const char* outcome, const String& details);

enum class View {
  kHome,
  kWifi,
  kChannels,
  kBle,
  kBleDetail,
  kSaved,
  kWifiAudit,
  kWifiMonitor,
  kDeauthMonitor,
  kDeauthSelect,
  kDeauthAttack,
  kHandshake,
  kClientSniffer,
  kRecon,
  kMonitor,
  kAttacks,
  kBeaconFlood,
  kEvilPortal,
  kGps,
  kWardrive,
  kPacketMon,
  kLocator,
  kWpsScan,
  kStatus,
  kSettings,
  kScreenTest,
  kFiles,
  kBleSpamWatch,
  kProbeLure,
  kRogueWatch,
  kHiddenReveal,
  kCameraScan,
  kSecurityAudit,
  kTrackerScan,
  kHarvester,
  kProbeIntel,
  kKarmaWatch,
  kBeaconWatch,
  kAuthFlood,
  kAdvancedWatch,
  kLinkWardrive,
  kNetworkMenu,
  kNetworkSetup,
  kNetworkEdit,
  kNetworkAps,
  kNetworkResults,
  kNetworkHost,
  kNetworkDetail,
  kWardriveUpload,
  kWardriveUploadFiles,
  kFleetHunt,
  kTopologyMap,
  kBleIntel,
  kSpectrogram,
  kWifi6Intel,
  kDeauthForensics
};

// ---- Link Mode (ESP-NOW pairing of two AxD units) -----------------------
// See docs/link-mode.md. v1 = shared core + Split Wardrive. All ESP-NOW frames
// are the single POD LinkPacket, tagged by `type`. The recv callback runs in the
// Wi-Fi task and only enqueues into linkPacketQueue; updateLink() parses.
// The wire format (LinkPacket, magic, keys, msg types, command opcodes) lives in
// link_protocol.h so the headless bridge chip shares it verbatim.
#include "link_protocol.h"
void linkStreamFileReliable(uint8_t index, uint32_t token);
void linkStreamFileVerified(uint8_t index, uint32_t token, uint32_t startSeq, uint32_t snapshotBytes, uint32_t expectedCrc);
void linkReceiveFileAck(uint32_t token, uint32_t seq);
bool linkReliableFileActive();
bool linkReliableFileTokenMatches(uint32_t token);
#ifdef AWOK_HEADLESS
void bridgeQueueFileChunk(const AxdFileChunkMsg& chunk);
void bridgeServiceFileTransfer();
bool bridgeFileTransferActive();
#endif
constexpr uint32_t kLinkRendezvousMs = 1000;  // beat period (live feel)
constexpr uint32_t kLinkWindowMs = 300;       // link-channel dwell per beat
constexpr uint32_t kLinkHelloIntervalMs = 250;
constexpr uint32_t kLinkPeerTimeoutMs = 4000;  // partner-lost threshold
constexpr uint32_t kLinkWardriveDwellMs = 200;  // per-channel async scan dwell
constexpr uint32_t kLinkWardriveRedrawMs = 700;

enum LinkState : uint8_t {
  kLinkOff = 0,         // ESP-NOW down / not on the Link screen
  kLinkDiscovering = 1,  // broadcasting HELLO, waiting to hear a peer
  kLinkAwaitConfirm = 2,  // peer found, 4-digit code shown, waiting for Confirm
  kLinkReady = 3         // paired; role + session settled
};

// This unit's Split Wardrive channel set (declared here so functions taking it
// get valid auto-prototypes). See link.ino linkAssignedPlan.
enum LinkPlan : uint8_t {
  kLinkPlan24 = 0,   // 2.4 GHz only
  kLinkPlan5 = 1,    // 5 GHz only
  kLinkPlanFull = 2  // 2.4 GHz then 5 GHz
};

// LinkPacket, kLinkFlagConfirmed, kLinkFlagDualBand, and the kLink24/5 channel
// plans: see link_protocol.h (shared with the bridge, which sweeps them to
// deliver commands to whatever channel the screen chip is hopping on).

// A received frame plus the RSSI the radio reported, queued for the main loop.
struct LinkQueueItem {
  LinkPacket pkt;
  int8_t rssi = -127;
};

// ---- Fleet Wardrive runtime state --------------------------------------
constexpr uint32_t kFleetInviteIntervalMs = 400;   // coordinator invite cadence
constexpr uint32_t kFleetRosterIntervalMs = 1000;  // coordinator roster cadence
constexpr uint32_t kFleetMemberTimeoutMs = 15000;  // drop a silent member (15s resilience)
// Per-worker outbound row ring. Each slot is a full FleetWardriveRow (~76 B).
// Deep enough to absorb multi-worker bursts without dropping rows over radio.
constexpr int kFleetRowRingSlots = AwokPins::kDualBand ? 96 : 16;

// One fleet member as tracked by the coordinator (and mirrored on every node
// from the roster). `mac`/`caps` come from the roster; the rest are live.
struct FleetMember {
  uint8_t mac[6] = {0};
  uint8_t caps = 0;
  uint32_t lastSeenMs = 0;  // coordinator: last FleetJoin/row heard
  uint32_t rows = 0;        // rows contributed (coordinator view)
  uint32_t ackSeq = 0;      // highest row seq stored from this member
};

// A suspected surveillance camera found by the camera scan.
struct CameraEntry {
  String label;   // SSID or BLE name
  String mac;     // BSSID or BLE address
  String vendor;  // matched vendor
  String reason;  // why it flagged (OUI / SSID / BLE)
  int32_t rssi = -127;
  int16_t channel = 0;  // 0 for BLE
  bool ble = false;
};

// POD observation handed from the Wi-Fi task to the camera-scan loop.
struct CameraHit {
  uint8_t mac[6];
  int8_t rssi;
  uint8_t channel;
  uint8_t nameLen;
  char name[33];
};

// One WPS-capable AP found by the WPS scan (POD handed from the Wi-Fi task to
// the main loop, which merges it into the table).
struct WpsHit {
  uint8_t bssid[6];
  uint8_t channel;
  int8_t rssi;
  bool locked;
  uint8_t ssidLen;
  char ssid[33];
};

struct WpsEntry {
  String ssid;
  uint8_t bssid[6] = {0};
  uint8_t channel = 0;
  int32_t rssi = -127;
  bool locked = false;
};

// Rogue-AP / evil-twin detection.
struct RogueHit {
  uint8_t bssid[6];
  uint8_t channel;
  int8_t rssi;
  uint8_t ssidLen;
  char ssid[33];
};

struct ApSighting {
  String ssid;
  uint8_t bssid[6] = {0};
  uint8_t channel = 0;
  int32_t rssi = -127;
  bool suspicious = false;
  bool savedTwin = false;  // matches a saved SSID on a different BSSID
  bool logged = false;
};

// Hidden-SSID reveal.
struct HiddenHit {
  uint8_t bssid[6];
  uint8_t channel;
  int8_t rssi;
  uint8_t kind;  // 0 = hidden beacon, 1 = SSID reveal
  uint8_t ssidLen;
  char ssid[33];
};

struct HiddenEntry {
  uint8_t bssid[6] = {0};
  uint8_t channel = 0;
  int32_t rssi = -127;
  String ssid;
  bool revealed = false;
};

struct WifiEntry {
  String ssid;
  String bssid;
  int32_t rssi = -127;
  int32_t channel = 0;
  wifi_auth_mode_t auth = WIFI_AUTH_OPEN;
};

// Fixed-width NVS format: the whole list is replaced as one blob, so a failed
// update does not erase the previously saved networks. Never persist String.
struct SavedNetworkRecord {
  char ssid[33];
  char bssid[18];
  uint8_t auth;
  int32_t rssi;
  int32_t channel;
};

struct SavedNetworkSnapshot {
  uint32_t version;
  uint32_t count;
  SavedNetworkRecord entries[kMaxSaved];
};
static_assert(sizeof(SavedNetworkRecord) == 60, "NVS record layout changed");
static_assert(sizeof(SavedNetworkSnapshot) == 608, "NVS snapshot layout changed");

// Device preferences (GPS baud, backlight). Separate NVS blob from saved
// networks so a failed settings write cannot clobber the AP list.
struct DeviceSettingsRecord {
  uint32_t version;
  uint32_t gpsBaud;
  uint32_t backlightTimeoutMs;  // 0 = always on
  uint8_t brightnessPercent;    // 20–100
  uint8_t flags;                // kSetting*
  uint8_t reserved[6];
};
static_assert(sizeof(DeviceSettingsRecord) == 20, "NVS settings layout changed");
constexpr uint32_t kDeviceSettingsVersion = 1;
constexpr uint8_t kSettingConfirmAttacks = 0x01;
constexpr uint8_t kSettingSkipSplash = 0x02;
constexpr uint8_t kSettingNmeaEcho = 0x04;

struct DeauthTarget {
  uint8_t bssid[6] = {0};
  uint8_t channel = 0;
  String ssid;
};

// One captured 802.11 frame queued from the Wi-Fi task for the main loop to
// write into the pcap file (single-producer / single-consumer ring buffer).
struct CaptureFrame {
  uint16_t len = 0;      // bytes stored in data (capped at kCaptureSlotBytes)
  uint16_t origLen = 0;  // on-air length before capping
  uint32_t tsSec = 0;
  uint32_t tsUsec = 0;
  bool isEapol = false;
  uint8_t data[kCaptureSlotBytes] = {0};
};

// A discovered client station tracked by the probe-request sniffer.
struct ClientEntry {
  uint8_t mac[6] = {0};
  uint8_t bssid[6] = {0};
  bool hasBssid = false;
  String lastSsid;
  int32_t rssi = -127;
  uint32_t packets = 0;
  uint8_t channel = 0;
};

// A minimal fixed-size observation the sniffer callback hands to the main loop
// (POD only — no String — so it is safe to copy from the Wi-Fi task).
struct SnifferHit {
  uint8_t mac[6];
  uint8_t bssid[6];
  bool hasBssid;
  bool isProbe;
  int8_t rssi;
  uint8_t channel;
  uint8_t ssidLen;
  char ssid[33];
};

// A BLE advertisement handed from the NimBLE task to the wardrive loop.
struct BleHit {
  char addr[18];
  int8_t rssi;
  char name[24];
};

// Time-multiplexed dual-radio scheduling. The C5 cannot keep Wi-Fi and the
// BLE controller DMA-resident at once, so dual-radio views alternate windows;
// each view fills a RadioScheduler and drives it from its update loop.
enum class RadioPhase : uint8_t { kWifi, kBle };

struct RadioScheduler {
  bool active = false;
  bool bleAvailable = false;    // false => permanent Wi-Fi-only session
  RadioPhase phase = RadioPhase::kWifi;
  uint32_t phaseStartMs = 0;
  uint32_t wifiWindowMs = 8000;   // Wi-Fi window length / safety cap
  uint32_t bleWindowMs = 6000;
  // Views that do a discrete Wi-Fi scan (wardrive) set this true when the scan
  // completes so the Wi-Fi window ends as soon as there are results, instead
  // of being cut off mid-scan. Promiscuous views leave it false (timer only).
  bool wifiScanDone = false;
  void (*enterWifi)() = nullptr;  // STA resident; (re)configure Wi-Fi capture
  void (*exitWifi)() = nullptr;   // stop Wi-Fi capture before STA teardown
  NimBLEScanCallbacks* bleCallbacks = nullptr;
  bool bleActiveScan = false;
  uint16_t bleInterval = 160;
  uint16_t bleWindow = 80;
};

// Security Audit encryption tiers (worst -> best), used for scoring/coloring.
enum AuditEnc {
  kAuditOpen = 0,   // no privacy bit
  kAuditWep = 1,    // privacy, no RSN/WPA IE
  kAuditWpa = 2,    // WPA1 vendor IE only (TKIP)
  kAuditWpa2 = 3,   // RSN PSK/CCMP
  kAuditWpa2Tkip = 4,
  kAuditWpa23 = 5,  // RSN PSK+SAE transition
  kAuditWpa3 = 6,   // RSN SAE only
  kAuditOwe = 7,    // Enhanced Open
  kAuditEnterprise = 8
};

// POD beacon observation handed from the Wi-Fi task to the security audit.
struct AuditHit {
  uint8_t bssid[6];
  uint8_t channel;
  int8_t rssi;
  uint8_t enc;  // AuditEnc
  uint8_t pmf;  // 0 none, 1 capable, 2 required
  uint8_t wps;  // 0 none, 1 open, 2 locked
  uint8_t ssidLen;
  char ssid[33];
};

struct AuditEntry {
  uint8_t bssid[6] = {0};
  uint8_t channel = 0;
  int32_t rssi = -127;
  uint8_t enc = kAuditOpen;
  uint8_t pmf = 0;
  uint8_t wps = 0;
  int risk = 0;
  bool logged = false;
  String ssid;
};

// POD BLE tracker sighting handed from the NimBLE task to the tracker scan.
struct TrackerHit {
  char addr[18];
  int8_t rssi;
  uint8_t kind;  // 1 Apple Find My, 2 Tile, 3 Samsung SmartTag
};

struct TrackerEntry {
  String addr;
  int32_t rssi = -127;
  uint8_t kind = 0;
  uint32_t firstSeenMs = 0;
  uint32_t lastSeenMs = 0;
  uint32_t sightings = 0;
  bool following = false;
  bool logged = false;
};

// One AP tracked by the handshake harvester (SSID learned from beacons, EAPOL
// progress and PMKID learned from captured key frames).
struct HarvestAp {
  uint8_t bssid[6] = {0};
  char ssid[33] = {0};
  uint8_t msgMask = 0;  // EAPOL messages 1..4
  bool pmkid = false;
};

// POD probe-request observation handed from the Wi-Fi task to Probe Intel.
struct ProbeHit {
  uint8_t mac[6];
  int8_t rssi;
  uint8_t ssidLen;
  char ssid[33];
};

struct ProbeSsidEntry {
  String ssid;
  uint32_t probes = 0;
  int32_t rssi = -127;
  uint8_t lastMac[6] = {0};
  uint8_t macs[kProbeMacsPerSsid][6] = {{0}};
  int macCount = 0;
  bool macOverflow = false;
};

// One AP tracked by Karma Watch: the distinct SSIDs a single BSSID claims.
struct KarmaEntry {
  uint8_t bssid[6] = {0};
  uint8_t channel = 0;
  int32_t rssi = -127;
  String ssids[kKarmaSsidsPerAp];
  int ssidCount = 0;
  bool overflow = false;
  bool suspicious = false;
  bool logged = false;
};

// Minimal POD beacon observation (BSSID only) for Beacon Watch.
struct BssidHit {
  uint8_t bssid[6];
  uint8_t channel;
  int8_t rssi;
};

// Minimal POD link observation for Swarm Topology Map.
struct TopoHit {
  uint8_t type;  // 0 = Station->AP Data, 1 = Probe Request, 2 = Beacon/ProbeResp
  uint8_t clientMac[6];
  uint8_t bssid[6];
  int8_t rssi;
  uint8_t channel;
  bool isOpen;
  uint8_t security;
  char ssid[33];
};

struct BleEntry {
  String name;
  String address;
  String serviceUuids;
  String manufacturerDataHex;
  int32_t rssi = -127;
  int32_t txPower = 0;
  int32_t manufacturerId = -1;
  uint16_t advertisementBytes = 0;
  uint8_t addressType = 0;
  bool hasTxPower = false;
  bool connectable = false;
  bool scannable = false;
  bool flipperLike = false;
};

// Declarations so feature tabs can use the short forms. Definitions that take
// default arguments omit those defaults in the .ino body.
void copyMac(uint8_t* dest, const volatile uint8_t* src);
String macToString(const uint8_t* mac);
String macToString(const volatile uint8_t* mac);
String bytesToHex(const std::string& data, size_t maximumBytes = 16);
void drawButton(int x, int y, int w, int h, const String& label,
                uint16_t outline = kAccent);
void drawHeader(const String& title, const String& detail = "");
void logMemory(const char* stage);
void showRadioError(const char* message);
void shutdownWifi();
bool ensureWifiStation(bool releaseBle = true);
bool ensureBleReady(bool needsWifi);
void releaseBleMemory();

void configureBleScan(NimBLEScan* scan, NimBLEScanCallbacks* callbacks,
                      bool active, uint16_t interval, uint16_t window,
                      uint8_t maxResults);
bool radioSchedulerBegin(RadioScheduler& s);
void radioSchedulerTick(RadioScheduler& s);
void radioSchedulerEnd(RadioScheduler& s);

void wardriveResetDedup();
void closeWardriveCsv();
void flushWardriveCsv();

void startSpectrogram();
void stopSpectrogram();
void updateSpectrogram();
void drawSpectrogram();
void cycleSpectrogramMode();
void handleSpectrogramBarTouch(int touchedIdx);
bool exportSpectrogramToSd();
void spectrogramLockStep(int dir);
void spectrogramCycleBand();
void spectrogramToggleHop();
void spectrogramLockToChannel(uint8_t ch);

extern int wifi6Page;
int wifi6PageCount();
void clearWifi6Intel();
void startWifi6Intel();
void stopWifi6Intel();
void updateWifi6Intel();
void drawWifi6Intel();
bool exportWifi6IntelToSd();
void wifi6ProcessHit(const Wifi6Hit& hit);

extern int deauthForensicsPage;
int deauthForensicsPageCount();
void clearDeauthForensics();
void startDeauthForensics();
void stopDeauthForensics();
void updateDeauthForensics();
void drawDeauthForensics();
bool exportDeauthForensicsToSd();
void deauthProcessHit(const DeauthHit& hit);

void openFilesManager();
bool scanSdFiles();
void linkStreamFileList();
void linkStreamFileData(uint8_t index);
void linkDeleteFile(uint8_t index);
void linkAbortFileStream();

// Network Tools types precede Arduino-generated function prototypes.
enum class NetJob { None, Join, Hosts, Ports, Cameras, Printers, Sip, Upnp };
enum class NetStage { Idle, ArpSend, ArpWait, Probe, Connecting, Sending, Reading, SipWait, SsdpWait };
struct NetHost { uint32_t ip; uint8_t mac[6]; };
struct NetResult { uint32_t ip; uint16_t port; char kind[16]; char detail[160]; };
struct NetArpCall {
  tcpip_api_call_data call;
  uint32_t ip; bool send; bool found; uint8_t mac[6];
};

struct NetSummary {
  NetJob job = NetJob::None;
  char status[96] = {}, ssid[33] = {};
  uint32_t ip = 0, mask = 0, checked = 0, timeouts = 0, refused = 0, errors = 0;
  bool limited = false, subnetLimited = false, hostsLimited = false;
};
