#include "awok_common.h"

// The Wi-Fi driver refuses to transmit raw management frames it deems
// malformed, which includes deauthentication and disassociation frames.
// Overriding this sanity check lets the authorized deauth test inject them.
// Returning 0 tells the driver the frame is acceptable.
extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2,
                                                int32_t arg3) {
  return 0;
}

Adafruit_ILI9341 display(&SPI, AwokPins::kDisplayDc, AwokPins::kDisplayCs,
                         AwokPins::kDisplayReset);
XPT2046_Touchscreen touch(AwokPins::kTouchCs);

WifiEntry wifiEntries[kMaxResults];
WifiEntry savedEntries[kMaxSaved];
BleEntry bleEntries[kMaxResults];
WifiEntry selectedWifi;
BleEntry selectedBle;
int wifiCount = 0;
int savedCount = 0;
int bleCount = 0;
View currentView = View::kHome;
View auditReturnView = View::kWifi;
int reconPage = 0;
int monitorPage = 0;
int homePage = 0;
bool scanInProgress = false;
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
// SD export status for the new recon tabs (read by input.ino, which is
// concatenated before those tabs, so the flags must live in the main sketch).
bool lastAuditCsvOk = false;
bool lastTrackerCsvOk = false;
bool lastProbeIntelCsvOk = false;
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
uint8_t lastDeauthSource[6] = {0};
uint8_t lastDeauthBssid[6] = {0};
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
ClientEntry clientEntries[kMaxClients];
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
uint8_t wardriveMacs[kMaxWardriveMacs][6];
int wardriveMacCount = 0;
uint32_t wardriveBleCount = 0;
BleHit bleHitQueue[kBleHitQueueSlots];
volatile int bleHitHead = 0;
volatile int bleHitTail = 0;

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
  digitalWrite(AwokPins::kTouchCs, HIGH);
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
      "service_uuids");
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
    file.println(csvField(bleEntries[i].serviceUuids));
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
  Preferences preferences;
  if (!preferences.begin("awokxdag", true)) {
    Serial.println("[saved] could not open NVS");
    return;
  }

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
  Preferences preferences;
  if (!preferences.begin("awokxdag", false)) {
    lastSavedSdWriteOk = false;
    return false;
  }
  preferences.clear();
  preferences.putUChar("count", savedCount);
  for (int i = 0; i < savedCount; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "s%d", i);
    preferences.putString(key, savedEntries[i].ssid);
    snprintf(key, sizeof(key), "b%d", i);
    preferences.putString(key, savedEntries[i].bssid);
    snprintf(key, sizeof(key), "r%d", i);
    preferences.putInt(key, savedEntries[i].rssi);
    snprintf(key, sizeof(key), "c%d", i);
    preferences.putInt(key, savedEntries[i].channel);
    snprintf(key, sizeof(key), "a%d", i);
    preferences.putUChar(key, static_cast<uint8_t>(savedEntries[i].auth));
  }
  preferences.end();
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
      return true;
    }
    for (int i = savedCount; i > existing; --i) {
      savedEntries[i] = savedEntries[i - 1];
    }
    savedEntries[existing] = removed;
    ++savedCount;
    return false;
  }

  if (savedCount >= kMaxSaved) {
    Serial.println("[saved] list full");
    return false;
  }
  savedEntries[savedCount++] = entry;
  if (writeSavedNetworks()) {
    Serial.printf("[saved] added %s\n", entry.ssid.c_str());
    return true;
  }
  --savedCount;
  return false;
}

void drawButton(int x, int y, int w, int h, const String& label,
                uint16_t outline) {
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
  display.fillRect(0, 0, kScreenWidth, kHeaderHeight, kPanel);
  display.setTextColor(kAccent, kPanel);
  display.setTextSize(2);
  display.setCursor(8, 7);
  display.print(title);
  if (detail.length()) {
    display.setTextColor(ILI9341_WHITE, kPanel);
    display.setTextSize(1);
    display.setCursor(8, 29);
    display.print(clipped(detail, 34));
  }
  // GPS fix indicator: green = fix, yellow = data but no fix, dim = no data.
  const uint16_t gpsColor =
      gpsHasFix() ? kGood
                  : (gpsCharsProcessed() > 10 ? kWarn : kMuted);
  display.fillCircle(224, 10, 4, gpsColor);
  display.setTextColor(gpsColor, kPanel);
  display.setTextSize(1);
  display.setCursor(202, 7);
  display.print("GPS");
}

void drawFooter(const char* leftLabel, const char* rightLabel) {
  display.fillRect(0, kFooterTop, kScreenWidth, kScreenHeight - kFooterTop,
                   kBackground);
  drawButton(6, 284, 106, 30, leftLabel, kMuted);
  drawButton(128, 284, 106, 30, rightLabel, kAccent);
}

void drawSmallButton(int x, int y, int w, int h, const String& label,
                     uint16_t outline) {
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
  drawHeader("ABOUT", "AWOKxDAG");
  display.setTextSize(2);
  display.setTextColor(kAccent, kBackground);
  display.setCursor(6, 52);
  display.print("AWOKxDAG");
  display.setTextSize(1);
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(6, 80);
  display.print("Dual-band Wi-Fi/BLE pentest toolkit");
  display.setCursor(6, 92);
  display.print("for the ESP32-C5 (AWOK Dual C5).");

  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 116);
  display.print("Author:  ");
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.print(kAuthor);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 130);
  display.print("Version: ");
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.print(kVersion);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 144);
  display.print("Board:   ESP32-C5, ILI9341 touch");
  display.setCursor(6, 158);
  display.print("Storage: microSD at /awokxdag");

  display.drawFastHLine(6, 176, 228, kPanel);
  display.setTextColor(kBad, kBackground);
  display.setCursor(6, 186);
  display.print("Authorized testing only. You are");
  display.setCursor(6, 198);
  display.print("responsible for how you use this.");
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 218);
  display.print("Tap anywhere to go back.");
  drawFooter("< Back", "< Back");
}

void drawHome() {
  signalMonitorActive = false;
  currentView = View::kHome;
  if (homePage == 1) {
    drawAboutPage();
    return;
  }
  display.fillScreen(kBackground);
  drawHeader("AWOKxDAG", sdReady ? "SD ready | pentest toolkit"
                                 : "SD missing | pentest toolkit");
  drawButton(20, 44, 200, 40, "Recon");
  drawButton(20, 88, 200, 40, "Attacks", kBad);
  drawButton(20, 132, 200, 40, "Monitor");
  drawButton(20, 176, 200, 40, "GPS");
  drawButton(20, 220, 200, 40, "Status");
  drawFooter(kVersion, "About >");
}

void drawBootScreen() {
  // XBM stores black source pixels as set bits. Painting those black over a
  // white canvas preserves the supplied white-on-black composition exactly.
  display.fillScreen(ILI9341_WHITE);
  display.drawXBitmap(0, 0, kBootScreenBitmap, kBootScreenWidth,
                      kBootScreenHeight, ILI9341_BLACK);
}

void drawScanning(const String& kind) {
  display.fillScreen(kBackground);
  drawHeader(kind + " SCAN", "passive discovery in progress");
  display.setTextColor(kAccent, kBackground);
  display.setTextSize(2);
  display.setCursor(43, 135);
  display.print("Scanning...");
}

void drawWifiResults() {
  currentView = View::kWifi;
  display.fillScreen(kBackground);
  String detail = String(wifiCount) + " APs | SD ";
  detail += lastScanSdWriteOk ? "saved" : (sdReady ? "write error" : "missing");
  drawHeader("WI-FI RESULTS", detail);
  display.setTextSize(1);
  const int rows = min(wifiCount, kVisibleRows);
  for (int i = 0; i < rows; ++i) {
    const int y = 48 + i * 22;
    display.setTextColor(isSaved(wifiEntries[i]) ? kAccent : ILI9341_WHITE,
                         kBackground);
    display.setCursor(5, y);
    display.print(clipped(wifiEntries[i].ssid.length() ? wifiEntries[i].ssid
                                                       : "<hidden>",
                          20));
    if (isSaved(wifiEntries[i])) display.print(" *");
    display.setTextColor(kMuted, kBackground);
    display.setCursor(5, y + 11);
    display.printf("%4ld dBm  ch%-3ld %sG  %s",
                   static_cast<long>(wifiEntries[i].rssi),
                   static_cast<long>(wifiEntries[i].channel),
                   bandLabel(wifiEntries[i].channel),
                   authShortLabel(wifiEntries[i].auth));
  }
  if (wifiCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(52, 145);
    display.print("No access points found");
  }
  drawThreeButtonFooter("Home", "Deauth", "Rescan");
}

void drawSavedNetworks() {
  currentView = View::kSaved;
  display.fillScreen(kBackground);
  String detail = String(savedCount) + " saved | SD ";
  detail += lastSavedSdWriteOk ? "synced" : (sdReady ? "ready" : "missing");
  drawHeader("SAVED NETWORKS", detail);
  display.setTextSize(1);
  for (int i = 0; i < savedCount; ++i) {
    const int y = 48 + i * 22;
    display.setTextColor(kAccent, kBackground);
    display.setCursor(5, y);
    display.print(clipped(savedEntries[i].ssid.length() ? savedEntries[i].ssid
                                                        : "<hidden>",
                          24));
    display.setTextColor(kMuted, kBackground);
    display.setCursor(5, y + 11);
    display.printf("%4ld dBm  ch%-3ld %s",
                   static_cast<long>(savedEntries[i].rssi),
                   static_cast<long>(savedEntries[i].channel),
                   authShortLabel(savedEntries[i].auth));
  }
  if (savedCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(43, 137);
    display.print("No saved networks yet");
    display.setCursor(24, 153);
    display.print("Scan Wi-Fi, select one, then Save");
  }
  drawFooter("Home", "Wi-Fi Scan");
}

void drawBleResults() {
  currentView = View::kBle;
  display.fillScreen(kBackground);
  String detail = String(bleCount) + " advertisers | SD ";
  detail +=
      lastBleScanSdWriteOk ? "saved" : (sdReady ? "write error" : "missing");
  drawHeader("BLE RESULTS", detail);
  display.setTextSize(1);
  const int rows = min(bleCount, kVisibleRows);
  for (int i = 0; i < rows; ++i) {
    const int y = 48 + i * 22;
    display.setTextColor(ILI9341_WHITE, kBackground);
    display.setCursor(5, y);
    display.print(clipped(bleEntries[i].name.length() ? bleEntries[i].name
                                                      : "<unnamed>",
                          20));
    display.setTextColor(kMuted, kBackground);
    display.setCursor(5, y + 11);
    display.printf("%4ld dBm  %s", static_cast<long>(bleEntries[i].rssi),
                   bleEntries[i].address.c_str());
  }
  if (bleCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(54, 145);
    display.print("No advertisers found");
  }
  drawFooter("Home", "Rescan");
}

void drawBleDetail() {
  currentView = View::kBleDetail;
  display.fillScreen(kBackground);
  drawHeader("BLE INSPECT",
             selectedBle.name.length() ? selectedBle.name : "<unnamed>");
  display.setTextSize(1);

  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 50);
  display.print("Address: ");
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.print(selectedBle.address);

  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 66);
  display.print("Type: ");
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.print(bleAddressTypeLabel(selectedBle.addressType));

  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 82);
  display.printf("Signal: %ld dBm (%s)", static_cast<long>(selectedBle.rssi),
                 signalLabel(selectedBle.rssi));
  display.setCursor(6, 98);
  if (selectedBle.hasTxPower) {
    display.printf("TX power: %ld dBm", static_cast<long>(selectedBle.txPower));
  } else {
    display.print("TX power: not advertised");
  }
  display.setCursor(6, 114);
  display.printf("Connectable: %s   Scannable: %s",
                 selectedBle.connectable ? "yes" : "no",
                 selectedBle.scannable ? "yes" : "no");
  display.setCursor(6, 130);
  display.printf("Advertisement bytes: %u", selectedBle.advertisementBytes);

  display.drawFastHLine(6, 145, 228, kPanel);
  display.setTextColor(kAccent, kBackground);
  display.setCursor(6, 153);
  display.print("MANUFACTURER");
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(6, 169);
  if (selectedBle.manufacturerId >= 0) {
    display.printf("Company ID: 0x%04lX",
                   static_cast<long>(selectedBle.manufacturerId));
  } else {
    display.print("Company ID: not advertised");
  }
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 185);
  display.print("Data: ");
  display.print(selectedBle.manufacturerDataHex.length()
                    ? clipped(selectedBle.manufacturerDataHex, 31)
                    : "not advertised");

  display.setTextColor(kAccent, kBackground);
  display.setCursor(6, 205);
  display.print("SERVICE UUIDS");
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(6, 221);
  if (selectedBle.serviceUuids.length()) {
    display.print(clipped(selectedBle.serviceUuids, 37));
    if (selectedBle.serviceUuids.length() > 37) {
      display.setCursor(6, 234);
      display.print(clipped(selectedBle.serviceUuids.substring(37), 37));
    }
  } else {
    display.print("None advertised");
  }
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 258);
  display.print("Passive advertisement metadata only.");
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

  if (wifiCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(43, 132);
    display.print("No channel data available");
    display.setCursor(36, 149);
    display.print("Tap Scan to collect it now");
    drawFooter("Home", "Scan");
    return;
  }

  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(5, 49);
  display.print("2.4 GHz access points per channel");
  int maximum24 = 1;
  for (int channel = 1; channel <= 14; ++channel) {
    maximum24 = max(maximum24, accessPointsOnChannel(channel));
  }
  constexpr int kBase24 = 126;
  display.drawFastHLine(4, kBase24, 232, kMuted);
  for (int channel = 1; channel <= 14; ++channel) {
    const int count = accessPointsOnChannel(channel);
    const int height = count ? max(3, count * 55 / maximum24) : 1;
    const int x = 4 + (channel - 1) * 16;
    display.fillRect(x, kBase24 - height, 11, height,
                     channelBarColor(count));
    display.setTextColor(kMuted, kBackground);
    display.setCursor(x + (channel < 10 ? 3 : 0), 130);
    display.print(channel);
  }

  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(5, 151);
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

  constexpr int kBase5 = 243;
  display.drawFastHLine(4, kBase5, 232, kMuted);
  if (channel5Count == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(53, 202);
    display.print("No 5 GHz APs detected");
  } else {
    int maximum5 = 1;
    for (int i = 0; i < channel5Count; ++i) {
      maximum5 = max(maximum5, counts5[i]);
    }
    const int spacing = 230 / channel5Count;
    const int barWidth = min(18, spacing - 4);
    for (int i = 0; i < channel5Count; ++i) {
      const int height = max(3, counts5[i] * 62 / maximum5);
      const int x = 5 + i * spacing + (spacing - barWidth) / 2;
      display.fillRect(x, kBase5 - height, barWidth, height,
                       channelBarColor(counts5[i]));
      display.setTextColor(kMuted, kBackground);
      display.setCursor(x, 248);
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

void drawAuditFinding(int y, uint16_t color, const String& text) {
  display.fillCircle(9, y + 3, 3, color);
  display.setTextColor(color, kBackground);
  display.setCursor(17, y);
  display.print(clipped(text, 36));
}

void drawWifiAudit() {
  currentView = View::kWifiAudit;
  display.fillScreen(kBackground);
  drawHeader("WI-FI AUDIT",
             selectedWifi.ssid.length() ? selectedWifi.ssid : "<hidden>");
  display.setTextSize(1);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 49);
  display.print("BSSID: ");
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.print(selectedWifi.bssid.length() ? selectedWifi.bssid : "unknown");
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 64);
  display.print("Security: ");
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.print(authLongLabel(selectedWifi.auth));
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 79);
  display.printf("Signal: %ld dBm (%s)", static_cast<long>(selectedWifi.rssi),
                 signalLabel(selectedWifi.rssi));
  display.setCursor(6, 94);
  display.printf("Channel: %ld / %s GHz", static_cast<long>(selectedWifi.channel),
                 bandLabel(selectedWifi.channel));
  display.setCursor(6, 109);
  display.printf("Saved: %s", isSaved(selectedWifi) ? "yes" : "no");
  display.drawFastHLine(6, 124, 228, kPanel);
  display.setTextColor(kAccent, kBackground);
  display.setCursor(6, 132);
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
  display.setCursor(6, 221);
  display.print("RECOMMEND: ");
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.print(clipped(auditRecommendation(selectedWifi), 27));
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 242);
  display.print("Metadata only; no connection attempted.");
  if (auditStatus.length()) {
    display.setTextColor(kAccent, kBackground);
    display.setCursor(6, 257);
    display.print(clipped(auditStatus, 37));
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
  display.setCursor(6, 49);
  display.print("BSSID: ");
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.print(selectedWifi.bssid);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 64);
  if (signalSampleCount > 0 &&
      signalSamples[signalSampleCount - 1] > -127) {
    const int32_t latest = signalSamples[signalSampleCount - 1];
    display.printf("Latest: %ld dBm (%s)", static_cast<long>(latest),
                   signalLabel(latest));
  } else {
    display.print("Latest: waiting for target");
  }
  display.setCursor(6, 79);
  display.printf("Channel %ld | samples %d | misses %d",
                 static_cast<long>(selectedWifi.channel), signalSampleCount,
                 signalMisses);

  constexpr int kGraphLeft = 34;
  constexpr int kGraphRight = 232;
  constexpr int kGraphTop = 96;
  constexpr int kGraphBottom = 242;
  const int levels[] = {-40, -60, -80, -100};
  for (int level : levels) {
    const int y = map(level, -100, -30, kGraphBottom, kGraphTop);
    display.setTextColor(kMuted, kBackground);
    display.setCursor(4, y - 3);
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
    const int x = kGraphLeft + 3 +
                  i * (kGraphRight - kGraphLeft - 6) /
                      (kSignalSampleCount - 1);
    const int constrainedRssi = constrain(signalSamples[i], -100, -30);
    const int y = map(constrainedRssi, -100, -30, kGraphBottom - 3,
                      kGraphTop + 3);
    const uint16_t color = signalColor(signalSamples[i]);
    if (previousX >= 0) display.drawLine(previousX, previousY, x, y, color);
    display.fillCircle(x, y, 2, color);
    previousX = x;
    previousY = y;
  }

  display.setTextColor(signalSdLogReady ? kAccent : kMuted, kBackground);
  display.setCursor(6, 258);
  display.print(signalSdLogReady ? "SD logging: latest_wifi_signal.csv"
                                 : "SD logging unavailable; graph still works");
  drawThreeButtonFooter("Back", "Restart", "Locate");
}

int32_t sampleSelectedWifiSignal(bool& found) {
  WiFi.mode(WIFI_STA);
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

// Data-driven Recon menu: append an item here (label + a case in
// launchReconItem) and it paginates automatically. 6 items per page.
const char* const kReconItems[] = {
    "Wi-Fi Scan",   "Channel Map",  "BLE Scan",     "Clients",
    "Packet Mon",   "WPS Scan",     "Hidden SSID",  "Cameras",
    "Security Audit", "BLE Trackers", "Harvester",  "Probe Intel",
    "Saved"};
constexpr int kReconItemCount =
    static_cast<int>(sizeof(kReconItems) / sizeof(kReconItems[0]));
constexpr int kMenuPerPage = 6;
constexpr int kMenuFirstY = 50;
constexpr int kMenuRowPitch = 32;
constexpr int kMenuRowHeight = 30;

int reconPageCount() {
  return (kReconItemCount + kMenuPerPage - 1) / kMenuPerPage;
}

String reconItemLabel(int index) {
  if (strcmp(kReconItems[index], "Saved") == 0) {
    return "Saved (" + String(savedCount) + ")";
  }
  return String(kReconItems[index]);
}

void launchReconItem(int index) {
  const String label = kReconItems[index];
  if (label == "Wi-Fi Scan") {
    scanWifi();
  } else if (label == "Channel Map") {
    if (wifiCount) {
      drawChannelMap();
    } else {
      scanWifiForChannelMap();
    }
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
  } else if (label == "Harvester") {
    startHarvester();
  } else if (label == "Probe Intel") {
    startProbeIntel();
  } else if (label == "Saved") {
    drawSavedNetworks();
  }
}

void drawReconMenu() {
  currentView = View::kRecon;
  const int pages = reconPageCount();
  if (reconPage >= pages) reconPage = 0;
  display.fillScreen(kBackground);
  drawHeader("RECON", pages > 1 ? "passive discovery  " + String(reconPage + 1) +
                                      "/" + String(pages)
                                : "passive discovery");
  const int start = reconPage * kMenuPerPage;
  for (int row = 0; row < kMenuPerPage; ++row) {
    const int index = start + row;
    if (index >= kReconItemCount) break;
    drawButton(20, kMenuFirstY + row * kMenuRowPitch, 200, kMenuRowHeight,
               reconItemLabel(index));
  }
  if (pages > 1) {
    drawThreeButtonFooter("Home", "< Prev", "Next >");
  } else {
    drawFooter("Home", "Home");
  }
}

// Data-driven Monitor menu (mirrors the Recon menu): append an item here plus a
// case in launchMonitorItem and it paginates automatically. 6 items per page.
const char* const kMonitorItems[] = {
    "Deauth Watch",   "Rogue Watch", "BLE Spam Watch",
    "Karma Watch",    "Beacon Watch", "Auth Flood"};
constexpr int kMonitorItemCount =
    static_cast<int>(sizeof(kMonitorItems) / sizeof(kMonitorItems[0]));

int monitorPageCount() {
  return (kMonitorItemCount + kMenuPerPage - 1) / kMenuPerPage;
}

void launchMonitorItem(int index) {
  const String label = kMonitorItems[index];
  if (label == "Deauth Watch") {
    startDeauthMonitor();
  } else if (label == "Rogue Watch") {
    startRogueWatch();
  } else if (label == "BLE Spam Watch") {
    startBleDetect();
  } else if (label == "Karma Watch") {
    startKarmaWatch();
  } else if (label == "Beacon Watch") {
    startBeaconWatch();
  } else if (label == "Auth Flood") {
    startAuthFlood();
  }
}

void drawMonitorMenu() {
  currentView = View::kMonitor;
  const int pages = monitorPageCount();
  if (monitorPage >= pages) monitorPage = 0;
  display.fillScreen(kBackground);
  drawHeader("MONITOR", pages > 1 ? "detect attacks  " + String(monitorPage + 1) +
                                        "/" + String(pages)
                                  : "detect attacks in the air");
  const int start = monitorPage * kMenuPerPage;
  for (int row = 0; row < kMenuPerPage; ++row) {
    const int index = start + row;
    if (index >= kMonitorItemCount) break;
    drawButton(20, kMenuFirstY + row * kMenuRowPitch, 200, kMenuRowHeight,
               kMonitorItems[index]);
  }
  if (pages > 1) {
    drawThreeButtonFooter("Home", "< Prev", "Next >");
  } else {
    drawFooter("Home", "Home");
  }
}

void drawAttacksMenu() {
  currentView = View::kAttacks;
  display.fillScreen(kBackground);
  drawHeader("ATTACK TOOLS", "active RF | authorized use only");
  drawButton(20, 44, 200, 38, "Beacon Flood");
  drawButton(20, 86, 200, 38, "Evil Portal");
  drawButton(20, 128, 200, 38, "Evil Twin");
  drawButton(20, 170, 200, 38, "Probe Lure");
  display.setTextSize(1);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(18, 216);
  display.print("Evil Twin/Probe Lure use the last-");
  display.setCursor(18, 228);
  display.print("scanned SSID. Deauth: under an AP.");
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

void scanWifi() {
  if (scanInProgress) return;
  scanInProgress = true;
  drawScanning("WI-FI");
  Serial.println("[wifi] passive access-point scan started");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  delay(100);
  const int found = WiFi.scanNetworks(false, true, true);
  wifiCount = found > 0 ? min(found, kMaxResults) : 0;
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
  drawWifiResults();
}

void scanWifiForChannelMap() {
  scanWifi();
  drawChannelMap();
}

void scanBle() {
  if (scanInProgress) return;
  scanInProgress = true;
  drawScanning("BLE");
  Serial.println("[ble] passive advertisement scan started");
  NimBLEScan* scanner = NimBLEDevice::getScan();
  scanner->clearResults();
  scanner->setActiveScan(false);
  scanner->setInterval(100);
  scanner->setWindow(80);
  scanner->setMaxResults(kMaxResults);
  NimBLEScanResults results = scanner->getResults(kBleScanMs, false);
  bleCount = min(results.getCount(), kMaxResults);
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
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("AWOKxDAG starting");
  Serial.println(
      "Commands: w=Wi-Fi, c=channels, b=BLE, p=clients, g=gps, "
      "m=deauth watch, s=saved, d=SD retry, h=home");
  initializeDisplayAndTouch();
  drawBootScreen();
  delay(kBootScreenMs);
  initializeSdCard();
  loadSavedNetworks();
  if (sdReady) lastSavedSdWriteOk = exportSavedNetworksToSd();
  initGps();
  drawHome();
  NimBLEDevice::init("");
  NimBLEDevice::setPower(3);
}

void loop() {
  updateGps();
  handleTouch();
  handleSerial();
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
  updateWps();
  updateRogueWatch();
  updateHiddenReveal();
  updateSecurityAudit();
  updateTrackerScan();
  updateHarvester();
  updateProbeIntel();
  updateKarmaWatch();
  updateBeaconWatch();
  updateAuthFlood();
  // Live-refresh the GPS status screen while it is open.
  static uint32_t lastGpsScreenDrawMs = 0;
  if (currentView == View::kGps && millis() - lastGpsScreenDrawMs >= 1000) {
    lastGpsScreenDrawMs = millis();
    drawGps();
  }
  delay(10);
}
