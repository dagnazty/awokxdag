// Shared declarations for the AWOKxDAG firmware. Included once by the main
// sketch; the feature tabs (deauth/handshake/sniffer/beacon/portal/gps/input)
// are concatenated into the same translation unit, so all globals defined in
// the main sketch and the tabs are visible everywhere without extern churn.
#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SD.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <SPI.h>
#include <WiFi.h>
#include <XPT2046_Touchscreen.h>
#include <TinyGPSPlus.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <string>

#include "board_pins.h"
#include "boot_screen_data.h"

constexpr int kScreenWidth = 240;
constexpr int kScreenHeight = 320;
constexpr int kHeaderHeight = 42;
constexpr int kFooterTop = 278;
constexpr int kMaxResults = 24;
constexpr int kVisibleRows = 10;
constexpr int kMaxSaved = 10;
constexpr uint32_t kBleScanMs = 5000;
constexpr uint32_t kBootScreenMs = 1800;
constexpr uint32_t kSdClockHz = 10000000;
constexpr char kSdDirectory[] = "/awokxdag";
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
    36,  40,  44,  48,  52,  56,  60,  64,  100, 104, 108, 112, 116,
    120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165};
constexpr int kDeauthHopChannelCount =
    static_cast<int>(sizeof(kDeauthHopChannels));
constexpr int kMaxDeauthTargets = 8;
constexpr char kVersion[] = "1.1.0";
constexpr char kAuthor[] = "dag nazty";
constexpr char kHandshakePcapPath[] = "/awokxdag/latest_handshake.pcap";
constexpr uint32_t kHandshakeRedrawMs = 500;
constexpr uint32_t kHandshakePulseMs = 2000;
constexpr int kCaptureSlotBytes = 256;
constexpr int kCaptureQueueSlots = 24;
constexpr char kClientCsvPath[] = "/awokxdag/latest_clients.csv";
constexpr int kMaxClients = 24;
constexpr int kSnifferQueueSlots = 32;
constexpr uint32_t kClientHopIntervalMs = 300;
constexpr uint32_t kClientRedrawMs = 700;
constexpr uint32_t kBeaconBurstIntervalMs = 20;
constexpr uint32_t kBeaconRedrawMs = 500;
constexpr int kBeaconsPerBurst = 5;
constexpr uint8_t kBeaconChannels[] = {1, 6, 11};
constexpr int kBeaconChannelCount = static_cast<int>(sizeof(kBeaconChannels));
constexpr char kPortalSsid[] = "Free_WiFi";
constexpr char kPortalCredsPath[] = "/awokxdag/portal_creds.csv";
constexpr uint32_t kPortalRedrawMs = 1000;
constexpr char kWardriveCsvPath[] = "/awokxdag/wardrive.csv";
constexpr int kMaxWardriveMacs = 512;
constexpr uint32_t kWardriveRedrawMs = 800;
constexpr int kBleHitQueueSlots = 24;

// Security Audit: passive beacon-IE posture report (encryption tier, PMF, WPS).
constexpr char kSecurityAuditCsvPath[] = "/awokxdag/security_audit.csv";
constexpr int kMaxAudit = 24;
constexpr int kAuditHitQueueSlots = 24;
constexpr uint32_t kAuditHopIntervalMs = 300;
constexpr uint32_t kAuditRedrawMs = 700;

// BLE Trackers: passive AirTag/Find My, Tile, Samsung SmartTag detection.
constexpr char kTrackerCsvPath[] = "/awokxdag/ble_trackers.csv";
constexpr int kMaxTrackers = 24;
constexpr int kTrackerHitQueueSlots = 32;
constexpr uint32_t kTrackerRedrawMs = 700;
// A tracker seen over a span longer than this (with repeat sightings) while you
// move is flagged as potentially following you.
constexpr uint32_t kTrackerFollowMs = 45000;
constexpr uint32_t kTrackerMinSightings = 4;

// Harvester: all-channel passive EAPOL/PMKID collection (no deauth).
constexpr char kHarvestPcapPath[] = "/awokxdag/harvest.pcap";
constexpr char kHarvestPmkidPath[] = "/awokxdag/harvest_pmkid.txt";
constexpr int kMaxHarvestAp = 24;
constexpr int kMaxHarvestSeen = 64;  // beacons written once per BSSID
constexpr uint32_t kHarvestHopIntervalMs = 300;
constexpr uint32_t kHarvestRedrawMs = 700;

// Probe Intel: directed probe-request SSID aggregation.
constexpr char kProbeIntelCsvPath[] = "/awokxdag/probe_intel.csv";
constexpr int kMaxProbeSsids = 24;
constexpr int kProbeMacsPerSsid = 8;
constexpr int kProbeHitQueueSlots = 32;
constexpr uint32_t kProbeHopIntervalMs = 300;
constexpr uint32_t kProbeRedrawMs = 700;

// Karma Watch: one BSSID answering many SSIDs (WiFi Pineapple / Karma / MANA).
constexpr char kKarmaLogCsvPath[] = "/awokxdag/karma_log.csv";
constexpr int kMaxKarmaAps = 24;
constexpr int kKarmaSsidsPerAp = 6;
constexpr int kKarmaHitQueueSlots = 24;
constexpr int kKarmaSsidThreshold = 3;  // distinct SSIDs => suspicious
constexpr uint32_t kKarmaHopIntervalMs = 300;
constexpr uint32_t kKarmaRedrawMs = 700;

// Beacon Watch: beacon-flood / fake-AP detection by BSSID diversity per window.
constexpr char kBeaconWatchLogCsvPath[] = "/awokxdag/beacon_flood_log.csv";
constexpr int kBeaconWatchWindowSet = 64;   // distinct BSSIDs counted per window
constexpr int kBeaconWatchHitQueueSlots = 32;
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

constexpr uint16_t kBackground = ILI9341_BLACK;
constexpr uint16_t kPanel = 0x1082;
constexpr uint16_t kAccent = ILI9341_CYAN;
constexpr uint16_t kMuted = 0x7BEF;
constexpr uint16_t kGood = ILI9341_GREEN;
constexpr uint16_t kWarn = ILI9341_YELLOW;
constexpr uint16_t kBad = ILI9341_RED;

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
  kAuthFlood
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
};

// Declarations for the three functions with default arguments, so callers in
// the feature tabs can use the short forms. Definitions (in the main sketch)
// omit the defaults.
String bytesToHex(const std::string& data, size_t maximumBytes = 16);
void drawButton(int x, int y, int w, int h, const String& label,
                uint16_t outline = kAccent);
void drawHeader(const String& title, const String& detail = "");
