// Arduino IDE selection.
#if !defined(AWOK_DUAL_C5_TOUCH) && !defined(PANCAKE_C5) && \
    !defined(AWOK_DUAL_C5_MINI) && \
    !defined(AWOK_DUAL_C5_BRIDGE) && \
    !defined(AWOK_DUAL_ESP32_TOUCH_V1) && !defined(AWOK_DUAL_ESP32_TOUCH_V2) && \
    !defined(AWOK_DUAL_ESP32_TOUCH_V3) && \
    !defined(AWOK_DUAL_ESP32_MINI_V1) && !defined(AWOK_DUAL_ESP32_MINI_V2) && \
    !defined(AWOK_DUAL_ESP32_MINI_V3) && \
    !defined(AWOK_DUAL_ESP32_TOUCH_BRIDGE_V1) && \
    !defined(AWOK_DUAL_ESP32_TOUCH_BRIDGE_V2) && \
    !defined(AWOK_DUAL_ESP32_TOUCH_BRIDGE_V3) && \
    !defined(AWOK_DUAL_ESP32_MINI_BRIDGE_V1) && \
    !defined(AWOK_DUAL_ESP32_MINI_BRIDGE_V2) && \
    !defined(AWOK_DUAL_ESP32_MINI_BRIDGE_V3)
//#define AWOK_DUAL_C5_TOUCH
//#define PANCAKE_C5
#define AWOK_DUAL_C5_MINI
//#define AWOK_DUAL_ESP32_TOUCH_V1
//#define AWOK_DUAL_ESP32_TOUCH_V2
//#define AWOK_DUAL_ESP32_TOUCH_V3
//#define AWOK_DUAL_ESP32_MINI_V1
//#define AWOK_DUAL_ESP32_MINI_V2
//#define AWOK_DUAL_ESP32_MINI_V3
#endif
#include "awok_common.h"

// The Wi-Fi driver refuses to transmit raw management frames it deems
// malformed, which includes deauthentication and disassociation frames.
// Overriding this sanity check lets the authorized deauth test inject them.
// Returning 0 tells the driver the frame is acceptable. Arduino IDE Verify
// needs -Wl,-z,muldefs so this symbol wins over libnet80211.a — run
// scripts/setup_arduino_ide.py after installing or updating the ESP32 core.
extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2,
                                                int32_t arg3) {
  return 0;
}

#ifdef AWOK_MINI_DISPLAY
AwokMiniDisplay display;
#elif defined(AWOK_HEADLESS)
AwokHeadlessDisplay display;  // no-op sink; the bridge has no screen
// No touch panel on the bridge; a stub keeps every touch.* call compiling and
// inert (touched() == false), so the input paths just never report a press.
struct AwokHeadlessTouch {
  bool begin() { return false; }
  void setRotation(uint8_t) {}
  bool touched() { return false; }
  TS_Point getPoint() { return TS_Point(0, 0, 0); }
} touch;
#elif defined(PANCAKE_DISPLAY)
AwokPancakeDisplay display(AwokPins::kDisplayDc, AwokPins::kDisplayCs,
                           AwokPins::kDisplayReset);
AwokCapTouch touch;  // FT6336 capacitive controller (I2C)
#else
AwokTouchDisplay display(AwokPins::kDisplayDc, AwokPins::kDisplayCs,
                         AwokPins::kDisplayReset);
XPT2046_Touchscreen touch(AwokPins::kTouchCs);
#endif

#if defined(AWOK_MINI_DISPLAY) || defined(AWOK_HEADLESS)
MiniResultTable<WifiEntry, kMaxWifiResults> wifiEntries;
MiniResultTable<BleEntry, kMaxBleResults> bleEntries;
MiniResultTable<ClientEntry, kMaxClients> clientEntries;
MiniResultTable<ProbeSsidEntry, kMaxProbeSsids> probeSsids;
MiniResultTable<KarmaEntry, kMaxKarmaAps> karmaAps;
size_t resultTableExternalBytes = 0;
bool resultTablesReady = false;
#else
WifiEntry wifiEntries[kMaxWifiResults];
BleEntry bleEntries[kMaxBleResults];
ClientEntry clientEntries[kMaxClients];
ProbeSsidEntry probeSsids[kMaxProbeSsids];
KarmaEntry karmaAps[kMaxKarmaAps];
#endif
WifiEntry savedEntries[kMaxSaved];
WifiEntry selectedWifi;
BleEntry selectedBle;
int wifiCount = 0;
int wifiPage = 0;
int savedCount = 0;
int bleCount = 0;
int blePage = 0;
View currentView = View::kHome;
View auditReturnView = View::kWifi;
int reconPage = 0;
int reconCategory = -1;  // -1 = category picker; tools return to their category
int monitorPage = 0;
int monitorCategory = -1;  // -1 group picker; preserve tool origin on return
int homePage = 0;
// Home overlay: the About page and the memory/radio error screens all want a
// single "any tap returns to the tiles" gesture. homePage stays the tile page.
bool homeOverlay = false;
int wifi6Page = 0;
int deauthForensicsPage = 0;
DeviceSettingsRecord deviceSettings = {};
bool backlightDimmed = false;
uint32_t lastActivityMs = 0;
bool lastSettingsWriteOk = true;
bool scanInProgress = false;
bool wifiScanContinuous = false;  // Wi-Fi Scan tool: keep scanning + merging
bool pktmonActive = false;      // packet monitor (state defined in pktmon.ino)
bool wpsScanActive = false;     // WPS scan (state defined in wps.ino)
bool rogueWatchActive = false;  // rogue-AP watch (state in roguewatch.ino)
bool hiddenRevealActive = false;  // hidden-SSID reveal (state in hidden.ino)
bool cameraActive = false;      // camera scan (state defined in cameras.ino)
bool bleDetectActive = false;   // BLE spam watch (state in bledetect.ino)
bool probeLureActive = false;   // PineAP-lite probe lure (state in probelure.ino)
bool securityAuditActive = false;  // security posture audit (securityaudit.ino)
bool trackerScanActive = false;    // BLE tracker scan (state in tracker.ino)
bool harvesterActive = false;      // handshake harvester (state in harvester.ino)
bool probeIntelActive = false;     // probe-request SSID map (probeintel.ino)
bool karmaWatchActive = false;     // Karma/Pineapple watch (karmawatch.ino)
bool beaconWatchActive = false;    // beacon-flood watch (state in beaconwatch.ino)
bool authFloodActive = false;      // auth/assoc flood watch (authflood.ino)
bool advancedWatchActive = false;  // combined Wi-Fi/BLE anomaly watch
bool locatorActive = false;        // RSSI fox-hunt (state in locator.ino)
bool fleetHuntActive = false;      // multi-node trilateration hunt (locator.ino)
bool topologyActive = false;       // live swarm mesh topology mapping (topologymap.ino)
bool bleIntelActive = false;       // BLE ecosystem intel & continuity decoder (bleintel.ino)
bool spectrogramActive = false;    // RF spectrogram & waterfall analyzer (spectrogram.ino)
bool wifi6IntelActive = false;      // 802.11ax OFDMA & BSS Color intel (wifi6intel.ino)
bool deauthForensicsActive = false; // targeted deauth & disassoc forensics (deauthforensics.ino)
// SD export status for the new recon tabs (read by input.ino, which is
// concatenated before those tabs, so the flags must live in the main sketch).
bool lastAuditCsvOk = false;
bool lastTrackerCsvOk = false;
bool lastProbeIntelCsvOk = false;
bool lastTopologyCsvOk = false;
bool lastBleIntelCsvOk = false;
bool lastSpectrogramCsvOk = false;
bool lastWifi6IntelCsvOk = false;
bool lastDeauthForensicsCsvOk = false;
bool sdReady = false;
bool lastSavedSdWriteOk = false;
bool lastScanSdWriteOk = false;
bool lastBleScanSdWriteOk = false;
uint32_t lastTouchMs = 0;
String auditStatus;
int16_t signalSamples[kSignalSampleCount] = {};
uint32_t signalSampleTimes[kSignalSampleCount] = {};
int signalSampleCount = 0;
int signalMisses = 0;
uint32_t lastSignalSampleMs = 0;
bool signalMonitorActive = false;
bool signalSdLogReady = false;

// Deauth detection (passive promiscuous monitor). Counters and last-hit fields
// are updated from the Wi-Fi driver task inside the promiscuous callback.
volatile uint32_t deauthFrameCount = 0;
volatile uint32_t disassocFrameCount = 0;
volatile uint32_t deauthEventsSinceDraw = 0;
volatile int lastDeauthRssi = 0;
volatile uint8_t lastDeauthChannel = 0;
volatile bool haveDeauthHit = false;
volatile uint8_t lastDeauthSource[6] = {0};
volatile uint8_t lastDeauthBssid[6] = {0};
bool deauthMonitorActive = false;
int deauthHopIndex = 0;
uint32_t lastDeauthHopMs = 0;
uint32_t lastDeauthDrawMs = 0;
uint32_t deauthMonitorStartMs = 0;
bool deauthLogReady = false;

// Deauth attack (active transmission against one or more target APs). The
// target set lets a dual-band network be covered by selecting its 2.4 GHz and
// 5 GHz BSSIDs together; the burst loop round-robins across them.
bool deauthAttackActive = false;
uint32_t deauthFramesSent = 0;
uint32_t deauthAttackStartMs = 0;
uint32_t lastDeauthBurstMs = 0;
uint32_t lastDeauthAttackDrawMs = 0;
DeauthTarget deauthTargets[kMaxDeauthTargets];
int deauthTargetCount = 0;
int deauthTargetCursor = 0;
View deauthAttackReturnView = View::kWifiAudit;

// Handshake / PMKID capture. The promiscuous callback fills captureQueue; the
// main loop drains it to a pcap file on SD. EAPOL frames are matched against
// the locked target (deauthTargets[0]); an optional deauth pulse nudges clients
// into reauthenticating so the 4-way handshake is observed.
CaptureFrame captureQueue[kCaptureQueueSlots];
volatile int captureHead = 0;
volatile int captureTail = 0;
File captureFile;
bool captureFileOpen = false;
bool handshakeCaptureActive = false;
bool handshakePulseEnabled = true;
uint8_t handshakeChannel = 0;
uint8_t handshakeTargetBssid[6] = {0};
String handshakeTargetSsid;
String handshakeCapturePath;
uint32_t handshakeEapolCount = 0;
uint32_t handshakeBeaconCount = 0;
uint32_t handshakeFramesWritten = 0;
uint8_t handshakeMsgSeen = 0;  // bitmask of EAPOL messages 1..4
bool handshakePmkidSeen = false;
uint32_t handshakeStartMs = 0;
uint32_t lastHandshakeDrawMs = 0;
uint32_t lastHandshakePulseMs = 0;

// Client / probe-request sniffer. Callback enqueues SnifferHit records; the
// main loop merges them into the clientEntries table (String work off-task).
int clientCount = 0;
SnifferHit snifferQueue[kSnifferQueueSlots];
volatile int snifferHead = 0;
volatile int snifferTail = 0;
bool clientSnifferActive = false;
int clientHopIndex = 0;
uint32_t lastClientHopMs = 0;
uint32_t lastClientDrawMs = 0;
uint32_t clientSnifferStartMs = 0;
bool lastClientCsvOk = false;

// Beacon / SSID flood. Broadcasts fake AP beacons with obviously-test SSIDs.
bool beaconFloodActive = false;
uint32_t beaconFramesSent = 0;
uint32_t beaconFloodStartMs = 0;
uint32_t lastBeaconBurstMs = 0;
uint32_t lastBeaconDrawMs = 0;
int beaconChannelIndex = 0;
int beaconNameIndex = 0;
String lastBeaconSsid;

// Evil / captive portal. Opens a SoftAP, redirects all DNS to the device, and
// serves a login page; submitted fields are logged to SD for the audit report.
WebServer portalServer(80);
DNSServer portalDns;
IPAddress portalIp;
bool evilPortalActive = false;
uint32_t portalCredsCount = 0;
uint32_t evilPortalStartMs = 0;
uint32_t lastPortalDrawMs = 0;
String lastPortalCred;
bool portalLogReady = false;

// GPS wardriving. Async Wi-Fi scans are logged, once per BSSID, to a
// WiGLE-compatible CSV whenever a GPS fix is available.
bool wardriveActive = false;
uint32_t wardriveNetworks = 0;
uint32_t wardriveScans = 0;
uint32_t wardriveStartMs = 0;
uint32_t lastWardriveDrawMs = 0;
bool wardriveCsvReady = false;
String g_wardriveCsvPath;  // this run's CSV file (a new one is made each start)
File g_wardriveFile;       // persistent open file handle during wardrive
uint8_t wardriveBloom[kWardriveBloomFilterBytes] = {0};  // fast O(1) MAC deduplication bitset
uint8_t wardriveMacs[kMaxWardriveMacs][6];
int wardriveMacCount = 0;
uint32_t wardriveBleCount = 0;
BleHit bleHitQueue[kBleHitQueueSlots];
volatile int bleHitHead = 0;
volatile int bleHitTail = 0;

// Link Mode (ESP-NOW pairing + Split Wardrive). Cross-tab globals must be
// defined here (Arduino auto-prototypes functions but not variables, and
// link.ino is concatenated after input.ino which references stop guards).
bool linkEspNowReady = false;      // esp_now_init() succeeded this session
bool remoteActive = false;         // bridge (BLE phone) control listening
LinkState linkState = kLinkOff;
bool linkRoleMaster = true;        // lower MAC wins; set at pairing
bool linkConfirmedLocal = false;   // this unit pressed Confirm
uint8_t linkSelfMac[6] = {0};
uint8_t linkPeerMac[6] = {0};
bool linkPeerValid = false;        // heard a peer HELLO this pairing
bool linkPeerDualBand = false;     // peer advertised 5 GHz (from HELLO flags)
uint16_t linkCode = 0;             // 4-digit visual confirm code
uint32_t linkSessionId = 0;
int32_t linkClockOffset = 0;       // masterMillis - localMillis (slave only)
uint32_t linkPartnerNetworks = 0;
uint32_t linkPartnerBle = 0;
uint8_t linkPartnerChannel = 0;
int8_t linkPartnerRssi = -127;
uint32_t linkPartnerLastSeenMs = 0;
bool linkWardriveActive = false;
int linkChannelCursor = 0;         // round-robin index into the assigned half
bool linkInWindow = false;         // currently parked on the rendezvous channel
uint32_t linkWindowBeat = 0;       // beat number of the window we last opened
uint32_t lastLinkHelloMs = 0;
uint32_t lastLinkSyncMs = 0;
uint32_t lastLinkTelemMs = 0;
uint32_t lastLinkWardriveDrawMs = 0;
bool linkWindowScanStopped = false;  // aborted the async scan for this window

// ---- Fleet Wardrive globals --------------------------------------------
bool fleetActive = false;          // in a fleet session (supersedes 1:1 pair)
bool fleetListening = false;       // armed to auto-join a fleet invite
bool fleetCoordinator = false;     // this node coordinates + aggregates
uint8_t fleetCoordinatorMac[6] = {0}; // authority pinned by explicit Start/Join
uint32_t fleetSessionId = 0;
uint16_t fleetCode = 0;            // short join code
FleetMember fleetMembers[kFleetMaxNodes];
int fleetMemberCount = 0;
int fleetMyIndex = 0;              // my slot in the roster
int fleetCoordIndex = 0;
int fleetBleNodeIndex = -1;        // roster index that scans BLE (-1 = none)
int fleetSinkIndex = -1;          // roster index that owns the SD CSV (-1 = none)
uint32_t fleetRowSeq = 0;         // my outbound row sequence (worker)
uint32_t fleetAckedSeq = 0;       // coordinator ack of my rows (worker)
bool fleetWardriveOn = false;     // coordinator intent: fleet wardrive running
bool fleetBleScanRunning = false; // this node is the fleet's BLE scanner
bool fleetMenuOpen = false;       // Link screen is showing the fleet menu
uint32_t lastFleetInviteMs = 0;
uint32_t lastFleetRosterMs = 0;
// Roster handed from the ESP-NOW recv callback to updateLink (larger than a
// LinkPacket, so it rides its own single-slot mailbox instead of the ring).
volatile bool fleetRosterPending = false;
FleetRoster fleetPendingRoster;
LinkQueueItem linkPacketQueue[kLinkPacketQueueSlots];
volatile int linkPacketHead = 0;
volatile int linkPacketTail = 0;

String clipped(const String& value, size_t maxChars) {
  if (value.length() <= maxChars) return value;
  return value.substring(0, maxChars - 1) + "~";
}

const char* authShortLabel(wifi_auth_mode_t auth) {
  return auth == WIFI_AUTH_OPEN ? "open" : "lock";
}

const char* authLongLabel(wifi_auth_mode_t auth) {
  switch (auth) {
    case WIFI_AUTH_OPEN:
      return "Open";
    case WIFI_AUTH_WEP:
      return "WEP";
    case WIFI_AUTH_WPA_PSK:
      return "WPA-Personal";
    case WIFI_AUTH_WPA2_PSK:
      return "WPA2-Personal";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "WPA/WPA2 mixed";
    case WIFI_AUTH_ENTERPRISE:
      return "WPA2-Enterprise";
    case WIFI_AUTH_WPA3_PSK:
    case WIFI_AUTH_WPA3_EXT_PSK:
      return "WPA3-Personal";
    case WIFI_AUTH_WPA2_WPA3_PSK:
    case WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE:
      return "WPA2/WPA3 mixed";
    case WIFI_AUTH_OWE:
      return "Enhanced Open (OWE)";
    case WIFI_AUTH_WPA3_ENT_192:
    case WIFI_AUTH_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
      return "WPA3-Enterprise";
    case WIFI_AUTH_WAPI_PSK:
      return "WAPI-Personal";
    default:
      return "Other/unknown";
  }
}

const char* bandLabel(int channel) {
  return channel <= 14 ? "2.4" : "5";
}

const char* signalLabel(int32_t rssi) {
  if (rssi >= -55) return "Excellent";
  if (rssi >= -67) return "Good";
  if (rssi >= -75) return "Weak";
  return "Poor";
}

String csvField(String value) {
  value.replace("\r", " ");
  value.replace("\n", " ");
  value.replace("\"", "\"\"");
  return "\"" + value + "\"";
}

const char* bleAddressTypeLabel(uint8_t type) {
  switch (type) {
    case 0:
      return "public";
    case 1:
      return "random";
    case 2:
      return "public identity";
    case 3:
      return "random identity";
    default:
      return "unknown";
  }
}

String bytesToHex(const std::string& data, size_t maximumBytes) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  String result;
  const size_t bytes = min(data.length(), maximumBytes);
  result.reserve(bytes * 2 + (data.length() > maximumBytes ? 3 : 0));
  for (size_t i = 0; i < bytes; ++i) {
    const uint8_t value = static_cast<uint8_t>(data[i]);
    result += kHex[value >> 4];
    result += kHex[value & 0x0F];
  }
  if (data.length() > maximumBytes) result += "...";
  return result;
}

bool initializeSdCard() {
  digitalWrite(AwokPins::kDisplayCs, HIGH);
  if (AwokPins::kTouchCs >= 0) digitalWrite(AwokPins::kTouchCs, HIGH);
  digitalWrite(AwokPins::kSdCs, HIGH);

  if (!SD.begin(AwokPins::kSdCs, SPI, kSdClockHz) ||
      SD.cardType() == CARD_NONE) {
    sdReady = false;
    Serial.println("[sd] card not available");
    return false;
  }
  sdReady = true;
  if (!SD.exists(kSdDirectory)) SD.mkdir(kSdDirectory);
  Serial.println("[sd] card ready at /awokxdag");
  return true;
}

bool ensureSdCard() {
  if (sdReady) return true;
  return initializeSdCard();
}

bool writeNetworkCsv(const char* path, const WifiEntry* entries, int count,
                     bool includeScanUptime) {
  if (!ensureSdCard()) return false;

  const String temporaryPath = String(path) + ".tmp";
  SD.remove(temporaryPath.c_str());
  File file = SD.open(temporaryPath.c_str(), FILE_WRITE);
  if (!file) {
    sdReady = false;
    Serial.printf("[sd] could not open %s\n", temporaryPath.c_str());
    return false;
  }

  if (includeScanUptime) file.print("scan_uptime_ms,");
  file.print("ssid,bssid,rssi,channel,band_ghz,authentication");
  if (includeScanUptime) file.print(",latitude,longitude,altitude_m");
  file.println();
  const uint32_t scanUptime = millis();
  for (int i = 0; i < count; ++i) {
    if (includeScanUptime) {
      file.print(scanUptime);
      file.print(',');
    }
    file.print(csvField(entries[i].ssid));
    file.print(',');
    file.print(csvField(entries[i].bssid));
    file.print(',');
    file.print(entries[i].rssi);
    file.print(',');
    file.print(entries[i].channel);
    file.print(',');
    file.print(bandLabel(entries[i].channel));
    file.print(',');
    file.print(csvField(authLongLabel(entries[i].auth)));
    if (includeScanUptime) file.print(gpsCsvFields());
    file.println();
  }
  file.flush();
  const bool writeOk = file.getWriteError() == 0;
  file.close();

  if (!writeOk) {
    SD.remove(temporaryPath.c_str());
    Serial.printf("[sd] write failed for %s\n", path);
    return false;
  }
  SD.remove(path);
  if (!SD.rename(temporaryPath.c_str(), path)) {
    Serial.printf("[sd] rename failed for %s\n", path);
    return false;
  }
  Serial.printf("[sd] wrote %d row(s) to %s\n", count, path);
  return true;
}

bool exportSavedNetworksToSd() {
  return writeNetworkCsv(kSavedCsvPath, savedEntries, savedCount, false);
}

bool exportWifiScanToSd() {
  return writeNetworkCsv(kScanCsvPath, wifiEntries, wifiCount, true);
}

bool exportBleScanToSd() {
  if (!ensureSdCard()) return false;

  const String temporaryPath = String(kBleScanCsvPath) + ".tmp";
  SD.remove(temporaryPath.c_str());
  File file = SD.open(temporaryPath.c_str(), FILE_WRITE);
  if (!file) {
    sdReady = false;
    Serial.printf("[sd] could not open %s\n", temporaryPath.c_str());
    return false;
  }

  file.println(
      "scan_uptime_ms,name,address,address_type,rssi,tx_power,connectable,"
      "scannable,advertisement_bytes,manufacturer_id,manufacturer_data_hex,"
      "service_uuids,identification_hint");
  const uint32_t scanUptime = millis();
  for (int i = 0; i < bleCount; ++i) {
    file.print(scanUptime);
    file.print(',');
    file.print(csvField(bleEntries[i].name));
    file.print(',');
    file.print(csvField(bleEntries[i].address));
    file.print(',');
    file.print(csvField(bleAddressTypeLabel(bleEntries[i].addressType)));
    file.print(',');
    file.print(bleEntries[i].rssi);
    file.print(',');
    if (bleEntries[i].hasTxPower) file.print(bleEntries[i].txPower);
    file.print(',');
    file.print(bleEntries[i].connectable ? "true" : "false");
    file.print(',');
    file.print(bleEntries[i].scannable ? "true" : "false");
    file.print(',');
    file.print(bleEntries[i].advertisementBytes);
    file.print(',');
    if (bleEntries[i].manufacturerId >= 0) {
      file.printf("0x%04lX", static_cast<long>(bleEntries[i].manufacturerId));
    }
    file.print(',');
    file.print(csvField(bleEntries[i].manufacturerDataHex));
    file.print(',');
    file.print(csvField(bleEntries[i].serviceUuids));
    file.print(',');
    file.println(csvField((bleEntries[i].flipperLike ? "Flipper-like service" : "")));
  }
  file.flush();
  const bool writeOk = file.getWriteError() == 0;
  file.close();
  if (!writeOk) {
    SD.remove(temporaryPath.c_str());
    Serial.printf("[sd] write failed for %s\n", kBleScanCsvPath);
    return false;
  }
  SD.remove(kBleScanCsvPath);
  if (!SD.rename(temporaryPath.c_str(), kBleScanCsvPath)) {
    Serial.printf("[sd] rename failed for %s\n", kBleScanCsvPath);
    return false;
  }
  Serial.printf("[sd] wrote %d row(s) to %s\n", bleCount,
                kBleScanCsvPath);
  return true;
}

bool sameNetwork(const WifiEntry& first, const WifiEntry& second) {
  if (first.bssid.length() && second.bssid.length()) {
    return first.bssid.equalsIgnoreCase(second.bssid);
  }
  return first.ssid == second.ssid && first.channel == second.channel;
}

int savedIndex(const WifiEntry& entry) {
  for (int i = 0; i < savedCount; ++i) {
    if (sameNetwork(entry, savedEntries[i])) return i;
  }
  return -1;
}

bool isSaved(const WifiEntry& entry) { return savedIndex(entry) >= 0; }

void loadSavedNetworks() {
  savedCount = 0;
  nvs_handle_t handle;
  const esp_err_t nvsError = nvs_open("awokxdag", NVS_READONLY, &handle);
  if (nvsError != ESP_OK) {
    if (nvsError == ESP_ERR_NVS_NOT_FOUND) {
      Serial.println("[saved] no saved networks yet");
    } else {
      Serial.printf("[saved] NVS open failed: %s (0x%x)\n",
                    esp_err_to_name(nvsError), unsigned(nvsError));
      logMemory("saved NVS open failed");
    }
    return;
  }
  nvs_close(handle);
  Preferences preferences;
  if (!preferences.begin("awokxdag", true)) {
    Serial.println("[saved] could not open NVS");
    return;
  }

  if (preferences.isKey("networks")) {
    SavedNetworkSnapshot snapshot = {};
    bool valid = preferences.getBytesLength("networks") == sizeof(snapshot) &&
                 preferences.getBytes("networks", &snapshot, sizeof(snapshot)) ==
                     sizeof(snapshot) &&
                 snapshot.version == 1 && snapshot.count <= kMaxSaved;
    for (uint32_t i = 0; valid && i < snapshot.count; ++i) {
      valid = memchr(snapshot.entries[i].ssid, 0,
                     sizeof(snapshot.entries[i].ssid)) != nullptr &&
              memchr(snapshot.entries[i].bssid, 0,
                     sizeof(snapshot.entries[i].bssid)) != nullptr;
    }
    if (valid) {
      savedCount = static_cast<int>(snapshot.count);
      for (int i = 0; i < savedCount; ++i) {
        const SavedNetworkRecord& entry = snapshot.entries[i];
        savedEntries[i].ssid = entry.ssid;
        savedEntries[i].bssid = entry.bssid;
        savedEntries[i].rssi = entry.rssi;
        savedEntries[i].channel = entry.channel;
        savedEntries[i].auth = static_cast<wifi_auth_mode_t>(entry.auth);
      }
    } else {
      Serial.println("[saved] invalid NVS snapshot");
    }
    preferences.end();
    Serial.printf("[saved] loaded %d network(s)\n", savedCount);
    return;
  }

  // Read the original per-field format until the next successful save migrates
  // it. Keep these keys intact in case that first snapshot write fails.
  savedCount = min(static_cast<int>(preferences.getUChar("count", 0)),
                   kMaxSaved);
  for (int i = 0; i < savedCount; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "s%d", i);
    savedEntries[i].ssid = preferences.getString(key, "");
    snprintf(key, sizeof(key), "b%d", i);
    savedEntries[i].bssid = preferences.getString(key, "");
    snprintf(key, sizeof(key), "r%d", i);
    savedEntries[i].rssi = preferences.getInt(key, -127);
    snprintf(key, sizeof(key), "c%d", i);
    savedEntries[i].channel = preferences.getInt(key, 0);
    snprintf(key, sizeof(key), "a%d", i);
    savedEntries[i].auth = static_cast<wifi_auth_mode_t>(
        preferences.getUChar(key, WIFI_AUTH_OPEN));
  }
  preferences.end();
  Serial.printf("[saved] loaded %d network(s)\n", savedCount);
}

bool writeSavedNetworks() {
  lastSavedSdWriteOk = false;
  if (savedCount < 0 || savedCount > kMaxSaved) return false;
  SavedNetworkSnapshot snapshot = {};
  snapshot.version = 1;
  snapshot.count = savedCount;
  for (int i = 0; i < savedCount; ++i) {
    SavedNetworkRecord& entry = snapshot.entries[i];
    if (savedEntries[i].ssid.length() >= sizeof(entry.ssid) ||
        savedEntries[i].bssid.length() >= sizeof(entry.bssid)) return false;
    memcpy(entry.ssid, savedEntries[i].ssid.c_str(),
           savedEntries[i].ssid.length() + 1);
    memcpy(entry.bssid, savedEntries[i].bssid.c_str(),
           savedEntries[i].bssid.length() + 1);
    entry.rssi = savedEntries[i].rssi;
    entry.channel = savedEntries[i].channel;
    entry.auth = static_cast<uint8_t>(savedEntries[i].auth);
  }
  Preferences preferences;
  if (!preferences.begin("awokxdag", false)) {
    return false;
  }
  const bool written = preferences.putBytes("networks", &snapshot,
                                            sizeof(snapshot)) == sizeof(snapshot);
  preferences.end();
  if (!written) return false;
  lastSavedSdWriteOk = exportSavedNetworksToSd();
  return true;
}

bool toggleSavedNetwork(const WifiEntry& entry) {
  const int existing = savedIndex(entry);
  if (existing >= 0) {
    const WifiEntry removed = savedEntries[existing];
    for (int i = existing; i < savedCount - 1; ++i) {
      savedEntries[i] = savedEntries[i + 1];
    }
    --savedCount;
    if (writeSavedNetworks()) {
      Serial.printf("[saved] removed %s\n", entry.ssid.c_str());
      recordFirmwareAudit("configuration", "saved_network_remove", "success",
                          "ssid=" + entry.ssid + "; bssid=" + entry.bssid);
      return true;
    }
    for (int i = savedCount; i > existing; --i) {
      savedEntries[i] = savedEntries[i - 1];
    }
    savedEntries[existing] = removed;
    ++savedCount;
    recordFirmwareAudit("configuration", "saved_network_remove", "failed",
                        "persistent storage write failed; bssid=" +
                            entry.bssid);
    return false;
  }

  if (savedCount >= kMaxSaved) {
    Serial.println("[saved] list full");
    recordFirmwareAudit("configuration", "saved_network_add", "rejected",
                        "saved network limit reached; bssid=" + entry.bssid);
    return false;
  }
  savedEntries[savedCount++] = entry;
  if (writeSavedNetworks()) {
    Serial.printf("[saved] added %s\n", entry.ssid.c_str());
    recordFirmwareAudit("configuration", "saved_network_add", "success",
                        "ssid=" + entry.ssid + "; bssid=" + entry.bssid);
    return true;
  }
  --savedCount;
  recordFirmwareAudit("configuration", "saved_network_add", "failed",
                      "persistent storage write failed; bssid=" + entry.bssid);
  return false;
}

void drawButton(int x, int y, int w, int h, const String& label,
                uint16_t outline) {
#ifdef AWOK_MINI_DISPLAY
  display.button(x, y, w, h, label.c_str(), outline);
  return;
#endif
  x = scaleX(x); y = scaleY(y); w = scaleX(w); h = scaleY(h);
  display.drawRoundRect(x, y, w, h, 6, outline);
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setTextSize(2);
  int16_t x1;
  int16_t y1;
  uint16_t textWidth;
  uint16_t textHeight;
  display.getTextBounds(label, 0, 0, &x1, &y1, &textWidth, &textHeight);
  display.setCursor(x + (w - textWidth) / 2, y + (h - textHeight) / 2);
  display.print(label);
}

void drawHeader(const String& title, const String& detail) {
#ifdef AWOK_MINI_DISPLAY
  display.header(title.c_str(), detail.c_str());
  return;
#endif
  display.fillRect(0, 0, kScreenWidth, kHeaderHeight, kPanel);
  display.setTextColor(kAccent, kPanel);
  display.setTextSize(2);
  display.setCursor(scaleX(8), scaleY(7));
  display.print(title);
  if (detail.length()) {
    display.setTextColor(ILI9341_WHITE, kPanel);
    display.setTextSize(1);
    display.setCursor(scaleX(8), scaleY(29));
    display.print(clipped(detail, 34));
  }
  // GPS fix indicator: green = fix, yellow = data but no fix, dim = no data.
  const uint16_t gpsColor =
      gpsHasFix() ? kGood
                  : (gpsCharsProcessed() > 10 ? kWarn : kMuted);
  display.fillCircle(scaleX(224), scaleY(10), 4, gpsColor);
  display.setTextColor(gpsColor, kPanel);
  display.setTextSize(1);
  display.setCursor(scaleX(202), scaleY(7));
  display.print("GPS");
}

void drawFooter(const char* leftLabel, const char* rightLabel) {
  display.fillRect(0, kFooterTop, kScreenWidth, kScreenHeight - kFooterTop,
                   kBackground);
  drawButton(4, 284, 112, 30, leftLabel, kMuted);
  drawButton(124, 284, 112, 30, rightLabel, kAccent);
}

void drawSmallButton(int x, int y, int w, int h, const String& label,
                     uint16_t outline) {
#ifdef AWOK_MINI_DISPLAY
  display.button(x, y, w, h, label.c_str(), outline);
  return;
#endif
  x = scaleX(x); y = scaleY(y); w = scaleX(w); h = scaleY(h);
  display.drawRoundRect(x, y, w, h, 5, outline);
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setTextSize(1);
  int16_t x1;
  int16_t y1;
  uint16_t textWidth;
  uint16_t textHeight;
  display.getTextBounds(label, 0, 0, &x1, &y1, &textWidth, &textHeight);
  display.setCursor(x + (w - textWidth) / 2, y + (h - textHeight) / 2);
  display.print(label);
}

// Uses the small (size-1) button so longer labels like "Wardrive"/"Capture" fit
// within the 72px footer buttons.
void drawThreeButtonFooter(const char* leftLabel, const char* middleLabel,
                           const char* rightLabel) {
  display.fillRect(0, kFooterTop, kScreenWidth, kScreenHeight - kFooterTop,
                   kBackground);
  drawSmallButton(4, 284, 72, 30, leftLabel, kMuted);
  drawSmallButton(84, 284, 72, 30, middleLabel, kAccent);
  drawSmallButton(164, 284, 72, 30, rightLabel, kAccent);
}

void drawFourButtonFooter(const String& first, const String& second,
                          const String& third, const String& fourth) {
  display.fillRect(0, kFooterTop, kScreenWidth, kScreenHeight - kFooterTop,
                   kBackground);
  drawSmallButton(2, 284, 56, 30, first, kMuted);
  drawSmallButton(62, 284, 56, 30, second, kAccent);
  drawSmallButton(122, 284, 56, 30, third, kAccent);
  drawSmallButton(182, 284, 56, 30, fourth, kBad);
}

void drawFiveButtonFooter(const String& a, const String& b, const String& c,
                          const String& d, const String& e) {
  display.fillRect(0, kFooterTop, kScreenWidth, kScreenHeight - kFooterTop,
                   kBackground);
  drawSmallButton(2, 284, 44, 30, a, kMuted);
  drawSmallButton(50, 284, 44, 30, b, kAccent);
  drawSmallButton(98, 284, 44, 30, c, kAccent);
  drawSmallButton(146, 284, 44, 30, d, kBad);
  drawSmallButton(194, 284, 44, 30, e, kBad);
}

void drawAboutPage() {
  display.fillScreen(kBackground);
  drawHeader("ABOUT", "AxD");
  display.setTextSize(2);
  display.setTextColor(kAccent, kBackground);
  display.setCursor(scaleX(6), scaleY(52));
  display.print("AxD");
  display.setTextSize(1);
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(scaleX(6), scaleY(80));
  display.print(AwokPins::kDualBand ? "Dual-band Wi-Fi/BLE pentest toolkit"
                                  : "2.4 GHz Wi-Fi/BLE pentest toolkit");
  display.setCursor(scaleX(6), scaleY(92));
  display.print(AwokPins::kBoardLabel);

  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(116));
  display.print("Author:  ");
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.print(kAuthor);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(130));
  display.print("Version: ");
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.print(kVersion);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(144));
  display.print(AwokPins::kBoardLabel);
  display.setCursor(scaleX(6), scaleY(158));
  display.print("Storage: microSD at /awokxdag");

  display.drawFastHLine(scaleX(6), scaleY(176), scaleX(228), kPanel);
  display.setTextColor(kBad, kBackground);
  display.setCursor(scaleX(6), scaleY(186));
  display.print("Authorized testing only. You are");
  display.setCursor(scaleX(6), scaleY(198));
  display.print("responsible for how you use this.");
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(218));
  display.print("Tap anywhere to go back.");
  drawFooter("Back", "Back");
}

// Home destinations. Files and Settings are first-class entries here so neither
// is reachable only through the device-health Status screen.
enum HomeAction {
  kHomeRecon, kHomeAttacks, kHomeMonitor, kHomeGps,
  kHomeFiles, kHomeSettings, kHomeStatus, kHomeAbout
};
struct HomeTile {
  const char* label;
  uint16_t color;
  HomeAction action;
};
const HomeTile kHomeTiles[] = {
    {"Recon", kAccent, kHomeRecon},   {"Attacks", kBad, kHomeAttacks},
    {"Monitor", kAccent, kHomeMonitor}, {"GPS", kAccent, kHomeGps},
    {"Files", kAccent, kHomeFiles},   {"Settings", kAccent, kHomeSettings},
    {"Status", kAccent, kHomeStatus}, {"About", kAccent, kHomeAbout},
};
const int kHomeTileCount = sizeof(kHomeTiles) / sizeof(kHomeTiles[0]);
const int kHomeTilesPerPage = 5;
const int kHomeFirstY = 44;
const int kHomeRowPitch = 44;
const int kHomeTileHeight = 40;

int homePageCount() {
  return max(1, (kHomeTileCount + kHomeTilesPerPage - 1) / kHomeTilesPerPage);
}

void launchHomeTile(int index) {
  if (index < 0 || index >= kHomeTileCount) return;
  switch (kHomeTiles[index].action) {
    case kHomeRecon: reconCategory = -1; reconPage = 0; drawReconMenu(); break;
    case kHomeAttacks: drawAttacksMenu(); break;
    case kHomeMonitor: openMonitorMenu(); break;
    case kHomeGps: drawGps(); break;
    case kHomeFiles: openFilesManager(); break;
    case kHomeSettings: openSettings(); break;
    case kHomeStatus: drawStatus(); break;
    case kHomeAbout: openAbout(); break;
  }
}

void openAbout() {
  homeOverlay = true;
  drawHome();
}

void drawHome() {
  if (currentView == View::kScreenTest) finishScreenTest();
  if (networkToolsOpen()) closeNetworkTools();
  signalMonitorActive = false;
  currentView = View::kHome;
  if (homeOverlay) {
    drawAboutPage();
    return;
  }
  const int pages = homePageCount();
  if (homePage < 0 || homePage >= pages) homePage = 0;
  display.fillScreen(kBackground);
  String detail = sdReady ? "SD ready" : "SD missing";
  detail += " | " + String(homePage + 1) + "/" + String(pages);
  drawHeader("AxD", detail);
  const int start = homePage * kHomeTilesPerPage;
  const int rows = min(kHomeTilesPerPage, kHomeTileCount - start);
  for (int row = 0; row < rows; ++row) {
    const HomeTile& tile = kHomeTiles[start + row];
    drawButton(12, kHomeFirstY + row * kHomeRowPitch, 216, kHomeTileHeight,
               tile.label, tile.color);
  }
  // Standard paging: Prev on earlier pages, Next while more pages remain. The
  // firmware version fills the slot that has no page to move toward.
#ifdef AWOK_MINI_DISPLAY
  if (homePage > 0) display.button(4, 284, 106, 30, "Prev", kMuted);
  if (homePage + 1 < pages) display.button(128, 284, 106, 30, "Next", kAccent);
#else
  drawFooter(homePage > 0 ? "Prev" : kVersion,
             homePage + 1 < pages ? "Next" : kVersion);
#endif
}

void drawBootScreen() {
#ifdef AWOK_MINI_DISPLAY
  // Same logo as Touch, downscaled to 96x128 and centered on the 128x128 panel
  // (see scripts/gen_mini_boot.py). splash() pushes it straight to the ST7735.
  display.splash(kMiniBootScreenBitmap, kMiniBootScreenWidth,
                 kMiniBootScreenHeight);
#elif defined(AWOK_HEADLESS)
  // Headless bridge: no splash.
#else
  // XBM stores black source pixels as set bits. Painting those black over a
  // white canvas preserves the supplied white-on-black composition exactly.
  display.fillScreen(ILI9341_WHITE);
  // The splash bitmap is authored at the 240x320 design size; centre it on the
  // panel (identity on 240x320, a framed logo on the larger Pancake screen).
  display.drawXBitmap((kScreenWidth - kBootScreenWidth) / 2,
                      (kScreenHeight - kBootScreenHeight) / 2, kBootScreenBitmap,
                      kBootScreenWidth, kBootScreenHeight, ILI9341_BLACK);
  display.present();  // setup() blocks on delay() next; show the splash now
#endif
}

void drawScanning(const String& kind) {
  display.fillScreen(kBackground);
  drawHeader(kind + " SCAN", "passive discovery in progress");
  display.setTextColor(kAccent, kBackground);
  display.setTextSize(2);
  display.setCursor(scaleX(43), scaleY(135));
  display.print("Scanning...");
  // Show it now: the caller blocks on a synchronous scan before returning to
  // loop(), so the end-of-loop present() would be too late.
  display.present(false);
}

void drawWifiResults() {
  currentView = View::kWifi;
  const int pages = max(1, (wifiCount + kVisibleRows - 1) / kVisibleRows);
  if (wifiPage >= pages) wifiPage = pages - 1;
  if (wifiPage < 0) wifiPage = 0;
  display.fillScreen(kBackground);
  String detail = String(wifiCount) + " APs | " + String(wifiPage + 1) + "/" +
                  String(pages) + " | SD ";
  detail += lastScanSdWriteOk ? "saved" : (sdReady ? "write error" : "missing");
  drawHeader("WI-FI RESULTS", detail);
  display.setTextSize(1);
  const int start = wifiPage * kVisibleRows;
  const int rows = min(kVisibleRows, wifiCount - start);
#ifdef AWOK_MINI_DISPLAY
  display.selectableRows(rows);
#endif
  for (int row = 0; row < rows; ++row) {
    const int index = start + row;
    const int y = 48 + row * 22;  // design-space row top
    display.setTextColor(isSaved(wifiEntries[index]) ? kAccent
                                                     : ILI9341_WHITE,
                         kBackground);
    display.setCursor(scaleX(5), scaleY(y));
    display.print(clipped(wifiEntries[index].ssid.length()
                              ? wifiEntries[index].ssid
                              : "<hidden>",
                          20));
    if (isSaved(wifiEntries[index])) display.print(" *");
    display.setTextColor(kMuted, kBackground);
    display.setCursor(scaleX(5), scaleY(y + 11));
    display.printf("%4ld dBm  ch%-3ld %sG  %s",
                   static_cast<long>(wifiEntries[index].rssi),
                   static_cast<long>(wifiEntries[index].channel),
                   bandLabel(wifiEntries[index].channel),
                   authShortLabel(wifiEntries[index].auth));
  }
  if (wifiCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(scaleX(52), scaleY(145));
    display.print("No access points found");
  }
  if (pages > 1) {
    drawFiveButtonFooter("Home", "Prev", "Next", "Deauth", "Scan");
  } else {
    drawThreeButtonFooter("Home", "Deauth", "Rescan");
  }
}

void drawSavedNetworks() {
  currentView = View::kSaved;
  display.fillScreen(kBackground);
  String detail = String(savedCount) + " saved | SD ";
  detail += lastSavedSdWriteOk ? "synced" : (sdReady ? "ready" : "missing");
  drawHeader("SAVED NETWORKS", detail);
#ifdef AWOK_MINI_DISPLAY
  display.selectableRows(savedCount);
#endif
  display.setTextSize(1);
  for (int i = 0; i < savedCount; ++i) {
    const int y = 48 + i * 22;  // design-space row top
    display.setTextColor(kAccent, kBackground);
    display.setCursor(scaleX(5), scaleY(y));
    display.print(clipped(savedEntries[i].ssid.length() ? savedEntries[i].ssid
                                                        : "<hidden>",
                          24));
    display.setTextColor(kMuted, kBackground);
    display.setCursor(scaleX(5), scaleY(y + 11));
    display.printf("%4ld dBm  ch%-3ld %s",
                   static_cast<long>(savedEntries[i].rssi),
                   static_cast<long>(savedEntries[i].channel),
                   authShortLabel(savedEntries[i].auth));
  }
  if (savedCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(scaleX(43), scaleY(137));
    display.print("No saved networks yet");
    display.setCursor(scaleX(24), scaleY(153));
    display.print("Scan Wi-Fi, select one, then Save");
  }
  drawFooter("Home", "Wi-Fi Scan");
}

int blePageCount() {
  return max(1, (bleCount + kVisibleRows - 1) / kVisibleRows);
}

// Maps a visible row to the retained result; the last page may be partial.
int bleResultIndex(int row) {
  if (row < 0 || row >= kVisibleRows) return -1;
  const int index = blePage * kVisibleRows + row;
  return index >= 0 && index < bleCount ? index : -1;
}

void drawBleResults() {
  currentView = View::kBle;
  const int pages = blePageCount();
  if (blePage < 0 || blePage >= pages) blePage = 0;
  display.fillScreen(kBackground);
  String detail = String(bleCount) + " BLE | " + String(blePage + 1) + "/" +
                  String(pages) + " | SD ";
  detail +=
      lastBleScanSdWriteOk ? "saved" : (sdReady ? "write error" : "missing");
  drawHeader("BLE RESULTS", detail);
#ifdef AWOK_MINI_DISPLAY
  display.selectableRows(min(kVisibleRows, bleCount - blePage * kVisibleRows));
#endif
  display.setTextSize(1);
  for (int row = 0; row < kVisibleRows; ++row) {
    const int i = bleResultIndex(row);
    if (i < 0) break;
    const int y = 48 + row * 22;  // design-space row top
    display.setTextColor(ILI9341_WHITE, kBackground);
    display.setCursor(scaleX(5), scaleY(y));
    display.print(clipped(bleEntries[i].flipperLike
                              ? String("[F?] ") + bleEntries[i].name
                              : bleEntries[i].name.length() ? bleEntries[i].name : "<unnamed>",
                          20));
    display.setTextColor(kMuted, kBackground);
    display.setCursor(scaleX(5), scaleY(y + 11));
    display.printf("%4ld dBm  %s", static_cast<long>(bleEntries[i].rssi),
                   bleEntries[i].address.c_str());
  }
  if (bleCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(scaleX(54), scaleY(145));
    display.print("No advertisers found");
  }
  if (pages > 1) {
    drawFourButtonFooter("Home", "Prev", "Next", "Scan");
  } else {
    drawFooter("Home", "Rescan");
  }
}

void drawBleDetail() {
  currentView = View::kBleDetail;
  display.fillScreen(kBackground);
  drawHeader("BLE INSPECT",
             selectedBle.name.length() ? selectedBle.name : "<unnamed>");
  display.setTextSize(1);

  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(50));
  display.print("Address: ");
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.print(selectedBle.address);

  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(66));
  display.print("Type: ");
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.print(bleAddressTypeLabel(selectedBle.addressType));

  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(82));
  display.printf("Signal: %ld dBm (%s)", static_cast<long>(selectedBle.rssi),
                 signalLabel(selectedBle.rssi));
  display.setCursor(scaleX(6), scaleY(98));
  if (selectedBle.hasTxPower) {
    display.printf("TX power: %ld dBm", static_cast<long>(selectedBle.txPower));
  } else {
    display.print("TX power: not advertised");
  }
  display.setCursor(scaleX(6), scaleY(114));
  display.printf("Connectable: %s   Scannable: %s",
                 selectedBle.connectable ? "yes" : "no",
                 selectedBle.scannable ? "yes" : "no");
  display.setCursor(scaleX(6), scaleY(130));
  display.printf("Advertisement bytes: %u", selectedBle.advertisementBytes);

  display.drawFastHLine(scaleX(6), scaleY(145), scaleX(228), kPanel);
  display.setTextColor(kAccent, kBackground);
  display.setCursor(scaleX(6), scaleY(153));
  display.print("MANUFACTURER");
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(scaleX(6), scaleY(169));
  if (selectedBle.manufacturerId >= 0) {
    display.printf("Company ID: 0x%04lX",
                   static_cast<long>(selectedBle.manufacturerId));
  } else {
    display.print("Company ID: not advertised");
  }
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(185));
  display.print("Data: ");
  display.print(selectedBle.manufacturerDataHex.length()
                    ? clipped(selectedBle.manufacturerDataHex, 31)
                    : "not advertised");

  display.setTextColor(kAccent, kBackground);
  display.setCursor(scaleX(6), scaleY(205));
  display.print("SERVICE UUIDS");
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(scaleX(6), scaleY(221));
  if (selectedBle.serviceUuids.length()) {
    display.print(clipped(selectedBle.serviceUuids, 37));
    if (selectedBle.serviceUuids.length() > 37) {
      display.setCursor(scaleX(6), scaleY(234));
      display.print(clipped(selectedBle.serviceUuids.substring(37), 37));
    }
  } else {
    display.print("None advertised");
  }
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(258));
  const char* hint = (selectedBle.flipperLike ? "Flipper-like service" : "");
  display.print(*hint ? "Flipper-like UUID; identity unverified"
                      : "Passive advertisement metadata only.");
  drawFooter("Back", "Rescan");
}

int accessPointsOnChannel(int channel) {
  int count = 0;
  for (int i = 0; i < wifiCount; ++i) {
    if (wifiEntries[i].channel == channel) ++count;
  }
  return count;
}

uint16_t channelBarColor(int count) {
  if (count == 0) return kPanel;
  if (count >= 4) return kWarn;
  return kAccent;
}

void drawChannelMap() {
  currentView = View::kChannels;
  display.fillScreen(kBackground);
  drawHeader("CHANNEL MAP",
             wifiCount ? String(wifiCount) + " APs from latest passive scan"
                       : "run a Wi-Fi scan to collect data");
  display.setTextSize(1);

#ifdef AWOK_MINI_DISPLAY
  if (wifiCount == 0) {
    display.setCursor(0, 48); display.print("No channel data");
  } else {
    int maximum = 1;
    for (int ch = 1; ch <= 165; ++ch)
      maximum = max(maximum, accessPointsOnChannel(ch));
    int row = 0;
    for (int ch = 1; ch <= 165; ++ch) {
      const int count = accessPointsOnChannel(ch);
      if (!count) continue;
      const String label = "Ch " + String(ch) + ": " + String(count) + " APs";
      display.bar(48 + row++ * 2, label.c_str(), count, maximum);
    }
  }
  drawFooter("Home", "Rescan");
  return;
#endif

  if (wifiCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(scaleX(43), scaleY(132));
    display.print("No channel data available");
    display.setCursor(scaleX(36), scaleY(149));
    display.print("Tap Scan to collect it now");
    drawFooter("Home", "Scan");
    return;
  }

  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(scaleX(5), scaleY(49));
  display.print("2.4 GHz access points per channel");
  int maximum24 = 1;
  for (int channel = 1; channel <= 14; ++channel) {
    maximum24 = max(maximum24, accessPointsOnChannel(channel));
  }
  constexpr int kBase24 = scaleY(126);
  display.drawFastHLine(scaleX(4), kBase24, scaleX(232), kMuted);
  for (int channel = 1; channel <= 14; ++channel) {
    const int count = accessPointsOnChannel(channel);
    const int height = count ? max(3, count * 55 / maximum24) : 1;  // design px
    const int x = 4 + (channel - 1) * 16;  // design x
    display.fillRect(scaleX(x), kBase24 - scaleY(height), scaleX(11),
                     scaleY(height), channelBarColor(count));
    display.setTextColor(kMuted, kBackground);
    display.setCursor(scaleX(x + (channel < 10 ? 3 : 0)), scaleY(130));
    display.print(channel);
  }

  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(scaleX(5), scaleY(151));
  if (!AwokPins::kDualBand) {
    display.print("This board supports 2.4 GHz only");
    drawFooter("Home", "Scan");
    return;
  }
  display.print("5 GHz channels found in scan");
  constexpr int kMax5Bars = 9;
  int channels5[kMax5Bars] = {};
  int counts5[kMax5Bars] = {};
  int channel5Count = 0;
  for (int i = 0; i < wifiCount; ++i) {
    const int channel = wifiEntries[i].channel;
    if (channel <= 14) continue;
    int slot = -1;
    for (int j = 0; j < channel5Count; ++j) {
      if (channels5[j] == channel) slot = j;
    }
    if (slot >= 0) {
      ++counts5[slot];
    } else if (channel5Count < kMax5Bars) {
      channels5[channel5Count] = channel;
      counts5[channel5Count] = 1;
      ++channel5Count;
    }
  }
  for (int i = 0; i < channel5Count - 1; ++i) {
    for (int j = i + 1; j < channel5Count; ++j) {
      if (channels5[j] < channels5[i]) {
        const int temporaryChannel = channels5[i];
        const int temporaryCount = counts5[i];
        channels5[i] = channels5[j];
        counts5[i] = counts5[j];
        channels5[j] = temporaryChannel;
        counts5[j] = temporaryCount;
      }
    }
  }

  constexpr int kBase5 = scaleY(243);
  display.drawFastHLine(scaleX(4), kBase5, scaleX(232), kMuted);
  if (channel5Count == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(scaleX(53), scaleY(202));
    display.print("No 5 GHz APs detected");
  } else {
    int maximum5 = 1;
    for (int i = 0; i < channel5Count; ++i) {
      maximum5 = max(maximum5, counts5[i]);
    }
    const int spacing = 230 / channel5Count;         // design x-span
    const int barWidth = min(18, spacing - 4);        // design width
    for (int i = 0; i < channel5Count; ++i) {
      const int height = max(3, counts5[i] * 62 / maximum5);  // design height
      const int x = 5 + i * spacing + (spacing - barWidth) / 2;  // design x
      display.fillRect(scaleX(x), kBase5 - scaleY(height), scaleX(barWidth),
                       scaleY(height), channelBarColor(counts5[i]));
      display.setTextColor(kMuted, kBackground);
      display.setCursor(scaleX(x), scaleY(248));
      display.print(channels5[i]);
    }
  }
  drawFooter("Home", "Rescan");
}

int sameChannelNeighbors(const WifiEntry& selected) {
  int count = 0;
  for (int i = 0; i < wifiCount; ++i) {
    if (wifiEntries[i].channel == selected.channel &&
        !sameNetwork(wifiEntries[i], selected)) {
      ++count;
    }
  }
  return count;
}

String auditRecommendation(const WifiEntry& selected) {
  if (selected.auth == WIFI_AUTH_OPEN) {
    return "Enable WPA2/WPA3 or Enhanced Open.";
  }
  if (selected.auth == WIFI_AUTH_WEP || selected.auth == WIFI_AUTH_WPA_PSK ||
      selected.auth == WIFI_AUTH_WPA_WPA2_PSK) {
    return "Upgrade the access point to WPA2/WPA3.";
  }
  if (selected.rssi < -75) {
    return "Improve placement or move closer to the AP.";
  }
  if (wifiCount > 0 && sameChannelNeighbors(selected) >= 4) {
    return "Review the local channel plan.";
  }
  return "No obvious issue in advertised metadata.";
}

// Size-1 characters that fit from design-x `x` to the right edge of the panel
// (the built-in font is 6 px per character). Lets text fill the actual display.
int charsForWidth(int x) {
  return max(1, (kScreenWidth - scaleX(x) - scaleX(4)) / 6);
}

// Word-wrap `text` from design (x, y) across up to `maxLines` lines, each
// stepping down `stepDesign` design px. Line width fills the panel (see
// charsForWidth), so on the wider Pancake screen long text shows in full,
// wrapping under the first row when it still overflows. Returns lines drawn.
int drawWrappedText(int x, int y, uint16_t color, const String& text,
                    int maxLines, int stepDesign) {
  display.setTextColor(color, kBackground);
  const int perLine = charsForWidth(x);
  int line = 0, start = 0;
  const int n = text.length();
  while (start < n && line < maxLines) {
    int end = (n - start > perLine) ? start + perLine : n;
    if (end < n) {  // break on a word boundary when possible
      int space = end;
      while (space > start && text.charAt(space) != ' ') --space;
      if (space > start) end = space;
    }
    display.setCursor(scaleX(x), scaleY(y + line * stepDesign));
    display.print(text.substring(start, end));
    start = end;
    while (start < n && text.charAt(start) == ' ') ++start;
    ++line;
  }
  return line;
}

void drawAuditFinding(int y, uint16_t color, const String& text) {
  display.fillCircle(scaleX(9), scaleY(y + 3), 3, color);
  drawWrappedText(17, y, color, text, 2, 9);
}

void drawWifiAudit() {
  currentView = View::kWifiAudit;
  display.fillScreen(kBackground);
  drawHeader("WI-FI AUDIT",
             selectedWifi.ssid.length() ? selectedWifi.ssid : "<hidden>");
  display.setTextSize(1);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(49));
  display.print("BSSID: ");
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.print(selectedWifi.bssid.length() ? selectedWifi.bssid : "unknown");
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(64));
  display.print("Security: ");
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.print(authLongLabel(selectedWifi.auth));
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(79));
  display.printf("Signal: %ld dBm (%s)", static_cast<long>(selectedWifi.rssi),
                 signalLabel(selectedWifi.rssi));
  display.setCursor(scaleX(6), scaleY(94));
  display.printf("Channel: %ld / %s GHz", static_cast<long>(selectedWifi.channel),
                 bandLabel(selectedWifi.channel));
  display.setCursor(scaleX(6), scaleY(109));
  display.printf("Saved: %s", isSaved(selectedWifi) ? "yes" : "no");
  display.drawFastHLine(scaleX(6), scaleY(124), scaleX(228), kPanel);
  display.setTextColor(kAccent, kBackground);
  display.setCursor(scaleX(6), scaleY(132));
  display.print("PASSIVE FINDINGS");

  if (selectedWifi.auth == WIFI_AUTH_OPEN) {
    drawAuditFinding(149, kBad, "HIGH: Wi-Fi link has no encryption");
  } else if (selectedWifi.auth == WIFI_AUTH_WEP) {
    drawAuditFinding(149, kBad, "HIGH: WEP encryption is obsolete");
  } else if (selectedWifi.auth == WIFI_AUTH_WPA_PSK ||
             selectedWifi.auth == WIFI_AUTH_WPA_WPA2_PSK) {
    drawAuditFinding(149, kWarn, "WARN: legacy WPA is permitted");
  } else {
    drawAuditFinding(149, kGood, "OK: protected authentication advertised");
  }
  if (selectedWifi.rssi < -75) {
    drawAuditFinding(166, kWarn, "WARN: poor signal at this location");
  } else {
    drawAuditFinding(166, kGood, "OK: signal is usable at this location");
  }
  const int neighbors = sameChannelNeighbors(selectedWifi);
  if (wifiCount == 0) {
    drawAuditFinding(183, kMuted, "INFO: rescan to measure congestion");
  } else if (neighbors >= 4) {
    drawAuditFinding(183, kWarn,
                     "WARN: " + String(neighbors) + " other APs share channel");
  } else {
    drawAuditFinding(183, kGood,
                     "OK: " + String(neighbors) + " other APs share channel");
  }
  if (!selectedWifi.ssid.length()) {
    drawAuditFinding(200, kMuted, "INFO: network does not advertise SSID");
  } else {
    drawAuditFinding(200, kGood, "OK: network name is advertised");
  }
  display.setTextColor(kAccent, kBackground);
  display.setCursor(scaleX(6), scaleY(221));
  display.print("RECOMMEND: ");
  // Fill the line after the label, then wrap the remainder underneath.
  const String rec = auditRecommendation(selectedWifi);
  const int firstCap = max(1, charsForWidth(6) - 11);  // "RECOMMEND: " = 11 chars
  display.setTextColor(ILI9341_WHITE, kBackground);
  if (static_cast<int>(rec.length()) <= firstCap) {
    display.print(rec);
  } else {
    int brk = firstCap;
    while (brk > 0 && rec.charAt(brk) != ' ') --brk;
    if (brk == 0) brk = firstCap;
    display.print(rec.substring(0, brk));
    int rest = brk;
    while (rest < static_cast<int>(rec.length()) && rec.charAt(rest) == ' ') ++rest;
    drawWrappedText(6, 233, ILI9341_WHITE, rec.substring(rest), 1, 9);
  }
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(242));
  display.print("Metadata only; no connection attempted.");
  if (auditStatus.length()) {
    display.setTextColor(kAccent, kBackground);
    display.setCursor(scaleX(6), scaleY(257));
    display.print(clipped(auditStatus, charsForWidth(6)));
  }
  drawFiveButtonFooter("Back", "Signal",
                       isSaved(selectedWifi) ? "Remove" : "Save", "Deauth",
                       "Grab");
}

// One-tap handshake audit: target the selected AP and jump straight into the
// deauth-driven handshake capture, skipping the intermediate attack screens.
void grabHandshake() {
  deauthTargetCount = 0;
  addDeauthTarget(selectedWifi);
  deauthAttackReturnView = View::kWifiAudit;
  startHandshakeCapture();
}

bool parseBssid(const String& text, uint8_t output[6]) {
  unsigned int bytes[6];
  if (sscanf(text.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x", &bytes[0],
             &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]) != 6) {
    return false;
  }
  for (int i = 0; i < 6; ++i) output[i] = static_cast<uint8_t>(bytes[i]);
  return true;
}

bool startSignalSdLog() {
  if (!ensureSdCard()) return false;
  SD.remove(kSignalCsvPath);
  File file = SD.open(kSignalCsvPath, FILE_WRITE);
  if (!file) {
    sdReady = false;
    return false;
  }
  file.println("uptime_ms,ssid,bssid,channel,rssi,found");
  file.flush();
  const bool ok = file.getWriteError() == 0;
  file.close();
  if (ok) Serial.printf("[sd] started %s\n", kSignalCsvPath);
  return ok;
}

void appendSignalSdLog(uint32_t sampleTime, int32_t rssi, bool found) {
  if (!signalSdLogReady) return;
  File file = SD.open(kSignalCsvPath, FILE_APPEND);
  if (!file) {
    signalSdLogReady = false;
    sdReady = false;
    return;
  }
  file.print(sampleTime);
  file.print(',');
  file.print(csvField(selectedWifi.ssid));
  file.print(',');
  file.print(csvField(selectedWifi.bssid));
  file.print(',');
  file.print(selectedWifi.channel);
  file.print(',');
  if (found) file.print(rssi);
  file.print(',');
  file.println(found ? "true" : "false");
  file.close();
}

uint16_t signalColor(int32_t rssi) {
  if (rssi >= -67) return kGood;
  if (rssi >= -75) return kAccent;
  if (rssi >= -85) return kWarn;
  return kBad;
}

void drawWifiSignalMonitor() {
  currentView = View::kWifiMonitor;
  display.fillScreen(kBackground);
  drawHeader("SIGNAL MONITOR",
             selectedWifi.ssid.length() ? selectedWifi.ssid : "<hidden>");
  display.setTextSize(1);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(49));
  display.print("BSSID: ");
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.print(selectedWifi.bssid);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(64));
  if (signalSampleCount > 0 &&
      signalSamples[signalSampleCount - 1] > -127) {
    const int32_t latest = signalSamples[signalSampleCount - 1];
    display.printf("Latest: %ld dBm (%s)", static_cast<long>(latest),
                   signalLabel(latest));
  } else {
    display.print("Latest: waiting for target");
  }
  display.setCursor(scaleX(6), scaleY(79));
  display.printf("Channel %ld | samples %d | misses %d",
                 static_cast<long>(selectedWifi.channel), signalSampleCount,
                 signalMisses);

#ifdef AWOK_MINI_DISPLAY
  display.graph(96, signalSamples, signalSampleCount);
#else
  const int kGraphLeft = scaleX(34);
  const int kGraphRight = scaleX(232);
  const int kGraphTop = scaleY(96);
  const int kGraphBottom = scaleY(242);
  const int levels[] = {-40, -60, -80, -100};
  for (int level : levels) {
    const int y = map(level, -100, -30, kGraphBottom, kGraphTop);
    display.setTextColor(kMuted, kBackground);
    display.setCursor(scaleX(4), y - scaleY(3));
    display.print(level);
    display.drawFastHLine(kGraphLeft, y, kGraphRight - kGraphLeft, kPanel);
  }
  display.drawRect(kGraphLeft, kGraphTop, kGraphRight - kGraphLeft + 1,
                   kGraphBottom - kGraphTop + 1, kMuted);

  int previousX = -1;
  int previousY = -1;
  for (int i = 0; i < signalSampleCount; ++i) {
    if (signalSamples[i] <= -127) {
      previousX = -1;
      previousY = -1;
      continue;
    }
    const int x = kGraphLeft + scaleX(3) +
                  i * (kGraphRight - kGraphLeft - scaleX(6)) /
                      (kSignalSampleCount - 1);
    const int constrainedRssi = constrain(signalSamples[i], -100, -30);
    const int y = map(constrainedRssi, -100, -30, kGraphBottom - scaleY(3),
                      kGraphTop + scaleY(3));
    const uint16_t color = signalColor(signalSamples[i]);
    if (previousX >= 0) display.drawLine(previousX, previousY, x, y, color);
    display.fillCircle(x, y, 2, color);
    previousX = x;
    previousY = y;
  }

#endif
  display.setTextColor(signalSdLogReady ? kAccent : kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(258));
  display.print(signalSdLogReady ? "SD logging: latest_wifi_signal.csv"
                                 : "SD logging unavailable; graph still works");
  drawThreeButtonFooter("Back", "Restart", "Locate");
}

int32_t sampleSelectedWifiSignal(bool& found) {
  found = false;
  if (!ensureWifiStation()) return -127;
  WiFi.disconnect(false, false);
  uint8_t bssid[6];
  const bool haveBssid = parseBssid(selectedWifi.bssid, bssid);
  const int scanCount = WiFi.scanNetworks(
      false, true, true, kSignalPassiveDwellMs,
      static_cast<uint8_t>(selectedWifi.channel), nullptr,
      haveBssid ? bssid : nullptr);
  int32_t result = -127;
  found = false;
  for (int i = 0; i < scanCount; ++i) {
    const bool matches = haveBssid
                             ? WiFi.BSSIDstr(i).equalsIgnoreCase(
                                   selectedWifi.bssid)
                             : WiFi.SSID(i) == selectedWifi.ssid;
    if (matches && (!found || WiFi.RSSI(i) > result)) {
      result = WiFi.RSSI(i);
      found = true;
    }
  }
  WiFi.scanDelete();
  return result;
}

void beginWifiSignalMonitor() {
  signalSampleCount = 0;
  signalMisses = 0;
  lastSignalSampleMs = millis() - kSignalSampleIntervalMs;
  signalMonitorActive = true;
  signalSdLogReady = startSignalSdLog();
  Serial.printf("[signal] monitoring %s on channel %ld\n",
                selectedWifi.bssid.c_str(),
                static_cast<long>(selectedWifi.channel));
  drawWifiSignalMonitor();
}

void updateWifiSignalMonitor() {
  if (!signalMonitorActive || currentView != View::kWifiMonitor ||
      millis() - lastSignalSampleMs < kSignalSampleIntervalMs) {
    return;
  }
  lastSignalSampleMs = millis();
  scanInProgress = true;
  bool found = false;
  const int32_t rssi = sampleSelectedWifiSignal(found);
  scanInProgress = false;
  const uint32_t sampleTime = millis();

  if (signalSampleCount < kSignalSampleCount) {
    signalSamples[signalSampleCount] = found ? rssi : -127;
    signalSampleTimes[signalSampleCount] = sampleTime;
    ++signalSampleCount;
  } else {
    for (int i = 1; i < kSignalSampleCount; ++i) {
      signalSamples[i - 1] = signalSamples[i];
      signalSampleTimes[i - 1] = signalSampleTimes[i];
    }
    signalSamples[kSignalSampleCount - 1] = found ? rssi : -127;
    signalSampleTimes[kSignalSampleCount - 1] = sampleTime;
  }
  if (found) {
    selectedWifi.rssi = rssi;
  } else {
    ++signalMisses;
  }
  appendSignalSdLog(sampleTime, rssi, found);
  Serial.printf("[signal] %s rssi=%ld\n", found ? "seen" : "missed",
                static_cast<long>(rssi));
  drawWifiSignalMonitor();
}

// Keep each Recon category small enough to scan on Touch and Mini. Network
// Tools already has its own menu, so it opens directly from the picker.
const char* const kReconCategories[] = {
    "Wi-Fi", "Bluetooth", "RF & Packets", "Field Tools", "Network Tools"};
constexpr int kReconCategoryCount =
    static_cast<int>(sizeof(kReconCategories) / sizeof(kReconCategories[0]));
struct ReconMenuItem {
  const char* label;
  int category;
};
const ReconMenuItem kReconItems[] = {
    {"Wi-Fi Scan", 0}, {"Saved", 0}, {"WPS Scan", 0},
    {"Hidden SSID", 0}, {"Security Audit", 0}, {"Wi-Fi 6 Intel", 0},
    {"BLE Scan", 1}, {"BLE Trackers", 1}, {"BLE Intel", 1},
    {"Channel Map", 2}, {"Spectrogram", 2}, {"Packet Mon", 2},
    {"Clients", 3}, {"Cameras", 3}, {"Harvester", 3},
    {"Probe Intel", 3}, {"Fleet Hunter", 3}, {"Topology Map", 3}};
constexpr int kReconItemCount =
    static_cast<int>(sizeof(kReconItems) / sizeof(kReconItems[0]));
constexpr int kMenuPerPage = 6;
constexpr int kMenuFirstY = 50;
constexpr int kMenuRowPitch = 32;
constexpr int kMenuRowHeight = 30;

int reconVisibleItemCount() {
  if (reconCategory < 0) return kReconCategoryCount;
  int count = 0;
  for (const auto& item : kReconItems) {
    if (item.category == reconCategory) ++count;
  }
  return count;
}

int reconPageCount() {
  return (reconVisibleItemCount() + kMenuPerPage - 1) / kMenuPerPage;
}

// Convert a position inside the selected category into a tool index.
int reconItemIndex(int position) {
  for (int i = 0; i < kReconItemCount; ++i) {
    if (kReconItems[i].category == reconCategory && position-- == 0) return i;
  }
  return -1;
}

String reconItemLabel(int index) {
  if (strcmp(kReconItems[index].label, "Saved") == 0) {
    return "Saved (" + String(savedCount) + ")";
  }
  return String(kReconItems[index].label);
}

void launchReconItem(int index) {
  if (index < 0 || index >= kReconItemCount) return;
  const String label = kReconItems[index].label;
  if (label == "Wi-Fi Scan") {
    startWifiScanContinuous();
  } else if (label == "Channel Map") {
    if (wifiCount) {
      drawChannelMap();
    } else {
      scanWifiForChannelMap();
    }
  } else if (label == "Spectrogram") {
    startSpectrogram();
  } else if (label == "BLE Scan") {
    scanBle();
  } else if (label == "Clients") {
    startClientSniffer();
  } else if (label == "Packet Mon") {
    startPacketMon();
  } else if (label == "WPS Scan") {
    startWpsScan();
  } else if (label == "Hidden SSID") {
    startHiddenReveal();
  } else if (label == "Cameras") {
    startCameraScan();
  } else if (label == "Security Audit") {
    startSecurityAudit();
  } else if (label == "BLE Trackers") {
    startTrackerScan();
  } else if (label == "BLE Intel") {
    startBleIntel();
  } else if (label == "Harvester") {
    startHarvester();
  } else if (label == "Probe Intel") {
    startProbeIntel();
  } else if (label == "Fleet Hunter") {
    if (selectedWifi.bssid.length()) {
      startFleetHunt();
    } else {
      startWifiScanContinuous();
    }
  } else if (label == "Topology Map") {
    startTopologyMap();
  } else if (label == "Wi-Fi 6 Intel") {
    startWifi6Intel();
  } else if (label == "Saved") {
    drawSavedNetworks();
  }
}

void drawReconMenu() {
  currentView = View::kRecon;
  const int pages = reconPageCount();
  if (reconPage < 0 || reconPage >= pages) reconPage = 0;
  display.fillScreen(kBackground);
  const String title = reconCategory < 0 ? "RECON" : kReconCategories[reconCategory];
  const String detail = reconCategory < 0 ? "choose a tool group" : "Recon / " + title;
  drawHeader(title, pages > 1 ? detail + " " + String(reconPage + 1) +
                                      "/" + String(pages) : detail);
  const int start = reconPage * kMenuPerPage;
  for (int row = 0; row < kMenuPerPage; ++row) {
    const int position = start + row;
    if (position >= reconVisibleItemCount()) break;
    const String label = reconCategory < 0 ? String(kReconCategories[position])
                                          : reconItemLabel(reconItemIndex(position));
    drawButton(12, kMenuFirstY + row * kMenuRowPitch, 216, kMenuRowHeight, label);
  }
  if (pages > 1) {
    drawThreeButtonFooter("Back", "Prev", "Next");
  } else if (reconCategory < 0) {
    drawFooter("Home", "Home");
  } else {
    drawFooter("< Groups", "Home");
  }
}

// Stable tool indices; grouping changes navigation only, not commands or View IDs.
const char* const kMonitorItems[] = {
    "Deauth Watch", "Rogue Watch", "BLE Spam Watch", "Karma Watch",
    "Beacon Watch", "Auth Flood", "Advanced Watch", "Deauth Forensics"};
const char* const kMonitorDescriptions[] = {
    "Detect disconnect-frame bursts", "Spot possible AP impersonation",
    "Detect Bluetooth advert floods", "Flag one AP posing as many SSIDs",
    "Watch for fake-AP beacon floods", "Detect auth / association floods",
    "Combined Wi-Fi / BLE anomalies", "Inspect disconnect-frame evidence"};
const uint8_t kMonitorGroups[] = {0, 0, 1, 0, 0, 0, 2, 2};
const char* const kMonitorCategories[] = {"WI-FI", "BLUETOOTH", "ADVANCED"};
const char* const kMonitorGroupDescriptions[] = {
    "AP identity and Wi-Fi floods", "Bluetooth advertisement floods", "Combined detection and evidence"};
constexpr int kMonitorItemCount = sizeof(kMonitorItems) / sizeof(kMonitorItems[0]);
constexpr int kMonitorPerPage = 3;
static_assert(sizeof(kMonitorGroups) / sizeof(kMonitorGroups[0]) == kMonitorItemCount, "Monitor groups must cover every tool");
static_assert(sizeof(kMonitorDescriptions) / sizeof(kMonitorDescriptions[0]) == kMonitorItemCount, "Monitor descriptions must cover every tool");

bool monitorItemRunning(int index) {
  switch (index) {
    case 0: return deauthMonitorActive;
    case 1: return rogueWatchActive;
    case 2: return bleDetectActive;
    case 3: return karmaWatchActive;
    case 4: return beaconWatchActive;
    case 5: return authFloodActive;
    case 6: return advancedWatchActive;
    case 7: return deauthForensicsActive;
    default: return false;
  }
}

int monitorVisibleCount() {
  if (monitorCategory < 0) return 3;
  int count = 0;
  for (int i = 0; i < kMonitorItemCount; ++i) if (kMonitorGroups[i] == monitorCategory) ++count;
  return count;
}

int monitorItemIndex(int position) {
  if (position < 0 || monitorCategory < 0) return -1;
  for (int i = 0; i < kMonitorItemCount; ++i)
    if (kMonitorGroups[i] == monitorCategory && position-- == 0) return i;
  return -1;
}

int monitorPageCount() { return max(1, (monitorVisibleCount() + kMonitorPerPage - 1) / kMonitorPerPage); }

void setMonitorOrigin(int index) {
  if (index < 0 || index >= kMonitorItemCount) return;
  monitorCategory = kMonitorGroups[index];
  int position = 0;
  for (int i = 0; i < index; ++i) if (kMonitorGroups[i] == monitorCategory) ++position;
  monitorPage = position / kMonitorPerPage;
}

void launchMonitorItem(int index) {
  if (index < 0 || index >= kMonitorItemCount) return;
  setMonitorOrigin(index);
  const bool running = monitorItemRunning(index);
  switch (index) {
    case 0: if (running) drawDeauthMonitor(); else startDeauthMonitor(); break;
    case 1: if (running) drawRogueWatch(); else startRogueWatch(); break;
    case 2: if (running) drawBleDetect(); else startBleDetect(); break;
    case 3: if (running) drawKarmaWatch(); else startKarmaWatch(); break;
    case 4: if (running) drawBeaconWatch(); else startBeaconWatch(); break;
    case 5: if (running) drawAuthFlood(); else startAuthFlood(); break;
    case 6: if (running) drawAdvancedWatch(); else startAdvancedWatch(); break;
    case 7: if (running) drawDeauthForensics(); else startDeauthForensics(); break;
  }
}

void returnToMonitor(int index) {
  if (index < 0 || index >= kMonitorItemCount) return;
  // Also derive the correct group when a tool was launched via serial/Bluetooth.
  setMonitorOrigin(index);
  if (monitorItemRunning(index)) {
    switch (index) {
      case 0: stopDeauthMonitor(); break;
      case 1: stopRogueWatch(); break;
      case 2: stopBleDetect(); break;
      case 3: stopKarmaWatch(); break;
      case 4: stopBeaconWatch(); break;
      case 5: stopAuthFlood(); break;
      case 6: stopAdvancedWatch(); break;
      case 7: stopDeauthForensics(); break;
    }
  }
  drawMonitorMenu();
}

void drawMonitorHeader(const String& title, bool running, const String& detail) {
  drawHeader(title, String(running ? "Running | " : "Stopped | ") + detail);
}

void drawMonitorCard(int row, const String& title, const String& state, const String& description, bool running) {
  const int y = 48 + row * 68;
#ifdef AWOK_MINI_DISPLAY
  display.button(8, y, 224, 60, (title + " | " + state + " | " + description).c_str(), running ? kGood : kAccent);
#else
  display.drawRoundRect(scaleX(8), scaleY(y), scaleX(224), scaleY(60), 5, running ? kGood : kAccent);
  display.setTextSize(2); display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(scaleX(16), scaleY(y + 6)); display.print(title);
  display.setTextSize(1); display.setTextColor(running ? kGood : kMuted, kBackground);
  display.setCursor(scaleX(16), scaleY(y + 28)); display.print(state);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(16), scaleY(y + 44)); display.print(description);
#endif
}

void openMonitorMenu() { monitorCategory = -1; monitorPage = 0; drawMonitorMenu(); }

void drawMonitorMenu() {
  currentView = View::kMonitor;
  if (monitorCategory < -1 || monitorCategory > 2) monitorCategory = -1;
  const int pages = monitorPageCount();
  monitorPage = max(0, min(monitorPage, pages - 1));
  display.fillScreen(kBackground);
  drawHeader(monitorCategory < 0 ? "MONITOR" : kMonitorCategories[monitorCategory],
      monitorCategory < 0 ? "choose a detector group" : "Monitor | " + String(monitorPage + 1) + "/" + String(pages));
  for (int row = 0; row < kMonitorPerPage; ++row) {
    const int position = monitorPage * kMonitorPerPage + row;
    if (position >= monitorVisibleCount()) break;
    if (monitorCategory < 0) {
      int count = 0, active = 0;
      for (int i = 0; i < kMonitorItemCount; ++i) if (kMonitorGroups[i] == position) {
        ++count; if (monitorItemRunning(i)) ++active;
      }
      drawMonitorCard(row, kMonitorCategories[position], String(count) + (count == 1 ? " tool | " : " tools | ") + String(active) + " running",
                      kMonitorGroupDescriptions[position], active > 0);
    } else {
      const int index = monitorItemIndex(position);
      const bool running = monitorItemRunning(index);
      drawMonitorCard(row, kMonitorItems[index], running ? "Running | tap to view" : "Stopped | tap to start",
                      kMonitorDescriptions[index], running);
    }
  }
  if (monitorCategory < 0) drawSmallButton(4, 280, 232, 36, "Home", kMuted);
  else if (pages == 1) {
    drawSmallButton(4, 280, 112, 36, "Groups", kMuted);
    drawSmallButton(124, 280, 112, 36, "Home", kAccent);
  } else {
    drawSmallButton(4, 280, 72, 36, "Groups", kMuted);
    if (monitorPage > 0) drawSmallButton(84, 280, 72, 36, "Prev", kAccent);
    if (monitorPage + 1 < pages) drawSmallButton(164, 280, 72, 36, "Next", kAccent);
  }
}

void handleMonitorTouch(int x, int y) {
  for (int row = 0; row < kMonitorPerPage; ++row) {
    if (!gpsMenuHit(x, y, 8, 48 + row * 68, 224, 60)) continue;
    const int position = monitorPage * kMonitorPerPage + row;
    if (position >= monitorVisibleCount()) return;
    if (monitorCategory < 0) { monitorCategory = position; monitorPage = 0; drawMonitorMenu(); }
    else launchMonitorItem(monitorItemIndex(position));
    return;
  }
  if (monitorCategory < 0) {
    if (gpsMenuHit(x, y, 4, 280, 232, 36)) drawHome();
  } else if (monitorPageCount() == 1) {
    if (gpsMenuHit(x, y, 4, 280, 112, 36)) openMonitorMenu();
    else if (gpsMenuHit(x, y, 124, 280, 112, 36)) drawHome();
  } else {
    if (gpsMenuHit(x, y, 4, 280, 72, 36)) openMonitorMenu();
    else if (gpsMenuHit(x, y, 84, 280, 72, 36) && monitorPage > 0) { --monitorPage; drawMonitorMenu(); }
    else if (gpsMenuHit(x, y, 164, 280, 72, 36) && monitorPage + 1 < monitorPageCount()) { ++monitorPage; drawMonitorMenu(); }
  }
}

void drawAttacksMenu() {
  currentView = View::kAttacks;
  display.fillScreen(kBackground);
  drawHeader("ATTACK TOOLS", "active RF | authorized use only");
  drawButton(12, 44, 216, 38, "Beacon Flood");
  drawButton(12, 86, 216, 38, "Evil Portal");
  drawButton(12, 128, 216, 38, "Evil Twin");
  drawButton(12, 170, 216, 38, "Probe Lure");
  display.setTextSize(1);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(18), scaleY(216));
  display.print("Evil Twin/Probe Lure use the last-");
  display.setCursor(scaleX(18), scaleY(228));
  display.print("scanned SSID. Deauth: under an AP.");
  drawConfirmBanner();
  drawFooter("Home", "Home");
}

void sortWifi() {
  for (int i = 0; i < wifiCount - 1; ++i) {
    for (int j = i + 1; j < wifiCount; ++j) {
      if (wifiEntries[j].rssi > wifiEntries[i].rssi) {
        WifiEntry temporary = wifiEntries[i];
        wifiEntries[i] = wifiEntries[j];
        wifiEntries[j] = temporary;
      }
    }
  }
}

void sortBle() {
  for (int i = 0; i < bleCount - 1; ++i) {
    for (int j = i + 1; j < bleCount; ++j) {
      if (bleEntries[j].rssi > bleEntries[i].rssi) {
        BleEntry temporary = bleEntries[i];
        bleEntries[i] = bleEntries[j];
        bleEntries[j] = temporary;
      }
    }
  }
}

void logMemory(const char* stage) {
  // C5 Wi-Fi and BLE coexistence needs internal/DMA-capable memory. Report
  // both total free and largest blocks: free PSRAM alone does not establish
  // whether the controllers can initialize. RadioScheduler alternates scan
  // windows while keeping both initialized when coexistence succeeds.
  const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  Serial.printf(
      "[memory] %s: internal free=%u largest=%u; dma free=%u largest=%u; "
      "PSRAM free=%u\n",
      stage, unsigned(heap_caps_get_free_size(caps)),
      unsigned(heap_caps_get_largest_free_block(caps)),
      unsigned(heap_caps_get_free_size(MALLOC_CAP_DMA)),
      unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_DMA)),
      unsigned(ESP.getFreePsram()));
}

void showToolMemoryError(const char* tool) {
  Serial.printf("[memory] %s: buffer allocation failed; tool not started\n", tool);
  logMemory(tool);
  currentView = View::kHome;
  homeOverlay = true;  // any tap returns to the Home tiles
  signalMonitorActive = false;
  display.fillScreen(kBackground);
  drawHeader("MEMORY LOW", tool);
  display.setTextSize(1);
  display.setTextColor(kWarn, kBackground);
  display.setCursor(scaleX(6), scaleY(60));
  display.print("Tool could not start.");
  display.setCursor(scaleX(6), scaleY(80));
  display.print("Check Serial Monitor.");
  drawFooter("Home", "Home");
}

void showRadioError(const char* message) {
  // Reuse the Home overlay return handler: either footer goes back to Home.
  currentView = View::kHome;
  homeOverlay = true;
  signalMonitorActive = false;
  display.fillScreen(kBackground);
  drawHeader("RADIO ERROR", message);
  display.setTextSize(1);
  display.setTextColor(kWarn, kBackground);
  display.setCursor(scaleX(6), scaleY(60));
  display.print("Radio initialization failed.");
  display.setCursor(scaleX(6), scaleY(80));
  display.print("Check Serial Monitor for details.");
  drawFooter("Home", "Home");
}

bool ensureWifiStation(bool releaseBle) {
  // Wi-Fi-only tools free the BLE controller to leave more runtime headroom.
  // Dual-radio views pass releaseBle=false to keep BLE up alongside Wi-Fi.
  if (releaseBle) releaseBleMemory();
  if (WiFi.getMode() == WIFI_STA) return true;
  logMemory("before Wi-Fi init");
  // A failed esp_wifi_init logs 0x3001 on its deinit cleanup (driver never
  // came up). Retrying immediately shrinks the largest heap block further.
  if (WiFi.mode(WIFI_STA)) {
    logMemory("after Wi-Fi init");
    return true;
  }
  Serial.println("[wifi] station initialization failed; operation cancelled");
  logMemory("Wi-Fi init failed");
  return false;
}

// Tear Wi-Fi down through the Arduino wrapper only. WiFi.mode(WIFI_OFF)
// already stops and deinits the driver; a second esp_wifi_deinit() logs
// ESP_ERR_WIFI_NOT_INIT (0x3001) and desyncs Arduino from the IDF driver.
void shutdownWifi() {
  esp_wifi_set_promiscuous(false);
  WiFi.scanDelete();
  WiFi.disconnect(false, false);
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.mode(WIFI_OFF);
  }
}

bool ensureBleReady(bool needsWifi) {
  // BLE-only tools release Wi-Fi; dual-radio views keep STA and retry BLE
  // themselves instead of jumping to a radio-error screen.
  if (!needsWifi) shutdownWifi();
  if (NimBLEDevice::isInitialized()) return true;
  logMemory("before BLE init");
  if (!NimBLEDevice::init("")) {
    logMemory("BLE init failed");
    if (!needsWifi) showRadioError("BLE initialization failed");
    return false;
  }
  NimBLEDevice::setPower(3);
  logMemory("after BLE init");
  return true;
}

void releaseBleMemory() {
  // Release BLE at tool exit or when a Wi-Fi-only tool needs its memory.
  // Dual-radio scan windows keep the controllers resident. NimBLE >= 2.5.1
  // (enforced in awok_common.h) cleans up the scan timer BEFORE port teardown;
  // 2.5.0 instead dereferenced the freed NPL function table in ~NimBLEScan.
  if (!NimBLEDevice::isInitialized()) return;
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (scan) {
    scan->stop();
    scan->clearResults();
  }
#ifdef AWOK_HEADLESS
  // The orange bridge hosts the BLE GATT server the phone is connected to;
  // deinit-ing NimBLE would drop that connection. It has ample DMA to keep BLE
  // resident (Gate 0: ~29 KB free with server + Wi-Fi up), so only stop the
  // scan -- never tear the controller down.
  return;
#else
  delay(100);
  Serial.println("[ble] releasing scan and controller");
  if (!NimBLEDevice::deinit(true)) {
    Serial.println("[ble] deinit failed");
    logMemory("BLE shutdown failed");
    return;
  }
  logMemory("after BLE shutdown");
#endif
}

// --- Dual-radio scheduler (both radios resident, alternate scans) ---------
// Historically the C5 couldn't hold Wi-Fi and the BLE controller at once, so
// dual-radio views tore one down each window -- but the closed C5 controller
// leaks ~0.4 KB DMA per init/deinit, which killed long wardrives. Shrinking the
// in-RAM result tables (kResultCapacity) freed enough DMA-capable SRAM to keep
// BOTH radios resident for the whole session (Wi-Fi ~34 KB + BLE ~33 KB, ~28 KB
// to spare). So we now init BLE once and alternate only the SCANS -- no
// teardown, no reinit, no leak (the ESP32-Marauder model). GPS logs throughout.

// Start a fresh Wi-Fi window: STA is resident, (re)configure the view's
// capture/scan via its enterWifi hook.
static void radioEnterWifiPhase(RadioScheduler& s) {
  s.phase = RadioPhase::kWifi;
  s.phaseStartMs = millis();
  s.wifiScanDone = false;
  if (s.enterWifi) s.enterWifi();
}

// Bring BLE up (Wi-Fi already torn down) and start the view's scan. Returns
// false if BLE init or scan-enable fails, leaving BLE released.
// Enter a dual-radio view. Both radios come up RESIDENT and stay up for the
// whole session -- only the SCANS alternate. Nothing is deinit-ed per cycle, so
// the ESP32-C5 BLE controller's ~0.4 KB/cycle init/deinit leak never happens and
// BLE runs the entire session (the ESP32-Marauder model). The shrunk result
// tables (kResultCapacity) leave enough DMA for both to coexist (~28 KB free
// with Wi-Fi + BLE both resident, measured).
bool radioSchedulerBegin(RadioScheduler& s) {
#if defined(AWOK_CLASSIC_ESP32)
  s.bleAvailable = false;
#else
  s.bleAvailable = true;
#endif
  if (!ensureWifiStation(false)) {
    showRadioError("Wi-Fi initialization failed");
    return false;
  }
  if (s.bleAvailable) {
    // Bring BLE up ALONGSIDE Wi-Fi (needsWifi=true -> no Wi-Fi teardown) and keep
    // it resident. If the pool somehow can't seat both, fall back to Wi-Fi-only.
    if (ensureBleReady(true)) {
      NimBLEScan* scan = NimBLEDevice::getScan();
      configureBleScan(scan, s.bleCallbacks, s.bleActiveScan, s.bleInterval,
                       s.bleWindow, 0);
    } else {
      s.bleAvailable = false;
      Serial.println("[radio] BLE could not coexist; Wi-Fi-only this session");
    }
  }
  radiosCoexist = s.bleAvailable;  // drives "(Wi-Fi only)" UI + BLE counters
  s.active = true;
  radioEnterWifiPhase(s);  // Wi-Fi scans first; BLE controller resident, idle
  return true;
}

// Called every update pass; alternates which radio is SCANNING when the current
// window elapses. Both controllers stay resident -- no teardown, no reinit, no
// settle gap, no leak.
void radioSchedulerTick(RadioScheduler& s) {
  if (!s.active || !s.bleAvailable) return;  // Wi-Fi-only: nothing to alternate
  const uint32_t now = millis();
  const uint32_t elapsed = now - s.phaseStartMs;
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (s.phase == RadioPhase::kWifi) {
    if (elapsed < s.wifiWindowMs && !s.wifiScanDone) return;
    // Hand airtime to BLE: stop Wi-Fi capture, start the BLE scan. Wi-Fi STA
    // stays resident (idle).
    if (s.exitWifi) s.exitWifi();
    if (scan) { scan->clearResults(); scan->start(0, false, true); }
    s.phase = RadioPhase::kBle;
    s.phaseStartMs = now;
    Serial.println("[radio] scan -> BLE (both resident)");
  } else {
    if (elapsed < s.bleWindowMs) return;
    // Hand it back to Wi-Fi: stop the BLE scan (controller stays resident).
    if (scan) { scan->stop(); scan->clearResults(); }
    radioEnterWifiPhase(s);
    Serial.println("[radio] scan -> Wi-Fi (both resident)");
  }
}

// Leave a dual-radio view: stop scanning, free the resident BLE controller ONCE
// (the only deinit of the session), and restore the resident-STA invariant.
void radioSchedulerEnd(RadioScheduler& s) {
  if (!s.active) return;
  s.active = false;
  if (s.phase == RadioPhase::kWifi && s.exitWifi) s.exitWifi();
  if (s.bleAvailable) {
    releaseBleMemory();          // stop/clear/deinit once, at tool exit only
  }
  ensureWifiStation(false);      // STA resident for the rest of the firmware
}

bool lastWifiScanOk = false;
void scanWifi() {
  if (scanInProgress) return;
  lastWifiScanOk = false;
  scanInProgress = true;
  drawScanning("WI-FI");
  Serial.println("[wifi] passive access-point scan started");
  if (!ensureWifiStation()) {
    scanInProgress = false;
    showRadioError("Wi-Fi initialization failed");
    return;
  }
  WiFi.disconnect(false, false);
  delay(100);
  const int found = WiFi.scanNetworks(false, true, true);
  if (found < 0) {
    Serial.printf("[wifi] scan failed (%d); previous results retained\n", found);
    WiFi.scanDelete();
    scanInProgress = false;
    logMemory("Wi-Fi scan failed");
    showRadioError("Wi-Fi scan failed");
    return;
  }
  wifiCount = found > 0 ? min(found, kMaxWifiResults) : 0;
  wifiPage = 0;
  for (int i = 0; i < wifiCount; ++i) {
    wifiEntries[i].ssid = WiFi.SSID(i);
    wifiEntries[i].bssid = WiFi.BSSIDstr(i);
    wifiEntries[i].rssi = WiFi.RSSI(i);
    wifiEntries[i].channel = WiFi.channel(i);
    wifiEntries[i].auth = WiFi.encryptionType(i);
    Serial.printf("[wifi] %4ld dBm ch%-3ld %s %s\n",
                  static_cast<long>(wifiEntries[i].rssi),
                  static_cast<long>(wifiEntries[i].channel),
                  wifiEntries[i].bssid.c_str(), wifiEntries[i].ssid.c_str());
  }
  WiFi.scanDelete();
  sortWifi();
  lastScanSdWriteOk = exportWifiScanToSd();
  scanInProgress = false;
  lastWifiScanOk = true;
  drawWifiResults();
#ifdef AWOK_HEADLESS
  if (g_bridgePhoneConnected) linkStreamWifiResults();  // bridge -> phone (BLE)
#else
  if (remoteActive) linkStreamWifiResults();  // push the list to the phone
#endif
}

// Continuous Wi-Fi scan for the Wi-Fi Scan tool: async re-scan on a loop,
// MERGING results by BSSID so the list accumulates every AP seen (not just the
// ones present in the latest sweep). New APs are streamed to the phone as they
// appear; RSSI of known APs is refreshed in place. Runs until the view changes
// or a Stop. scanWifi() above stays the blocking one-shot primitive that the
// channel map / connect flow rely on.
static int findWifiByBssid(const String& bssid) {
  for (int i = 0; i < wifiCount; ++i)
    if (wifiEntries[i].bssid == bssid) return i;
  return -1;
}

static int mergeScannedWifi(int found) {
  int added = 0;
  for (int i = 0; i < found; ++i) {
    String bssid = WiFi.BSSIDstr(i);
    int idx = findWifiByBssid(bssid);
    if (idx < 0) {
      if (wifiCount >= kMaxWifiResults) continue;
      idx = wifiCount++;
      wifiEntries[idx].bssid = bssid;
      ++added;
    }
    wifiEntries[idx].ssid = WiFi.SSID(i);
    wifiEntries[idx].rssi = WiFi.RSSI(i);
    wifiEntries[idx].channel = WiFi.channel(i);
    wifiEntries[idx].auth = WiFi.encryptionType(i);
  }
  return added;
}

void startWifiScanContinuous() {
  if (!ensureWifiStation()) {
    showRadioError("Wi-Fi initialization failed");
    return;
  }
  WiFi.disconnect(false, false);
  wifiCount = 0;
  wifiPage = 0;
  wifiScanContinuous = true;
  lastWifiScanOk = true;
  currentView = View::kWifi;
  WiFi.scanNetworks(true, true, true);  // async, show-hidden, passive
  drawScanning("WI-FI");  // immediate feedback; first results replace it
  Serial.println("[wifi] continuous scan started");
}

void stopWifiScanContinuous() {
  if (!wifiScanContinuous) return;
  wifiScanContinuous = false;
  WiFi.scanDelete();
  Serial.println("[wifi] continuous scan stopped");
}

void updateWifiScan() {
  if (!wifiScanContinuous) return;
  if (currentView != View::kWifi) { stopWifiScanContinuous(); return; }
  const int r = WiFi.scanComplete();
  if (r == WIFI_SCAN_RUNNING) return;
  if (r >= 0) {
    const int added = mergeScannedWifi(r);
    WiFi.scanDelete();
    if (added > 0) sortWifi();
    drawWifiResults();
    if (added > 0) {  // only stream when the list grew (keeps BLE traffic sane)
#ifdef AWOK_HEADLESS
      if (g_bridgePhoneConnected) linkStreamWifiResults();
#else
      if (remoteActive) linkStreamWifiResults();
#endif
    }
    // Throttle SD writes: saving the full CSV every sweep thrashes the card.
    static uint32_t lastWifiExportMs = 0;
    if (added > 0 && millis() - lastWifiExportMs >= 15000) {
      lastWifiExportMs = millis();
      lastScanSdWriteOk = exportWifiScanToSd();
    }
  }
  if (wifiScanContinuous) WiFi.scanNetworks(true, true, true);  // next sweep
}

void scanWifiForChannelMap() {
  scanWifi();
  if (lastWifiScanOk) drawChannelMap();
}

// NimBLE uses one scanner across all views. Always replace the previous
// callback, duplicate policy and result limit when changing scan modes.
void configureBleScan(NimBLEScan* scan, NimBLEScanCallbacks* callbacks,
                       bool active, uint16_t interval, uint16_t window,
                       uint8_t maxResults) {
  scan->stop();
  scan->clearResults();
  scan->setScanCallbacks(callbacks, callbacks != nullptr);
  scan->setMaxResults(maxResults);
  scan->setActiveScan(active);
  scan->setInterval(interval);
  scan->setWindow(window);
}

void scanBle() {
  if (scanInProgress) return;
  if (!ensureBleReady(false)) return;
  scanInProgress = true;
  drawScanning("BLE");
  Serial.println("[ble] passive advertisement scan started");
  NimBLEScan* scanner = NimBLEDevice::getScan();
  configureBleScan(scanner, nullptr, false, 100, 80, kMaxBleResults);
  NimBLEScanResults results = scanner->getResults(kBleScanMs, false);
  bleCount = min(results.getCount(), kMaxBleResults);
  blePage = 0;
  for (int i = 0; i < bleCount; ++i) {
    const NimBLEAdvertisedDevice* device = results.getDevice(i);
    bleEntries[i].name = device->haveName()
                             ? String(device->getName().c_str())
                             : String();
    bleEntries[i].address = String(device->getAddress().toString().c_str());
    bleEntries[i].rssi = device->getRSSI();
    bleEntries[i].addressType = device->getAddressType();
    bleEntries[i].hasTxPower = device->haveTXPower();
    bleEntries[i].txPower = bleEntries[i].hasTxPower ? device->getTXPower() : 0;
    bleEntries[i].connectable = device->isConnectable();
    bleEntries[i].scannable = device->isScannable();
    bleEntries[i].advertisementBytes = device->getAdvLength();
    bleEntries[i].manufacturerId = -1;
    bleEntries[i].manufacturerDataHex = "";
    if (device->haveManufacturerData()) {
      const std::string manufacturerData = device->getManufacturerData();
      bleEntries[i].manufacturerDataHex = bytesToHex(manufacturerData);
      if (manufacturerData.length() >= 2) {
        bleEntries[i].manufacturerId =
            static_cast<uint8_t>(manufacturerData[0]) |
            (static_cast<uint16_t>(
                 static_cast<uint8_t>(manufacturerData[1]))
             << 8);
      }
    }
    bleEntries[i].flipperLike = false;
    for (int service = 0; service < device->getServiceUUIDCount(); ++service) {
      if (*NetworkParse::flipper(device->getServiceUUID(service).toString().c_str()))
        bleEntries[i].flipperLike = true;
    }
    bleEntries[i].serviceUuids = "";
    const int serviceCount = min(static_cast<int>(device->getServiceUUIDCount()),
                                 3);
    for (int service = 0; service < serviceCount; ++service) {
      if (service) bleEntries[i].serviceUuids += ";";
      bleEntries[i].serviceUuids +=
          String(device->getServiceUUID(service).toString().c_str());
    }
    Serial.printf("[ble] %4ld dBm %s %s services=%d\n",
                  static_cast<long>(bleEntries[i].rssi),
                  bleEntries[i].address.c_str(), bleEntries[i].name.c_str(),
                  serviceCount);
  }
  sortBle();
  lastBleScanSdWriteOk = exportBleScanToSd();
  scanner->clearResults();
  releaseBleMemory();
  scanInProgress = false;
  drawBleResults();
}

void openWifiAudit(const WifiEntry& entry, View returnView) {
  selectedWifi = entry;
  auditReturnView = returnView;
  auditStatus = "";
  Serial.printf("[audit] selected %s (%s)\n", selectedWifi.ssid.c_str(),
                selectedWifi.bssid.c_str());
  drawWifiAudit();
}

void openBleDetail(const BleEntry& entry) {
  selectedBle = entry;
  Serial.printf("[ble] selected %s (%s)\n", selectedBle.name.c_str(),
                selectedBle.address.c_str());
  drawBleDetail();
}

void initializeDisplayAndTouch() {
#ifdef AWOK_HEADLESS
  display.begin();
  display.setTextWrap(false);
#elif defined(AWOK_MINI_DISPLAY)
  for (int pin : {AwokPins::kButtonLeft, AwokPins::kButtonCenter,
                  AwokPins::kButtonUp, AwokPins::kButtonRight,
                  AwokPins::kButtonDown}) {
    pinMode(pin, AwokPins::buttonHasInternalPullup(pin) ? INPUT_PULLUP : INPUT);
  }
  pinMode(AwokPins::kDisplayCs, OUTPUT);
  pinMode(AwokPins::kSdCs, OUTPUT);
  pinMode(AwokPins::kBacklight, OUTPUT);
  digitalWrite(AwokPins::kDisplayCs, HIGH);
  digitalWrite(AwokPins::kSdCs, HIGH);
  digitalWrite(AwokPins::kBacklight, HIGH); // off until panel initialization
  SPI.begin(AwokPins::kSpiSck, AwokPins::kSpiMiso, AwokPins::kSpiMosi, -1);
  if (!display.begin()) {
    while (true) delay(1000);
  }
  display.setTextWrap(false);
#elif defined(PANCAKE_DISPLAY)
  // Pancake C5: ST7796 on the FSPI bus, FT6336 capacitive touch on I2C. Touch
  // has no SPI chip select, so there is no CS juggling as on the XPT2046 path.
  pinMode(AwokPins::kDisplayCs, OUTPUT);
  pinMode(AwokPins::kSdCs, OUTPUT);
  digitalWrite(AwokPins::kDisplayCs, HIGH);
  digitalWrite(AwokPins::kSdCs, HIGH);
  SPI.begin(AwokPins::kSpiSck, AwokPins::kSpiMiso, AwokPins::kSpiMosi, -1);
  pinMode(AwokPins::kBacklight, OUTPUT);
  digitalWrite(AwokPins::kBacklight, AwokPins::kBacklightOn ? HIGH : LOW);
  display.begin(27000000);
  display.setRotation(0);
  display.setTextWrap(false);
  touch.begin();  // Wire + FT6336 on the I2C bus
#else
  pinMode(AwokPins::kDisplayCs, OUTPUT);
  pinMode(AwokPins::kTouchCs, OUTPUT);
  pinMode(AwokPins::kSdCs, OUTPUT);
  digitalWrite(AwokPins::kDisplayCs, HIGH);
  digitalWrite(AwokPins::kTouchCs, HIGH);
  digitalWrite(AwokPins::kSdCs, HIGH);
  touch.begin();
  SPI.end();
  SPI.begin(AwokPins::kSpiSck, AwokPins::kSpiMiso, AwokPins::kSpiMosi, -1);
  touch.setRotation(0);
  pinMode(AwokPins::kBacklight, OUTPUT);
  digitalWrite(AwokPins::kBacklight, AwokPins::kBacklightOn ? HIGH : LOW);
  display.begin(27000000);
  display.setRotation(0);
  display.setTextWrap(false);
#endif
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("AxD starting");
  Serial.println(AwokPins::kBoardLabel);
  Serial.printf("[board] %s, %s, result capacity=%d\n", AwokPins::kChipLabel,
                AwokPins::kDualBand ? "2.4/5 GHz" : "2.4 GHz", kResultCapacity);
  logMemory("boot");
  Serial.println(
      "Commands: w=Wi-Fi, c=channels, b=BLE, p=clients, k=packet mon, "
      "m=deauth watch, g=gps, n=link, u=GPS baud, r=raw NMEA, "
      "s=saved, t=settings, d=SD retry, h=home");
  initializeDisplayAndTouch();
  logMemory("after display init");
  loadDeviceSettings();
  noteActivity();
  setBacklightLit(true);
#ifndef AWOK_MINI_DISPLAY
  // SD + NVS + UI Strings fragment the largest internal block. STA used to
  // need a ~40 KB contiguous chunk; after the SD mount it is often ~34 KB
  // and esp_wifi_init fails (IDF then logs 0x3001 on cleanup). Bring Wi-Fi
  // up while the heap is still one large piece. BLE is NOT started here: the
  // radios time-share the DMA pool, so NimBLE is brought up only inside a BLE
  // window (RadioScheduler), with Wi-Fi torn down first -- keeping the boot
  // controller resident would strand the DMA and crash on the first teardown.
  ensureWifiStation(false);
#endif
#if defined(AWOK_MINI_DISPLAY) || defined(AWOK_HEADLESS)
  resultTablesReady = wifiEntries.initialize(resultTableExternalBytes) &&
      bleEntries.initialize(resultTableExternalBytes) &&
      clientEntries.initialize(resultTableExternalBytes) &&
      probeSsids.initialize(resultTableExternalBytes) &&
      karmaAps.initialize(resultTableExternalBytes);
  Serial.printf("[memory] result tables in PSRAM=%u bytes\n",
                unsigned(resultTableExternalBytes));
  logMemory("after result table allocation");
  if (!resultTablesReady) {
    showRadioError("Result table allocation failed");
    display.present(false);
    return;
  }
#endif
  if (!settingFlag(kSettingSkipSplash)) {
    drawBootScreen();  // Mini: splash() pushes to the panel itself
    delay(kBootScreenMs);
  }
  initializeSdCard();
  initGps();
  initializeFirmwareAudit();
  recordFirmwareAudit(
      "system", "boot", "success",
      "reset_reason=" + String(static_cast<int>(esp_reset_reason())) +
          "; sd=" + (sdReady ? String("ready") : String("unavailable")));
  loadSavedNetworks();
  if (sdReady) lastSavedSdWriteOk = exportSavedNetworksToSd();
  drawHome();
  display.present();  // both boards buffer now; blit the first frame
#ifdef AWOK_HEADLESS
  bridgeBleBegin();  // orange bridge: run the BLE GATT server the phone connects
                     // to; commands dispatch locally or relay to the white chip.
#else
  remoteBegin();  // Touch and Mini alike: listen for the orange bridge chip so
                  // a phone can drive this screen chip over ESP-NOW.
#endif
  logMemory("ready");
}

void loop() {
#if defined(AWOK_MINI_DISPLAY) || defined(AWOK_HEADLESS)
  if (!resultTablesReady) { delay(50); return; }
#endif
  updateGps();
  updateWardriveSessionStats();
#ifdef AWOK_HEADLESS
  bridgeServiceFileTransfer();
  bridgeServiceWardriveDashboard();
  if (bridgeFileTransferActive()) {
    // Keep radio-owning tools from hopping away during a reliable download.
    bridgeServiceCommand();
    delay(1);
    return;
  }
#endif
#ifdef AWOK_MINI_DISPLAY
  updateMiniJoystick();
#endif
  handleTouch();
  handleSerial();
  updateBacklightSleep();
  updateScreenTest();
  updateWifiScan();
  updateWifiSignalMonitor();
  updateDeauthMonitor();
  updateDeauthAttack();
  updateHandshakeCapture();
  updateClientSniffer();
  updateBeaconFlood();
  updateEvilPortal();
  updateWardrive();
  updatePacketMon();
  updateCameraScan();
  updateStatus();
  updateBleDetect();
  updateProbeLure();
  updateLocator();
  updateFleetHunt();
  updateTopologyMap();
  updateWps();
  updateRogueWatch();
  updateHiddenReveal();
  updateSecurityAudit();
  updateTrackerScan();
  updateBleIntel();
  updateSpectrogram();
  updateHarvester();
  updateProbeIntel();
  updateKarmaWatch();
  updateBeaconWatch();
  updateAuthFlood();
  updateAdvancedWatch();
  updateWifi6Intel();
  updateDeauthForensics();
  updateLink();
#ifdef AWOK_HEADLESS
  bridgeServiceCommand();  // run any phone command off the BLE host task
  // Bridge: stream live status to the phone about once a second (no ESP-NOW).
  static uint32_t lastBridgeStatusMs = 0;
  if (g_bridgePhoneConnected && millis() - lastBridgeStatusMs >= 1000) {
    lastBridgeStatusMs = millis();
    linkBroadcastStatus();
  }
#endif
  updateNetworkTools();
  // Live-refresh the GPS status screen while it is open.
  static uint32_t lastGpsScreenDrawMs = 0;
  if (currentView == View::kGps && millis() - lastGpsScreenDrawMs >= 1000) {
    lastGpsScreenDrawMs = millis();
    redrawGpsPage();
  }
  updateSettingsPage();
#ifdef AWOK_MINI_DISPLAY
  // Mini screen test writes the ST7735 directly, so skip the canvas blit there.
  if (currentView != View::kScreenTest) display.present();
#else
  display.present();  // dirty-gated: only blits when a view actually redrew
#endif
  delay(10);
}
