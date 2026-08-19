// AWOKxDAG — packet monitor (compiled as part of the sketch; see awok_common.h)
//
// Promiscuous capture of every 802.11 frame on the hopped channels, written to
// a pcap on SD with a live management/data/control breakdown. Reuses the pcap
// helpers and capture ring buffer from the handshake module (they never run at
// the same time).

constexpr char kPktmonPcapPath[] = "/awokxdag/pktmon.pcap";
constexpr uint32_t kPktmonHopIntervalMs = 300;
constexpr uint32_t kPktmonRedrawMs = 500;

// pktmonActive lives in the main sketch's global block (defined before the
// input tab, which reads it in the serial guard).
File pktmonFile;
bool pktmonFileOpen = false;
uint32_t pktMgmt = 0;
uint32_t pktData = 0;
uint32_t pktCtrl = 0;
uint32_t pktTotal = 0;
uint32_t pktWritten = 0;
int pktmonHopIndex = 0;
uint32_t lastPktmonHopMs = 0;
uint32_t lastPktmonDrawMs = 0;
uint32_t pktmonStartMs = 0;

// Runs in the Wi-Fi task: tally by frame type and enqueue a copy for the loop.
void pktmonCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  const wifi_promiscuous_pkt_t* packet =
      static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const int length = packet->rx_ctrl.sig_len;
  if (length < 4) return;
  if (type == WIFI_PKT_MGMT) {
    ++pktMgmt;
  } else if (type == WIFI_PKT_DATA) {
    ++pktData;
  } else {
    ++pktCtrl;  // WIFI_PKT_CTRL / MISC
  }
  ++pktTotal;

  const int next = (captureHead + 1) % kCaptureQueueSlots;
  if (next == captureTail) return;  // queue full: drop the payload, keep count
  CaptureFrame& slot = captureQueue[captureHead];
  const int copyLen = length > kCaptureSlotBytes ? kCaptureSlotBytes : length;
  memcpy(slot.data, packet->payload, copyLen);
  slot.len = static_cast<uint16_t>(copyLen);
  slot.origLen = static_cast<uint16_t>(length);
  const uint32_t nowUs = micros();
  slot.tsSec = nowUs / 1000000UL;
  slot.tsUsec = nowUs % 1000000UL;
  slot.isEapol = false;
  captureHead = next;
}

bool openPktmonPcap() {
  if (!ensureSdCard()) return false;
  SD.remove(kPktmonPcapPath);
  pktmonFile = SD.open(kPktmonPcapPath, FILE_WRITE);
  if (!pktmonFile) {
    sdReady = false;
    return false;
  }
  writePcapLe32(pktmonFile, 0xA1B2C3D4);
  writePcapLe16(pktmonFile, 2);
  writePcapLe16(pktmonFile, 4);
  writePcapLe32(pktmonFile, 0);
  writePcapLe32(pktmonFile, 0);
  writePcapLe32(pktmonFile, kCaptureSlotBytes);
  writePcapLe32(pktmonFile, 105);  // LINKTYPE_IEEE802_11
  pktmonFile.flush();
  pktmonFileOpen = pktmonFile.getWriteError() == 0;
  return pktmonFileOpen;
}

void writePktmonFrame(const CaptureFrame& frame) {
  if (!pktmonFileOpen) return;
  writePcapLe32(pktmonFile, frame.tsSec);
  writePcapLe32(pktmonFile, frame.tsUsec);
  writePcapLe32(pktmonFile, frame.len);
  writePcapLe32(pktmonFile, frame.origLen);
  pktmonFile.write(frame.data, frame.len);
  ++pktWritten;
}

void drawPacketMon() {
  currentView = View::kPacketMon;
  display.fillScreen(kBackground);
  drawHeader("PACKET MONITOR",
             pktmonFileOpen ? "capturing to pktmon.pcap" : "live counts only");
  display.setTextSize(2);
  display.setTextColor(kAccent, kBackground);
  display.setCursor(6, 52);
  display.printf("%lu pkts", static_cast<unsigned long>(pktTotal));

  display.setTextSize(1);
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(6, 90);
  display.printf("Mgmt: %lu", static_cast<unsigned long>(pktMgmt));
  display.setCursor(6, 104);
  display.printf("Data: %lu", static_cast<unsigned long>(pktData));
  display.setCursor(6, 118);
  display.printf("Ctrl/misc: %lu", static_cast<unsigned long>(pktCtrl));

  const uint32_t elapsed = (millis() - pktmonStartMs) / 1000;
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 138);
  display.printf("Rate: %lu pkt/s",
                 static_cast<unsigned long>(elapsed ? pktTotal / elapsed : 0));
  display.setCursor(6, 152);
  display.printf("Channel %d %sG   %lus",
                 kDeauthHopChannels[pktmonHopIndex],
                 bandLabel(kDeauthHopChannels[pktmonHopIndex]),
                 static_cast<unsigned long>(elapsed));

  display.drawFastHLine(6, 172, 228, kPanel);
  display.setTextColor(pktmonFileOpen ? kAccent : kWarn, kBackground);
  display.setCursor(6, 182);
  display.printf("Written: %lu frame(s)",
                 static_cast<unsigned long>(pktWritten));
  display.setCursor(6, 196);
  display.print(pktmonFileOpen ? "SD: pktmon.pcap (open in Wireshark)"
                               : "SD unavailable; counts only");
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 216);
  display.print("Hops 2.4/5 GHz; drops payload if the");
  display.setCursor(6, 228);
  display.print("SD write cannot keep up (counts stay).");
  drawFooter("Home", "Home");
}

void startPacketMon() {
  pktMgmt = 0;
  pktData = 0;
  pktCtrl = 0;
  pktTotal = 0;
  pktWritten = 0;
  captureHead = 0;
  captureTail = 0;
  pktmonHopIndex = 0;
  lastPktmonHopMs = millis();
  lastPktmonDrawMs = 0;
  pktmonStartMs = millis();
  signalMonitorActive = false;

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&pktmonCallback);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(kDeauthHopChannels[0], WIFI_SECOND_CHAN_NONE);

  pktmonFileOpen = openPktmonPcap();
  pktmonActive = true;
  Serial.println("[pktmon] started");
  drawPacketMon();
}

void stopPacketMon() {
  pktmonActive = false;
  esp_wifi_set_promiscuous(false);
  if (pktmonFileOpen) {
    pktmonFile.flush();
    pktmonFile.close();
    pktmonFileOpen = false;
  }
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true, false);
  Serial.printf("[pktmon] stopped; %lu captured, %lu written\n",
                static_cast<unsigned long>(pktTotal),
                static_cast<unsigned long>(pktWritten));
}

void updatePacketMon() {
  if (!pktmonActive || currentView != View::kPacketMon) return;
  while (captureTail != captureHead) {
    writePktmonFrame(captureQueue[captureTail]);
    captureTail = (captureTail + 1) % kCaptureQueueSlots;
  }
  const uint32_t now = millis();
  if (now - lastPktmonHopMs >= kPktmonHopIntervalMs) {
    lastPktmonHopMs = now;
    pktmonHopIndex = (pktmonHopIndex + 1) % kDeauthHopChannelCount;
    esp_wifi_set_channel(kDeauthHopChannels[pktmonHopIndex],
                         WIFI_SECOND_CHAN_NONE);
  }
  if (now - lastPktmonDrawMs >= kPktmonRedrawMs) {
    lastPktmonDrawMs = now;
    if (pktmonFileOpen) pktmonFile.flush();
    drawPacketMon();
  }
}
