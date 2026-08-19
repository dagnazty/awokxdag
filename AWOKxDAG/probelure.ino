// AWOKxDAG — PineAP-lite / targeted probe lure (compiled as part of the sketch;
// see awok_common.h)
//
// Scoped to ONE SSID (the last-selected network). Beacons that SSID from a
// stable fake BSSID on the target's channel so its clients believe the network
// is present, and counts probe requests asking for that exact SSID (logging the
// probing client). Authorized testing only; it targets a single named network,
// not everyone in range.

constexpr uint32_t kLureBeaconIntervalMs = 100;
constexpr uint32_t kLureRedrawMs = 500;

uint8_t lureBssid[6] = {0};
String lureSsid;
uint8_t lureChannel = 1;
uint32_t lureBeaconsSent = 0;
volatile uint32_t lureProbes = 0;
volatile uint8_t lureLastClient[6] = {0};
volatile bool lureHaveClient = false;
uint32_t probeLureStartMs = 0;
uint32_t lastLureBeaconMs = 0;
uint32_t lastLureDrawMs = 0;

// Wi-Fi task: count probe requests that name our target SSID.
void probeLureCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* packet =
      static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* p = packet->payload;
  const int len = packet->rx_ctrl.sig_len;
  if (len < 26) return;
  if ((p[0] & 0xF0) != 0x40) return;  // probe request
  if (p[24] != 0) return;             // first IE must be SSID
  const uint8_t l = p[25];
  const int target = lureSsid.length();
  if (l == 0 || l != target || len < 26 + l) return;
  for (int i = 0; i < l; ++i) {
    if (tolower(p[26 + i]) != tolower(lureSsid[i])) return;
  }
  ++lureProbes;
  for (int i = 0; i < 6; ++i) lureLastClient[i] = p[10 + i];
  lureHaveClient = true;
}

void sendLureBeacon() {
  uint8_t packet[128];
  const uint8_t fixed[] = {
      0x80, 0x00, 0x00, 0x00,
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // addr1 broadcast
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // addr2 (BSSID)
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // addr3 (BSSID)
      0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // timestamp
      0x64, 0x00,                                       // beacon interval
      0x31, 0x04};                                      // capability
  memcpy(packet, fixed, sizeof(fixed));
  memcpy(packet + 10, lureBssid, 6);
  memcpy(packet + 16, lureBssid, 6);
  int len = sizeof(fixed);
  const int sl = min(static_cast<int>(lureSsid.length()), 32);
  packet[len++] = 0x00;
  packet[len++] = static_cast<uint8_t>(sl);
  memcpy(packet + len, lureSsid.c_str(), sl);
  len += sl;
  const uint8_t rates[] = {0x01, 0x08, 0x82, 0x84, 0x8B,
                           0x96, 0x24, 0x30, 0x48, 0x6C};
  memcpy(packet + len, rates, sizeof(rates));
  len += sizeof(rates);
  packet[len++] = 0x03;
  packet[len++] = 0x01;
  packet[len++] = lureChannel;
  if (esp_wifi_80211_tx(WIFI_IF_STA, packet, len, false) == ESP_OK) {
    ++lureBeaconsSent;
  }
}

void drawProbeLure() {
  currentView = View::kProbeLure;
  display.fillScreen(kBackground);
  drawHeader("PROBE LURE", lureSsid.length() ? lureSsid : "<no target>");
  display.setTextSize(1);
  if (lureSsid.length() == 0) {
    display.setTextColor(kWarn, kBackground);
    display.setCursor(6, 60);
    display.print("No target SSID.");
    display.setTextColor(kMuted, kBackground);
    display.setCursor(6, 78);
    display.print("Recon > Wi-Fi Scan, tap a network,");
    display.setCursor(6, 90);
    display.print("then open Probe Lure.");
    drawFooter("Back", "Back");
    return;
  }

  display.setTextSize(2);
  display.setTextColor(probeLureActive ? kBad : kMuted, kBackground);
  display.setCursor(6, 52);
  display.print(probeLureActive ? "LURING" : "IDLE");

  display.setTextSize(1);
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(6, 90);
  display.printf("Channel: %d", static_cast<int>(lureChannel));
  display.setCursor(6, 104);
  display.printf("Beacons sent: %lu",
                 static_cast<unsigned long>(lureBeaconsSent));
  display.setCursor(6, 118);
  display.printf("Probes for SSID: %lu",
                 static_cast<unsigned long>(lureProbes));
  display.setCursor(6, 132);
  display.print("Last client: ");
  if (lureHaveClient) {
    uint8_t mac[6];
    for (int i = 0; i < 6; ++i) mac[i] = lureLastClient[i];
    display.print(macToString(mac));
  } else {
    display.print("none");
  }

  display.drawFastHLine(6, 152, 228, kPanel);
  display.setTextColor(kBad, kBackground);
  display.setCursor(6, 162);
  display.print("Authorized testing only.");
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 178);
  display.print("Impersonates one named network to");
  display.setCursor(6, 190);
  display.print("lure its clients. Use only where you");
  display.setCursor(6, 202);
  display.print("are permitted to test that network.");
  drawFooter("Back", probeLureActive ? "Stop" : "Start");
}

void startProbeLure() {
  lureSsid = selectedWifi.ssid;
  if (lureSsid.length() == 0) {
    probeLureActive = false;
    drawProbeLure();
    return;
  }
  lureChannel = selectedWifi.channel > 0
                    ? static_cast<uint8_t>(selectedWifi.channel)
                    : 1;
  for (int i = 0; i < 6; ++i) lureBssid[i] = esp_random() & 0xFF;
  lureBssid[0] = 0x02;  // locally administered, unicast
  lureBeaconsSent = 0;
  lureProbes = 0;
  lureHaveClient = false;
  probeLureStartMs = millis();
  lastLureBeaconMs = 0;
  lastLureDrawMs = 0;
  signalMonitorActive = false;

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&probeLureCallback);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(lureChannel, WIFI_SECOND_CHAN_NONE);

  probeLureActive = true;
  Serial.printf("[lure] targeting '%s' on channel %d\n", lureSsid.c_str(),
                static_cast<int>(lureChannel));
  drawProbeLure();
}

void stopProbeLure() {
  probeLureActive = false;
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true, false);
  Serial.printf("[lure] stopped; %lu beacons, %lu probes\n",
                static_cast<unsigned long>(lureBeaconsSent),
                static_cast<unsigned long>(lureProbes));
}

void updateProbeLure() {
  if (!probeLureActive || currentView != View::kProbeLure) return;
  const uint32_t now = millis();
  if (now - lastLureBeaconMs >= kLureBeaconIntervalMs) {
    lastLureBeaconMs = now;
    sendLureBeacon();
  }
  if (now - lastLureDrawMs >= kLureRedrawMs) {
    lastLureDrawMs = now;
    drawProbeLure();
  }
}
