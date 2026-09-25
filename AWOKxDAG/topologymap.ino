// AWOKxDAG — Swarm Mesh Topology Map (compiled as part of the sketch; see awok_common.h)
//
// Passively listens for 802.11 client<->AP data associations and directed probe requests.
// Builds an in-memory cluster graph mapping active clients to their parent APs,
// detects open networks, tracks probe SSID leaks, and shares topology links across the ESP-NOW swarm.

struct TopoAp {
  uint8_t bssid[6] = {0};
  char ssid[33] = {0};
  uint8_t channel = 0;
  int8_t rssi = -127;
  bool isOpen = false;
  uint8_t security = 255;
  uint16_t clientCount = 0;
  uint32_t lastSeenMs = 0;
};

struct TopoClient {
  uint8_t mac[6] = {0};
  uint8_t bssid[6] = {0};
  bool hasBssid = false;
  uint8_t channel = 0;
  int8_t rssi = -127;
  uint16_t packetCount = 0;
  uint32_t lastSeenMs = 0;
};

struct TopoProbe {
  uint8_t clientMac[6] = {0};
  char ssid[33] = {0};
  int8_t rssi = -127;
  uint16_t count = 0;
  uint32_t lastSeenMs = 0;
};

constexpr int kMaxTopoAps = 32;
constexpr int kMaxTopoClients = 48;
constexpr int kMaxTopoProbes = 32;
constexpr int kTopoHitQueueSlots = AwokPins::kDualBand ? 32 : 16;
constexpr uint32_t kTopoHopIntervalMs = 250;
constexpr uint32_t kTopoRedrawMs = 1000;
constexpr uint32_t kTopoTelemIntervalMs = 2000;

constexpr uint8_t kTopoSecOpen = 0;
constexpr uint8_t kTopoSecWep = 1;
constexpr uint8_t kTopoSecWpa = 2;
constexpr uint8_t kTopoSecWpa2 = 3;
constexpr uint8_t kTopoSecWpa23 = 4;
constexpr uint8_t kTopoSecWpa3 = 5;
constexpr uint8_t kTopoSecUnknown = 255;

uint8_t topoSecurityFromAuth(uint8_t auth) {
  switch (auth) {
    case WIFI_AUTH_OPEN: return kTopoSecOpen;
    case WIFI_AUTH_WEP: return kTopoSecWep;
    case WIFI_AUTH_WPA_PSK: return kTopoSecWpa;
    case WIFI_AUTH_WPA2_WPA3_PSK:
    case WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE:
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE: return kTopoSecWpa23;
    case WIFI_AUTH_WPA3_PSK:
    case WIFI_AUTH_WPA3_EXT_PSK:
    case WIFI_AUTH_WPA3_ENT_192:
    case WIFI_AUTH_WPA3_ENTERPRISE:
    case WIFI_AUTH_OWE: return kTopoSecWpa3;
    case WIFI_AUTH_WPA2_PSK:
    case WIFI_AUTH_WPA_WPA2_PSK:
    case WIFI_AUTH_ENTERPRISE: return kTopoSecWpa2;
    default: return kTopoSecUnknown;
  }
}

static ToolBuffer<TopoAp, kMaxTopoAps> topoAps;
static int topoApCount = 0;

static ToolBuffer<TopoClient, kMaxTopoClients> topoClients;
static int topoClientCount = 0;

static ToolBuffer<TopoProbe, kMaxTopoProbes> topoProbes;
static int topoProbeCount = 0;

static ToolQueue<TopoHit, kTopoHitQueueSlots> topoHitQueue;

static int topoHopIndex = 0;
static uint32_t lastTopoHopMs = 0;
static uint32_t lastTopoDrawMs = 0;
static uint32_t lastTopoTelemMs = 0;
static uint32_t topoStartMs = 0;
static uint32_t topoTotalFrames = 0;

int topoFindAp(const uint8_t* bssid) {
  for (int i = 0; i < topoApCount; ++i) {
    if (memcmp(topoAps[i].bssid, bssid, 6) == 0) return i;
  }
  return -1;
}

int topoFindClient(const uint8_t* mac) {
  for (int i = 0; i < topoClientCount; ++i) {
    if (memcmp(topoClients[i].mac, mac, 6) == 0) return i;
  }
  return -1;
}

int topoFindProbe(const uint8_t* mac, const char* ssid) {
  for (int i = 0; i < topoProbeCount; ++i) {
    if (memcmp(topoProbes[i].clientMac, mac, 6) == 0 &&
        strncmp(topoProbes[i].ssid, ssid, 32) == 0) {
      return i;
    }
  }
  return -1;
}

void topoPromiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  const uint32_t generation = topoHitQueue.generation();
  if (!generation) return;
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
  const wifi_promiscuous_pkt_t* packet =
      static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* payload = packet->payload;
  const int length = packet->rx_ctrl.sig_len;
  if (length < 24) return;

  const uint8_t frameControl = payload[0];
  const uint8_t frameType = (frameControl >> 2) & 0x03;
  TopoHit hit = {};
  hit.security = kTopoSecUnknown;
  hit.rssi = packet->rx_ctrl.rssi;
  hit.channel = packet->rx_ctrl.channel;

  if (frameType == 0x00) {  // Management frame
    const uint8_t subtype = frameControl & 0xF0;
    if (subtype == 0x40) {  // Probe request
      const uint8_t* mac = payload + 10;
      if (mac[0] & 0x01) return;  // ignore multicast/broadcast
      hit.type = 1;
      memcpy(hit.clientMac, mac, 6);
      if (length >= 26) {
        const uint8_t elementId = payload[24];
        const uint8_t elementLen = payload[25];
        if (elementId == 0 && elementLen > 0 && elementLen <= 32 &&
            length >= 26 + elementLen) {
          memcpy(hit.ssid, payload + 26, elementLen);
          hit.ssid[elementLen] = '\0';
        }
      }
    } else if (subtype == 0x80 || subtype == 0x50) {  // Beacon or Probe Response
      hit.type = 2;
      memcpy(hit.bssid, payload + 16, 6);
      if (length >= 36) {
        uint16_t cap = payload[34] | (payload[35] << 8);
        bool privacy = (cap & 0x0010) != 0;
        bool hasRsn = false;
        bool hasWpa = false;
        bool sae = false;
        bool psk = false;
        int offset = 36;
        while (offset + 2 <= length) {
          uint8_t tag = payload[offset];
          uint8_t tagLen = payload[offset + 1];
          if (offset + 2 + tagLen > length) break;
          const uint8_t* data = payload + offset + 2;
          if (tag == 0 && tagLen > 0 && tagLen <= 32) {
            memcpy(hit.ssid, data, tagLen);
            hit.ssid[tagLen] = '\0';
          } else if (tag == 0x30 && tagLen >= 8) {
            hasRsn = true;
            int p = 6;
            if (p + 2 <= tagLen) {
              uint16_t pairwiseCount = data[p] | (data[p + 1] << 8);
              p += 2 + 4 * pairwiseCount;
              if (p + 2 <= tagLen) {
                uint16_t akmCount = data[p] | (data[p + 1] << 8);
                int akmBase = p + 2;
                for (int a = 0; a < akmCount && akmBase + 4 * a + 3 < tagLen; ++a) {
                  uint8_t suite = data[akmBase + 4 * a + 3];
                  if (suite == 0x08 || suite == 0x09) sae = true;
                  else if (suite == 0x02 || suite == 0x06) psk = true;
                }
              }
            }
          } else if (tag == 0xDD && tagLen >= 4 && data[0] == 0x00 &&
                     data[1] == 0x50 && data[2] == 0xF2 && data[3] == 0x01) {
            hasWpa = true;
          }
          offset += 2 + tagLen;
        }
        if (hasRsn) {
          if (sae && psk) hit.security = kTopoSecWpa23;
          else if (sae) hit.security = kTopoSecWpa3;
          else hit.security = kTopoSecWpa2;
        } else if (hasWpa) {
          hit.security = kTopoSecWpa;
        } else if (privacy) {
          hit.security = kTopoSecWep;
        } else {
          hit.security = kTopoSecOpen;
        }
        hit.isOpen = hit.security == kTopoSecOpen;
      }
    } else {
      return;
    }
  } else if (frameType == 0x02) {  // Data frame
    const uint8_t frameControl1 = payload[1];
    const bool toDs = frameControl1 & 0x01;
    const bool fromDs = frameControl1 & 0x02;
    if (toDs && !fromDs) {  // Station -> AP
      const uint8_t* staMac = payload + 10;
      const uint8_t* bssid = payload + 4;
      if ((staMac[0] & 0x01) || (bssid[0] & 0x01)) return;
      hit.type = 0;
      memcpy(hit.clientMac, staMac, 6);
      memcpy(hit.bssid, bssid, 6);
    } else {
      return;
    }
  } else {
    return;
  }

  topoHitQueue.push(hit, generation);
}

void topoBroadcastLink(uint8_t linkType, int8_t rssi, uint8_t channel,
                       const uint8_t* clientMac, const uint8_t* targetMac,
                       const char* targetName) {
  if (!linkEspNowReady) return;
  FleetTopologyLink linkMsg;
  linkMsg.linkType = linkType;
  linkMsg.rssi = rssi;
  linkMsg.channel = channel;
  memcpy(linkMsg.clientMac, clientMac, 6);
  if (targetMac) memcpy(linkMsg.targetMac, targetMac, 6);
  if (targetName) strncpy(linkMsg.targetName, targetName, sizeof(linkMsg.targetName) - 1);
  esp_now_send(kLinkBroadcastAddr, reinterpret_cast<const uint8_t*>(&linkMsg), sizeof(linkMsg));
}

void topoMergeHit(const TopoHit& hit) {
  ++topoTotalFrames;
  const uint32_t now = millis();

  if (hit.type == 2) {  // Beacon / Probe response
    int idx = topoFindAp(hit.bssid);
    if (idx < 0) {
      if (topoApCount >= kMaxTopoAps) return;
      idx = topoApCount++;
      topoAps[idx] = TopoAp();
      memcpy(topoAps[idx].bssid, hit.bssid, 6);
    }
    topoAps[idx].channel = hit.channel;
    topoAps[idx].rssi = hit.rssi;
    topoAps[idx].isOpen = hit.isOpen;
    topoAps[idx].security = hit.security;
    topoAps[idx].lastSeenMs = now;
    if (hit.ssid[0] != '\0') {
      strncpy(topoAps[idx].ssid, hit.ssid, sizeof(topoAps[idx].ssid) - 1);
    }
    return;
  }

  if (hit.type == 0) {  // Data frame: Station -> AP
    // Ensure AP entry exists
    int apIdx = topoFindAp(hit.bssid);
    if (apIdx < 0) {
      if (topoApCount < kMaxTopoAps) {
        apIdx = topoApCount++;
        topoAps[apIdx] = TopoAp();
        memcpy(topoAps[apIdx].bssid, hit.bssid, 6);
        topoAps[apIdx].channel = hit.channel;
        topoAps[apIdx].rssi = hit.rssi;
        topoAps[apIdx].lastSeenMs = now;
      }
    } else {
      topoAps[apIdx].rssi = hit.rssi;
      topoAps[apIdx].lastSeenMs = now;
    }

    // Upsert client
    int cliIdx = topoFindClient(hit.clientMac);
    bool isNewClient = false;
    if (cliIdx < 0) {
      if (topoClientCount >= kMaxTopoClients) return;
      cliIdx = topoClientCount++;
      topoClients[cliIdx] = TopoClient();
      memcpy(topoClients[cliIdx].mac, hit.clientMac, 6);
      isNewClient = true;
    }
    TopoClient& cli = topoClients[cliIdx];
    cli.channel = hit.channel;
    cli.rssi = hit.rssi;
    cli.lastSeenMs = now;
    ++cli.packetCount;

    if (!cli.hasBssid || memcmp(cli.bssid, hit.bssid, 6) != 0) {
      memcpy(cli.bssid, hit.bssid, 6);
      cli.hasBssid = true;
      if (apIdx >= 0) ++topoAps[apIdx].clientCount;
      // Share topology link across the swarm
      topoBroadcastLink(0, hit.rssi, hit.channel, hit.clientMac, hit.bssid, nullptr);
    } else if (isNewClient) {
      if (apIdx >= 0) ++topoAps[apIdx].clientCount;
      topoBroadcastLink(0, hit.rssi, hit.channel, hit.clientMac, hit.bssid, nullptr);
    }
    return;
  }

  if (hit.type == 1) {  // Probe request
    int cliIdx = topoFindClient(hit.clientMac);
    if (cliIdx < 0 && topoClientCount < kMaxTopoClients) {
      cliIdx = topoClientCount++;
      topoClients[cliIdx] = TopoClient();
      memcpy(topoClients[cliIdx].mac, hit.clientMac, 6);
      topoClients[cliIdx].channel = hit.channel;
      topoClients[cliIdx].rssi = hit.rssi;
      topoClients[cliIdx].lastSeenMs = now;
    } else if (cliIdx >= 0) {
      topoClients[cliIdx].rssi = hit.rssi;
      topoClients[cliIdx].lastSeenMs = now;
    }

    if (hit.ssid[0] != '\0') {
      int prbIdx = topoFindProbe(hit.clientMac, hit.ssid);
      if (prbIdx < 0) {
        if (topoProbeCount >= kMaxTopoProbes) return;
        prbIdx = topoProbeCount++;
        topoProbes[prbIdx] = TopoProbe();
        memcpy(topoProbes[prbIdx].clientMac, hit.clientMac, 6);
        strncpy(topoProbes[prbIdx].ssid, hit.ssid, sizeof(topoProbes[prbIdx].ssid) - 1);
        topoProbes[prbIdx].rssi = hit.rssi;
        topoProbes[prbIdx].count = 1;
        topoProbes[prbIdx].lastSeenMs = now;
        topoBroadcastLink(1, hit.rssi, hit.channel, hit.clientMac, nullptr, hit.ssid);
      } else {
        topoProbes[prbIdx].rssi = hit.rssi;
        ++topoProbes[prbIdx].count;
        topoProbes[prbIdx].lastSeenMs = now;
      }
    }
  }
}

// ESP-NOW callback: copy a bounded hit only. Remote link types 0/1 are
// tagged as 3/4 so the loop can merge them without rebroadcasting them.
void topologyOnLinkFrame(const uint8_t* data) {
  const uint32_t generation = topoHitQueue.generation();
  if (!generation) return;
  FleetTopologyLink r;
  memcpy(&r, data, sizeof(r));
  if (r.linkType > 1) return;
  TopoHit hit = {};
  hit.security = kTopoSecUnknown;
  hit.type = r.linkType + 3;
  hit.rssi = r.rssi;
  hit.channel = r.channel;
  memcpy(hit.clientMac, r.clientMac, 6);
  memcpy(hit.bssid, r.targetMac, 6);
  memcpy(hit.ssid, r.targetName, sizeof(hit.ssid) - 1);
  topoHitQueue.push(hit, generation);
}

void topologyMergeLink(const FleetTopologyLink& r) {
  const uint32_t now = millis();

  if (r.linkType == 0) {  // Client -> AP
    int apIdx = topoFindAp(r.targetMac);
    if (apIdx < 0 && topoApCount < kMaxTopoAps) {
      apIdx = topoApCount++;
      topoAps[apIdx] = TopoAp();
      memcpy(topoAps[apIdx].bssid, r.targetMac, 6);
      topoAps[apIdx].channel = r.channel;
      topoAps[apIdx].rssi = r.rssi;
      topoAps[apIdx].lastSeenMs = now;
    }
    int cliIdx = topoFindClient(r.clientMac);
    if (cliIdx < 0 && topoClientCount < kMaxTopoClients) {
      cliIdx = topoClientCount++;
      topoClients[cliIdx] = TopoClient();
      memcpy(topoClients[cliIdx].mac, r.clientMac, 6);
      memcpy(topoClients[cliIdx].bssid, r.targetMac, 6);
      topoClients[cliIdx].hasBssid = true;
      topoClients[cliIdx].channel = r.channel;
      topoClients[cliIdx].rssi = r.rssi;
      topoClients[cliIdx].packetCount = 1;
      topoClients[cliIdx].lastSeenMs = now;
      if (apIdx >= 0) ++topoAps[apIdx].clientCount;
    } else if (cliIdx >= 0) {
      memcpy(topoClients[cliIdx].bssid, r.targetMac, 6);
      topoClients[cliIdx].hasBssid = true;
      topoClients[cliIdx].rssi = r.rssi;
      topoClients[cliIdx].lastSeenMs = now;
    }
  } else if (r.linkType == 1 && r.targetName[0] != '\0') {  // Client -> Probe
    int prbIdx = topoFindProbe(r.clientMac, r.targetName);
    if (prbIdx < 0 && topoProbeCount < kMaxTopoProbes) {
      prbIdx = topoProbeCount++;
      topoProbes[prbIdx] = TopoProbe();
      memcpy(topoProbes[prbIdx].clientMac, r.clientMac, 6);
      strncpy(topoProbes[prbIdx].ssid, r.targetName, sizeof(topoProbes[prbIdx].ssid) - 1);
      topoProbes[prbIdx].rssi = r.rssi;
      topoProbes[prbIdx].count = 1;
      topoProbes[prbIdx].lastSeenMs = now;
    } else if (prbIdx >= 0) {
      topoProbes[prbIdx].rssi = r.rssi;
      ++topoProbes[prbIdx].count;
      topoProbes[prbIdx].lastSeenMs = now;
    }
  }
}

void topologyDrainHits() {
  TopoHit hit;
  while (topoHitQueue.pop(hit)) {
    if (hit.type < 3) {
      topoMergeHit(hit);
    } else {
      FleetTopologyLink r;
      r.linkType = hit.type - 3;
      r.rssi = hit.rssi;
      r.channel = hit.channel;
      memcpy(r.clientMac, hit.clientMac, 6);
      memcpy(r.targetMac, hit.bssid, 6);
      memcpy(r.targetName, hit.ssid, sizeof(r.targetName));
      topologyMergeLink(r);
    }
  }
}

bool exportTopologyToSd() {
  if (!topoAps) return lastTopologyCsvOk;
  if (!ensureSdCard()) return false;
  const String temporaryPath = String(kTopologyCsvPath) + ".tmp";
  SD.remove(temporaryPath.c_str());
  File file = SD.open(temporaryPath.c_str(), FILE_WRITE);
  if (!file) {
    sdReady = false;
    return false;
  }
  file.println("uptime_ms,type,client_mac,target_mac,target_name,channel,rssi,packets,latitude,longitude,altitude_m");
  const uint32_t uptime = millis();
  const String location = gpsCsvFields();

  // Write AP nodes
  for (int i = 0; i < topoApCount; ++i) {
    file.printf("%lu,AP,,%s,%s,%u,%d,%u%s\n",
                static_cast<unsigned long>(uptime),
                macToString(topoAps[i].bssid).c_str(),
                csvField(topoAps[i].ssid[0] ? topoAps[i].ssid : "<hidden>").c_str(),
                topoAps[i].channel,
                topoAps[i].rssi,
                topoAps[i].clientCount,
                location.c_str());
  }

  // Write Client nodes
  for (int i = 0; i < topoClientCount; ++i) {
    file.printf("%lu,CLIENT,%s,%s,,%u,%d,%u%s\n",
                static_cast<unsigned long>(uptime),
                macToString(topoClients[i].mac).c_str(),
                topoClients[i].hasBssid ? macToString(topoClients[i].bssid).c_str() : "",
                topoClients[i].channel,
                topoClients[i].rssi,
                topoClients[i].packetCount,
                location.c_str());
  }

  // Write Probe nodes
  for (int i = 0; i < topoProbeCount; ++i) {
    file.printf("%lu,PROBE,%s,,%s,,%d,%u%s\n",
                static_cast<unsigned long>(uptime),
                macToString(topoProbes[i].clientMac).c_str(),
                csvField(topoProbes[i].ssid).c_str(),
                topoProbes[i].rssi,
                topoProbes[i].count,
                location.c_str());
  }

  file.flush();
  const bool ok = (file.getWriteError() == 0);
  file.close();
  if (!ok) {
    SD.remove(temporaryPath.c_str());
    return false;
  }
  SD.remove(kTopologyCsvPath);
  if (!SD.rename(temporaryPath.c_str(), kTopologyCsvPath)) return false;
  Serial.printf("[topo] exported topology to %s\n", kTopologyCsvPath);
  return true;
}

void topologyEmitTelemetry() {
  // Emit stream rows for active entities
  for (int i = 0; i < topoApCount; ++i) {
    char rowBuf[128];
    int len = snprintf(rowBuf, sizeof(rowBuf), "$TOPO,AP,%s,%s,%u,%d,%d",
                       macToString(topoAps[i].bssid).c_str(),
                       topoAps[i].ssid[0] ? topoAps[i].ssid : "<hidden>",
                       topoAps[i].channel,
                       topoAps[i].rssi,
                       topoAps[i].isOpen ? 1 : 0);
    if (len > 0) {
      Serial.println(rowBuf);
#ifdef AWOK_HEADLESS
      if (g_bridgePhoneConnected) {
        bridgeNotifyResult(kSourceTopo, reinterpret_cast<const uint8_t*>(rowBuf), len);
      }
#endif
    }
  }

  for (int i = 0; i < topoClientCount; ++i) {
    if (!topoClients[i].hasBssid) continue;
    char rowBuf[128];
    int len = snprintf(rowBuf, sizeof(rowBuf), "$TOPO,CLI,%s,%s,%d,%u",
                       macToString(topoClients[i].mac).c_str(),
                       macToString(topoClients[i].bssid).c_str(),
                       topoClients[i].rssi,
                       topoClients[i].packetCount);
    if (len > 0) {
      Serial.println(rowBuf);
#ifdef AWOK_HEADLESS
      if (g_bridgePhoneConnected) {
        bridgeNotifyResult(kSourceTopo, reinterpret_cast<const uint8_t*>(rowBuf), len);
      }
#endif
    }
  }

  for (int i = 0; i < topoProbeCount; ++i) {
    char rowBuf[128];
    int len = snprintf(rowBuf, sizeof(rowBuf), "$TOPO,PRB,%s,%s,%d,%u",
                       macToString(topoProbes[i].clientMac).c_str(),
                       topoProbes[i].ssid,
                       topoProbes[i].rssi,
                       topoProbes[i].count);
    if (len > 0) {
      Serial.println(rowBuf);
#ifdef AWOK_HEADLESS
      if (g_bridgePhoneConnected) {
        bridgeNotifyResult(kSourceTopo, reinterpret_cast<const uint8_t*>(rowBuf), len);
      }
#endif
    }
  }
}

void drawTopologyMap() {
  currentView = View::kTopologyMap;
  display.fillScreen(kBackground);

  String headerSubtitle = String(topoApCount) + " AP | " + String(topoClientCount) + " Cli";
  if (topoProbeCount > 0) headerSubtitle += " | " + String(topoProbeCount) + " Prb";
  drawHeader("TOPOLOGY MAP", headerSubtitle);

  display.setTextSize(1);

  extern bool topoGraphMode;
  if (topoGraphMode) {
    drawTopologyGraphBody();
    drawThreeButtonFooter("Back", "List", lastTopologyCsvOk ? "Saved" : "Save");
    return;
  }

#ifdef AWOK_MINI_DISPLAY
  int y = 30;
  int linesRendered = 0;
  constexpr int kMiniMaxLines = 6;

  for (int a = 0; a < topoApCount && linesRendered < kMiniMaxLines; ++a) {
    display.setTextColor(topoAps[a].isOpen ? kWarn : kAccent, kBackground);
    display.setCursor(scaleX(4), scaleY(y));
    display.printf("[%d] %s (%d)", topoAps[a].channel,
                   clipped(topoAps[a].ssid[0] ? topoAps[a].ssid : macToString(topoAps[a].bssid), 11).c_str(),
                   topoAps[a].clientCount);
    y += 12;
    ++linesRendered;

    for (int c = 0; c < topoClientCount && linesRendered < kMiniMaxLines; ++c) {
      if (!topoClients[c].hasBssid || memcmp(topoClients[c].bssid, topoAps[a].bssid, 6) != 0) continue;
      display.setTextColor(kForeground, kBackground);
      display.setCursor(scaleX(12), scaleY(y));
      display.printf("> %02X:%02X %ddB", topoClients[c].mac[4], topoClients[c].mac[5], topoClients[c].rssi);
      y += 12;
      ++linesRendered;
    }
  }

  if (topoApCount == 0 && topoClientCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(scaleX(10), scaleY(60));
    display.print("Sniffing links...");
  }
#else
  int y = 48;
  int linesRendered = 0;
  constexpr int kTouchMaxLines = 11;

  for (int a = 0; a < topoApCount && linesRendered < kTouchMaxLines; ++a) {
    display.setTextColor(topoAps[a].isOpen ? kWarn : kAccent, kBackground);
    display.setCursor(scaleX(6), scaleY(y));
    display.printf("[Ch %2d] %s", topoAps[a].channel,
                   clipped(topoAps[a].ssid[0] ? topoAps[a].ssid : "<hidden>", 20).c_str());
    if (topoAps[a].isOpen) {
      display.setTextColor(kWarn, kBackground);
      display.print(" [OPEN]");
    }
    display.setTextColor(kMuted, kBackground);
    display.printf(" (%d cli, %d dBm)", topoAps[a].clientCount, topoAps[a].rssi);
    y += 18;
    ++linesRendered;

    for (int c = 0; c < topoClientCount && linesRendered < kTouchMaxLines; ++c) {
      if (!topoClients[c].hasBssid || memcmp(topoClients[c].bssid, topoAps[a].bssid, 6) != 0) continue;
      display.setTextColor(kForeground, kBackground);
      display.setCursor(scaleX(18), scaleY(y));
      display.printf("-> %s  %d dBm  (%d pkts)",
                     macToString(topoClients[c].mac).c_str(),
                     topoClients[c].rssi,
                     topoClients[c].packetCount);
      y += 18;
      ++linesRendered;
    }
  }

  // If room left, show probe leaks
  for (int p = 0; p < topoProbeCount && linesRendered < kTouchMaxLines; ++p) {
    display.setTextColor(kAccent, kBackground);
    display.setCursor(scaleX(6), scaleY(y));
    display.printf("[PRB] %02X:%02X -> \"%s\" (%d dBm)",
                   topoProbes[p].clientMac[4], topoProbes[p].clientMac[5],
                   clipped(topoProbes[p].ssid, 14).c_str(),
                   topoProbes[p].rssi);
    y += 18;
    ++linesRendered;
  }

  if (topoApCount == 0 && topoClientCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(scaleX(20), scaleY(140));
    display.print("Sniffing 802.11 client-AP associations & probes...");
  }
#endif

  drawThreeButtonFooter("Back", "Graph", lastTopologyCsvOk ? "Saved" : "Save");
}

void startTopologyMap() {
  stopActiveTools();
  if (!topoAps.allocate() || !topoClients.allocate() ||
      !topoProbes.allocate() || !topoHitQueue.begin()) {
    topoHitQueue.release();
    topoProbes.release();
    topoClients.release();
    topoAps.release();
    showToolMemoryError("Topology Map");
    return;
  }
  topoApCount = 0;
  topoClientCount = 0;
  topoProbeCount = 0;
  topoHopIndex = 0;
  topoTotalFrames = 0;
  lastTopoHopMs = millis();
  lastTopoDrawMs = 0;
  lastTopoTelemMs = 0;
  topoStartMs = millis();
  lastTopologyCsvOk = false;
  signalMonitorActive = false;
  topoGraphReset();

  // Pre-seed known APs from wifiEntries if previously scanned
  if (wifiCount > 0) {
    for (int i = 0; i < wifiCount && topoApCount < kMaxTopoAps; ++i) {
      uint8_t mac[6];
      if (parseBssid(wifiEntries[i].bssid, mac)) {
        int idx = topoApCount++;
        topoAps[idx] = TopoAp();
        memcpy(topoAps[idx].bssid, mac, 6);
        strncpy(topoAps[idx].ssid, wifiEntries[i].ssid.c_str(), sizeof(topoAps[idx].ssid) - 1);
        topoAps[idx].channel = wifiEntries[i].channel;
        topoAps[idx].rssi = wifiEntries[i].rssi;
        topoAps[idx].isOpen = (wifiEntries[i].auth == WIFI_AUTH_OPEN);
        topoAps[idx].security = topoSecurityFromAuth((uint8_t)wifiEntries[i].auth);
        topoAps[idx].lastSeenMs = millis();
      }
    }
  }

  WiFi.disconnect(true, false);
  if (!WiFi.mode(WIFI_MODE_STA)) {
    topoHitQueue.release();
    topoProbes.release();
    topoClients.release();
    topoAps.release();
    topoApCount = topoClientCount = topoProbeCount = 0;
    showRadioError("Topology Wi-Fi init failed");
    return;
  }
  esp_wifi_set_promiscuous(false);

  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&topoPromiscuousCallback);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(kDeauthHopChannels[0], WIFI_SECOND_CHAN_NONE);

  topologyActive = true;
  Serial.println("[topo] swarm mesh topology map started");
  drawTopologyMap();
}

void stopTopologyMap() {
  if (!topologyActive) return;
  topologyActive = false;
  topoHitQueue.pause();
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true, false);
  topologyDrainHits();
  lastTopologyCsvOk = exportTopologyToSd();
  Serial.printf("[topo] stopped; %d APs, %d clients, %d probes, %lu frames\n",
                topoApCount, topoClientCount, topoProbeCount,
                static_cast<unsigned long>(topoTotalFrames));
  topoHitQueue.release();
  topoProbes.release();
  topoClients.release();
  topoAps.release();
  topoApCount = topoClientCount = topoProbeCount = 0;
  logMemory("Topology buffers released");
}

void updateTopologyMap() {
  if (!topologyActive) return;

  topologyDrainHits();

  const uint32_t now = millis();

  // Channel hopping
  if (now - lastTopoHopMs >= kTopoHopIntervalMs) {
    lastTopoHopMs = now;
    topoHopIndex = (topoHopIndex + 1) % kDeauthHopChannelCount;
    esp_wifi_set_channel(kDeauthHopChannels[topoHopIndex], WIFI_SECOND_CHAN_NONE);
  }

  // Telemetry stream
  if (now - lastTopoTelemMs >= kTopoTelemIntervalMs) {
    lastTopoTelemMs = now;
    topologyEmitTelemetry();
  }

  // Screen redraw
  if (currentView == View::kTopologyMap && now - lastTopoDrawMs >= kTopoRedrawMs) {
    lastTopoDrawMs = now;
    drawTopologyMap();
  }
}
