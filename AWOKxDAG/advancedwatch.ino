// AWOKxDAG — Advanced Watch (compiled as part of the sketch).
//
// One shared passive Wi-Fi callback feeds several defensive detectors at once:
// beacon/security integrity, saved-network downgrades, disconnect reason/rate,
// channel-switch abuse, EAPOL storms, auth/association storms, and RF noise
// anomalies. A concurrent passive BLE scan looks for rapid address rotation
// with a stable advertisement structure. Nothing in this module transmits.

AdvancedHit advancedQueue[kAdvancedHitQueueSlots];
volatile int advancedHead = 0;
volatile int advancedTail = 0;
AdvancedBleHit advancedBleQueue[kAdvancedBleQueueSlots];
volatile int advancedBleHead = 0;
volatile int advancedBleTail = 0;

AdvancedApEntry advancedAps[kAdvancedMaxAps];
int advancedApCount = 0;
AdvancedDisconnectGroup
    advancedDisconnectGroups[kAdvancedMaxDisconnectGroups];
int advancedDisconnectGroupCount = 0;
AdvancedBleFingerprint
    advancedBleFingerprints[kAdvancedMaxBleFingerprints];
int advancedBleFingerprintCount = 0;

volatile uint32_t advancedFrameWindow = 0;
volatile int32_t advancedNoiseSumWindow = 0;
volatile uint32_t advancedNoiseSamplesWindow = 0;
volatile uint32_t advancedDisconnectWindow = 0;
volatile uint32_t advancedEapolWindow = 0;
volatile uint32_t advancedAssocWindow = 0;
volatile uint32_t advancedCsaWindow = 0;
uint8_t advancedLastCsaBssid[6] = {0};
volatile uint8_t advancedLastCsaChannel = 0;

uint32_t advancedFrameRate = 0;
uint32_t advancedDisconnectRate = 0;
uint32_t advancedEapolRate = 0;
uint32_t advancedAssocRate = 0;
uint32_t advancedCsaRate = 0;
uint32_t advancedDisconnectPeak = 0;
uint32_t advancedEapolPeak = 0;
uint32_t advancedAssocPeak = 0;
int advancedNoiseCurrent = -127;
int advancedNoiseBaseline = 0;

bool advancedDisconnectAlert = false;
bool advancedEapolAlert = false;
bool advancedAssocAlert = false;
bool advancedCsaAlert = false;
bool advancedRfAlert = false;
uint32_t advancedIntegrityAlerts = 0;
uint32_t advancedSavedDowngradeAlerts = 0;
uint32_t advancedDisconnectAlerts = 0;
uint32_t advancedEapolAlerts = 0;
uint32_t advancedAssocAlerts = 0;
uint32_t advancedCsaAlerts = 0;
uint32_t advancedRfAlerts = 0;
uint32_t advancedBleChurnAlerts = 0;
uint32_t advancedAlertTotal = 0;

uint8_t advancedLastDisconnectSource[6] = {0};
uint8_t advancedLastDisconnectTarget[6] = {0};
uint16_t advancedLastDisconnectReason = 0;
uint8_t advancedLastEapolBssid[6] = {0};
uint8_t advancedLastEapolStation[6] = {0};
uint8_t advancedLastAssocTarget[6] = {0};
uint8_t advancedLastAssocSource[6] = {0};
String advancedLastAlert = "none";
String advancedLastDetails;

int advancedHopIndex = 0;
uint32_t lastAdvancedHopMs = 0;
uint32_t lastAdvancedWindowMs = 0;
uint32_t lastAdvancedDrawMs = 0;
uint32_t advancedStartMs = 0;
bool advancedLogReady = false;

bool advancedMacEqual(const uint8_t* first, const uint8_t* second) {
  return memcmp(first, second, 6) == 0;
}

uint32_t advancedHashByte(uint32_t hash, uint8_t value) {
  return (hash ^ value) * 16777619UL;
}

uint32_t advancedHashBytes(uint32_t hash, const uint8_t* data, int length) {
  for (int i = 0; i < length; ++i) hash = advancedHashByte(hash, data[i]);
  return hash;
}

void advancedEnqueue(const AdvancedHit& hit) {
  const int next = (advancedHead + 1) % kAdvancedHitQueueSlots;
  if (next == advancedTail) return;
  advancedQueue[advancedHead] = hit;
  advancedHead = next;
}

// Runs in the Wi-Fi task. It parses only the fields needed by the detectors and
// hands POD records to the main loop; SD/String/UI work stays off this task.
void advancedWatchCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
  const wifi_promiscuous_pkt_t* packet =
      static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* payload = packet->payload;
  const int length = packet->rx_ctrl.sig_len;
  if (length < 24) return;

  ++advancedFrameWindow;
  const int noise = packet->rx_ctrl.noise_floor;
  if (noise < 0 && noise > -128) {
    advancedNoiseSumWindow += noise;
    ++advancedNoiseSamplesWindow;
  }

  const uint8_t frameType = (payload[0] >> 2) & 0x03;
  const uint8_t subtype = payload[0] & 0xF0;
  if (frameType == 0 && subtype == 0x80 && length >= 38) {
    AdvancedHit hit;
    hit.kind = kAdvancedBeaconHit;
    hit.subtype = subtype;
    memcpy(hit.source, payload + 10, 6);
    memcpy(hit.target, payload + 16, 6);  // BSSID
    hit.channel = packet->rx_ctrl.channel;
    hit.rssi = packet->rx_ctrl.rssi;
    hit.noise = static_cast<int8_t>(noise);
    hit.beaconInterval = payload[32] | (payload[33] << 8);
    const uint16_t capability = payload[34] | (payload[35] << 8);
    const bool privacy = capability & 0x0010;

    bool hasRsn = false;
    bool hasWpa = false;
    bool sae = false;
    bool psk = false;
    bool owe = false;
    bool enterpriseModern = false;
    uint32_t fingerprint = 2166136261UL;
    fingerprint = advancedHashByte(fingerprint, payload[32]);
    fingerprint = advancedHashByte(fingerprint, payload[33]);
    fingerprint = advancedHashByte(fingerprint, payload[34]);
    fingerprint = advancedHashByte(fingerprint, payload[35]);

    int i = 36;
    while (i + 2 <= length) {
      const uint8_t tag = payload[i];
      const uint8_t tagLen = payload[i + 1];
      if (i + 2 + tagLen > length) break;
      const uint8_t* data = payload + i + 2;
      if (tag == 0x00) {
        hit.ssidLen = tagLen > 32 ? 32 : tagLen;
        memcpy(hit.ssid, data, hit.ssidLen);
        hit.ssid[hit.ssidLen] = 0;
        fingerprint = advancedHashByte(fingerprint, tag);
        fingerprint = advancedHashBytes(fingerprint, data, hit.ssidLen);
      } else if (tag == 0x03 && tagLen >= 1) {
        hit.channel = data[0];
      } else if (tag == 0x25 && tagLen >= 3) {
        hit.csaChannel = data[1];
      } else if (tag == 0x30 && tagLen >= 8) {
        hasRsn = true;
        fingerprint = advancedHashByte(fingerprint, tag);
        fingerprint = advancedHashBytes(fingerprint, data, tagLen);
        int p = 6;
        if (p + 2 <= tagLen) {
          const uint16_t pairwiseCount = data[p] | (data[p + 1] << 8);
          p += 2;
          if (pairwiseCount <= static_cast<uint16_t>((tagLen - p) / 4)) {
            p += pairwiseCount * 4;
            if (p + 2 <= tagLen) {
              const uint16_t akmCount = data[p] | (data[p + 1] << 8);
              const int akmBase = p + 2;
              if (akmCount <=
                  static_cast<uint16_t>((tagLen - akmBase) / 4)) {
                for (int a = 0; a < akmCount; ++a) {
                  const uint8_t suite = data[akmBase + a * 4 + 3];
                  if (suite == 0x08 || suite == 0x09) sae = true;
                  if (suite == 0x02 || suite == 0x06) psk = true;
                  if (suite == 0x12) owe = true;
                  if (suite == 0x0B || suite == 0x0C) {
                    enterpriseModern = true;
                  }
                }
                const int caps = akmBase + akmCount * 4;
                if (caps + 2 <= tagLen) {
                  const uint16_t rsnCaps = data[caps] | (data[caps + 1] << 8);
                  if (rsnCaps & 0x0040) hit.pmf = 2;
                  else if (rsnCaps & 0x0080) hit.pmf = 1;
                }
              }
            }
          }
        }
      } else if (tag == 0xDD && tagLen >= 4 && data[0] == 0x00 &&
                 data[1] == 0x50 && data[2] == 0xF2) {
        if (data[3] == 0x01) hasWpa = true;
        if (data[3] == 0x04) hit.wps = 1;
        if (data[3] == 0x01 || data[3] == 0x04) {
          fingerprint = advancedHashByte(fingerprint, tag);
          fingerprint = advancedHashBytes(fingerprint, data, tagLen);
        }
      }
      i += 2 + tagLen;
    }

    if (hasRsn) {
      if ((sae && !psk) || enterpriseModern) hit.security = 6;
      else if (sae || owe) hit.security = 5;
      else hit.security = 4;
    } else if (hasWpa) {
      hit.security = 2;
    } else if (privacy) {
      hit.security = 1;
    } else {
      hit.security = 0;
    }
    hit.fingerprint = fingerprint;
    if (hit.csaChannel) {
      ++advancedCsaWindow;
      memcpy(advancedLastCsaBssid, hit.target, 6);
      advancedLastCsaChannel = hit.csaChannel;
    }
    advancedEnqueue(hit);
    return;
  }

  if (frameType == 0 && (subtype == 0xC0 || subtype == 0xA0) &&
      length >= 26) {
    AdvancedHit hit;
    hit.kind = kAdvancedDisconnectHit;
    hit.subtype = subtype;
    memcpy(hit.source, payload + 10, 6);
    memcpy(hit.target, payload + 16, 6);
    hit.channel = packet->rx_ctrl.channel;
    hit.rssi = packet->rx_ctrl.rssi;
    hit.reason = payload[24] | (payload[25] << 8);
    ++advancedDisconnectWindow;
    advancedEnqueue(hit);
    return;
  }

  if (frameType == 0 &&
      (subtype == 0xB0 || subtype == 0x00 || subtype == 0x20)) {
    AdvancedHit hit;
    hit.kind = kAdvancedAssocHit;
    hit.subtype = subtype;
    memcpy(hit.source, payload + 10, 6);
    memcpy(hit.target, payload + 4, 6);
    hit.channel = packet->rx_ctrl.channel;
    hit.rssi = packet->rx_ctrl.rssi;
    ++advancedAssocWindow;
    advancedEnqueue(hit);
    return;
  }

  if (frameType == 2) {
    int header = 24;
    const uint8_t dataSubtype = (payload[0] >> 4) & 0x0F;
    if (dataSubtype & 0x08) header += 2;
    const bool toDs = payload[1] & 0x01;
    const bool fromDs = payload[1] & 0x02;
    if (toDs && fromDs) header += 6;
    if (length < header + 8) return;
    const uint8_t* llc = payload + header;
    if (llc[0] != 0xAA || llc[1] != 0xAA || llc[2] != 0x03 ||
        llc[6] != 0x88 || llc[7] != 0x8E) return;
    AdvancedHit hit;
    hit.kind = kAdvancedEapolHit;
    hit.channel = packet->rx_ctrl.channel;
    hit.rssi = packet->rx_ctrl.rssi;
    const uint8_t* bssid = fromDs ? payload + 10 : payload + 4;
    const uint8_t* station = fromDs ? payload + 4 : payload + 10;
    memcpy(hit.target, bssid, 6);
    memcpy(hit.source, station, 6);
    ++advancedEapolWindow;
    advancedEnqueue(hit);
  }
}

uint32_t advancedBleStructuralFingerprint(
    const NimBLEAdvertisedDevice* device) {
  const std::vector<uint8_t>& payload = device->getPayload();
  uint32_t hash = 2166136261UL;
  size_t i = 0;
  while (i < payload.size()) {
    const uint8_t fieldLen = payload[i];
    if (fieldLen == 0 || i + 1 + fieldLen > payload.size()) break;
    const uint8_t type = payload[i + 1];
    const uint8_t* data = payload.data() + i + 2;
    const int dataLen = fieldLen - 1;
    hash = advancedHashByte(hash, type);
    hash = advancedHashByte(hash, fieldLen);
    if (type == 0xFF) {  // company id + vendor message type, not rotating bytes
      hash = advancedHashBytes(hash, data, min(dataLen, 3));
    } else if (type == 0x08 || type == 0x09 || type == 0x19 ||
               (type >= 0x02 && type <= 0x07)) {
      hash = advancedHashBytes(hash, data, dataLen);
    } else if (type == 0x16) {
      hash = advancedHashBytes(hash, data, min(dataLen, 3));
    } else if (type == 0x20) {
      hash = advancedHashBytes(hash, data, min(dataLen, 5));
    } else if (type == 0x21) {
      hash = advancedHashBytes(hash, data, min(dataLen, 17));
    }
    i += 1 + fieldLen;
  }
  return hash;
}

class AdvancedBleCallbacks : public NimBLEScanCallbacks {
 public:
  void onResult(const NimBLEAdvertisedDevice* device) override {
    // Address rotation is meaningful only for random-address advertisers.
    if (device->getAddressType() != 1 || device->getPayload().size() < 4) return;
    const int next = (advancedBleHead + 1) % kAdvancedBleQueueSlots;
    if (next == advancedBleTail) return;
    AdvancedBleHit& hit = advancedBleQueue[advancedBleHead];
    const std::string address = device->getAddress().toString();
    strncpy(hit.address, address.c_str(), sizeof(hit.address) - 1);
    hit.address[sizeof(hit.address) - 1] = 0;
    hit.fingerprint = advancedBleStructuralFingerprint(device);
    hit.rssi = device->getRSSI();
    advancedBleHead = next;
  }
};

AdvancedBleCallbacks advancedBleCallbacks;

String advancedMacOrBlank(const uint8_t* mac) {
  return mac ? macToString(mac) : String();
}

void appendAdvancedAlert(const char* event, const char* severity,
                         const uint8_t* source, const uint8_t* target,
                         int channel, uint32_t count, const String& details) {
  ++advancedAlertTotal;
  advancedLastAlert = event;
  advancedLastDetails = details;
  if (advancedLogReady) {
    File file = SD.open(kAdvancedWatchLogCsvPath, FILE_APPEND);
    if (file) {
      file.print(millis());
      file.print(',');
      file.print(csvField(event));
      file.print(',');
      file.print(csvField(severity));
      file.print(',');
      file.print(advancedMacOrBlank(source));
      file.print(',');
      file.print(advancedMacOrBlank(target));
      file.print(',');
      file.print(channel);
      file.print(',');
      file.print(count);
      file.print(',');
      file.print(csvField(details));
      file.print(gpsCsvFields());
      file.println();
      file.close();
    } else {
      advancedLogReady = false;
    }
  }
  recordFirmwareAudit("monitor", "advanced_alert", "detected",
                      String(event) + "; severity=" + severity + "; " +
                          details);
  Serial.printf("[advanced] %s: %s\n", event, details.c_str());
}

int advancedSavedSecurityGrade(wifi_auth_mode_t auth) {
  switch (auth) {
    case WIFI_AUTH_OPEN: return 0;
    case WIFI_AUTH_WEP: return 1;
    case WIFI_AUTH_WPA_PSK: return 2;
    case WIFI_AUTH_WPA_WPA2_PSK: return 3;
    case WIFI_AUTH_WPA2_PSK: return 4;
    case WIFI_AUTH_ENTERPRISE: return 4;
    case WIFI_AUTH_WPA2_WPA3_PSK:
    case WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE:
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
    case WIFI_AUTH_OWE: return 5;
    case WIFI_AUTH_WPA3_PSK:
    case WIFI_AUTH_WPA3_EXT_PSK:
    case WIFI_AUTH_WPA3_ENT_192:
    case WIFI_AUTH_WPA3_ENTERPRISE: return 6;
    default: return 3;
  }
}

int advancedApIndexOf(const uint8_t* bssid) {
  for (int i = 0; i < advancedApCount; ++i) {
    if (advancedMacEqual(advancedAps[i].bssid, bssid)) return i;
  }
  return -1;
}

void advancedCheckSavedDowngrade(AdvancedApEntry& ap) {
  if (ap.savedDowngradeLogged || !ap.ssid.length()) return;
  const String bssid = macToString(ap.bssid);
  for (int i = 0; i < savedCount; ++i) {
    const bool sameBssid =
        savedEntries[i].bssid.length() &&
        savedEntries[i].bssid.equalsIgnoreCase(bssid);
    const bool sameSsid = savedEntries[i].ssid.length() &&
                          savedEntries[i].ssid == ap.ssid;
    if (!sameBssid && !sameSsid) continue;
    const int savedGrade = advancedSavedSecurityGrade(savedEntries[i].auth);
    if (ap.security < savedGrade) {
      ap.savedDowngradeLogged = true;
      ++advancedSavedDowngradeAlerts;
      appendAdvancedAlert(
          "saved_network_downgrade", "high", ap.bssid, ap.bssid, ap.channel,
          1, "ssid=" + ap.ssid + "; saved_grade=" + String(savedGrade) +
                 "; observed_grade=" + String(ap.security) +
                 (sameBssid ? "; same_bssid=yes" : "; same_bssid=no"));
      return;
    }
  }
}

void advancedAcceptBeaconBaseline(AdvancedApEntry& ap,
                                  const AdvancedHit& hit) {
  ap.ssid = hit.ssidLen ? String(hit.ssid) : String();
  ap.channel = hit.channel;
  ap.rssi = hit.rssi;
  ap.beaconInterval = hit.beaconInterval;
  ap.fingerprint = hit.fingerprint;
  ap.security = hit.security;
  ap.pmf = hit.pmf;
  ap.wps = hit.wps;
  ap.lastSeenMs = millis();
}

void mergeAdvancedBeacon(const AdvancedHit& hit) {
  int index = advancedApIndexOf(hit.target);
  if (index < 0) {
    if (advancedApCount < kAdvancedMaxAps) {
      index = advancedApCount++;
    } else {
      index = 0;
      for (int i = 1; i < advancedApCount; ++i) {
        if (advancedAps[i].lastSeenMs < advancedAps[index].lastSeenMs) {
          index = i;
        }
      }
    }
    advancedAps[index] = AdvancedApEntry();
    memcpy(advancedAps[index].bssid, hit.target, 6);
    advancedAcceptBeaconBaseline(advancedAps[index], hit);
    advancedCheckSavedDowngrade(advancedAps[index]);
    return;
  }

  AdvancedApEntry& ap = advancedAps[index];
  ap.rssi = hit.rssi;
  ap.lastSeenMs = millis();
  const String newSsid = hit.ssidLen ? String(hit.ssid) : String();
  const bool changed =
      ap.ssid != newSsid || ap.channel != hit.channel ||
      ap.beaconInterval != hit.beaconInterval ||
      ap.fingerprint != hit.fingerprint || ap.security != hit.security ||
      ap.pmf != hit.pmf || ap.wps != hit.wps;
  if (!changed) {
    ap.pendingCount = 0;
    return;
  }

  uint32_t key = hit.fingerprint;
  key ^= static_cast<uint32_t>(hit.channel) << 24;
  key ^= static_cast<uint32_t>(hit.security) << 20;
  key ^= static_cast<uint32_t>(hit.pmf) << 18;
  key ^= static_cast<uint32_t>(hit.wps) << 16;
  key ^= hit.beaconInterval;
  key = advancedHashBytes(key, reinterpret_cast<const uint8_t*>(hit.ssid),
                          hit.ssidLen);
  if (ap.pendingCount == 0 || ap.pendingKey != key) {
    ap.pendingKey = key;
    ap.pendingCount = 1;
    ap.pendingChannel = hit.channel;
    ap.pendingBeaconInterval = hit.beaconInterval;
    ap.pendingFingerprint = hit.fingerprint;
    ap.pendingSecurity = hit.security;
    ap.pendingPmf = hit.pmf;
    ap.pendingWps = hit.wps;
    memcpy(ap.pendingSsid, hit.ssid, hit.ssidLen + 1);
    return;
  }
  if (++ap.pendingCount < 3) return;

  String details = "bssid=" + macToString(ap.bssid);
  if (ap.ssid != String(ap.pendingSsid)) {
    details += "; ssid=" + ap.ssid + "->" + String(ap.pendingSsid);
  }
  if (ap.channel != ap.pendingChannel) {
    details += "; channel=" + String(ap.channel) + "->" +
               String(ap.pendingChannel);
  }
  if (ap.security != ap.pendingSecurity) {
    details += "; security=" + String(ap.security) + "->" +
               String(ap.pendingSecurity);
  }
  if (ap.pmf != ap.pendingPmf) {
    details += "; pmf=" + String(ap.pmf) + "->" + String(ap.pendingPmf);
  }
  if (ap.wps != ap.pendingWps) {
    details += "; wps=" + String(ap.wps) + "->" + String(ap.pendingWps);
  }
  if (ap.beaconInterval != ap.pendingBeaconInterval) {
    details += "; interval=" + String(ap.beaconInterval) + "->" +
               String(ap.pendingBeaconInterval);
  }
  if (ap.fingerprint != ap.pendingFingerprint) {
    details += "; rsn_wps_fingerprint=changed";
  }
  const bool high = ap.pendingSecurity < ap.security ||
                    ap.pendingPmf < ap.pmf ||
                    (ap.pendingWps && !ap.wps);
  ++advancedIntegrityAlerts;
  appendAdvancedAlert("beacon_integrity_change", high ? "high" : "warning",
                      ap.bssid, ap.bssid, ap.pendingChannel, 3, details);

  ap.ssid = String(ap.pendingSsid);
  ap.channel = ap.pendingChannel;
  ap.beaconInterval = ap.pendingBeaconInterval;
  ap.fingerprint = ap.pendingFingerprint;
  ap.security = ap.pendingSecurity;
  ap.pmf = ap.pendingPmf;
  ap.wps = ap.pendingWps;
  ap.pendingCount = 0;
  advancedCheckSavedDowngrade(ap);
}

void mergeAdvancedDisconnect(const AdvancedHit& hit) {
  int index = -1;
  for (int i = 0; i < advancedDisconnectGroupCount; ++i) {
    if (advancedMacEqual(advancedDisconnectGroups[i].source, hit.source) &&
        advancedMacEqual(advancedDisconnectGroups[i].target, hit.target) &&
        advancedDisconnectGroups[i].reason == hit.reason) {
      index = i;
      break;
    }
  }
  if (index < 0) {
    if (advancedDisconnectGroupCount >= kAdvancedMaxDisconnectGroups) return;
    index = advancedDisconnectGroupCount++;
    memcpy(advancedDisconnectGroups[index].source, hit.source, 6);
    memcpy(advancedDisconnectGroups[index].target, hit.target, 6);
    advancedDisconnectGroups[index].reason = hit.reason;
  }
  ++advancedDisconnectGroups[index].count;
}

void mergeAdvancedHit(const AdvancedHit& hit) {
  if (hit.kind == kAdvancedBeaconHit) {
    mergeAdvancedBeacon(hit);
  } else if (hit.kind == kAdvancedDisconnectHit) {
    mergeAdvancedDisconnect(hit);
  } else if (hit.kind == kAdvancedEapolHit) {
    memcpy(advancedLastEapolBssid, hit.target, 6);
    memcpy(advancedLastEapolStation, hit.source, 6);
  } else if (hit.kind == kAdvancedAssocHit) {
    memcpy(advancedLastAssocTarget, hit.target, 6);
    memcpy(advancedLastAssocSource, hit.source, 6);
  }
}

void mergeAdvancedBleHit(const AdvancedBleHit& hit) {
  const uint32_t now = millis();
  int index = -1;
  for (int i = 0; i < advancedBleFingerprintCount; ++i) {
    if (advancedBleFingerprints[i].fingerprint == hit.fingerprint) {
      index = i;
      break;
    }
  }
  if (index < 0) {
    if (advancedBleFingerprintCount < kAdvancedMaxBleFingerprints) {
      index = advancedBleFingerprintCount++;
    } else {
      index = 0;
      for (int i = 1; i < advancedBleFingerprintCount; ++i) {
        if (advancedBleFingerprints[i].lastSeenMs <
            advancedBleFingerprints[index].lastSeenMs) {
          index = i;
        }
      }
    }
    advancedBleFingerprints[index] = AdvancedBleFingerprint();
    advancedBleFingerprints[index].fingerprint = hit.fingerprint;
  }
  AdvancedBleFingerprint& entry = advancedBleFingerprints[index];
  if (entry.lastSeenMs &&
      now - entry.lastSeenMs > kAdvancedBleChurnWindowMs) {
    const uint32_t fingerprint = entry.fingerprint;
    entry = AdvancedBleFingerprint();
    entry.fingerprint = fingerprint;
  }
  if (entry.firstSeenMs == 0) entry.firstSeenMs = now;
  entry.lastSeenMs = now;

  for (int i = 0; i < entry.addressCount; ++i) {
    if (strcmp(entry.addresses[i], hit.address) == 0) {
      entry.lastRssi = hit.rssi;
      return;
    }
  }
  // Start a fresh observation window when a new address arrives after the
  // prior window aged out; otherwise a busy signature could become immortal.
  if (now - entry.firstSeenMs > kAdvancedBleChurnWindowMs) {
    const uint32_t fingerprint = entry.fingerprint;
    entry = AdvancedBleFingerprint();
    entry.fingerprint = fingerprint;
    entry.firstSeenMs = now;
    entry.lastSeenMs = now;
  }
  if (entry.addressCount > 0 && entry.lastRssi > -126 &&
      abs(static_cast<int>(hit.rssi) - static_cast<int>(entry.lastRssi)) > 8) {
    return;  // likely a different nearby device with the same payload shape
  }
  if (entry.addressCount < kAdvancedBleChurnAddresses + 2) {
    strncpy(entry.addresses[entry.addressCount], hit.address,
            sizeof(entry.addresses[0]) - 1);
    ++entry.addressCount;
  }
  entry.lastRssi = hit.rssi;
  if (!entry.alerted &&
      entry.addressCount >= kAdvancedBleChurnAddresses &&
      now - entry.firstSeenMs <= kAdvancedBleChurnWindowMs) {
    entry.alerted = true;
    ++advancedBleChurnAlerts;
    char fingerprint[12];
    snprintf(fingerprint, sizeof(fingerprint), "%08lX",
             static_cast<unsigned long>(entry.fingerprint));
    appendAdvancedAlert(
        "ble_identity_churn", "warning", nullptr, nullptr, 0,
        entry.addressCount,
        "payload_shape=" + String(fingerprint) +
            "; addresses=" + String(entry.addressCount) +
            "; latest=" + String(hit.address));
  }
}

void closeAdvancedWindow() {
  advancedFrameRate = advancedFrameWindow;
  advancedDisconnectRate = advancedDisconnectWindow;
  advancedEapolRate = advancedEapolWindow;
  advancedAssocRate = advancedAssocWindow;
  advancedCsaRate = advancedCsaWindow;
  const int32_t noiseSum = advancedNoiseSumWindow;
  const uint32_t noiseSamples = advancedNoiseSamplesWindow;
  advancedFrameWindow = 0;
  advancedDisconnectWindow = 0;
  advancedEapolWindow = 0;
  advancedAssocWindow = 0;
  advancedCsaWindow = 0;
  advancedNoiseSumWindow = 0;
  advancedNoiseSamplesWindow = 0;

  advancedDisconnectPeak = max(advancedDisconnectPeak, advancedDisconnectRate);
  advancedEapolPeak = max(advancedEapolPeak, advancedEapolRate);
  advancedAssocPeak = max(advancedAssocPeak, advancedAssocRate);

  int dominant = -1;
  for (int i = 0; i < advancedDisconnectGroupCount; ++i) {
    if (dominant < 0 || advancedDisconnectGroups[i].count >
                            advancedDisconnectGroups[dominant].count) {
      dominant = i;
    }
  }
  if (dominant >= 0) {
    memcpy(advancedLastDisconnectSource,
           advancedDisconnectGroups[dominant].source, 6);
    memcpy(advancedLastDisconnectTarget,
           advancedDisconnectGroups[dominant].target, 6);
    advancedLastDisconnectReason =
        advancedDisconnectGroups[dominant].reason;
  }

  const bool wasDisconnect = advancedDisconnectAlert;
  advancedDisconnectAlert =
      advancedDisconnectRate >= kAdvancedDisconnectThreshold;
  if (advancedDisconnectAlert && !wasDisconnect) {
    ++advancedDisconnectAlerts;
    appendAdvancedAlert(
        "disconnect_storm", "high", advancedLastDisconnectSource,
        advancedLastDisconnectTarget,
        kDeauthHopChannels[advancedHopIndex], advancedDisconnectRate,
        "reason=" + String(advancedLastDisconnectReason) +
            "; peak_group=" +
            String(dominant >= 0 ? advancedDisconnectGroups[dominant].count
                                 : 0));
  }

  const bool wasEapol = advancedEapolAlert;
  advancedEapolAlert = advancedEapolRate >= kAdvancedEapolThreshold;
  if (advancedEapolAlert && !wasEapol) {
    ++advancedEapolAlerts;
    appendAdvancedAlert("eapol_storm", "warning", advancedLastEapolStation,
                        advancedLastEapolBssid,
                        kDeauthHopChannels[advancedHopIndex], advancedEapolRate,
                        "repeated 802.1X key traffic");
  }

  const bool wasAssoc = advancedAssocAlert;
  advancedAssocAlert = advancedAssocRate >= kAdvancedAssocThreshold;
  if (advancedAssocAlert && !wasAssoc) {
    ++advancedAssocAlerts;
    appendAdvancedAlert("association_storm", "high", advancedLastAssocSource,
                        advancedLastAssocTarget,
                        kDeauthHopChannels[advancedHopIndex], advancedAssocRate,
                        "auth/assoc/reassoc request spike");
  }

  const bool wasCsa = advancedCsaAlert;
  advancedCsaAlert = advancedCsaRate >= kAdvancedCsaThreshold;
  if (advancedCsaAlert && !wasCsa) {
    ++advancedCsaAlerts;
    appendAdvancedAlert(
        "channel_switch_abuse", "warning", advancedLastCsaBssid,
        advancedLastCsaBssid, advancedLastCsaChannel, advancedCsaRate,
        "repeated CSA announcements; requested_channel=" +
            String(advancedLastCsaChannel));
  }

  if (noiseSamples > 0) {
    advancedNoiseCurrent = noiseSum / static_cast<int32_t>(noiseSamples);
    if (advancedNoiseBaseline == 0) {
      advancedNoiseBaseline = advancedNoiseCurrent;
    }
    const bool wasRf = advancedRfAlert;
    advancedRfAlert =
        advancedFrameRate >= 20 &&
        advancedNoiseCurrent >= kAdvancedNoiseFloorMinDbm &&
        advancedNoiseCurrent - advancedNoiseBaseline >= kAdvancedNoiseRiseDb;
    if (advancedRfAlert && !wasRf) {
      ++advancedRfAlerts;
      appendAdvancedAlert(
          "rf_noise_anomaly", "warning", nullptr, nullptr,
          kDeauthHopChannels[advancedHopIndex], advancedFrameRate,
          "noise=" + String(advancedNoiseCurrent) +
              "dBm; baseline=" + String(advancedNoiseBaseline) + "dBm");
    }
    if (!advancedRfAlert) {
      advancedNoiseBaseline =
          (advancedNoiseBaseline * 7 + advancedNoiseCurrent) / 8;
    }
  } else {
    advancedRfAlert = false;
  }
  advancedDisconnectGroupCount = 0;
}

void drawAdvancedWatch() {
  currentView = View::kAdvancedWatch;
  const bool alert = advancedDisconnectAlert || advancedEapolAlert ||
                     advancedAssocAlert || advancedCsaAlert || advancedRfAlert ||
                     advancedAlertTotal > 0;
  display.fillScreen(kBackground);
  drawMonitorHeader("ADVANCED WATCH", advancedWatchActive,
             alert ? "ALERT: anomaly detected"
                   : (radiosCoexist ? "Wi-Fi + BLE integrity"
                                      : "Wi-Fi only; Mini BLE off"));
  display.setTextSize(2);
  display.setTextColor(alert ? kBad : kGood, kBackground);
  display.setCursor(6, 48);
  display.print(alert ? "ALERT" : "ALL CLEAR");

  display.setTextSize(1);
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(6, 76);
  display.printf("Alerts %lu | APs %d | ch %d",
                 static_cast<unsigned long>(advancedAlertTotal),
                 advancedApCount, kDeauthHopChannels[advancedHopIndex]);
  display.setCursor(6, 92);
  display.printf("Beacon integrity %lu | saved down %lu",
                 static_cast<unsigned long>(advancedIntegrityAlerts),
                 static_cast<unsigned long>(advancedSavedDowngradeAlerts));
  display.setCursor(6, 108);
  display.printf("Disconnect %lu/w  peak %lu  alerts %lu",
                 static_cast<unsigned long>(advancedDisconnectRate),
                 static_cast<unsigned long>(advancedDisconnectPeak),
                 static_cast<unsigned long>(advancedDisconnectAlerts));
  display.setCursor(6, 124);
  display.printf("EAPOL %lu/w  peak %lu  alerts %lu",
                 static_cast<unsigned long>(advancedEapolRate),
                 static_cast<unsigned long>(advancedEapolPeak),
                 static_cast<unsigned long>(advancedEapolAlerts));
  display.setCursor(6, 140);
  display.printf("Auth/assoc %lu/w peak %lu alerts %lu",
                 static_cast<unsigned long>(advancedAssocRate),
                 static_cast<unsigned long>(advancedAssocPeak),
                 static_cast<unsigned long>(advancedAssocAlerts));
  display.setCursor(6, 156);
  display.printf("CSA %lu/w alerts %lu | RF %d/%d dBm",
                 static_cast<unsigned long>(advancedCsaRate),
                 static_cast<unsigned long>(advancedCsaAlerts),
                 advancedNoiseCurrent, advancedNoiseBaseline);
  display.setCursor(6, 172);
  if (radiosCoexist) {
    display.printf("RF alerts %lu | BLE churn %lu",
                   static_cast<unsigned long>(advancedRfAlerts),
                   static_cast<unsigned long>(advancedBleChurnAlerts));
  } else {
    display.printf("RF alerts %lu | BLE churn off",
                   static_cast<unsigned long>(advancedRfAlerts));
  }

  display.drawFastHLine(6, 190, 228, kPanel);
  display.setTextColor(advancedAlertTotal ? kBad : kMuted, kBackground);
  display.setCursor(6, 199);
  display.print("Last: ");
  display.print(clipped(advancedLastAlert, 30));
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 214);
  display.print(clipped(advancedLastDetails, 37));
  if (advancedLastDetails.length() > 37) {
    display.setCursor(6, 227);
    display.print(clipped(advancedLastDetails.substring(37), 37));
  }
  display.setCursor(6, 247);
  display.print(advancedLogReady ? "SD: advanced_watch.csv"
                                 : "SD log unavailable; live view only");
  display.setCursor(6, 260);
  display.print("Passive heuristics; verify alerts manually.");
  drawFooter(advancedWatchActive ? "Stop" : "Back", "Reset");
}

void resetAdvancedWatch() {
  advancedHead = 0;
  advancedTail = 0;
  advancedBleHead = 0;
  advancedBleTail = 0;
  advancedFrameWindow = 0;
  advancedNoiseSumWindow = 0;
  advancedNoiseSamplesWindow = 0;
  advancedDisconnectWindow = 0;
  advancedEapolWindow = 0;
  advancedAssocWindow = 0;
  advancedCsaWindow = 0;
  advancedApCount = 0;
  advancedDisconnectGroupCount = 0;
  advancedBleFingerprintCount = 0;
  for (int i = 0; i < kAdvancedMaxAps; ++i) {
    advancedAps[i] = AdvancedApEntry();
  }
  for (int i = 0; i < kAdvancedMaxBleFingerprints; ++i) {
    advancedBleFingerprints[i] = AdvancedBleFingerprint();
  }
  advancedFrameRate = 0;
  advancedDisconnectRate = 0;
  advancedEapolRate = 0;
  advancedAssocRate = 0;
  advancedCsaRate = 0;
  advancedDisconnectPeak = 0;
  advancedEapolPeak = 0;
  advancedAssocPeak = 0;
  advancedNoiseCurrent = -127;
  advancedNoiseBaseline = 0;
  advancedDisconnectAlert = false;
  advancedEapolAlert = false;
  advancedAssocAlert = false;
  advancedCsaAlert = false;
  advancedRfAlert = false;
  advancedIntegrityAlerts = 0;
  advancedSavedDowngradeAlerts = 0;
  advancedDisconnectAlerts = 0;
  advancedEapolAlerts = 0;
  advancedAssocAlerts = 0;
  advancedCsaAlerts = 0;
  advancedRfAlerts = 0;
  advancedBleChurnAlerts = 0;
  advancedAlertTotal = 0;
  advancedLastAlert = "none";
  advancedLastDetails = "";
  memset(advancedLastDisconnectSource, 0, 6);
  memset(advancedLastDisconnectTarget, 0, 6);
  advancedLastDisconnectReason = 0;
  memset(advancedLastEapolBssid, 0, 6);
  memset(advancedLastEapolStation, 0, 6);
  memset(advancedLastAssocTarget, 0, 6);
  memset(advancedLastAssocSource, 0, 6);
  memset(advancedLastCsaBssid, 0, 6);
  advancedLastCsaChannel = 0;
  lastAdvancedWindowMs = millis();
}

// Wi-Fi window hooks: (re)arm promiscuous capture on the current hop channel,
// and drop promiscuous before Wi-Fi is released for the BLE window.
static void advancedEnterWifi() {
  WiFi.disconnect(false, false);
  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask =
      WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&advancedWatchCallback);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(kDeauthHopChannels[advancedHopIndex],
                       WIFI_SECOND_CHAN_NONE);
}
static void advancedExitWifi() { esp_wifi_set_promiscuous(false); }

RadioScheduler advancedSched;

void startAdvancedWatch() {
  resetAdvancedWatch();
  advancedHopIndex = 0;
  lastAdvancedHopMs = millis();
  lastAdvancedDrawMs = 0;
  advancedStartMs = millis();
  signalMonitorActive = false;

  advancedLogReady = false;
  if (ensureSdCard()) {
    SD.remove(kAdvancedWatchLogCsvPath);
    File file = SD.open(kAdvancedWatchLogCsvPath, FILE_WRITE);
    if (file) {
      file.println(
          "uptime_ms,event,severity,source,target,channel,count,details,"
          "latitude,longitude,altitude_m");
      file.close();
      advancedLogReady = true;
    }
  }

  advancedSched = RadioScheduler();
  advancedSched.enterWifi = advancedEnterWifi;
  advancedSched.exitWifi = advancedExitWifi;
  advancedSched.bleCallbacks = &advancedBleCallbacks;  // passive scan
  if (!radioSchedulerBegin(advancedSched)) return;

  Serial.println(radiosCoexist
                     ? "[advanced] Wi-Fi + BLE watch started (time-shared)"
                     : "[advanced] Wi-Fi-only watch started");
  advancedWatchActive = true;
  recordFirmwareAudit("monitor", "advanced_watch_start", "success",
                      advancedLogReady ? "sd_log=ready" : "sd_log=unavailable");
  drawAdvancedWatch();
}

void stopAdvancedWatch() {
  advancedWatchActive = false;
  radioSchedulerEnd(advancedSched);
  recordFirmwareAudit("monitor", "advanced_watch_stop", "success",
                      "alerts=" + String(advancedAlertTotal));
  Serial.printf("[advanced] stopped; %lu alert(s)\n",
                static_cast<unsigned long>(advancedAlertTotal));
}

void updateAdvancedWatch() {
  if (!advancedWatchActive || currentView != View::kAdvancedWatch) return;
  radioSchedulerTick(advancedSched);
  while (advancedTail != advancedHead) {
    mergeAdvancedHit(advancedQueue[advancedTail]);
    advancedTail = (advancedTail + 1) % kAdvancedHitQueueSlots;
  }
  while (advancedBleTail != advancedBleHead) {
    mergeAdvancedBleHit(advancedBleQueue[advancedBleTail]);
    advancedBleTail = (advancedBleTail + 1) % kAdvancedBleQueueSlots;
  }
  const uint32_t now = millis();
  if (now - lastAdvancedWindowMs >= kAdvancedWindowMs) {
    lastAdvancedWindowMs = now;
    closeAdvancedWindow();
  }
  // Channel-hop only during the Wi-Fi window (Wi-Fi is down otherwise).
  if (advancedSched.phase == RadioPhase::kWifi &&
      now - lastAdvancedHopMs >= kAdvancedHopIntervalMs) {
    lastAdvancedHopMs = now;
    advancedHopIndex = (advancedHopIndex + 1) % kDeauthHopChannelCount;
    esp_wifi_set_channel(kDeauthHopChannels[advancedHopIndex],
                         WIFI_SECOND_CHAN_NONE);
  }
  if (now - lastAdvancedDrawMs >= kAdvancedRedrawMs) {
    lastAdvancedDrawMs = now;
    drawAdvancedWatch();
  }
}
