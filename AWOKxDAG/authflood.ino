// AWOKxDAG — Auth Flood Watch (compiled as part of the sketch; see awok_common.h)
//
// Detects authentication / association flood DoS against an AP: a burst of auth
// (subtype 0xB0) and (re)association-request (0x00 / 0x20) frames, as produced
// by mdk4 'a' or a connection-flood tool, exhausting the AP's client table.
// Counts matching frames per short window and alerts on a spike, noting the
// targeted AP. Promiscuous, listen-only. authFloodActive lives in the main
// sketch (read by the input tab).

volatile uint32_t authFloodWindow = 0;  // matching frames in the open window
volatile uint32_t authFloodTotal = 0;
uint8_t authLastTarget[6] = {0};  // AP under attack (addr1)
uint8_t authLastSource[6] = {0};  // claimed client (addr2)
uint32_t authFloodRate = 0;       // frames in the last closed window
uint32_t authFloodPeak = 0;
uint32_t authFloodAlerts = 0;
bool authFloodAlert = false;
int authFloodHopIndex = 0;
uint32_t lastAuthFloodHopMs = 0;
uint32_t lastAuthFloodWindowMs = 0;
uint32_t lastAuthFloodDrawMs = 0;
uint32_t authFloodStartMs = 0;
bool authFloodLogReady = false;

// Runs in the Wi-Fi task: count auth / assoc-request frames, note the target.
void authFloodCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* packet =
      static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* payload = packet->payload;
  const int length = packet->rx_ctrl.sig_len;
  if (length < 24) return;
  if (((payload[0] >> 2) & 0x03) != 0x00) return;  // management frames only
  const uint8_t subtype = payload[0] & 0xF0;
  if (subtype != 0xB0 && subtype != 0x00 && subtype != 0x20) return;

  ++authFloodWindow;
  ++authFloodTotal;
  memcpy(authLastTarget, payload + 4, 6);   // addr1 = destination / AP
  memcpy(authLastSource, payload + 10, 6);  // addr2 = source / client
}

void appendAuthFloodLog() {
  if (!authFloodLogReady) return;
  File file = SD.open(kAuthFloodLogCsvPath, FILE_APPEND);
  if (!file) {
    authFloodLogReady = false;
    sdReady = false;
    return;
  }
  file.printf("%lu,%lu,%lu,%s,%s%s\n", static_cast<unsigned long>(millis()),
              static_cast<unsigned long>(authFloodRate),
              static_cast<unsigned long>(authFloodPeak),
              macToString(authLastTarget).c_str(),
              macToString(authLastSource).c_str(), gpsCsvFields().c_str());
  file.close();
}

void drawAuthFlood() {
  currentView = View::kAuthFlood;
  display.fillScreen(kBackground);
  drawHeader("AUTH FLOOD", authFloodAlert ? "ALERT: auth/assoc flood"
                                          : "watching for connection DoS");
  display.setTextSize(2);
  display.setTextColor(authFloodAlert ? kBad : kGood, kBackground);
  display.setCursor(6, 52);
  display.print(authFloodAlert ? "FLOOD" : "CLEAR");

  display.setTextSize(1);
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(6, 90);
  display.printf("Auth/assoc per win: %lu",
                 static_cast<unsigned long>(authFloodRate));
  display.setCursor(6, 104);
  display.printf("Peak/win: %lu   thr %lu",
                 static_cast<unsigned long>(authFloodPeak),
                 static_cast<unsigned long>(kAuthFloodThreshold));
  display.setCursor(6, 118);
  display.printf("Total frames: %lu",
                 static_cast<unsigned long>(authFloodTotal));
  display.setCursor(6, 132);
  display.printf("Flood windows: %lu | ch %d",
                 static_cast<unsigned long>(authFloodAlerts),
                 kDeauthHopChannels[authFloodHopIndex]);

  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 152);
  display.print("Target AP: ");
  display.print(macToString(authLastTarget));
  display.setCursor(6, 164);
  display.print("Claimed by: ");
  display.print(macToString(authLastSource));

  display.drawFastHLine(6, 182, 228, kPanel);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 192);
  display.print("A spike of auth/assoc requests");
  display.setCursor(6, 204);
  display.print("floods an AP's client table (DoS).");
  display.setCursor(6, 216);
  display.print("Passive; nothing transmitted.");
  drawFooter("Back", "Reset");
}

void resetAuthFlood() {
  authFloodWindow = 0;
  authFloodTotal = 0;
  authFloodRate = 0;
  authFloodPeak = 0;
  authFloodAlerts = 0;
  authFloodAlert = false;
  memset(authLastTarget, 0, 6);
  memset(authLastSource, 0, 6);
  lastAuthFloodWindowMs = millis();
}

void startAuthFlood() {
  resetAuthFlood();
  authFloodHopIndex = 0;
  lastAuthFloodHopMs = millis();
  lastAuthFloodDrawMs = 0;
  authFloodStartMs = millis();
  signalMonitorActive = false;

  authFloodLogReady = false;
  if (ensureSdCard()) {
    SD.remove(kAuthFloodLogCsvPath);
    File file = SD.open(kAuthFloodLogCsvPath, FILE_WRITE);
    if (file) {
      file.println(
          "uptime_ms,rate,peak,target_ap,source,latitude,longitude,altitude_m");
      file.close();
      authFloodLogReady = true;
    }
  }

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&authFloodCallback);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(kDeauthHopChannels[0], WIFI_SECOND_CHAN_NONE);

  authFloodActive = true;
  Serial.println("[authflood] watch started");
  drawAuthFlood();
}

void stopAuthFlood() {
  authFloodActive = false;
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true, false);
  Serial.printf("[authflood] stopped; %lu flood window(s)\n",
                static_cast<unsigned long>(authFloodAlerts));
}

void updateAuthFlood() {
  if (!authFloodActive || currentView != View::kAuthFlood) return;
  const uint32_t now = millis();
  if (now - lastAuthFloodWindowMs >= kAuthFloodWindowMs) {
    lastAuthFloodWindowMs = now;
    authFloodRate = authFloodWindow;
    authFloodWindow = 0;
    if (authFloodRate > authFloodPeak) authFloodPeak = authFloodRate;
    const bool wasAlert = authFloodAlert;
    authFloodAlert = authFloodRate >= kAuthFloodThreshold;
    if (authFloodAlert && !wasAlert) {
      ++authFloodAlerts;
      appendAuthFloodLog();
    }
  }
  if (now - lastAuthFloodHopMs >= kAuthFloodHopIntervalMs) {
    lastAuthFloodHopMs = now;
    authFloodHopIndex = (authFloodHopIndex + 1) % kDeauthHopChannelCount;
    esp_wifi_set_channel(kDeauthHopChannels[authFloodHopIndex],
                         WIFI_SECOND_CHAN_NONE);
  }
  if (now - lastAuthFloodDrawMs >= kAuthFloodRedrawMs) {
    lastAuthFloodDrawMs = now;
    drawAuthFlood();
  }
}
