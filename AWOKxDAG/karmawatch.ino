// AWOKxDAG — Karma Watch (compiled as part of the sketch; see awok_common.h)
//
// The inverse of Rogue Watch: instead of one SSID on many BSSIDs, this flags a
// single BSSID that answers for MANY different SSIDs -- the signature of a
// WiFi Pineapple / Karma / MANA rogue AP that beacons or probe-responds for
// every network a victim looks for. Promiscuous, listen-only. karmaWatchActive
// lives in the main sketch (read by the input tab).

RogueHit karmaQueue[kKarmaHitQueueSlots];
volatile int karmaHead = 0;
volatile int karmaTail = 0;
KarmaEntry karmaAps[kMaxKarmaAps];
int karmaApCount = 0;
int karmaAlertCount = 0;
int karmaHopIndex = 0;
uint32_t lastKarmaHopMs = 0;
uint32_t lastKarmaDrawMs = 0;
uint32_t karmaStartMs = 0;
bool karmaLogReady = false;

// Runs in the Wi-Fi task: pull BSSID + named SSID from beacons and probe
// responses (both carry the SSID as the first tagged element at offset 36).
void karmaCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* packet =
      static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* payload = packet->payload;
  const int length = packet->rx_ctrl.sig_len;
  if (length < 38) return;
  const uint8_t subtype = payload[0] & 0xF0;
  if (subtype != 0x80 && subtype != 0x50) return;  // beacon or probe response
  const uint8_t tag = payload[36];
  const uint8_t tagLen = payload[37];
  if (tag != 0x00 || tagLen == 0 || tagLen > 32) return;  // need a named SSID
  if (38 + tagLen > length) return;

  const int next = (karmaHead + 1) % kKarmaHitQueueSlots;
  if (next == karmaTail) return;
  RogueHit& hit = karmaQueue[karmaHead];
  memcpy(hit.bssid, payload + 16, 6);
  hit.channel = packet->rx_ctrl.channel;
  hit.rssi = packet->rx_ctrl.rssi;
  hit.ssidLen = tagLen;
  memcpy(hit.ssid, payload + 38, tagLen);
  hit.ssid[tagLen] = 0;
  karmaHead = next;
}

int karmaIndexOf(const uint8_t* bssid) {
  for (int i = 0; i < karmaApCount; ++i) {
    bool equal = true;
    for (int j = 0; j < 6; ++j) {
      if (karmaAps[i].bssid[j] != bssid[j]) { equal = false; break; }
    }
    if (equal) return i;
  }
  return -1;
}

void appendKarmaLog(const KarmaEntry& ap) {
  if (!karmaLogReady) return;
  File file = SD.open(kKarmaLogCsvPath, FILE_APPEND);
  if (!file) {
    karmaLogReady = false;
    sdReady = false;
    return;
  }
  file.printf("%lu,%s,%d,%d,%d%s%s\n",
              static_cast<unsigned long>(millis()),
              macToString(ap.bssid).c_str(), static_cast<int>(ap.channel),
              static_cast<int>(ap.rssi), ap.ssidCount, ap.overflow ? "+" : "",
              gpsCsvFields().c_str());
  file.close();
}

void mergeKarmaHit(const RogueHit& hit) {
  int index = karmaIndexOf(hit.bssid);
  if (index < 0) {
    if (karmaApCount >= kMaxKarmaAps) return;
    index = karmaApCount++;
    karmaAps[index] = KarmaEntry();
    memcpy(karmaAps[index].bssid, hit.bssid, 6);
  }
  KarmaEntry& ap = karmaAps[index];
  ap.channel = hit.channel;
  ap.rssi = hit.rssi;
  const String ssid = String(hit.ssid);
  bool known = false;
  for (int i = 0; i < ap.ssidCount; ++i) {
    if (ap.ssids[i] == ssid) { known = true; break; }
  }
  if (!known) {
    if (ap.ssidCount < kKarmaSsidsPerAp) {
      ap.ssids[ap.ssidCount++] = ssid;
    } else {
      ap.overflow = true;
    }
  }
  const bool wasSuspicious = ap.suspicious;
  ap.suspicious = ap.ssidCount >= kKarmaSsidThreshold || ap.overflow;
  if (ap.suspicious && !ap.logged) {
    appendKarmaLog(ap);
    ap.logged = true;
  }
  if (ap.suspicious && !wasSuspicious) ++karmaAlertCount;
}

void drawKarmaWatch() {
  currentView = View::kKarmaWatch;
  display.fillScreen(kBackground);
  drawHeader("KARMA WATCH", karmaAlertCount
                                ? "ALERT: multi-SSID AP (Pineapple?)"
                                : "watching for Karma / fake AP");
  display.setTextSize(2);
  display.setTextColor(karmaAlertCount ? kBad : kGood, kBackground);
  display.setCursor(6, 50);
  display.print(karmaAlertCount ? "KARMA AP" : "ALL CLEAR");

  display.setTextSize(1);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 78);
  display.printf("%d AP(s) | %d alert(s) | ch %d", karmaApCount, karmaAlertCount,
                 kDeauthHopChannels[karmaHopIndex]);

  int row = 0;
  for (int i = 0; i < karmaApCount && row < 8; ++i) {
    if (!karmaAps[i].suspicious) continue;
    const int y = 96 + row * 20;
    display.setTextColor(kBad, kBackground);
    display.setCursor(6, y);
    display.printf("%s  %d%s SSIDs", macToString(karmaAps[i].bssid).c_str(),
                   karmaAps[i].ssidCount, karmaAps[i].overflow ? "+" : "");
    display.setTextColor(kMuted, kBackground);
    display.setCursor(6, y + 9);
    String names;
    for (int s = 0; s < karmaAps[i].ssidCount && s < 3; ++s) {
      if (s) names += ", ";
      names += karmaAps[i].ssids[s];
    }
    display.print(clipped(names, 38));
    ++row;
  }
  if (karmaAlertCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(6, 110);
    display.printf("No AP claiming %d+ SSIDs yet.", kKarmaSsidThreshold);
  }
  drawFooter("Back", "Reset");
}

void resetKarmaWatch() {
  karmaApCount = 0;
  karmaAlertCount = 0;
  karmaHead = 0;
  karmaTail = 0;
}

void startKarmaWatch() {
  resetKarmaWatch();
  karmaHopIndex = 0;
  lastKarmaHopMs = millis();
  lastKarmaDrawMs = 0;
  karmaStartMs = millis();
  signalMonitorActive = false;

  karmaLogReady = false;
  if (ensureSdCard()) {
    SD.remove(kKarmaLogCsvPath);
    File file = SD.open(kKarmaLogCsvPath, FILE_WRITE);
    if (file) {
      file.println(
          "uptime_ms,bssid,channel,rssi,ssid_count,latitude,longitude,"
          "altitude_m");
      file.close();
      karmaLogReady = true;
    }
  }

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&karmaCallback);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(kDeauthHopChannels[0], WIFI_SECOND_CHAN_NONE);

  karmaWatchActive = true;
  Serial.println("[karma] watch started");
  drawKarmaWatch();
}

void stopKarmaWatch() {
  karmaWatchActive = false;
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true, false);
  Serial.printf("[karma] stopped; %d alert(s)\n", karmaAlertCount);
}

void updateKarmaWatch() {
  if (!karmaWatchActive || currentView != View::kKarmaWatch) return;
  while (karmaTail != karmaHead) {
    mergeKarmaHit(karmaQueue[karmaTail]);
    karmaTail = (karmaTail + 1) % kKarmaHitQueueSlots;
  }
  const uint32_t now = millis();
  if (now - lastKarmaHopMs >= kKarmaHopIntervalMs) {
    lastKarmaHopMs = now;
    karmaHopIndex = (karmaHopIndex + 1) % kDeauthHopChannelCount;
    esp_wifi_set_channel(kDeauthHopChannels[karmaHopIndex],
                         WIFI_SECOND_CHAN_NONE);
  }
  if (now - lastKarmaDrawMs >= kKarmaRedrawMs) {
    lastKarmaDrawMs = now;
    drawKarmaWatch();
  }
}
