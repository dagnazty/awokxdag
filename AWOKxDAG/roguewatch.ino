// AWOKxDAG — rogue-AP / evil-twin detection (compiled as part of the sketch;
// see awok_common.h)
//
// Promiscuously parses beacons and flags: one SSID broadcast by multiple
// BSSIDs (classic evil-twin/karma), and a saved network's SSID appearing on a
// BSSID other than the one that was saved. rogueWatchActive lives in the main
// sketch (read by the input tab).

constexpr int kMaxRogueAps = 32;
constexpr int kRogueHitQueueSlots = 24;
constexpr uint32_t kRogueHopIntervalMs = 300;
constexpr uint32_t kRogueRedrawMs = 700;
constexpr char kRogueLogCsvPath[] = "/awokxdag/rogue_log.csv";
// RogueHit and ApSighting are declared in awok_common.h.

RogueHit rogueQueue[kRogueHitQueueSlots];
volatile int rogueHead = 0;
volatile int rogueTail = 0;
ApSighting rogueAps[kMaxRogueAps];
int rogueApCount = 0;
int rogueAlertCount = 0;
int rogueHopIndex = 0;
uint32_t lastRogueHopMs = 0;
uint32_t lastRogueDrawMs = 0;
uint32_t rogueStartMs = 0;
bool rogueLogReady = false;

void rogueBeaconCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* packet =
      static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* payload = packet->payload;
  const int length = packet->rx_ctrl.sig_len;
  if (length < 38) return;
  if ((payload[0] & 0xF0) != 0x80) return;  // beacon
  const uint8_t tag = payload[36];
  const uint8_t tagLen = payload[37];
  if (tag != 0x00 || tagLen == 0 || tagLen > 32) return;  // need a named SSID
  if (38 + tagLen > length) return;

  const int next = (rogueHead + 1) % kRogueHitQueueSlots;
  if (next == rogueTail) return;
  RogueHit& hit = rogueQueue[rogueHead];
  memcpy(hit.bssid, payload + 16, 6);
  hit.channel = packet->rx_ctrl.channel;
  hit.rssi = packet->rx_ctrl.rssi;
  hit.ssidLen = tagLen;
  memcpy(hit.ssid, payload + 38, tagLen);
  hit.ssid[tagLen] = 0;
  rogueHead = next;
}

int rogueIndexOf(const uint8_t* bssid) {
  for (int i = 0; i < rogueApCount; ++i) {
    bool equal = true;
    for (int j = 0; j < 6; ++j) {
      if (rogueAps[i].bssid[j] != bssid[j]) {
        equal = false;
        break;
      }
    }
    if (equal) return i;
  }
  return -1;
}

bool savedSsidOnOtherBssid(const ApSighting& ap) {
  for (int i = 0; i < savedCount; ++i) {
    if (savedEntries[i].ssid.length() &&
        savedEntries[i].ssid == ap.ssid &&
        !savedEntries[i].bssid.equalsIgnoreCase(macToString(ap.bssid))) {
      return true;
    }
  }
  return false;
}

void appendRogueLog(const ApSighting& ap, const char* reason) {
  if (!rogueLogReady) return;
  File file = SD.open(kRogueLogCsvPath, FILE_APPEND);
  if (!file) {
    rogueLogReady = false;
    sdReady = false;
    return;
  }
  file.printf("%lu,%s,%s,%d,%d,%s%s\n",
              static_cast<unsigned long>(millis()), csvField(ap.ssid).c_str(),
              macToString(ap.bssid).c_str(), static_cast<int>(ap.channel),
              static_cast<int>(ap.rssi), reason, gpsCsvFields().c_str());
  file.close();
}

void recomputeRogue() {
  rogueAlertCount = 0;
  for (int i = 0; i < rogueApCount; ++i) {
    bool multi = false;
    for (int j = 0; j < rogueApCount; ++j) {
      if (j != i && rogueAps[j].ssid == rogueAps[i].ssid) {
        multi = true;
        break;
      }
    }
    rogueAps[i].savedTwin = savedSsidOnOtherBssid(rogueAps[i]);
    rogueAps[i].suspicious = multi || rogueAps[i].savedTwin;
    if (rogueAps[i].suspicious) {
      ++rogueAlertCount;
      if (!rogueAps[i].logged) {
        appendRogueLog(rogueAps[i],
                       rogueAps[i].savedTwin ? "saved_twin" : "multi_bssid");
        rogueAps[i].logged = true;
      }
    }
  }
}

void mergeRogueHit(const RogueHit& hit) {
  int index = rogueIndexOf(hit.bssid);
  if (index < 0) {
    if (rogueApCount >= kMaxRogueAps) return;
    index = rogueApCount++;
    rogueAps[index] = ApSighting();
    memcpy(rogueAps[index].bssid, hit.bssid, 6);
  }
  rogueAps[index].ssid = String(hit.ssid);
  rogueAps[index].channel = hit.channel;
  rogueAps[index].rssi = hit.rssi;
  recomputeRogue();
}

void drawRogueWatch() {
  currentView = View::kRogueWatch;
  display.fillScreen(kBackground);
  drawHeader("ROGUE WATCH", rogueAlertCount
                                ? "ALERT: possible evil twin"
                                : "watching for AP impersonation");
  display.setTextSize(2);
  display.setTextColor(rogueAlertCount ? kBad : kGood, kBackground);
  display.setCursor(6, 50);
  display.print(rogueAlertCount ? "SUSPECT APs" : "ALL CLEAR");

  display.setTextSize(1);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 78);
  display.printf("%d AP(s) seen | %d alert(s) | ch %d", rogueApCount,
                 rogueAlertCount, kDeauthHopChannels[rogueHopIndex]);

  int row = 0;
  for (int i = 0; i < rogueApCount && row < 8; ++i) {
    if (!rogueAps[i].suspicious) continue;
    const int y = 96 + row * 20;
    display.setTextColor(kBad, kBackground);
    display.setCursor(6, y);
    display.print(clipped(rogueAps[i].ssid.length() ? rogueAps[i].ssid
                                                    : "<hidden>",
                          20));
    display.setTextColor(kMuted, kBackground);
    display.setCursor(6, y + 9);
    display.printf("%s %s", macToString(rogueAps[i].bssid).c_str(),
                   rogueAps[i].savedTwin ? "saved-twin" : "multi-BSSID");
    ++row;
  }
  if (rogueAlertCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(6, 110);
    display.print("No SSID seen on two BSSIDs yet.");
  }
  drawFooter("Home", "Home");
}

void startRogueWatch() {
  rogueApCount = 0;
  rogueAlertCount = 0;
  rogueHead = 0;
  rogueTail = 0;
  rogueHopIndex = 0;
  lastRogueHopMs = millis();
  lastRogueDrawMs = 0;
  rogueStartMs = millis();
  signalMonitorActive = false;

  if (ensureSdCard()) {
    SD.remove(kRogueLogCsvPath);
    File file = SD.open(kRogueLogCsvPath, FILE_WRITE);
    if (file) {
      file.println(
          "uptime_ms,ssid,bssid,channel,rssi,reason,latitude,longitude,"
          "altitude_m");
      file.close();
      rogueLogReady = true;
    }
  }

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&rogueBeaconCallback);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(kDeauthHopChannels[0], WIFI_SECOND_CHAN_NONE);

  rogueWatchActive = true;
  Serial.println("[rogue] watch started");
  drawRogueWatch();
}

void stopRogueWatch() {
  rogueWatchActive = false;
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true, false);
  Serial.printf("[rogue] stopped; %d alert(s)\n", rogueAlertCount);
}

void updateRogueWatch() {
  if (!rogueWatchActive || currentView != View::kRogueWatch) return;
  while (rogueTail != rogueHead) {
    mergeRogueHit(rogueQueue[rogueTail]);
    rogueTail = (rogueTail + 1) % kRogueHitQueueSlots;
  }
  const uint32_t now = millis();
  if (now - lastRogueHopMs >= kRogueHopIntervalMs) {
    lastRogueHopMs = now;
    rogueHopIndex = (rogueHopIndex + 1) % kDeauthHopChannelCount;
    esp_wifi_set_channel(kDeauthHopChannels[rogueHopIndex],
                         WIFI_SECOND_CHAN_NONE);
  }
  if (now - lastRogueDrawMs >= kRogueRedrawMs) {
    lastRogueDrawMs = now;
    drawRogueWatch();
  }
}
