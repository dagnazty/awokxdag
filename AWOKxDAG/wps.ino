// AWOKxDAG — WPS scan (compiled as part of the sketch; see awok_common.h)
//
// Promiscuously parses beacons for the WPS information element and lists APs
// that advertise WPS, flagging whether "AP Setup Locked" is set. wpsScanActive
// lives in the main sketch (read by the input tab); the rest is local here.

constexpr int kMaxWps = 24;
constexpr int kWpsHitQueueSlots = 24;
constexpr uint32_t kWpsHopIntervalMs = 300;
constexpr uint32_t kWpsRedrawMs = 700;

WpsEntry wpsEntries[kMaxWps];
int wpsCount = 0;
WpsHit wpsHitQueue[kWpsHitQueueSlots];
volatile int wpsHitHead = 0;
volatile int wpsHitTail = 0;
int wpsHopIndex = 0;
uint32_t lastWpsHopMs = 0;
uint32_t lastWpsDrawMs = 0;
uint32_t wpsScanStartMs = 0;

// Runs in the Wi-Fi task: walk beacon IEs, detect the WPS vendor IE and the
// AP Setup Locked attribute, enqueue a POD hit.
void wpsSnifferCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* packet =
      static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* payload = packet->payload;
  const int length = packet->rx_ctrl.sig_len;
  if (length < 38) return;
  if ((payload[0] & 0xF0) != 0x80) return;  // beacon subtype

  bool hasWps = false;
  bool locked = false;
  char ssid[33];
  uint8_t ssidLen = 0;
  ssid[0] = 0;

  int i = 36;  // tagged parameters start after the fixed beacon fields
  while (i + 2 <= length) {
    const uint8_t tag = payload[i];
    const uint8_t tagLen = payload[i + 1];
    if (i + 2 + tagLen > length) break;
    const uint8_t* data = payload + i + 2;
    if (tag == 0x00) {  // SSID
      ssidLen = tagLen > 32 ? 32 : tagLen;
      memcpy(ssid, data, ssidLen);
      ssid[ssidLen] = 0;
    } else if (tag == 0xDD && tagLen >= 4 && data[0] == 0x00 &&
               data[1] == 0x50 && data[2] == 0xF2 && data[3] == 0x04) {
      hasWps = true;  // WPS vendor IE (OUI 00:50:F2, type 0x04)
      int j = 4;
      while (j + 4 <= tagLen) {
        const uint16_t attrType = (data[j] << 8) | data[j + 1];
        const uint16_t attrLen = (data[j + 2] << 8) | data[j + 3];
        if (j + 4 + attrLen > tagLen) break;
        if (attrType == 0x1057 && attrLen >= 1) {  // AP Setup Locked
          locked = data[j + 4] != 0;
        }
        j += 4 + attrLen;
      }
    }
    i += 2 + tagLen;
  }
  if (!hasWps) return;

  const int next = (wpsHitHead + 1) % kWpsHitQueueSlots;
  if (next == wpsHitTail) return;
  WpsHit& hit = wpsHitQueue[wpsHitHead];
  memcpy(hit.bssid, payload + 16, 6);
  hit.channel = packet->rx_ctrl.channel;
  hit.rssi = packet->rx_ctrl.rssi;
  hit.locked = locked;
  hit.ssidLen = ssidLen;
  memcpy(hit.ssid, ssid, ssidLen + 1);
  wpsHitHead = next;
}

int wpsIndexOf(const uint8_t* bssid) {
  for (int i = 0; i < wpsCount; ++i) {
    bool equal = true;
    for (int j = 0; j < 6; ++j) {
      if (wpsEntries[i].bssid[j] != bssid[j]) {
        equal = false;
        break;
      }
    }
    if (equal) return i;
  }
  return -1;
}

void mergeWpsHit(const WpsHit& hit) {
  int index = wpsIndexOf(hit.bssid);
  if (index < 0) {
    if (wpsCount >= kMaxWps) return;
    index = wpsCount++;
    memcpy(wpsEntries[index].bssid, hit.bssid, 6);
  }
  WpsEntry& entry = wpsEntries[index];
  entry.ssid = hit.ssidLen ? String(hit.ssid) : String();
  entry.channel = hit.channel;
  entry.rssi = hit.rssi;
  entry.locked = hit.locked;
}

void drawWpsScan() {
  currentView = View::kWpsScan;
  display.fillScreen(kBackground);
  drawHeader("WPS SCAN", String(wpsCount) + " WPS AP(s) | ch " +
                             String(kDeauthHopChannels[wpsHopIndex]));
  display.setTextSize(1);
  const int rows = min(wpsCount, kVisibleRows);
  for (int i = 0; i < rows; ++i) {
    const int y = 48 + i * 22;
    display.setTextColor(ILI9341_WHITE, kBackground);
    display.setCursor(5, y);
    display.print(clipped(wpsEntries[i].ssid.length() ? wpsEntries[i].ssid
                                                      : "<hidden>",
                          22));
    display.setTextColor(wpsEntries[i].locked ? kMuted : kBad, kBackground);
    display.setCursor(5, y + 11);
    display.printf("%4ld dBm ch%-3d %s WPS",
                   static_cast<long>(wpsEntries[i].rssi),
                   static_cast<int>(wpsEntries[i].channel),
                   wpsEntries[i].locked ? "LOCKED" : "OPEN");
  }
  if (wpsCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(40, 145);
    display.print("Listening for WPS beacons...");
  }
  drawFooter("Back", "Back");
}

void startWpsScan() {
  wpsCount = 0;
  wpsHitHead = 0;
  wpsHitTail = 0;
  wpsHopIndex = 0;
  lastWpsHopMs = millis();
  lastWpsDrawMs = 0;
  wpsScanStartMs = millis();
  signalMonitorActive = false;

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&wpsSnifferCallback);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(kDeauthHopChannels[0], WIFI_SECOND_CHAN_NONE);

  wpsScanActive = true;
  Serial.println("[wps] scan started");
  drawWpsScan();
}

void stopWpsScan() {
  wpsScanActive = false;
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true, false);
  Serial.printf("[wps] stopped; %d WPS AP(s)\n", wpsCount);
}

void updateWps() {
  if (!wpsScanActive || currentView != View::kWpsScan) return;
  while (wpsHitTail != wpsHitHead) {
    mergeWpsHit(wpsHitQueue[wpsHitTail]);
    wpsHitTail = (wpsHitTail + 1) % kWpsHitQueueSlots;
  }
  const uint32_t now = millis();
  if (now - lastWpsHopMs >= kWpsHopIntervalMs) {
    lastWpsHopMs = now;
    wpsHopIndex = (wpsHopIndex + 1) % kDeauthHopChannelCount;
    esp_wifi_set_channel(kDeauthHopChannels[wpsHopIndex], WIFI_SECOND_CHAN_NONE);
  }
  if (now - lastWpsDrawMs >= kWpsRedrawMs) {
    lastWpsDrawMs = now;
    drawWpsScan();
  }
}
