// AWOKxDAG — Probe Intel (compiled as part of the sketch; see awok_common.h)
//
// Aggregates directed probe requests by the SSID they name (the network a
// nearby device is actively looking for). Unlike the client sniffer, which keys
// by station MAC, this keys by SSID -- surfacing the preferred-network lists
// leaking from devices in range, ranked by how many probes and how many
// distinct devices ask for each. Passive: it never transmits.

ProbeSsidEntry probeSsids[kMaxProbeSsids];
int probeSsidCount = 0;
ProbeHit probeHitQueue[kProbeHitQueueSlots];
volatile int probeHitHead = 0;
volatile int probeHitTail = 0;
int probeHopIndex = 0;
uint32_t lastProbeHopMs = 0;
uint32_t lastProbeDrawMs = 0;
uint32_t probeIntelStartMs = 0;
uint32_t probeTotalCount = 0;

// Runs in the Wi-Fi task: pull the directed SSID out of a probe request.
void probeIntelCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* packet =
      static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* payload = packet->payload;
  const int length = packet->rx_ctrl.sig_len;
  if (length < 28) return;
  if ((payload[0] & 0xF0) != 0x40) return;  // probe request subtype

  const uint8_t elementId = payload[24];
  const uint8_t elementLen = payload[25];
  if (elementId != 0 || elementLen == 0 || elementLen > 32) return;  // directed
  if (length < 26 + elementLen) return;

  const uint8_t* mac = payload + 10;
  if (mac[0] & 0x01) return;  // skip group/broadcast source addresses

  const int next = (probeHitHead + 1) % kProbeHitQueueSlots;
  if (next == probeHitTail) return;
  ProbeHit& hit = probeHitQueue[probeHitHead];
  memcpy(hit.mac, mac, 6);
  hit.rssi = packet->rx_ctrl.rssi;
  hit.ssidLen = elementLen;
  memcpy(hit.ssid, payload + 26, elementLen);
  hit.ssid[elementLen] = 0;
  probeHitHead = next;
}

int probeSsidIndexOf(const char* ssid) {
  for (int i = 0; i < probeSsidCount; ++i) {
    if (probeSsids[i].ssid.equals(ssid)) return i;
  }
  return -1;
}

void mergeProbeHit(const ProbeHit& hit) {
  int index = probeSsidIndexOf(hit.ssid);
  if (index < 0) {
    if (probeSsidCount >= kMaxProbeSsids) return;
    index = probeSsidCount++;
    probeSsids[index] = ProbeSsidEntry();
    probeSsids[index].ssid = String(hit.ssid);
  }
  ProbeSsidEntry& entry = probeSsids[index];
  ++entry.probes;
  ++probeTotalCount;
  entry.rssi = hit.rssi;
  memcpy(entry.lastMac, hit.mac, 6);
  // Count distinct devices up to a small cap; beyond it, flag overflow.
  bool known = false;
  for (int i = 0; i < entry.macCount; ++i) {
    bool equal = true;
    for (int j = 0; j < 6; ++j) {
      if (entry.macs[i][j] != hit.mac[j]) { equal = false; break; }
    }
    if (equal) { known = true; break; }
  }
  if (!known) {
    if (entry.macCount < kProbeMacsPerSsid) {
      memcpy(entry.macs[entry.macCount], hit.mac, 6);
      ++entry.macCount;
    } else {
      entry.macOverflow = true;
    }
  }
}

void sortProbeSsids() {
  for (int i = 0; i < probeSsidCount - 1; ++i) {
    for (int j = i + 1; j < probeSsidCount; ++j) {
      if (probeSsids[j].probes > probeSsids[i].probes) {
        ProbeSsidEntry temporary = probeSsids[i];
        probeSsids[i] = probeSsids[j];
        probeSsids[j] = temporary;
      }
    }
  }
}

bool exportProbeIntelToSd() {
  if (!ensureSdCard()) return false;
  const String temporaryPath = String(kProbeIntelCsvPath) + ".tmp";
  SD.remove(temporaryPath.c_str());
  File file = SD.open(temporaryPath.c_str(), FILE_WRITE);
  if (!file) {
    sdReady = false;
    return false;
  }
  file.println(
      "uptime_ms,probed_ssid,probes,devices,device_overflow,last_mac,rssi,"
      "latitude,longitude,altitude_m");
  const uint32_t uptime = millis();
  const String location = gpsCsvFields();
  for (int i = 0; i < probeSsidCount; ++i) {
    file.print(uptime);
    file.print(',');
    file.print(csvField(probeSsids[i].ssid));
    file.print(',');
    file.print(probeSsids[i].probes);
    file.print(',');
    file.print(probeSsids[i].macCount);
    file.print(',');
    file.print(probeSsids[i].macOverflow ? "yes" : "no");
    file.print(',');
    file.print(macToString(probeSsids[i].lastMac));
    file.print(',');
    file.print(probeSsids[i].rssi);
    file.println(location);
  }
  file.flush();
  const bool ok = file.getWriteError() == 0;
  file.close();
  if (!ok) {
    SD.remove(temporaryPath.c_str());
    return false;
  }
  SD.remove(kProbeIntelCsvPath);
  if (!SD.rename(temporaryPath.c_str(), kProbeIntelCsvPath)) return false;
  Serial.printf("[probeintel] wrote %d row(s) to %s\n", probeSsidCount,
                kProbeIntelCsvPath);
  return true;
}

void drawProbeIntel() {
  currentView = View::kProbeIntel;
  display.fillScreen(kBackground);
  sortProbeSsids();
  drawHeader("PROBE INTEL",
             String(probeSsidCount) + " SSID(s) | ch " +
                 String(kDeauthHopChannels[probeHopIndex]));
  display.setTextSize(1);
  const int rows = min(probeSsidCount, kVisibleRows);
  for (int i = 0; i < rows; ++i) {
    const int y = 48 + i * 22;
    display.setTextColor(ILI9341_WHITE, kBackground);
    display.setCursor(5, y);
    display.print(clipped(probeSsids[i].ssid, 28));
    display.setTextColor(kMuted, kBackground);
    display.setCursor(5, y + 11);
    display.printf("x%lu probes  %d%s dev  %ld dBm",
                   static_cast<unsigned long>(probeSsids[i].probes),
                   probeSsids[i].macCount, probeSsids[i].macOverflow ? "+" : "",
                   static_cast<long>(probeSsids[i].rssi));
  }
  if (probeSsidCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(20, 145);
    display.print("Listening for probe requests...");
  }
  drawFooter("Back", lastProbeIntelCsvOk ? "Saved" : "Save");
}

void startProbeIntel() {
  probeSsidCount = 0;
  probeHitHead = 0;
  probeHitTail = 0;
  probeHopIndex = 0;
  probeTotalCount = 0;
  lastProbeHopMs = millis();
  lastProbeDrawMs = 0;
  probeIntelStartMs = millis();
  lastProbeIntelCsvOk = false;
  signalMonitorActive = false;

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&probeIntelCallback);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(kDeauthHopChannels[0], WIFI_SECOND_CHAN_NONE);

  probeIntelActive = true;
  Serial.println("[probeintel] probe-request SSID map started");
  drawProbeIntel();
}

void stopProbeIntel() {
  probeIntelActive = false;
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true, false);
  lastProbeIntelCsvOk = exportProbeIntelToSd();
  Serial.printf("[probeintel] stopped; %d SSID(s), %lu probe(s)\n",
                probeSsidCount, static_cast<unsigned long>(probeTotalCount));
}

void updateProbeIntel() {
  if (!probeIntelActive || currentView != View::kProbeIntel) return;
  while (probeHitTail != probeHitHead) {
    mergeProbeHit(probeHitQueue[probeHitTail]);
    probeHitTail = (probeHitTail + 1) % kProbeHitQueueSlots;
  }
  const uint32_t now = millis();
  if (now - lastProbeHopMs >= kProbeHopIntervalMs) {
    lastProbeHopMs = now;
    probeHopIndex = (probeHopIndex + 1) % kDeauthHopChannelCount;
    esp_wifi_set_channel(kDeauthHopChannels[probeHopIndex],
                         WIFI_SECOND_CHAN_NONE);
  }
  if (now - lastProbeDrawMs >= kProbeRedrawMs) {
    lastProbeDrawMs = now;
    drawProbeIntel();
  }
}
