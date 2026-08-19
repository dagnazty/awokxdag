// AWOKxDAG — beacon / SSID flood (compiled as part of the sketch; see awok_common.h)

// ---- Beacon / SSID flood ------------------------------------------------

// Clearly-synthetic base names so the flood is obviously a test, not an
// impersonation of any real network or brand.
const char* const kBeaconBaseNames[] = {"AWOK",    "PentestNet", "AuditAP",
                                        "TestNet", "RogueLab",   "FreeSpot"};
constexpr int kBeaconBaseNameCount =
    static_cast<int>(sizeof(kBeaconBaseNames) / sizeof(kBeaconBaseNames[0]));

String nextBeaconSsid() {
  const char* base = kBeaconBaseNames[beaconNameIndex % kBeaconBaseNameCount];
  ++beaconNameIndex;
  char suffix[6];
  snprintf(suffix, sizeof(suffix), "-%03X",
           static_cast<unsigned>(esp_random() & 0xFFF));
  return String(base) + suffix;
}

void sendBeaconFrame(const String& ssid, uint8_t channel) {
  uint8_t packet[128];
  const uint8_t fixed[] = {
      0x80, 0x00,                          // frame control: beacon
      0x00, 0x00,                          // duration
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // addr1 broadcast
      0x02, 0x00, 0x00, 0x00, 0x00, 0x00,  // addr2 (BSSID, locally administered)
      0x02, 0x00, 0x00, 0x00, 0x00, 0x00,  // addr3 (BSSID)
      0x00, 0x00,                          // sequence
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // timestamp
      0x64, 0x00,                          // beacon interval
      0x31, 0x04};                         // capability (ESS + privacy)
  memcpy(packet, fixed, sizeof(fixed));
  // Randomise the BSSID (keep locally-administered bit set) so each fake AP
  // looks distinct.
  const uint32_t r1 = esp_random();
  const uint32_t r2 = esp_random();
  packet[11] = packet[17] = (r1 >> 8) & 0xFF;
  packet[12] = packet[18] = (r1 >> 16) & 0xFF;
  packet[13] = packet[19] = (r1 >> 24) & 0xFF;
  packet[14] = packet[20] = r2 & 0xFF;
  packet[15] = packet[21] = (r2 >> 8) & 0xFF;

  int len = sizeof(fixed);
  const int ssidLen = min(static_cast<int>(ssid.length()), 32);
  packet[len++] = 0x00;  // SSID element
  packet[len++] = static_cast<uint8_t>(ssidLen);
  memcpy(packet + len, ssid.c_str(), ssidLen);
  len += ssidLen;
  const uint8_t rates[] = {0x01, 0x08, 0x82, 0x84, 0x8B,
                           0x96, 0x24, 0x30, 0x48, 0x6C};
  memcpy(packet + len, rates, sizeof(rates));
  len += sizeof(rates);
  packet[len++] = 0x03;     // DS parameter set
  packet[len++] = 0x01;
  packet[len++] = channel;

  if (esp_wifi_80211_tx(WIFI_IF_STA, packet, len, false) == ESP_OK) {
    ++beaconFramesSent;
  }
}

void drawBeaconFlood() {
  currentView = View::kBeaconFlood;
  display.fillScreen(kBackground);
  drawHeader("BEACON FLOOD", "broadcasting fake test APs");
  display.setTextSize(2);
  display.setTextColor(beaconFloodActive ? kBad : kMuted, kBackground);
  display.setCursor(6, 54);
  display.print(beaconFloodActive ? "FLOODING" : "IDLE");

  display.setTextSize(1);
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(6, 92);
  display.printf("Frames sent: %lu",
                 static_cast<unsigned long>(beaconFramesSent));
  display.setCursor(6, 106);
  display.printf("Channel: %d (2.4 GHz cycle)",
                 kBeaconChannels[beaconChannelIndex]);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 120);
  display.print("Last SSID: ");
  display.print(clipped(lastBeaconSsid, 24));

  display.drawFastHLine(6, 140, 228, kPanel);
  display.setTextColor(kBad, kBackground);
  display.setCursor(6, 150);
  display.print("Authorized testing only.");
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 168);
  display.print("Floods the air with bogus SSIDs");
  display.setCursor(6, 180);
  display.print("and can disrupt nearby clients.");
  display.setCursor(6, 192);
  display.print("Use only where you are allowed");
  display.setCursor(6, 204);
  display.print("to transmit.");
  drawFooter("Back", beaconFloodActive ? "Stop" : "Start");
}

void startBeaconFlood() {
  beaconFramesSent = 0;
  beaconFloodStartMs = millis();
  lastBeaconBurstMs = 0;
  lastBeaconDrawMs = 0;
  beaconChannelIndex = 0;
  signalMonitorActive = false;

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_channel(kBeaconChannels[0], WIFI_SECOND_CHAN_NONE);
  beaconFloodActive = true;
  Serial.println("[beacon] flood started");
  drawBeaconFlood();
}

void stopBeaconFlood() {
  beaconFloodActive = false;
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true, false);
  Serial.printf("[beacon] stopped after %lu frame(s)\n",
                static_cast<unsigned long>(beaconFramesSent));
}

void updateBeaconFlood() {
  if (!beaconFloodActive || currentView != View::kBeaconFlood) return;
  const uint32_t now = millis();
  if (now - lastBeaconBurstMs >= kBeaconBurstIntervalMs) {
    lastBeaconBurstMs = now;
    beaconChannelIndex = (beaconChannelIndex + 1) % kBeaconChannelCount;
    const uint8_t channel = kBeaconChannels[beaconChannelIndex];
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    for (int i = 0; i < kBeaconsPerBurst; ++i) {
      lastBeaconSsid = nextBeaconSsid();
      sendBeaconFrame(lastBeaconSsid, channel);
    }
  }
  if (now - lastBeaconDrawMs >= kBeaconRedrawMs) {
    lastBeaconDrawMs = now;
    drawBeaconFlood();
  }
}

