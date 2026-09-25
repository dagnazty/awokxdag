// AWOKxDAG — Targeted Deauth & Disassociation Forensic Analyzer
// (compiled as part of the sketch; see awok_common.h)
//
// Passive sniffer and attribution engine for 802.11 deauthentication and
// disassociation attacks. Differentiates shotgun broadcast floods from
// targeted victim station attacks, detects transmitter sequence number jumps,
// decodes 802.11 reason codes, logs forensic audit trails to SD, and streams
// $DEAUTH telemetry over Serial and Web Bluetooth.

ToolBuffer<DeauthForensicEvent, kMaxDeauthForensicEvents> deauthEvents;
int deauthEventCount = 0;

uint32_t deauthTotalFrames = 0;
uint32_t deauthTargetedCount = 0;
uint32_t deauthBroadcastCount = 0;
uint32_t deauthDisassocCount = 0;
uint32_t deauthAnomalyCount = 0;
uint32_t deauth24Count = 0;
uint32_t deauth5Count = 0;
uint32_t lastDeauthAttackMs = 0;

static constexpr int kDeauthQueueSlots = AwokPins::kDualBand ? 32 : 16;
static ToolQueue<DeauthHit, kDeauthQueueSlots> deauthQueue;

struct DeauthApSeq {
  uint8_t bssid[6] = {0};
  uint16_t lastSeq = 0;
  uint32_t lastSeenMs = 0;
};
static ToolBuffer<DeauthApSeq, 32> deauthApSeqs;
static int deauthApSeqCount = 0;

int deauthForensicsHopIndex = 0;
uint32_t lastDeauthForensicsHopMs = 0;
uint32_t lastDeauthForensicsDrawMs = 0;
uint32_t deauthForensicsStartMs = 0;

#ifdef AWOK_MINI_DISPLAY
constexpr int kDeauthForensicsRowsPerPage = 4;
#else
constexpr int kDeauthForensicsRowsPerPage = 9;
#endif

int deauthForensicsPageCount() {
  return max(1, (deauthEventCount + kDeauthForensicsRowsPerPage - 1) / kDeauthForensicsRowsPerPage);
}

void clearDeauthForensics() {
  deauthEventCount = 0;
  deauthTotalFrames = 0;
  deauthTargetedCount = 0;
  deauthBroadcastCount = 0;
  deauthDisassocCount = 0;
  deauthAnomalyCount = 0;
  deauth24Count = 0;
  deauth5Count = 0;
  deauthForensicsPage = 0;
}

const char* deauthReasonDescription(uint16_t reason) {
  switch (reason) {
    case 1: return "Unspecified";
    case 2: return "Prev Auth Invalid";
    case 3: return "Station Leaving";
    case 4: return "Inactivity";
    case 6: return "Class 2 NonAuth (Attack)";
    case 7: return "Class 3 NonAssoc (Attack)";
    case 8: return "Station Disassoc";
    case 15: return "4-Way Timeout";
    default: return "Reason Code";
  }
}

void IRAM_ATTR deauthForensicsCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  const uint32_t generation = deauthQueue.generation();
  if (!generation || type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* packet =
      static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* payload = packet->payload;
  const int length = packet->rx_ctrl.sig_len;
  if (length < 26) return;

  const uint8_t frameControl = payload[0];
  const uint8_t subtype = (frameControl >> 4) & 0x0F;

  // 12 = Deauth (0x0C), 10 = Disassoc (0x0A)
  if (subtype != 12 && subtype != 10) return;

  DeauthHit hit = {};
  hit.channel = packet->rx_ctrl.channel;
  hit.rssi = packet->rx_ctrl.rssi;

  // 802.11 MAC Header:
  // Addr1 (bytes 4-9): Destination / Victim
  // Addr2 (bytes 10-15): Source / Transmitter
  // Addr3 (bytes 16-21): BSSID / AP
  memcpy(hit.targetMac, payload + 4, 6);
  memcpy(hit.sourceMac, payload + 10, 6);
  memcpy(hit.bssid, payload + 16, 6);

  // Sequence control (bytes 22-23)
  hit.seqNum = (payload[22] | (static_cast<uint16_t>(payload[23]) << 8)) >> 4;

  // Reason code (bytes 24-25, little endian)
  hit.reasonCode = payload[24] | (static_cast<uint16_t>(payload[25]) << 8);

  // Classify attack pattern
  const bool isBroadcast = (hit.targetMac[0] == 0xFF && hit.targetMac[1] == 0xFF &&
                            hit.targetMac[2] == 0xFF && hit.targetMac[3] == 0xFF &&
                            hit.targetMac[4] == 0xFF && hit.targetMac[5] == 0xFF);

  if (subtype == 10) {
    hit.attackType = kDeauthTypeDisassoc;
  } else if (isBroadcast) {
    hit.attackType = kDeauthTypeBroadcast;
  } else {
    hit.attackType = kDeauthTypeTargeted;
  }

  deauthQueue.push(hit, generation);
}

static void deauthStreamTelemetry(const DeauthForensicEvent& ev) {
  char tgtStr[18], srcStr[18], bssidStr[18];
  snprintf(tgtStr, sizeof(tgtStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           ev.targetMac[0], ev.targetMac[1], ev.targetMac[2], ev.targetMac[3], ev.targetMac[4], ev.targetMac[5]);
  snprintf(srcStr, sizeof(srcStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           ev.sourceMac[0], ev.sourceMac[1], ev.sourceMac[2], ev.sourceMac[3], ev.sourceMac[4], ev.sourceMac[5]);
  snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           ev.bssid[0], ev.bssid[1], ev.bssid[2], ev.bssid[3], ev.bssid[4], ev.bssid[5]);

  char telem[160];
  snprintf(telem, sizeof(telem), "$DEAUTH,%u,%s,%s,%s,%u,%d,%d,%u",
           ev.attackType,
           tgtStr,
           srcStr,
           bssidStr,
           ev.reasonCode,
           ev.seqJump,
           ev.rssi,
           ev.channel);
  Serial.println(telem);
#ifdef AWOK_HEADLESS
  bridgeNotifyResult(kSourceDeauthForensics, reinterpret_cast<const uint8_t*>(telem), strlen(telem));
#endif
}

void deauthProcessHit(const DeauthHit& incoming) {
  // Sequence history belongs to loopTask, just like the result table.
  // Never read/write this allocation from the radio callback.
  DeauthHit hit = incoming;
  // Check sequence jump against AP baseline
  hit.seqJump = 0;
  for (int i = 0; i < deauthApSeqCount; ++i) {
    if (memcmp(deauthApSeqs[i].bssid, hit.bssid, 6) == 0) {
      hit.seqJump = static_cast<int16_t>(hit.seqNum) - static_cast<int16_t>(deauthApSeqs[i].lastSeq);
      if (abs(hit.seqJump) > 40) {
        hit.attackType = kDeauthTypeAnomaly;
      }
      deauthApSeqs[i].lastSeq = hit.seqNum;
      deauthApSeqs[i].lastSeenMs = millis();
      break;
    }
  }

  deauthTotalFrames++;
  lastDeauthAttackMs = millis();

  if (hit.attackType == kDeauthTypeBroadcast) deauthBroadcastCount++;
  else if (hit.attackType == kDeauthTypeTargeted) deauthTargetedCount++;
  else if (hit.attackType == kDeauthTypeDisassoc) deauthDisassocCount++;
  else if (hit.attackType == kDeauthTypeAnomaly) deauthAnomalyCount++;

  if (hit.channel <= 14) deauth24Count++;
  else deauth5Count++;

  // Update AP sequence tracker
  bool foundAp = false;
  for (int i = 0; i < deauthApSeqCount; ++i) {
    if (memcmp(deauthApSeqs[i].bssid, hit.bssid, 6) == 0) {
      foundAp = true;
      break;
    }
  }
  if (!foundAp && deauthApSeqCount < 32) {
    memcpy(deauthApSeqs[deauthApSeqCount].bssid, hit.bssid, 6);
    deauthApSeqs[deauthApSeqCount].lastSeq = hit.seqNum;
    deauthApSeqs[deauthApSeqCount].lastSeenMs = millis();
    deauthApSeqCount++;
  }

  // Prepend to recent events list
  if (deauthEventCount < kMaxDeauthForensicEvents) {
    deauthEventCount++;
  }
  for (int i = deauthEventCount - 1; i > 0; --i) {
    deauthEvents[i] = deauthEvents[i - 1];
  }

  DeauthForensicEvent& e = deauthEvents[0];
  e.timestampMs = millis();
  e.attackType = hit.attackType;
  memcpy(e.targetMac, hit.targetMac, 6);
  memcpy(e.sourceMac, hit.sourceMac, 6);
  memcpy(e.bssid, hit.bssid, 6);
  e.reasonCode = hit.reasonCode;
  e.seqNum = hit.seqNum;
  e.seqJump = hit.seqJump;
  e.rssi = hit.rssi;
  e.channel = hit.channel;

  deauthStreamTelemetry(e);
}

bool exportDeauthForensicsToSd() {
  if (!deauthEvents) return lastDeauthForensicsCsvOk;
  if (!ensureSdCard()) return false;
  const String temporaryPath = String(kDeauthForensicsCsvPath) + ".tmp";
  SD.remove(temporaryPath.c_str());
  File file = SD.open(temporaryPath.c_str(), FILE_WRITE);
  if (!file) {
    sdReady = false;
    return false;
  }
  file.println("uptime_ms,attack_type,target_mac,source_mac,bssid,reason_code,reason_desc,seq_num,seq_jump,rssi,channel,band,latitude,longitude,altitude_m");
  const String location = gpsCsvFields();

  for (int i = 0; i < deauthEventCount; ++i) {
    const DeauthForensicEvent& ev = deauthEvents[i];
    char tgtStr[18], srcStr[18], bssidStr[18];
    snprintf(tgtStr, sizeof(tgtStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             ev.targetMac[0], ev.targetMac[1], ev.targetMac[2], ev.targetMac[3], ev.targetMac[4], ev.targetMac[5]);
    snprintf(srcStr, sizeof(srcStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             ev.sourceMac[0], ev.sourceMac[1], ev.sourceMac[2], ev.sourceMac[3], ev.sourceMac[4], ev.sourceMac[5]);
    snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             ev.bssid[0], ev.bssid[1], ev.bssid[2], ev.bssid[3], ev.bssid[4], ev.bssid[5]);

    const char* typeStr = (ev.attackType == kDeauthTypeTargeted) ? "Targeted Unicast"
                        : (ev.attackType == kDeauthTypeBroadcast) ? "Broadcast Flood"
                        : (ev.attackType == kDeauthTypeDisassoc) ? "Disassociation"
                        : (ev.attackType == kDeauthTypeAnomaly) ? "Sequence Anomaly" : "Unknown";
    const char* band = (ev.channel <= 14) ? "2.4GHz" : "5GHz";

    file.printf("%lu,%s,%s,%s,%s,%u,\"%s\",%u,%d,%d,%u,%s%s\n",
                static_cast<unsigned long>(ev.timestampMs),
                typeStr,
                tgtStr,
                srcStr,
                bssidStr,
                ev.reasonCode,
                deauthReasonDescription(ev.reasonCode),
                ev.seqNum,
                ev.seqJump,
                ev.rssi,
                ev.channel,
                band,
                location.c_str());
  }
  file.flush();
  const bool ok = file.getWriteError() == 0;
  file.close();
  if (!ok) {
    SD.remove(temporaryPath.c_str());
    return false;
  }
  SD.remove(kDeauthForensicsCsvPath);
  if (!SD.rename(temporaryPath.c_str(), kDeauthForensicsCsvPath)) return false;
  Serial.printf("[deauth-forensics] wrote %d events to %s\n", deauthEventCount, kDeauthForensicsCsvPath);
  return true;
}

void drawDeauthForensics() {
  currentView = View::kDeauthForensics;
  display.fillScreen(kBackground);

  const uint32_t now = millis();
  const bool attackActive = (lastDeauthAttackMs > 0 && (now - lastDeauthAttackMs < 3000));

  const int pages = deauthForensicsPageCount();
  if (deauthForensicsPage >= pages) deauthForensicsPage = pages - 1;
  if (deauthForensicsPage < 0) deauthForensicsPage = 0;

  char sub[80];
  if (pages > 1) {
    snprintf(sub, sizeof(sub), "Tgt:%lu Bc:%lu | pg %d/%d",
             static_cast<unsigned long>(deauthTargetedCount),
             static_cast<unsigned long>(deauthBroadcastCount),
             deauthForensicsPage + 1, pages);
  } else if (AwokPins::kDualBand) {
    snprintf(sub, sizeof(sub), "2.4G:%lu 5G:%lu | Tgt:%lu Bc:%lu",
             static_cast<unsigned long>(deauth24Count),
             static_cast<unsigned long>(deauth5Count),
             static_cast<unsigned long>(deauthTargetedCount),
             static_cast<unsigned long>(deauthBroadcastCount));
  } else {
    snprintf(sub, sizeof(sub), "Tgt:%lu Bcast:%lu Anom:%lu",
             static_cast<unsigned long>(deauthTargetedCount),
             static_cast<unsigned long>(deauthBroadcastCount),
             static_cast<unsigned long>(deauthAnomalyCount));
  }
  drawMonitorHeader("DEAUTH FORENSICS", deauthForensicsActive, sub);

#ifdef AWOK_MINI_DISPLAY
  display.setTextSize(1);
  if (attackActive) {
    display.fillRect(scaleX(0), scaleY(42), scaleX(128), scaleY(14), ILI9341_RED);
    display.setTextColor(ILI9341_WHITE, ILI9341_RED);
    display.setCursor(scaleX(4), scaleY(45));
    display.print("! ACTIVE ATTACK !");
  } else {
    display.setTextColor(ILI9341_GREEN, kBackground);
    display.setCursor(scaleX(4), scaleY(45));
    display.print(AwokPins::kDualBand ? "Dual-Band 2.4/5G Active" : "Monitoring Airwaves");
  }

  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(scaleX(2), scaleY(60));
  if (pages > 1) {
    display.printf("Tgt:%-2lu Bc:%-2lu pg %d/%d",
                   static_cast<unsigned long>(deauthTargetedCount),
                   static_cast<unsigned long>(deauthBroadcastCount),
                   deauthForensicsPage + 1, pages);
  } else if (AwokPins::kDualBand) {
    display.printf("Tot:%-2lu 2.4G:%-2lu 5G:%-2lu",
                   static_cast<unsigned long>(deauthTotalFrames),
                   static_cast<unsigned long>(deauth24Count),
                   static_cast<unsigned long>(deauth5Count));
  } else {
    display.printf("Total: %lu Tgt: %lu",
                   static_cast<unsigned long>(deauthTotalFrames),
                   static_cast<unsigned long>(deauthTargetedCount));
  }

  const int startIdx = deauthForensicsPage * kDeauthForensicsRowsPerPage;
  const int visible = min(kDeauthForensicsRowsPerPage, deauthEventCount - startIdx);
  for (int i = 0; i < visible; ++i) {
    const auto& ev = deauthEvents[startIdx + i];
    const int y = 72 + i * 11;
    display.setTextColor((ev.attackType == kDeauthTypeTargeted) ? ILI9341_RED : ILI9341_YELLOW, kBackground);
    display.setCursor(scaleX(2), scaleY(y));
    display.printf("%02X:%02X c%-3u R%-2u %3d", ev.targetMac[4], ev.targetMac[5], ev.channel, ev.reasonCode, ev.rssi);
  }
  if (pages > 1) {
    drawFourButtonFooter(deauthForensicsActive ? "Stop" : "Back", "Prev", "Next", lastDeauthForensicsCsvOk ? "Saved" : "Save");
  } else {
    drawThreeButtonFooter(deauthForensicsActive ? "Stop" : "Back", "Clear", lastDeauthForensicsCsvOk ? "Saved" : "Save");
  }
  return;
#endif

  // Touch screen (240x320)
  if (attackActive) {
    display.fillRect(scaleX(8), scaleY(42), scaleX(224), scaleY(20), ILI9341_RED);
    display.setTextColor(ILI9341_WHITE, ILI9341_RED);
    display.setTextSize(1);
    display.setCursor(scaleX(14), scaleY(48));
    display.print("! ATTACK DETECTED: DEAUTHENTICATION !");
  } else {
    display.drawRoundRect(scaleX(8), scaleY(42), scaleX(224), scaleY(20), 4, 0x3186);
    display.setTextColor(ILI9341_GREEN, kBackground);
    display.setTextSize(1);
    display.setCursor(scaleX(14), scaleY(48));
    display.print(AwokPins::kDualBand ? "DUAL-BAND 2.4 & 5 GHz MONITOR: CLEAR" : "PASSIVE DEFENSIVE MONITOR: CLEAR");
  }

  display.setTextColor(0x7BEF, kBackground);
  display.setCursor(scaleX(8), scaleY(68));
  display.print("TYPE    VICTIM      BSSID    CH  RSN  RSSI");
  display.drawFastHLine(scaleX(8), scaleY(78), scaleX(224), 0x3186);

  constexpr int kRowY = 82;
  constexpr int kRowH = 20;

  if (deauthEventCount == 0) {
    display.setTextColor(0x7BEF, kBackground);
    display.setCursor(scaleX(20), scaleY(136));
    display.print("No deauth frames detected.");
    display.setCursor(scaleX(20), scaleY(150));
    if (AwokPins::kDualBand) {
      display.printf("Monitoring 2.4 & 5 GHz (Ch %u)...", kDeauthHopChannels[deauthForensicsHopIndex]);
    } else {
      display.printf("Monitoring Ch 1-13 (Ch %u)...", kDeauthHopChannels[deauthForensicsHopIndex]);
    }
    display.setCursor(scaleX(20), scaleY(164));
    display.print("Passive dual-radio attribution");
  } else {
    const int startIdx = deauthForensicsPage * kDeauthForensicsRowsPerPage;
    for (int r = 0; r < kDeauthForensicsRowsPerPage; ++r) {
      const int idx = startIdx + r;
      if (idx >= deauthEventCount) break;
      const auto& ev = deauthEvents[idx];
      const int y = kRowY + r * kRowH;

      // Attack Type Badge
      uint16_t tCol = ILI9341_YELLOW;
      const char* tStr = "BCAST";
      if (ev.attackType == kDeauthTypeTargeted) { tCol = ILI9341_RED; tStr = "TARGET"; }
      else if (ev.attackType == kDeauthTypeDisassoc) { tCol = ILI9341_CYAN; tStr = "DISAS"; }
      else if (ev.attackType == kDeauthTypeAnomaly) { tCol = ILI9341_MAGENTA; tStr = "ANOM "; }

      display.setTextColor(tCol, kBackground);
      display.setCursor(scaleX(8), scaleY(y + 3));
      display.print(tStr);

      // Victim MAC (last 3 bytes)
      display.setTextColor(ILI9341_WHITE, kBackground);
      display.setCursor(scaleX(50), scaleY(y + 3));
      if (ev.targetMac[0] == 0xFF) {
        display.print("FF:FF:FF");
      } else {
        display.printf("%02X:%02X:%02X", ev.targetMac[3], ev.targetMac[4], ev.targetMac[5]);
      }

      // AP BSSID (last 3 bytes)
      display.setTextColor(0x8410, kBackground);
      display.setCursor(scaleX(104), scaleY(y + 3));
      display.printf("%02X:%02X:%02X", ev.bssid[3], ev.bssid[4], ev.bssid[5]);

      // Channel (Cyan for 5 GHz, White for 2.4 GHz)
      display.setTextColor(ev.channel > 14 ? ILI9341_CYAN : ILI9341_WHITE, kBackground);
      display.setCursor(scaleX(158), scaleY(y + 3));
      display.printf("%-3u", ev.channel);

      // Reason Code
      display.setTextColor(ILI9341_YELLOW, kBackground);
      display.setCursor(scaleX(182), scaleY(y + 3));
      display.printf("R%-2u", ev.reasonCode);

      // RSSI
      display.setTextColor(ev.rssi >= -65 ? ILI9341_GREEN : (ev.rssi >= -80 ? ILI9341_YELLOW : ILI9341_RED), kBackground);
      display.setCursor(scaleX(210), scaleY(y + 3));
      display.printf("%3d", ev.rssi);
    }
  }

  if (pages > 1) {
    drawFourButtonFooter(deauthForensicsActive ? "Stop" : "Back", "Prev", "Next", lastDeauthForensicsCsvOk ? "Saved" : "Export");
  } else {
    drawThreeButtonFooter(deauthForensicsActive ? "Stop" : "Back", "Clear", lastDeauthForensicsCsvOk ? "Saved" : "Export");
  }
}

void startDeauthForensics() {
  stopActiveTools();
  if (!ensureWifiStation(true)) return;
  if (!deauthEvents.allocate() || !deauthApSeqs.allocate() || !deauthQueue.begin()) {
    deauthQueue.release();
    deauthApSeqs.release();
    deauthEvents.release();
    showToolMemoryError("Deauth Forensics");
    return;
  }
  deauthApSeqCount = 0;
  deauthEventCount = 0;
  deauthTotalFrames = 0;
  deauthTargetedCount = 0;
  deauthBroadcastCount = 0;
  deauthDisassocCount = 0;
  deauthAnomalyCount = 0;
  deauth24Count = 0;
  deauth5Count = 0;
  lastDeauthAttackMs = 0;
  deauthForensicsHopIndex = 0;
  lastDeauthForensicsHopMs = millis();
  lastDeauthForensicsDrawMs = 0;
  deauthForensicsStartMs = millis();
  lastDeauthForensicsCsvOk = false;
  deauthForensicsPage = 0;

  esp_wifi_set_promiscuous(false);
  esp_wifi_set_channel(kDeauthHopChannels[0], WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous_rx_cb(deauthForensicsCallback);
  esp_wifi_set_promiscuous(true);

  deauthForensicsActive = true;
  Serial.println("[deauth-forensics] started passive attack analyzer");
  drawDeauthForensics();
}

void stopDeauthForensics() {
  if (!deauthForensicsActive) return;
  deauthForensicsActive = false;
  deauthQueue.pause();
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  DeauthHit hit;
  while (deauthQueue.pop(hit)) deauthProcessHit(hit);
  lastDeauthForensicsCsvOk = exportDeauthForensicsToSd();
  deauthQueue.release();
  deauthApSeqs.release();
  deauthEvents.release();
  deauthEventCount = 0;
  deauthApSeqCount = 0;
  logMemory("Deauth Forensics buffers released");
  Serial.println("[deauth-forensics] stopped");
}

void updateDeauthForensics() {
  if (!deauthForensicsActive || currentView != View::kDeauthForensics) return;

  const uint32_t now = millis();

  // Drain queue
  DeauthHit hit;
  while (deauthQueue.pop(hit)) deauthProcessHit(hit);

  // Channel hopping across all 2.4 GHz and 5 GHz channels
  if (now - lastDeauthForensicsHopMs >= kDeauthForensicsHopMs) {
    lastDeauthForensicsHopMs = now;
    deauthForensicsHopIndex = (deauthForensicsHopIndex + 1) % kDeauthHopChannelCount;
    esp_wifi_set_channel(kDeauthHopChannels[deauthForensicsHopIndex], WIFI_SECOND_CHAN_NONE);
  }

  // UI redraw
  if (now - lastDeauthForensicsDrawMs >= kDeauthForensicsRedrawMs) {
    lastDeauthForensicsDrawMs = now;
    drawDeauthForensics();
  }
}
