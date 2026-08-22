// AWOKxDAG — Beacon Watch (compiled as part of the sketch; see awok_common.h)
//
// Beacon-flood / fake-AP detector: counts the number of DISTINCT BSSIDs
// beaconing within each short window. A handful is normal; dozens of unique
// BSSIDs in two seconds is the signature of a beacon-flood tool (mdk4, or this
// firmware's own Beacon Flood). Promiscuous, listen-only. The defensive
// counterpart to Beacon Flood. beaconWatchActive lives in the main sketch.

BssidHit beaconWatchQueue[kBeaconWatchHitQueueSlots];
volatile int beaconWatchHead = 0;
volatile int beaconWatchTail = 0;
volatile uint32_t beaconWatchTotal = 0;  // total beacons seen (all BSSIDs)
uint8_t beaconWindowBssids[kBeaconWatchWindowSet][6];
int beaconWindowCount = 0;   // distinct BSSIDs in the current window (capped)
uint32_t beaconWatchRate = 0;   // distinct BSSIDs in the last closed window
uint32_t beaconWatchPeak = 0;
uint32_t beaconWatchAlerts = 0;
bool beaconWatchAlert = false;
uint8_t beaconLastBssid[6] = {0};
int beaconWatchHopIndex = 0;
uint32_t lastBeaconWatchHopMs = 0;
uint32_t lastBeaconWatchWindowMs = 0;
uint32_t lastBeaconWatchDrawMs = 0;
uint32_t beaconWatchStartMs = 0;
bool beaconWatchLogReady = false;

// Runs in the Wi-Fi task: enqueue each beacon's BSSID.
void beaconWatchCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* packet =
      static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* payload = packet->payload;
  const int length = packet->rx_ctrl.sig_len;
  if (length < 24) return;
  if ((payload[0] & 0xF0) != 0x80) return;  // beacon subtype
  ++beaconWatchTotal;

  const int next = (beaconWatchHead + 1) % kBeaconWatchHitQueueSlots;
  if (next == beaconWatchTail) return;
  BssidHit& hit = beaconWatchQueue[beaconWatchHead];
  memcpy(hit.bssid, payload + 16, 6);
  hit.channel = packet->rx_ctrl.channel;
  hit.rssi = packet->rx_ctrl.rssi;
  beaconWatchHead = next;
}

bool beaconWindowHas(const uint8_t* bssid) {
  for (int i = 0; i < beaconWindowCount; ++i) {
    bool equal = true;
    for (int j = 0; j < 6; ++j) {
      if (beaconWindowBssids[i][j] != bssid[j]) { equal = false; break; }
    }
    if (equal) return true;
  }
  return false;
}

void appendBeaconWatchLog() {
  if (!beaconWatchLogReady) return;
  File file = SD.open(kBeaconWatchLogCsvPath, FILE_APPEND);
  if (!file) {
    beaconWatchLogReady = false;
    sdReady = false;
    return;
  }
  file.printf("%lu,%lu,%lu,%s%s\n", static_cast<unsigned long>(millis()),
              static_cast<unsigned long>(beaconWatchRate),
              static_cast<unsigned long>(beaconWatchPeak),
              macToString(beaconLastBssid).c_str(), gpsCsvFields().c_str());
  file.close();
}

void drawBeaconWatch() {
  currentView = View::kBeaconWatch;
  display.fillScreen(kBackground);
  drawHeader("BEACON WATCH", beaconWatchAlert ? "ALERT: beacon flood nearby"
                                              : "watching for fake-AP floods");
  display.setTextSize(2);
  display.setTextColor(beaconWatchAlert ? kBad : kGood, kBackground);
  display.setCursor(6, 52);
  display.print(beaconWatchAlert ? "FLOOD" : "CLEAR");

  display.setTextSize(1);
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(6, 90);
  display.printf("Distinct APs/win: %lu%s",
                 static_cast<unsigned long>(beaconWatchRate),
                 beaconWatchRate >= kBeaconWatchWindowSet ? "+" : "");
  display.setCursor(6, 104);
  display.printf("Peak/win: %lu   thr %lu",
                 static_cast<unsigned long>(beaconWatchPeak),
                 static_cast<unsigned long>(kBeaconFloodThreshold));
  display.setCursor(6, 118);
  display.printf("Total beacons: %lu",
                 static_cast<unsigned long>(beaconWatchTotal));
  display.setCursor(6, 132);
  display.printf("Flood windows: %lu | ch %d",
                 static_cast<unsigned long>(beaconWatchAlerts),
                 kDeauthHopChannels[beaconWatchHopIndex]);

  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 152);
  display.print("Last BSSID: ");
  display.print(macToString(beaconLastBssid));

  display.drawFastHLine(6, 172, 228, kPanel);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 182);
  display.print("Many unique BSSIDs beaconing in a");
  display.setCursor(6, 194);
  display.print("short window indicate a beacon");
  display.setCursor(6, 206);
  display.print("flood / fake-AP spam. Passive.");
  drawFooter("Back", "Reset");
}

void resetBeaconWatch() {
  beaconWatchHead = 0;
  beaconWatchTail = 0;
  beaconWatchTotal = 0;
  beaconWindowCount = 0;
  beaconWatchRate = 0;
  beaconWatchPeak = 0;
  beaconWatchAlerts = 0;
  beaconWatchAlert = false;
  memset(beaconLastBssid, 0, 6);
  lastBeaconWatchWindowMs = millis();
}

void startBeaconWatch() {
  resetBeaconWatch();
  beaconWatchHopIndex = 0;
  lastBeaconWatchHopMs = millis();
  lastBeaconWatchDrawMs = 0;
  beaconWatchStartMs = millis();
  signalMonitorActive = false;

  beaconWatchLogReady = false;
  if (ensureSdCard()) {
    SD.remove(kBeaconWatchLogCsvPath);
    File file = SD.open(kBeaconWatchLogCsvPath, FILE_WRITE);
    if (file) {
      file.println(
          "uptime_ms,distinct_bssids,peak,sample_bssid,latitude,longitude,"
          "altitude_m");
      file.close();
      beaconWatchLogReady = true;
    }
  }

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&beaconWatchCallback);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(kDeauthHopChannels[0], WIFI_SECOND_CHAN_NONE);

  beaconWatchActive = true;
  Serial.println("[beaconwatch] started");
  drawBeaconWatch();
}

void stopBeaconWatch() {
  beaconWatchActive = false;
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true, false);
  Serial.printf("[beaconwatch] stopped; %lu flood window(s)\n",
                static_cast<unsigned long>(beaconWatchAlerts));
}

void updateBeaconWatch() {
  if (!beaconWatchActive || currentView != View::kBeaconWatch) return;
  while (beaconWatchTail != beaconWatchHead) {
    const BssidHit& hit = beaconWatchQueue[beaconWatchTail];
    if (!beaconWindowHas(hit.bssid) &&
        beaconWindowCount < kBeaconWatchWindowSet) {
      memcpy(beaconWindowBssids[beaconWindowCount], hit.bssid, 6);
      ++beaconWindowCount;
      memcpy(beaconLastBssid, hit.bssid, 6);
    }
    beaconWatchTail = (beaconWatchTail + 1) % kBeaconWatchHitQueueSlots;
  }
  const uint32_t now = millis();
  if (now - lastBeaconWatchWindowMs >= kBeaconWatchWindowMs) {
    lastBeaconWatchWindowMs = now;
    beaconWatchRate = beaconWindowCount;
    if (beaconWatchRate > beaconWatchPeak) beaconWatchPeak = beaconWatchRate;
    const bool wasAlert = beaconWatchAlert;
    beaconWatchAlert = beaconWatchRate >= kBeaconFloodThreshold;
    if (beaconWatchAlert && !wasAlert) {
      ++beaconWatchAlerts;
      appendBeaconWatchLog();
    }
    beaconWindowCount = 0;
  }
  if (now - lastBeaconWatchHopMs >= kBeaconWatchHopIntervalMs) {
    lastBeaconWatchHopMs = now;
    beaconWatchHopIndex = (beaconWatchHopIndex + 1) % kDeauthHopChannelCount;
    esp_wifi_set_channel(kDeauthHopChannels[beaconWatchHopIndex],
                         WIFI_SECOND_CHAN_NONE);
  }
  if (now - lastBeaconWatchDrawMs >= kBeaconWatchRedrawMs) {
    lastBeaconWatchDrawMs = now;
    drawBeaconWatch();
  }
}
