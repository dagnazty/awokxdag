// AWOKxDAG — Security Audit (compiled as part of the sketch; see awok_common.h)
//
// Passive beacon-information-element posture report. Promiscuously parses each
// AP's beacon for the RSN IE (cipher + AKM suites, 802.11w/PMF capabilities),
// the legacy WPA vendor IE, and the WPS vendor IE, then classifies the network
// by encryption tier and scores its risk so the weakest APs sort to the top.
// Listen-only: it never transmits.

AuditEntry auditEntries[kMaxAudit];
int auditCount = 0;
AuditHit auditHitQueue[kAuditHitQueueSlots];
volatile int auditHitHead = 0;
volatile int auditHitTail = 0;
int auditHopIndex = 0;
uint32_t lastAuditHopMs = 0;
uint32_t lastAuditDrawMs = 0;
uint32_t auditStartMs = 0;

const char* auditEncShort(uint8_t enc) {
  switch (enc) {
    case kAuditOpen: return "OPEN";
    case kAuditWep: return "WEP";
    case kAuditWpa: return "WPA";
    case kAuditWpa2: return "WPA2";
    case kAuditWpa2Tkip: return "WPA2-TKIP";
    case kAuditWpa23: return "WPA2/3";
    case kAuditWpa3: return "WPA3";
    case kAuditOwe: return "OWE";
    case kAuditEnterprise: return "WPA-Ent";
    default: return "?";
  }
}

const char* auditPmfShort(uint8_t pmf) {
  return pmf == 2 ? "req" : (pmf == 1 ? "opt" : "no");
}

// Higher score = weaker / higher risk. Sorts the weakest APs to the top.
int auditRisk(uint8_t enc, uint8_t pmf, uint8_t wps) {
  int score = 0;
  switch (enc) {
    case kAuditOpen: score = 100; break;
    case kAuditWep: score = 90; break;
    case kAuditWpa: score = 70; break;
    case kAuditWpa2Tkip: score = 55; break;
    case kAuditOwe: score = 25; break;
    case kAuditWpa2: score = 30; break;
    case kAuditEnterprise: score = 12; break;
    case kAuditWpa23: score = 15; break;
    case kAuditWpa3: score = 5; break;
  }
  if (wps == 1) score += 15;   // WPS enabled and unlocked
  else if (wps == 2) score += 5;
  if (enc >= kAuditWpa2 && enc <= kAuditWpa3) {
    if (pmf == 0) score += 10;       // no management-frame protection
    else if (pmf == 1) score += 3;   // capable but not required
  }
  return score;
}

// Runs in the Wi-Fi task: parse one beacon into an AuditHit and enqueue it.
void securityAuditCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* packet =
      static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* payload = packet->payload;
  const int length = packet->rx_ctrl.sig_len;
  if (length < 38) return;
  if ((payload[0] & 0xF0) != 0x80) return;  // beacon subtype

  const uint16_t capability = payload[34] | (payload[35] << 8);
  const bool privacy = capability & 0x0010;

  char ssid[33];
  uint8_t ssidLen = 0;
  ssid[0] = 0;
  bool hasRsn = false;
  bool hasWpa = false;
  bool wps = false;
  bool wpsLocked = false;
  bool sae = false;
  bool psk = false;
  bool owe = false;
  bool enterprise = false;
  bool groupTkip = false;
  uint8_t pmf = 0;

  int i = 36;
  while (i + 2 <= length) {
    const uint8_t tag = payload[i];
    const uint8_t tagLen = payload[i + 1];
    if (i + 2 + tagLen > length) break;
    const uint8_t* data = payload + i + 2;
    if (tag == 0x00) {  // SSID
      ssidLen = tagLen > 32 ? 32 : tagLen;
      memcpy(ssid, data, ssidLen);
      ssid[ssidLen] = 0;
    } else if (tag == 0x30 && tagLen >= 8) {  // RSN IE (WPA2/WPA3)
      hasRsn = true;
      // group cipher suite type is the 4th byte of the suite at data[2..5].
      if (data[5] == 0x02) groupTkip = true;  // 00-0F-AC-02 = TKIP
      int p = 6;
      const uint16_t pairwiseCount = data[p] | (data[p + 1] << 8);
      p += 2 + 4 * pairwiseCount;  // skip pairwise cipher list
      if (p + 2 <= tagLen) {
        const uint16_t akmCount = data[p] | (data[p + 1] << 8);
        int akmBase = p + 2;
        for (int a = 0; a < akmCount && akmBase + 4 * a + 3 < tagLen; ++a) {
          const uint8_t suite = data[akmBase + 4 * a + 3];  // 00-0F-AC-xx
          if (suite == 0x08 || suite == 0x09) sae = true;   // SAE (WPA3)
          else if (suite == 0x02 || suite == 0x06) psk = true;
          else if (suite == 0x12) owe = true;               // OWE
          else if (suite == 0x01 || suite == 0x03 || suite == 0x0B ||
                   suite == 0x0C) enterprise = true;
        }
        const int capsOffset = akmBase + 4 * akmCount;  // RSN capabilities
        if (capsOffset + 2 <= tagLen) {
          const uint16_t rsnCaps = data[capsOffset] | (data[capsOffset + 1] << 8);
          if (rsnCaps & 0x0040) pmf = 2;       // MFPR (required)
          else if (rsnCaps & 0x0080) pmf = 1;  // MFPC (capable)
        }
      }
    } else if (tag == 0xDD && tagLen >= 4 && data[0] == 0x00 &&
               data[1] == 0x50 && data[2] == 0xF2) {
      if (data[3] == 0x01) {  // WPA vendor IE (WPA1)
        hasWpa = true;
      } else if (data[3] == 0x04) {  // WPS vendor IE
        wps = true;
        int j = 4;
        while (j + 4 <= tagLen) {
          const uint16_t attrType = (data[j] << 8) | data[j + 1];
          const uint16_t attrLen = (data[j + 2] << 8) | data[j + 3];
          if (j + 4 + attrLen > tagLen) break;
          if (attrType == 0x1057 && attrLen >= 1) wpsLocked = data[j + 4] != 0;
          j += 4 + attrLen;
        }
      }
    }
    i += 2 + tagLen;
  }

  uint8_t enc;
  if (hasRsn) {
    if (owe) enc = kAuditOwe;
    else if (enterprise && !psk && !sae) enc = kAuditEnterprise;
    else if (sae && psk) enc = kAuditWpa23;
    else if (sae) enc = kAuditWpa3;
    else if (groupTkip) enc = kAuditWpa2Tkip;
    else enc = kAuditWpa2;
  } else if (hasWpa) {
    enc = kAuditWpa;
  } else if (privacy) {
    enc = kAuditWep;
  } else {
    enc = kAuditOpen;
  }

  const int next = (auditHitHead + 1) % kAuditHitQueueSlots;
  if (next == auditHitTail) return;
  AuditHit& hit = auditHitQueue[auditHitHead];
  memcpy(hit.bssid, payload + 16, 6);
  hit.channel = packet->rx_ctrl.channel;
  hit.rssi = packet->rx_ctrl.rssi;
  hit.enc = enc;
  hit.pmf = pmf;
  hit.wps = wps ? (wpsLocked ? 2 : 1) : 0;
  hit.ssidLen = ssidLen;
  memcpy(hit.ssid, ssid, ssidLen + 1);
  auditHitHead = next;
}

int auditIndexOf(const uint8_t* bssid) {
  for (int i = 0; i < auditCount; ++i) {
    bool equal = true;
    for (int j = 0; j < 6; ++j) {
      if (auditEntries[i].bssid[j] != bssid[j]) { equal = false; break; }
    }
    if (equal) return i;
  }
  return -1;
}

void mergeAuditHit(const AuditHit& hit) {
  int index = auditIndexOf(hit.bssid);
  if (index < 0) {
    if (auditCount >= kMaxAudit) return;
    index = auditCount++;
    auditEntries[index] = AuditEntry();
    memcpy(auditEntries[index].bssid, hit.bssid, 6);
  }
  AuditEntry& entry = auditEntries[index];
  entry.ssid = hit.ssidLen ? String(hit.ssid) : String();
  entry.channel = hit.channel;
  entry.rssi = hit.rssi;
  entry.enc = hit.enc;
  entry.pmf = hit.pmf;
  entry.wps = hit.wps;
  entry.risk = auditRisk(hit.enc, hit.pmf, hit.wps);
}

void sortAuditByRisk() {
  for (int i = 0; i < auditCount - 1; ++i) {
    for (int j = i + 1; j < auditCount; ++j) {
      if (auditEntries[j].risk > auditEntries[i].risk) {
        AuditEntry temporary = auditEntries[i];
        auditEntries[i] = auditEntries[j];
        auditEntries[j] = temporary;
      }
    }
  }
}

bool exportSecurityAuditToSd() {
  if (!ensureSdCard()) return false;
  const String temporaryPath = String(kSecurityAuditCsvPath) + ".tmp";
  SD.remove(temporaryPath.c_str());
  File file = SD.open(temporaryPath.c_str(), FILE_WRITE);
  if (!file) {
    sdReady = false;
    return false;
  }
  file.println(
      "uptime_ms,ssid,bssid,channel,band_ghz,rssi,encryption,pmf,wps,risk,"
      "latitude,longitude,altitude_m");
  const uint32_t uptime = millis();
  const String location = gpsCsvFields();
  for (int i = 0; i < auditCount; ++i) {
    file.print(uptime);
    file.print(',');
    file.print(csvField(auditEntries[i].ssid));
    file.print(',');
    file.print(macToString(auditEntries[i].bssid));
    file.print(',');
    file.print(auditEntries[i].channel);
    file.print(',');
    file.print(bandLabel(auditEntries[i].channel));
    file.print(',');
    file.print(auditEntries[i].rssi);
    file.print(',');
    file.print(auditEncShort(auditEntries[i].enc));
    file.print(',');
    file.print(auditPmfShort(auditEntries[i].pmf));
    file.print(',');
    file.print(auditEntries[i].wps == 2 ? "locked"
                                        : (auditEntries[i].wps == 1 ? "open"
                                                                    : "no"));
    file.print(',');
    file.print(auditEntries[i].risk);
    file.println(location);
  }
  file.flush();
  const bool ok = file.getWriteError() == 0;
  file.close();
  if (!ok) {
    SD.remove(temporaryPath.c_str());
    return false;
  }
  SD.remove(kSecurityAuditCsvPath);
  if (!SD.rename(temporaryPath.c_str(), kSecurityAuditCsvPath)) return false;
  Serial.printf("[audit] wrote %d row(s) to %s\n", auditCount,
                kSecurityAuditCsvPath);
  return true;
}

void drawSecurityAudit() {
  currentView = View::kSecurityAudit;
  display.fillScreen(kBackground);
  drawHeader("SECURITY AUDIT",
             String(auditCount) + " AP(s) | ch " +
                 String(kDeauthHopChannels[auditHopIndex]) + " weakest first");
  display.setTextSize(1);
  sortAuditByRisk();
  const int rows = min(auditCount, kVisibleRows);
  for (int i = 0; i < rows; ++i) {
    const int y = 48 + i * 22;
    display.setTextColor(ILI9341_WHITE, kBackground);
    display.setCursor(5, y);
    display.print(clipped(auditEntries[i].ssid.length() ? auditEntries[i].ssid
                                                        : "<hidden>",
                          26));
    const int risk = auditEntries[i].risk;
    const uint16_t color = risk >= 70 ? kBad : (risk >= 30 ? kWarn : kGood);
    display.setTextColor(color, kBackground);
    display.setCursor(5, y + 11);
    display.printf("%-9s pmf %-3s wps %-4s r%d",
                   auditEncShort(auditEntries[i].enc),
                   auditPmfShort(auditEntries[i].pmf),
                   auditEntries[i].wps == 2 ? "lock"
                                            : (auditEntries[i].wps == 1 ? "open"
                                                                        : "-"),
                   risk);
  }
  if (auditCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(30, 145);
    display.print("Listening for beacons...");
  }
  drawFooter("Back", lastAuditCsvOk ? "Saved" : "Save");
}

void startSecurityAudit() {
  auditCount = 0;
  auditHitHead = 0;
  auditHitTail = 0;
  auditHopIndex = 0;
  lastAuditHopMs = millis();
  lastAuditDrawMs = 0;
  auditStartMs = millis();
  lastAuditCsvOk = false;
  signalMonitorActive = false;

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&securityAuditCallback);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(kDeauthHopChannels[0], WIFI_SECOND_CHAN_NONE);

  securityAuditActive = true;
  Serial.println("[audit] security audit started");
  drawSecurityAudit();
}

void stopSecurityAudit() {
  securityAuditActive = false;
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true, false);
  lastAuditCsvOk = exportSecurityAuditToSd();
  Serial.printf("[audit] stopped; %d AP(s)\n", auditCount);
}

void updateSecurityAudit() {
  if (!securityAuditActive || currentView != View::kSecurityAudit) return;
  while (auditHitTail != auditHitHead) {
    mergeAuditHit(auditHitQueue[auditHitTail]);
    auditHitTail = (auditHitTail + 1) % kAuditHitQueueSlots;
  }
  const uint32_t now = millis();
  if (now - lastAuditHopMs >= kAuditHopIntervalMs) {
    lastAuditHopMs = now;
    auditHopIndex = (auditHopIndex + 1) % kDeauthHopChannelCount;
    esp_wifi_set_channel(kDeauthHopChannels[auditHopIndex],
                         WIFI_SECOND_CHAN_NONE);
  }
  if (now - lastAuditDrawMs >= kAuditRedrawMs) {
    lastAuditDrawMs = now;
    drawSecurityAudit();
  }
}
