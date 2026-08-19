// AWOKxDAG — client / probe-request sniffer (compiled as part of the sketch; see awok_common.h)

// ---- Client / probe-request sniffer -------------------------------------

int clientIndexOfMac(const uint8_t* mac) {
  for (int i = 0; i < clientCount; ++i) {
    bool equal = true;
    for (int j = 0; j < 6; ++j) {
      if (clientEntries[i].mac[j] != mac[j]) {
        equal = false;
        break;
      }
    }
    if (equal) return i;
  }
  return -1;
}

// Runs in the Wi-Fi task. Extracts a client MAC (and probed SSID or BSSID) from
// probe requests and station-to-AP data frames, then enqueues a SnifferHit.
void clientSnifferCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
  const wifi_promiscuous_pkt_t* packet =
      static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* payload = packet->payload;
  const int length = packet->rx_ctrl.sig_len;
  if (length < 24) return;

  const uint8_t frameControl = payload[0];
  const uint8_t frameType = (frameControl >> 2) & 0x03;
  SnifferHit hit;
  hit.hasBssid = false;
  hit.isProbe = false;
  hit.ssidLen = 0;
  hit.ssid[0] = 0;

  if (frameType == 0x00 && (frameControl & 0xF0) == 0x40) {  // probe request
    memcpy(hit.mac, payload + 10, 6);
    hit.isProbe = true;
    if (length >= 26) {
      const uint8_t elementId = payload[24];
      const uint8_t elementLen = payload[25];
      if (elementId == 0 && elementLen > 0 && elementLen <= 32 &&
          length >= 26 + elementLen) {
        memcpy(hit.ssid, payload + 26, elementLen);
        hit.ssid[elementLen] = 0;
        hit.ssidLen = elementLen;
      }
    }
  } else if (frameType == 0x02) {  // data frame
    const uint8_t frameControl1 = payload[1];
    const bool toDs = frameControl1 & 0x01;
    const bool fromDs = frameControl1 & 0x02;
    if (toDs && !fromDs) {  // station -> AP: addr1 BSSID, addr2 station
      memcpy(hit.mac, payload + 10, 6);
      memcpy(hit.bssid, payload + 4, 6);
      hit.hasBssid = true;
    } else {
      return;
    }
  } else {
    return;
  }
  if (hit.mac[0] & 0x01) return;  // skip group/broadcast source addresses
  hit.rssi = packet->rx_ctrl.rssi;
  hit.channel = packet->rx_ctrl.channel;

  const int next = (snifferHead + 1) % kSnifferQueueSlots;
  if (next == snifferTail) return;  // queue full: drop
  snifferQueue[snifferHead] = hit;
  snifferHead = next;
}

void mergeSnifferHit(const SnifferHit& hit) {
  int index = clientIndexOfMac(hit.mac);
  if (index < 0) {
    if (clientCount >= kMaxClients) return;
    index = clientCount++;
    clientEntries[index] = ClientEntry();
    memcpy(clientEntries[index].mac, hit.mac, 6);
  }
  ClientEntry& client = clientEntries[index];
  client.rssi = hit.rssi;
  client.channel = hit.channel;
  ++client.packets;
  if (hit.hasBssid) {
    memcpy(client.bssid, hit.bssid, 6);
    client.hasBssid = true;
  }
  if (hit.isProbe && hit.ssidLen > 0) client.lastSsid = String(hit.ssid);
}

bool exportClientsToSd() {
  if (!ensureSdCard()) return false;
  const String temporaryPath = String(kClientCsvPath) + ".tmp";
  SD.remove(temporaryPath.c_str());
  File file = SD.open(temporaryPath.c_str(), FILE_WRITE);
  if (!file) {
    sdReady = false;
    return false;
  }
  file.println(
      "uptime_ms,client_mac,probed_ssid,bssid,rssi,packets,channel,"
      "latitude,longitude,altitude_m");
  const uint32_t uptime = millis();
  const String location = gpsCsvFields();
  for (int i = 0; i < clientCount; ++i) {
    file.print(uptime);
    file.print(',');
    file.print(macToString(clientEntries[i].mac));
    file.print(',');
    file.print(csvField(clientEntries[i].lastSsid));
    file.print(',');
    file.print(clientEntries[i].hasBssid ? macToString(clientEntries[i].bssid)
                                         : String(""));
    file.print(',');
    file.print(clientEntries[i].rssi);
    file.print(',');
    file.print(clientEntries[i].packets);
    file.print(',');
    file.print(clientEntries[i].channel);
    file.println(location);
  }
  file.flush();
  const bool ok = file.getWriteError() == 0;
  file.close();
  if (!ok) {
    SD.remove(temporaryPath.c_str());
    return false;
  }
  SD.remove(kClientCsvPath);
  if (!SD.rename(temporaryPath.c_str(), kClientCsvPath)) return false;
  Serial.printf("[clients] wrote %d row(s) to %s\n", clientCount,
                kClientCsvPath);
  return true;
}

void drawClientSniffer() {
  currentView = View::kClientSniffer;
  display.fillScreen(kBackground);
  drawHeader("CLIENT SNIFFER",
             String(clientCount) + " clients | ch " +
                 String(kDeauthHopChannels[clientHopIndex]) + " " +
                 bandLabel(kDeauthHopChannels[clientHopIndex]) + "G");
  display.setTextSize(1);
  const int rows = min(clientCount, kVisibleRows);
  for (int i = 0; i < rows; ++i) {
    const int y = 48 + i * 22;
    display.setTextColor(ILI9341_WHITE, kBackground);
    display.setCursor(5, y);
    display.print(macToString(clientEntries[i].mac));
    display.setTextColor(kMuted, kBackground);
    display.setCursor(5, y + 11);
    String detail;
    if (clientEntries[i].lastSsid.length()) {
      detail = "-> " + clientEntries[i].lastSsid;
    } else if (clientEntries[i].hasBssid) {
      detail = "ap " + macToString(clientEntries[i].bssid);
    } else {
      detail = "probing";
    }
    display.printf("%4ld dBm ch%-3d %s", static_cast<long>(clientEntries[i].rssi),
                   static_cast<int>(clientEntries[i].channel),
                   clipped(detail, 22).c_str());
  }
  if (clientCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(40, 145);
    display.print("Listening for clients...");
  }
  drawFooter("Home", "Save");
}

void startClientSniffer() {
  clientCount = 0;
  snifferHead = 0;
  snifferTail = 0;
  clientHopIndex = 0;
  lastClientHopMs = millis();
  lastClientDrawMs = 0;
  clientSnifferStartMs = millis();
  lastClientCsvOk = false;
  signalMonitorActive = false;

  Serial.println("[clients] starting probe-request sniffer");
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask =
      WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&clientSnifferCallback);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(kDeauthHopChannels[0], WIFI_SECOND_CHAN_NONE);

  clientSnifferActive = true;
  drawClientSniffer();
}

void stopClientSniffer() {
  clientSnifferActive = false;
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true, false);
  lastClientCsvOk = exportClientsToSd();
  Serial.printf("[clients] stopped; %d client(s)\n", clientCount);
}

void updateClientSniffer() {
  if (!clientSnifferActive || currentView != View::kClientSniffer) return;
  while (snifferTail != snifferHead) {
    mergeSnifferHit(snifferQueue[snifferTail]);
    snifferTail = (snifferTail + 1) % kSnifferQueueSlots;
  }
  const uint32_t now = millis();
  if (now - lastClientHopMs >= kClientHopIntervalMs) {
    lastClientHopMs = now;
    clientHopIndex = (clientHopIndex + 1) % kDeauthHopChannelCount;
    esp_wifi_set_channel(kDeauthHopChannels[clientHopIndex],
                         WIFI_SECOND_CHAN_NONE);
  }
  if (now - lastClientDrawMs >= kClientRedrawMs) {
    lastClientDrawMs = now;
    drawClientSniffer();
  }
}

