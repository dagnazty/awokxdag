// AWOKxDAG — Link Mode: pair two units over ESP-NOW (compiled as part of the
// sketch; see awok_common.h and docs/link-mode.md).
//
// v1 ships the shared core (pairing with a display-and-confirm 4-digit code,
// coarse clock sync, a heartbeat/partner-lost alert) plus Split Wardrive, where
// two paired units alternate-deal the dual-band channel list so each covers half
// the spectrum. Wi-Fi-only in v1: ESP-NOW shares the Wi-Fi radio and bringing
// NimBLE up alongside it would tear Wi-Fi (and the ESP-NOW session) down on the
// Mini, so Split Wardrive logs Wi-Fi APs only. Each board uses its own GPS.
//
// The ESP-NOW recv callback runs in the Wi-Fi task; it only copies the frame
// into linkPacketQueue (single-producer / single-consumer ring). All parsing and
// SD/UI work happens in updateLink() on the main loop.

#include <atomic>

// The Wi-Fi callback acknowledges readiness while the main loop waits, without
// draining/re-entering the command queue or starting a second file operation.
static std::atomic<uint32_t> linkFileWaitingToken{0};

// Channel the split scanner is currently dwelling on (reported in TELEM). Local
// to this tab; every link.ino function is defined after it.
static uint8_t linkScanChannel = 0;
#ifdef AWOK_HEADLESS
static AxdWardriveStatusMsg bridgeWardriveStatus;
static portMUX_TYPE bridgeWardriveStatusMux = portMUX_INITIALIZER_UNLOCKED;
static bool bridgeWardriveStatusPending = false;

void bridgeServiceWardriveDashboard() {
  AxdWardriveStatusMsg msg;
  bool pending;
  portENTER_CRITICAL(&bridgeWardriveStatusMux);
  pending = bridgeWardriveStatusPending;
  if (pending) { msg = bridgeWardriveStatus; bridgeWardriveStatusPending = false; }
  portEXIT_CRITICAL(&bridgeWardriveStatusMux);
  if (pending) bridgeNotifyResult(kSourceWardriveStatus,
      reinterpret_cast<const uint8_t*>(msg.data), strnlen(msg.data, sizeof(msg.data)));
}
#endif

// ---- ESP-NOW plumbing ---------------------------------------------------

// Copy a LinkPacket-sized control frame into the ring for the main loop.
static void linkQueuePacket(const esp_now_recv_info_t* info, const uint8_t* data) {
  const int next = (linkPacketHead + 1) % kLinkPacketQueueSlots;
  if (next == linkPacketTail) return;  // queue full: drop
  LinkQueueItem& item = linkPacketQueue[linkPacketHead];
  memcpy(&item.pkt, data, sizeof(LinkPacket));
  item.rssi = (info && info->rx_ctrl) ? info->rx_ctrl->rssi : -127;
  if (info && info->src_addr) memcpy(item.pkt.srcMac, info->src_addr, 6);
  linkPacketHead = next;
}

void onLinkRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len < 6) return;
  uint32_t magic;
  memcpy(&magic, data, 4);
  if (magic != kLinkMagic) return;
  const uint8_t type = data[5];

  if (type == kLinkMsgFileChunkAck && len == static_cast<int>(sizeof(AxdFileChunkAck))) {
    AxdFileChunkAck ack;
    memcpy(&ack, data, sizeof(ack));
    if (ack.version == kLinkProtoVersion) linkReceiveFileAck(ack.token, ack.seq);
    return;
  }
#ifdef AWOK_HEADLESS
  if (type == kLinkMsgWardriveStatus && len == sizeof(AxdWardriveStatusMsg)) {
    AxdWardriveStatusMsg msg;
    memcpy(&msg, data, sizeof(msg));
    if (msg.version != kLinkProtoVersion) return;
    msg.data[sizeof(msg.data) - 1] = 0;
    portENTER_CRITICAL(&bridgeWardriveStatusMux);
    bridgeWardriveStatus = msg;
    bridgeWardriveStatusPending = true;
    portEXIT_CRITICAL(&bridgeWardriveStatusMux);
    return;
  }
  if (type == kLinkMsgFileChunk && len == static_cast<int>(sizeof(AxdFileChunkMsg))) {
    AxdFileChunkMsg chunk;
    memcpy(&chunk, data, sizeof(chunk));
    if (chunk.version == kLinkProtoVersion) bridgeQueueFileChunk(chunk);
    return;
  }
#endif
  // A reliable sender blocks the main loop while waiting. Abort must bypass
  // the command queue so it can stop that wait immediately.
  if (type == kLinkMsgCommand && len == static_cast<int>(sizeof(LinkPacket)) &&
      linkReliableFileActive()) {
    LinkPacket command;
    memcpy(&command, data, sizeof(command));
    if (command.version == kLinkProtoVersion &&
        static_cast<uint8_t>(command.reserved) == kAxdCmdFileGetVerified &&
        !linkReliableFileTokenMatches(command.sessionId)) linkAbortFileStream();
    if (command.version == kLinkProtoVersion &&
        static_cast<uint8_t>(command.reserved) == kAxdCmdFileAbort) {
      linkAbortFileStream();
      return;
    }
  }

  if (type == kLinkMsgFileReady && len == static_cast<int>(sizeof(LinkPacket))) {
    LinkPacket ready;
    memcpy(&ready, data, sizeof(ready));
    if (ready.version != kLinkProtoVersion || ready.sessionId == 0) return;
#ifdef AWOK_HEADLESS
    if (ready.flags == kLinkFileReadyRequest &&
        bridgeFileReceiverReady(ready.sessionId)) {
      ready.flags = kLinkFileReadyAck;
      memcpy(ready.srcMac, linkSelfMac, 6);
      esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&ready), sizeof(ready));
    }
#endif
    if (ready.flags == kLinkFileReadyAck) {
      uint32_t expected = ready.sessionId;
      linkFileWaitingToken.compare_exchange_strong(expected, 0);
    }
    return;
  }

  // Fleet roster is larger than a LinkPacket: hand it to updateLink via a
  // single-slot mailbox. Filter before writing the slot so a foreign fleet
  // cannot overwrite the pinned coordinator's next valid roster.
  if (type == kLinkMsgFleetRoster && len == static_cast<int>(sizeof(FleetRoster))) {
    FleetRoster candidate;
    memcpy(&candidate, data, sizeof(candidate));
    if (!fleetActive || fleetCoordinator) return;
    if (candidate.sessionId != fleetSessionId || candidate.memberCount == 0 ||
        candidate.memberCount > kFleetMaxNodes ||
        candidate.coordIndex >= candidate.memberCount)
      return;
    if (memcmp(candidate.members[candidate.coordIndex].mac,
               fleetCoordinatorMac, 6) != 0)
      return;
    memcpy(&fleetPendingRoster, &candidate, sizeof(candidate));
    fleetRosterPending = true;
    return;
  }
  // Fleet wardrive rows (workers -> coordinator) and their ACKs.
  if (type == kLinkMsgFleetWardriveRow &&
      len == static_cast<int>(sizeof(FleetWardriveRow))) {
    if (fleetCoordinator) fleetOnRowFrame(data);
    return;
  }
  if (type == kLinkMsgFleetAck && len == static_cast<int>(sizeof(FleetAck))) {
    fleetOnAckFrame(data);
    return;
  }
  if (type == kLinkMsgFleetHuntObservation &&
      len == static_cast<int>(sizeof(FleetHuntObservation))) {
    huntOnObservationFrame(data);
    return;
  }
  if (type == kLinkMsgFleetTopology &&
      len == static_cast<int>(sizeof(FleetTopologyLink))) {
    topologyOnLinkFrame(data);
#ifdef AWOK_HEADLESS
    FleetTopologyLink r;
    memcpy(&r, data, sizeof(r));
    char clientStr[20];
    snprintf(clientStr, sizeof(clientStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             r.clientMac[0], r.clientMac[1], r.clientMac[2], r.clientMac[3], r.clientMac[4], r.clientMac[5]);
    char rowBuf[128];
    int rlen = 0;
    if (r.linkType == 0) {
      char targetStr[20];
      snprintf(targetStr, sizeof(targetStr), "%02X:%02X:%02X:%02X:%02X:%02X",
               r.targetMac[0], r.targetMac[1], r.targetMac[2], r.targetMac[3], r.targetMac[4], r.targetMac[5]);
      rlen = snprintf(rowBuf, sizeof(rowBuf), "$TOPO,CLI,%s,%s,%d,1",
                      clientStr, targetStr, r.rssi);
    } else {
      rlen = snprintf(rowBuf, sizeof(rowBuf), "$TOPO,PRB,%s,%s,%d,1",
                      clientStr, r.targetName, r.rssi);
    }
    if (rlen > 0) {
      bridgeNotifyResult(kSourceTopo, reinterpret_cast<const uint8_t*>(rowBuf), rlen);
    }
#endif
    return;
  }

#ifdef AWOK_HEADLESS
  // Bridge phone-relay role: forward a screen chip's telem/results to the phone.
  if (type == kLinkMsgTelem && len == static_cast<int>(sizeof(LinkPacket))) {
    LinkPacket p;
    memcpy(&p, data, sizeof(p));
    // Match the bridge's own 24-byte status body so the web UI can maintain
    // independent, complete status for both command targets.
    uint8_t blob[24] = {0};
    memcpy(blob + 0, &p.networks, 4);
    memcpy(blob + 4, &p.bleCount, 4);
    blob[8] = p.channel;
    if (p.flags & kLinkTelemStatus) {
      blob[9] = (p.flags & kLinkTelemGpsFix) ? 1 : 0;
      blob[10] = static_cast<uint8_t>(p.reserved >> 8);
      memcpy(blob + 11, &p.sessionId, 4);    // latitude float bits
      memcpy(blob + 15, &p.masterMillis, 4); // longitude float bits
      blob[19] = (p.flags & kLinkTelemFleetActive) ? 1 : 0;
      blob[20] = static_cast<uint8_t>(p.reserved & 0xFF);
      memcpy(blob + 21, &p.code, 2);
      blob[23] = ((p.flags & kLinkTelemFleetCoordinator) ? 0x01 : 0) |
                 ((p.flags & kLinkTelemFleetListening) ? 0x02 : 0) |
                 ((p.flags & kLinkTelemFleetRunning) ? 0x04 : 0);
    }
    bridgeNotifyStatus(kSourceScreen, blob, sizeof(blob));
  } else if (type == kLinkMsgWifiResult &&
             len == static_cast<int>(sizeof(AxdWifiResult))) {
    AxdWifiResult r;
    memcpy(&r, data, sizeof(r));
    uint8_t blob[11 + 32];
    blob[0] = r.index; blob[1] = r.count;
    blob[2] = static_cast<uint8_t>(r.rssi); blob[3] = r.channel; blob[4] = r.auth;
    memcpy(blob + 5, r.bssid, 6);
    size_t sl = strnlen(r.ssid, 32);
    memcpy(blob + 11, r.ssid, sl);
    bridgeNotifyResult(kSourceScreen, blob, 11 + sl);
    return;
  } else if (type == kLinkMsgFleetHuntResult &&
             len == static_cast<int>(sizeof(FleetHuntResult))) {
    FleetHuntResult r;
    memcpy(&r, data, sizeof(r));
    char bssidStr[20];
    snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             r.bssid[0], r.bssid[1], r.bssid[2], r.bssid[3], r.bssid[4], r.bssid[5]);
    char rowBuf[128];
    int rlen = snprintf(rowBuf, sizeof(rowBuf), "$HUNT,%s,%s,%.6f,%.6f,%.1f,%.1f,%.1f,%d,%u",
                        bssidStr, r.ssid,
                        r.lat, r.lon,
                        r.distanceM, r.bearingDeg,
                        r.confidenceM,
                        r.rssi,
                        r.points);
    if (rlen > 0) {
      bridgeNotifyResult(kSourceHunt, reinterpret_cast<const uint8_t*>(rowBuf), rlen);
    }
    return;
  } else if (type == kLinkMsgFileEntry && len >= 12) {
    AxdFileEntryMsg r = {};
    memcpy(&r, data, len < static_cast<int>(sizeof(r)) ? len : sizeof(r));
    char line[96];
    if (r.index == 255) {
      snprintf(line, sizeof(line), "$FILELIST_END,%u,%lu",
               r.count, static_cast<unsigned long>(r.size));
    } else {
      snprintf(line, sizeof(line), "$FILELIST,%u,%u,%lu,%s",
               r.index, r.count, static_cast<unsigned long>(r.size), r.name);
    }
    bridgeNotifyResult(kSourceFiles, reinterpret_cast<const uint8_t*>(line), strlen(line));
    return;
  } else if (type == kLinkMsgFileData && len >= 12) {
    AxdFileDataMsg r = {};
    memcpy(&r, data, len < static_cast<int>(sizeof(r)) ? len : sizeof(r));
    char line[180];
    snprintf(line, sizeof(line), "$FILEDATA,%u,%u,%s", r.chunkSeq, r.totalChunks, r.data);
    bridgeNotifyResult(kSourceFiles, reinterpret_cast<const uint8_t*>(line), strlen(line));
    return;
  } else if (type == kLinkMsgFileDone && len >= 12) {
    AxdFileDoneMsg r = {};
    memcpy(&r, data, len < static_cast<int>(sizeof(r)) ? len : sizeof(r));
    char line[80];
    if (r.status == 1) {
      snprintf(line, sizeof(line), "$FILEERR,TRANSFER_FAILED");
    } else if (r.status == 2) {
      snprintf(line, sizeof(line), "$FILEABORT,%s", r.name);
    } else {
      snprintf(line, sizeof(line), "$FILEDONE,%s,%lu", r.name, static_cast<unsigned long>(r.totalBytes));
    }
    bridgeNotifyResult(kSourceFiles, reinterpret_cast<const uint8_t*>(line), strlen(line));
    return;
  }
#endif
  // Link/fleet control frames (Hello, Sync, Telem, Command, FleetInvite,
  // FleetJoin) are LinkPacket-sized: queue for linkHandlePacket on the loop.
  if (len == static_cast<int>(sizeof(LinkPacket))) linkQueuePacket(info, data);
}

bool linkEnsureEspNow() {
  if (linkEspNowReady && WiFi.getMode() == WIFI_STA) return true;
  // A BLE-only tool visited in between can deinit Wi-Fi (and with it ESP-NOW);
  // treat that as stale and rebuild.
  linkEspNowReady = false;
  if (!ensureWifiStation(true)) return false;  // Wi-Fi-only: free BLE memory
  WiFi.disconnect(false, false);
  esp_wifi_get_mac(WIFI_IF_STA, linkSelfMac);
  if (esp_now_init() != ESP_OK) {
    Serial.println("[link] esp_now_init failed");
    return false;
  }
  esp_now_register_recv_cb(onLinkRecv);
  esp_now_set_pmk(kLinkPmk);  // enables encrypted unicast peers added at pairing
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, kLinkBroadcastAddr, 6);
  peer.channel = 0;  // send on the current channel
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  if (!esp_now_is_peer_exist(kLinkBroadcastAddr)) {
    esp_now_add_peer(&peer);
  }
  linkPacketHead = 0;
  linkPacketTail = 0;
  linkEspNowReady = true;
  Serial.printf("[link] esp-now ready, self %02X:%02X:%02X:%02X:%02X:%02X\n",
                linkSelfMac[0], linkSelfMac[1], linkSelfMac[2], linkSelfMac[3],
                linkSelfMac[4], linkSelfMac[5]);
  return true;
}

// ---- Fleet Wardrive: N linked nodes split the plan into one CSV ----------
// Coordinator inbox for peer rows (POD; filled in the ESP-NOW callback, drained
// by the aggregator in updateLink). Ack lets a worker retransmit only new rows.
static ToolQueue<FleetWardriveRow, kFleetRowRingSlots> fleetRowRing;

void fleetOnRowFrame(const uint8_t* data) {
  const uint32_t generation = fleetRowRing.generation();
  if (!generation) return;
  FleetWardriveRow row;
  memcpy(&row, data, sizeof(row));
  fleetRowRing.push(row, generation);  // full: worker retransmits
}

void fleetOnAckFrame(const uint8_t* data) {
  FleetAck a;
  memcpy(&a, data, sizeof(a));
  if (a.sessionId != fleetSessionId) return;
  if (memcmp(a.targetMac, linkSelfMac, 6) != 0) return;
  if (a.seq > fleetAckedSeq) fleetAckedSeq = a.seq;
}

uint8_t fleetLocalCaps() {
  uint8_t c = kFleetCapBle;  // every supported AxD board can scan BLE
  if (AwokPins::kDualBand) c |= kFleetCapDualBand;
  c |= kFleetCapGps;         // GPS is wired on the C5 boards
  if (sdReady) c |= kFleetCapSd;
  return c;
}

int fleetIndexOfMac(const uint8_t* mac) {
  for (int i = 0; i < fleetMemberCount; ++i)
    if (memcmp(fleetMembers[i].mac, mac, 6) == 0) return i;
  return -1;
}

// Choose exactly one BLE-only node while preserving the widest Wi-Fi coverage.
void fleetDealRoster() {
  const int previousBleNodeIndex = fleetBleNodeIndex;
  fleetBleNodeIndex = -1;
  int dualCount = 0, classicCount = 0;
  for (int i = 0; i < fleetMemberCount; ++i) {
    if (fleetMembers[i].caps & kFleetCapDualBand) ++dualCount;
    else ++classicCount;
  }
  // Never consume the only C5 in a mixed fleet: it is the only 5 GHz radio.
  if (dualCount == 1 && classicCount > 0) {
    for (int pass = 0; pass < 2 && fleetBleNodeIndex < 0; ++pass)
      for (int i = 0; i < fleetMemberCount; ++i)
        if ((pass == 1 || i != fleetCoordIndex) &&
            !(fleetMembers[i].caps & kFleetCapDualBand) &&
            (fleetMembers[i].caps & kFleetCapBle)) {
          fleetBleNodeIndex = i; break;
        }
  }
  // Otherwise use a spare C5 first so classic/WROOM boards retain 2.4 GHz
  // precedence, then any non-coordinator BLE-capable member.
  if (fleetBleNodeIndex < 0 && dualCount > 1)
    for (int i = 0; i < fleetMemberCount; ++i)
      if (i != fleetCoordIndex && (fleetMembers[i].caps & kFleetCapDualBand) &&
          (fleetMembers[i].caps & kFleetCapBle)) {
        fleetBleNodeIndex = i; break;
      }
  if (fleetBleNodeIndex < 0)
    for (int i = 0; i < fleetMemberCount; ++i)
      if (i != fleetCoordIndex && (fleetMembers[i].caps & kFleetCapBle)) {
        fleetBleNodeIndex = i; break;
      }
  if (fleetBleNodeIndex < 0 && (fleetMembers[fleetCoordIndex].caps & kFleetCapBle))
    fleetBleNodeIndex = fleetCoordIndex;
  if (linkWardriveActive && previousBleNodeIndex == fleetMyIndex &&
      fleetBleNodeIndex != fleetMyIndex)
    fleetStopBleScanOnly();
  linkChannelCursor = 0;
  fleetSinkIndex = -1;
  for (int i = 0; i < fleetMemberCount; ++i)
    if (fleetMembers[i].caps & kFleetCapSd) { fleetSinkIndex = i; break; }
}

void fleetBroadcastRoster() {
  if (!fleetCoordinator || !linkEspNowReady) return;
  FleetRoster r;
  r.sessionId = fleetSessionId;
  r.code = fleetCode;
  r.memberCount = static_cast<uint8_t>(fleetMemberCount);
  r.bleNodeIndex = fleetBleNodeIndex < 0 ? 0xFF : static_cast<uint8_t>(fleetBleNodeIndex);
  r.sinkNodeIndex = fleetSinkIndex < 0 ? 0xFF : static_cast<uint8_t>(fleetSinkIndex);
  r.coordIndex = static_cast<uint8_t>(fleetCoordIndex);
  r.wardriveOn = fleetWardriveOn ? 1 : 0;
  for (int i = 0; i < fleetMemberCount && i < kFleetMaxNodes; ++i) {
    memcpy(r.members[i].mac, fleetMembers[i].mac, 6);
    r.members[i].caps = fleetMembers[i].caps;
  }
  if (!linkWardriveActive) {
    esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
  }
  esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&r), sizeof(r));
}

void fleetBroadcastInvite() {
  if (!fleetCoordinator || !linkEspNowReady) return;
  LinkPacket p;
  linkFillCommon(p, kLinkMsgFleetInvite);
  p.sessionId = fleetSessionId;
  p.code = fleetCode;
  p.masterMillis = millis();  // members align their rendezvous window to this
  esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&p), sizeof(p));
}

// Arm a node to auto-join a fleet: bring ESP-NOW up and park on the rendezvous
// channel so updateLink runs and hears the coordinator's invite.
void fleetArm() {
  if (!linkEnsureEspNow()) return;
  fleetListening = true;
  esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
  Serial.println("[fleet] armed; listening for an invite");
}

void fleetStartCoordinator() {
  if (!linkEnsureEspNow()) return;
  fleetActive = true;
  fleetCoordinator = true;
  memcpy(fleetCoordinatorMac, linkSelfMac, 6);
  fleetSessionId = esp_random();
  fleetCode = static_cast<uint16_t>(esp_random() % 10000);
  fleetMemberCount = 1;
  memcpy(fleetMembers[0].mac, linkSelfMac, 6);
  fleetMembers[0].caps = fleetLocalCaps();
  fleetMembers[0].lastSeenMs = millis();
  fleetMembers[0].rows = 0;
  fleetMembers[0].ackSeq = 0;
  fleetCoordIndex = 0;
  fleetMyIndex = 0;
  fleetRowSeq = 0;
  fleetAckedSeq = 0;
  linkClockOffset = 0;
  esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
  fleetDealRoster();
  fleetBroadcastInvite();
  fleetBroadcastRoster();
  Serial.printf("[fleet] coordinator up, session %lu code %04u\n",
                static_cast<unsigned long>(fleetSessionId), fleetCode);
}

// Worker heard an invite: join that session (auto-join; the code is shown on the
// screen for the operator to eyeball).
void fleetJoinFromInvite(const LinkPacket& p) {
  if (fleetCoordinator || fleetActive) return;
  fleetActive = true;
  fleetCoordinator = false;
  fleetSessionId = p.sessionId;
  fleetCode = p.code;
  memcpy(fleetCoordinatorMac, p.srcMac, 6);
  fleetRowSeq = 0;
  fleetAckedSeq = 0;
  linkClockOffset =
      static_cast<int32_t>(p.masterMillis) - static_cast<int32_t>(millis());
  fleetSendJoin();
  fleetListening = false;  // the deliberate arm is consumed; we are now a member
}

// Worker -> pinned coordinator join/heartbeat. Repeating this does not reset
// worker state; it only lets the coordinator maintain a live connected count.
void fleetSendJoin() {
  LinkPacket j;
  linkFillCommon(j, kLinkMsgFleetJoin);
  j.sessionId = fleetSessionId;
  j.flags = fleetLocalCaps();
  esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&j), sizeof(j));
}

void fleetCoordHandleJoin(const LinkPacket& p) {
  if (!fleetCoordinator || p.sessionId != fleetSessionId) return;
  int idx = fleetIndexOfMac(p.srcMac);
  bool rosterChanged = false;
  if (idx < 0) {
    if (fleetMemberCount >= kFleetMaxNodes) return;
    idx = fleetMemberCount++;
    memcpy(fleetMembers[idx].mac, p.srcMac, 6);
    fleetMembers[idx].rows = 0;
    fleetMembers[idx].ackSeq = 0;
    rosterChanged = true;
    Serial.printf("[fleet] member %d joined\n", idx);
  }
  if (fleetMembers[idx].caps != p.flags) {
    fleetMembers[idx].caps = p.flags;
    rosterChanged = true;
  }
  fleetMembers[idx].lastSeenMs = millis();
  if (rosterChanged) {
    fleetDealRoster();
    if (!linkWardriveActive || linkInWindow) {
      fleetBroadcastRoster();
    }
    if (currentView == View::kLinkWardrive) drawFleetStatus();
  }
}

// A worker only accepts rosters from the coordinator/session it deliberately
// joined. Coordinators never consume a received roster: Start is an explicit
// authority choice, not an election, so another chip cannot demote it.
void fleetApplyRoster(const FleetRoster& r) {
  if (!fleetActive || fleetCoordinator) return;
  if (r.sessionId != fleetSessionId) return;
  if (r.memberCount == 0 || r.memberCount > kFleetMaxNodes) return;
  if (r.coordIndex >= r.memberCount) return;
  if (memcmp(r.members[r.coordIndex].mac, fleetCoordinatorMac, 6) != 0) {
    Serial.println("[fleet] ignored roster from unpinned coordinator");
    return;
  }

  int myIndex = -1;
  for (int i = 0; i < r.memberCount; ++i) {
    if (memcmp(r.members[i].mac, linkSelfMac, 6) == 0) {
      myIndex = i;
      break;
    }
  }
  // Never default an absent worker to slot zero: slot zero is normally the
  // coordinator, which was the source of workers promoting themselves.
  if (myIndex < 0) {
    Serial.println("[fleet] ignored roster that does not contain this node");
    return;
  }

  const bool wasBleNode = fleetImBleNode();
  fleetCode = r.code;
  fleetMemberCount = r.memberCount;
  for (int i = 0; i < fleetMemberCount; ++i) {
    memcpy(fleetMembers[i].mac, r.members[i].mac, 6);
    fleetMembers[i].caps = r.members[i].caps;
  }
  fleetCoordIndex = r.coordIndex;
  fleetBleNodeIndex = r.bleNodeIndex == 0xFF ? -1 : r.bleNodeIndex;
  fleetSinkIndex = r.sinkNodeIndex == 0xFF ? -1 : r.sinkNodeIndex;
  fleetMyIndex = myIndex;
  if (linkWardriveActive && wasBleNode && !fleetImBleNode())
    fleetStopBleScanOnly();
  linkChannelCursor = 0;
  Serial.printf("[fleet] roster N=%d myIndex=%d role=%s wifiSlice=%d/%d coord=%d\n",
                fleetMemberCount, fleetMyIndex,
                fleetImBleNode() ? "BLE" : "WIFI",
                fleetMyWifiSlice(), fleetMyWifiWorkerCount(), fleetCoordIndex);
  fleetWardriveOn = r.wardriveOn != 0;
  if (fleetWardriveOn && !linkWardriveActive) {
    startLinkWardrive();          // join the drive in progress
  } else if (!fleetWardriveOn && linkWardriveActive) {
    fleetStopLocal();             // coordinator ended the drive
  }
}

// Wi-Fi workers = every member except the dedicated BLE node; my rank among them
// drives the N-way channel deal.
int fleetWifiWorkerCount() {
  int n = 0;
  for (int i = 0; i < fleetMemberCount; ++i)
    if (i != fleetBleNodeIndex) ++n;
  return n < 1 ? 1 : n;
}
bool fleetHasClassicWifiWorker() {
  for (int i = 0; i < fleetMemberCount; ++i)
    if (i != fleetBleNodeIndex &&
        !(fleetMembers[i].caps & kFleetCapDualBand)) return true;
  return false;
}

int fleetMyWifiWorkerCount() {
  const bool mineDual = AwokPins::kDualBand;
  const bool splitByBand = fleetHasClassicWifiWorker();
  int n = 0;
  for (int i = 0; i < fleetMemberCount; ++i) {
    if (i == fleetBleNodeIndex) continue;
    const bool memberDual = (fleetMembers[i].caps & kFleetCapDualBand) != 0;
    if (!splitByBand || memberDual == mineDual) ++n;
  }
  return n < 1 ? 1 : n;
}

int fleetMyWifiSlice() {
  const bool mineDual = AwokPins::kDualBand;
  const bool splitByBand = fleetHasClassicWifiWorker();
  int rank = 0;
  for (int i = 0; i < fleetMemberCount; ++i) {
    if (i == fleetBleNodeIndex) continue;
    const bool memberDual = (fleetMembers[i].caps & kFleetCapDualBand) != 0;
    if (splitByBand && memberDual != mineDual) continue;
    if (i == fleetMyIndex) return rank;
    ++rank;
  }
  return 0;
}
bool fleetImBleNode() { return fleetActive && fleetMyIndex == fleetBleNodeIndex; }

// Coordinator housekeeping: re-broadcast invite/roster and drop silent members.
void fleetCoordinatorTick(uint32_t now) {
  if (!fleetCoordinator) return;
  bool rosterChanged = false;
  for (int i = fleetMemberCount - 1; i >= 0; --i) {
    if (i == fleetCoordIndex) continue;
    if (now - fleetMembers[i].lastSeenMs <= kFleetMemberTimeoutMs) continue;
    Serial.printf("[fleet] member %d timed out\n", i);
    for (int j = i; j + 1 < fleetMemberCount; ++j)
      fleetMembers[j] = fleetMembers[j + 1];
    --fleetMemberCount;
    rosterChanged = true;
  }
  if (rosterChanged) {
    fleetDealRoster();
    if (!linkWardriveActive || linkInWindow) {
      fleetBroadcastRoster();
    }
    if (currentView == View::kLinkWardrive) drawFleetStatus();
  }
  // During an active wardrive, invite and roster broadcasts happen during
  // the rendezvous window (when everyone is parked on kLinkChannel). Only
  // broadcast on timer when wardrive is idle.
  if (!linkWardriveActive) {
    if (now - lastFleetInviteMs >= kFleetInviteIntervalMs) {
      lastFleetInviteMs = now;
      fleetBroadcastInvite();
    }
    if (now - lastFleetRosterMs >= kFleetRosterIntervalMs) {
      lastFleetRosterMs = now;
      fleetBroadcastRoster();
    }
  }
  static uint32_t lastFleetStatusMs = 0;
  if (linkWardriveActive && now - lastFleetStatusMs >= 3000) {
    lastFleetStatusMs = now;
    Serial.printf("[fleet] N=%d agg wifi=%lu ble=%lu | rows",
                  fleetMemberCount, (unsigned long)wardriveNetworks,
                  (unsigned long)wardriveBleCount);
    for (int i = 0; i < fleetMemberCount; ++i)
      Serial.printf(" m%d=%lu", i, (unsigned long)fleetMembers[i].rows);
    Serial.println();
  }
}
// ---- Fleet Wardrive Phase 2: row aggregation -----------------------------
// Worker outbound ring: new rows wait here until the coordinator ACKs them.
static ToolBuffer<FleetWardriveRow, kFleetRowRingSlots> fleetOutRing;
static int fleetOutHead = 0;
static int fleetOutTail = 0;

void fleetQueueOutRow(const uint8_t* id, const String& name, uint8_t auth,
                      uint8_t channel, int rssi, bool isBle) {
  if (!fleetOutRing) return;
  FleetWardriveRow r;
  r.sessionId = fleetSessionId;
  r.seq = ++fleetRowSeq;
  memcpy(r.src, linkSelfMac, 6);
  memcpy(r.id, id, 6);
  r.rssi = static_cast<int8_t>(rssi);
  r.channel = channel;
  r.auth = auth;
  r.isBle = isBle ? 1 : 0;
  r.lat = gps.location.isValid() ? static_cast<float>(gps.location.lat()) : 0.0f;
  r.lon = gps.location.isValid() ? static_cast<float>(gps.location.lng()) : 0.0f;
  r.alt = static_cast<int16_t>(gps.altitude.isValid() ? gps.altitude.meters() : 0);
  strncpy(r.name, name.c_str(), sizeof(r.name) - 1);
  const int next = (fleetOutHead + 1) % kFleetRowRingSlots;
  if (next == fleetOutTail)  // ring full: drop oldest unacked row
    fleetOutTail = (fleetOutTail + 1) % kFleetRowRingSlots;
  fleetOutRing[fleetOutHead] = r;
  fleetOutHead = next;
}

// Worker: during a window, drop ACKed rows and (re)send a few unacked ones to
// the coordinator (broadcast; only the coordinator stores them).
void fleetWorkerSendRows() {
  if (!fleetOutRing) return;
  while (fleetOutTail != fleetOutHead &&
         fleetOutRing[fleetOutTail].seq <= fleetAckedSeq)
    fleetOutTail = (fleetOutTail + 1) % kFleetRowRingSlots;
  int idx = fleetOutTail, sent = 0;
  while (idx != fleetOutHead && sent < 8) {
    esp_now_send(kLinkBroadcastAddr,
                 reinterpret_cast<uint8_t*>(&fleetOutRing[idx]),
                 sizeof(FleetWardriveRow));
    idx = (idx + 1) % kFleetRowRingSlots;
    ++sent;
    delay(3);
  }
}

// Coordinator: merge every inbound row (dedup + SD + phone) and advance each
// member's ACK sequence. Capped to 32 rows per pass to prevent starvation.
void fleetCoordDrainRows() {
  if (!fleetCoordinator) return;
  int drained = 0;
  FleetWardriveRow r;
  while (drained < 32 && fleetRowRing.pop(r)) {
    ++drained;
    if (r.sessionId != fleetSessionId) continue;
    const int m = fleetIndexOfMac(r.src);
    if (m >= 0) {
      fleetMembers[m].lastSeenMs = millis();
      // If the worker reset its sequence (e.g. rebooted/rejoined), allow resync
      if (r.seq < fleetMembers[m].ackSeq && r.seq <= 2) {
        fleetMembers[m].ackSeq = r.seq;
      } else if (r.seq > fleetMembers[m].ackSeq) {
        fleetMembers[m].ackSeq = r.seq;
      }
      fleetMembers[m].rows++;
    }
    wardriveEmitPeerRow(r);  // dedups by id; writes SD + streams to phone
  }
}

// A node needs only its role's ring, and only while a drive is running.
// Worker storage is loopTask-only; the coordinator inbox stays in internal RAM.
bool fleetBeginRowBuffers() {
  fleetOutHead = fleetOutTail = 0;
  if (!fleetActive) return true;
  const bool ok = fleetCoordinator ? fleetRowRing.begin() : fleetOutRing.allocate();
  if (!ok) {
    fleetRowRing.release();
    fleetOutRing.release();
    showToolMemoryError("Fleet Wardrive");
  }
  return ok;
}

void fleetReleaseRowBuffers() {
  fleetRowRing.pause();
  // Finish accepted inbound rows before the caller closes the CSV. The paused
  // queue cannot grow; each drain consumes at most 32 of the bounded inbox.
  for (int i = 0; i < kFleetRowRingSlots; i += 32) fleetCoordDrainRows();
  fleetRowRing.release();
  fleetOutRing.release();
  fleetOutHead = fleetOutTail = 0;
}

// Coordinator: tell each member the highest row seq stored, so it can
// free ACKed rows.
void fleetSendAcks() {
  if (!fleetCoordinator) return;
  for (int i = 0; i < fleetMemberCount; ++i) {
    if (i == fleetCoordIndex) continue;
    FleetAck a;
    a.sessionId = fleetSessionId;
    memcpy(a.targetMac, fleetMembers[i].mac, 6);
    a.seq = fleetMembers[i].ackSeq;
    esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&a), sizeof(a));
    delay(2);
  }
}

// Coordinator entry point: form the session (if needed) and start fleet wardrive
// on every member (the roster's wardriveOn flag makes joiners start too).
void fleetStartWardrive() {
  if (!fleetActive) fleetStartCoordinator();
  if (!fleetCoordinator) return;  // only the coordinator starts the fleet
  startLinkWardrive();  // allocate our ring before asking workers to start
  fleetWardriveOn = linkWardriveActive;
  fleetBroadcastRoster();
}

// The designated BLE node runs a continuous BLE observer scan (co-resident with
// Wi-Fi/ESP-NOW) instead of hopping Wi-Fi channels, and ships BLE rows to the
// coordinator in the rendezvous window. It parks on the link channel since it
// never Wi-Fi-hops.
void fleetBleNodeStart() {
  WiFi.scanDelete();
  esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
  if (!ensureBleReady(true)) return;  // BLE up; keep Wi-Fi/ESP-NOW resident
  NimBLEScan* scan = NimBLEDevice::getScan();
  configureBleScan(scan, &wardriveBleCallbacks, false, 160, 80, 0);
  scan->start(0, false, true);
  fleetBleScanRunning = true;
  Serial.println("[fleet] BLE node scanning");
}

void fleetStopBleScanOnly() {
  if (fleetBleScanRunning) {
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (scan) { scan->stop(); scan->clearResults(); }
    fleetBleScanRunning = false;
  }
}

void fleetStopLocal() {
  linkWardriveActive = false;
  fleetStopBleScanOnly();
  fleetReleaseRowBuffers();
  closeWardriveCsv();
  logMemory("Fleet buffers released");
  esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
  linkBroadcastStatus();
}

// Coordinator: stop the fleet wardrive but keep the session up (members stop via
// the roster's wardriveOn flag). A second tap on Go restarts it.
void fleetStopWardrive() {
  if (!fleetCoordinator) return;
  fleetWardriveOn = false;
  fleetBroadcastRoster();
  fleetStopLocal();
}

// Tear the whole fleet down on this node. If we are the coordinator, tell the
// members to stop first. Leaves us back on the idle Link screen.
void fleetLeave() {
  if (fleetCoordinator && fleetWardriveOn) {
    fleetWardriveOn = false;
    fleetBroadcastRoster();  // members drop out of the drive
  }
  fleetStopLocal();
  fleetActive = false;
  fleetListening = false;
  fleetCoordinator = false;
  memset(fleetCoordinatorMac, 0, sizeof(fleetCoordinatorMac));
  fleetWardriveOn = false;
  fleetMemberCount = 0;
  fleetMyIndex = 0;
  fleetCoordIndex = 0;
  fleetBleNodeIndex = -1;
  fleetSinkIndex = -1;
  fleetMenuOpen = false;
  Serial.println("[fleet] left the session");
}

void updateFleetBleNode(uint32_t now) {
  if (!fleetBleScanRunning) fleetBleNodeStart();
  while (bleHitTail != bleHitHead) {
    const BleHit& hit = bleHitQueue[bleHitTail];
    uint8_t mac[6];
    if (gpsHasFix() && parseBssid(String(hit.addr), mac) &&
        !wardriveMacSeen(mac)) {
      wardriveAddMac(mac);
      if (fleetCoordinator)
        appendWardriveBleRow(String(hit.addr), String(hit.name), hit.rssi);
      else
        fleetQueueOutRow(mac, String(hit.name), 0, 0, hit.rssi, true);
      ++wardriveBleCount;
    }
    bleHitTail = (bleHitTail + 1) % kBleHitQueueSlots;
  }
  const uint32_t lnow = linkNow();
  const bool windowDue = (lnow % kLinkRendezvousMs) < kLinkWindowMs;
  if (windowDue && now - lastLinkTelemMs >= 60) {
    lastLinkTelemMs = now;
    if (fleetCoordinator) {
      fleetSendAcks();
      fleetCoordDrainRows();
      if (now - lastFleetInviteMs >= kFleetInviteIntervalMs) {
        lastFleetInviteMs = now;
        fleetBroadcastInvite();
      }
      if (now - lastFleetRosterMs >= kFleetRosterIntervalMs) {
        lastFleetRosterMs = now;
        fleetBroadcastRoster();
      }
    }
    else fleetWorkerSendRows();
  }
  if (windowDue) linkMaybeBroadcastRemoteStatus(now);
  if (currentView == View::kLinkWardrive &&
      now - lastLinkWardriveDrawMs >= kLinkWardriveRedrawMs) {
    lastLinkWardriveDrawMs = now;
    drawLinkWardrive();
  }
}


uint16_t linkComputeCode(const uint8_t* a, const uint8_t* b) {
  // Deterministic on the unordered MAC pair, so both units show the same code.
  const uint8_t* lo = a;
  const uint8_t* hi = b;
  if (memcmp(a, b, 6) > 0) {
    lo = b;
    hi = a;
  }
  uint32_t h = 2166136261u;  // FNV-1a
  for (int i = 0; i < 6; ++i) h = (h ^ lo[i]) * 16777619u;
  for (int i = 0; i < 6; ++i) h = (h ^ hi[i]) * 16777619u;
  return static_cast<uint16_t>(h % 10000);
}

uint32_t linkNow() {
  return static_cast<uint32_t>(static_cast<int32_t>(millis()) + linkClockOffset);
}

void linkFillCommon(LinkPacket& p, uint8_t type) {
  p.magic = kLinkMagic;
  p.version = kLinkProtoVersion;
  p.type = type;
  p.role = linkRoleMaster ? 0 : 1;
  memcpy(p.srcMac, linkSelfMac, 6);
}

void linkSendHello() {
  LinkPacket p;
  linkFillCommon(p, kLinkMsgHello);
  p.flags = (linkConfirmedLocal ? kLinkFlagConfirmed : 0) |
            (AwokPins::kDualBand ? kLinkFlagDualBand : 0);
  p.code = linkCode;
  p.sessionId = linkSessionId;
  // Carry the clock so the slave can sync while both sit on ch 1 during pairing,
  // before any drive window has to line up. Only meaningful from the master.
  p.masterMillis = millis();
  esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&p), sizeof(p));
}

void linkSendSync() {
  LinkPacket p;
  linkFillCommon(p, kLinkMsgSync);
  p.masterMillis = millis();
  p.sessionId = linkSessionId;
  // Unicast to the encrypted peer (only ever sent while paired).
  esp_now_send(linkPeerMac, reinterpret_cast<uint8_t*>(&p), sizeof(p));
}

void linkSendTelem() {
  LinkPacket p;
  linkFillCommon(p, kLinkMsgTelem);
  p.networks = wardriveNetworks;
  p.bleCount = wardriveBleCount;
  p.channel = linkScanChannel;
  p.sessionId = linkSessionId;
  // Unicast to the encrypted peer (only ever sent while paired).
  esp_now_send(linkPeerMac, reinterpret_cast<uint8_t*>(&p), sizeof(p));
}

// Per-pair link key: the baked base with the confirm code mixed in so each pair
// gets its own LMK. Not a secret (the code derives from broadcast MACs), just
// key separation between different unit pairs.
void linkComputeLmk(uint8_t out[16]) {
  memcpy(out, kLinkLmkBase, 16);
  out[0] ^= static_cast<uint8_t>(linkCode & 0xFF);
  out[1] ^= static_cast<uint8_t>(linkCode >> 8);
}

// Add (or refresh) the partner as an encrypted unicast peer. SYNC/TELEM then go
// out encrypted; the plaintext broadcast peer stays for HELLO discovery.
void linkAddEncryptedPeer() {
  if (!linkEspNowReady || !linkPeerValid) return;
  if (esp_now_is_peer_exist(linkPeerMac)) esp_now_del_peer(linkPeerMac);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, linkPeerMac, 6);
  peer.channel = 0;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = true;
  linkComputeLmk(peer.lmk);
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[link] add encrypted peer failed");
  }
}

// ---- pairing state machine ----------------------------------------------

void linkFinalizePairing(const LinkPacket& p) {
  linkState = kLinkReady;
  if (linkRoleMaster) {
    if (!linkSessionId) linkSessionId = esp_random();
    linkClockOffset = 0;
  } else if (p.sessionId) {
    linkSessionId = p.sessionId;
  }
  linkAddEncryptedPeer();  // SYNC/TELEM are encrypted unicast from here on
  linkPartnerLastSeenMs = millis();
  recordFirmwareAudit(
      "link", "paired", "success",
      "role=" + String(linkRoleMaster ? "master" : "slave") + "; session=" +
          String(static_cast<unsigned long>(linkSessionId)));
  Serial.printf("[link] paired as %s, session %lu\n",
                linkRoleMaster ? "master" : "slave",
                static_cast<unsigned long>(linkSessionId));
  if (currentView == View::kLinkWardrive) drawLinkWardrive();
}

// The bridge can deliver a command in its first burst, then spend the next
// ~200 ms sweeping other channels. Wait for a matching ACK after it parks back
// on channel 1, before opening/streaming a file or returning a listing/error.
static bool linkWaitForFileReceiver(const LinkPacket& command) {
  linkFileWaitingToken.store(command.sessionId);
  WiFi.scanDelete();  // an asynchronous scan must not move us off the rendezvous
  esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
  LinkPacket ready;
  ready.type = kLinkMsgFileReady;
  ready.flags = kLinkFileReadyRequest;
  ready.sessionId = command.sessionId;
  ready.code = command.code;
  memcpy(ready.srcMac, linkSelfMac, 6);
  const uint32_t started = millis();
  uint32_t lastRequest = started - kLinkFileReadyRetryMs;
  while (millis() - started < kLinkFileReadyTimeoutMs) {
    if (linkFileWaitingToken.load() == 0) return true;
    const uint32_t now = millis();
    if (now - lastRequest >= kLinkFileReadyRetryMs) {
      lastRequest = now;
      esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&ready), sizeof(ready));
    }
    delay(1);
  }
  linkFileWaitingToken.store(0);
  Serial.println("$FILEERR,RECEIVER_NOT_READY");
  AxdFileDoneMsg done;
  done.status = 1;
  esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&done), sizeof(done));
  return false;
}

void linkHandlePacket(const LinkQueueItem& item) {
  const LinkPacket& p = item.pkt;
  if (p.magic != kLinkMagic || p.version != kLinkProtoVersion) return;
  if (memcmp(p.srcMac, linkSelfMac, 6) == 0) return;  // ignore our own echo

  // Fleet Wardrive control frames (see link_protocol.h).
  if (p.type == kLinkMsgFleetInvite) {
    if (fleetCoordinator) {
      // Start is authoritative. A coordinator never participates in election
      // or steps down because another chip also broadcasts an invite.
      return;
    }
    // A joined worker is pinned to one coordinator/session until Leave. Reply
    // periodically as a heartbeat so the coordinator's node count stays live.
    if (fleetActive) {
      if (p.sessionId == fleetSessionId &&
          memcmp(p.srcMac, fleetCoordinatorMac, 6) == 0) {
        static uint32_t lastFleetHeartbeatMs = 0;
        const uint32_t now = millis();
        if (now - lastFleetHeartbeatMs >= 1000) {
          lastFleetHeartbeatMs = now;
          fleetSendJoin();
        }
      }
      return;
    }
    // Otherwise only join if the operator deliberately armed this node (Join
    // button / serial `j`). Powering up must never auto-link a chip into a fleet.
    if (fleetListening) fleetJoinFromInvite(p);
    return;
  }
  if (p.type == kLinkMsgFleetJoin) {
    fleetCoordHandleJoin(p);
    return;
  }

  if (p.type == kLinkMsgHello) {
    if (linkState == kLinkDiscovering) {
      memcpy(linkPeerMac, p.srcMac, 6);
      linkPeerValid = true;
      linkPeerDualBand = (p.flags & kLinkFlagDualBand) != 0;
      linkRoleMaster = memcmp(linkSelfMac, linkPeerMac, 6) < 0;
      if (linkRoleMaster && !linkSessionId) linkSessionId = esp_random();
      linkCode = linkComputeCode(linkSelfMac, linkPeerMac);
      linkState = kLinkAwaitConfirm;
      if (!linkRoleMaster) {
        linkClockOffset =
            static_cast<int32_t>(p.masterMillis) - static_cast<int32_t>(millis());
      }
      if (currentView == View::kLinkWardrive) drawLinkWardrive();
    } else if (linkState == kLinkAwaitConfirm) {
      if (memcmp(p.srcMac, linkPeerMac, 6) != 0) return;  // different unit
      if (!linkRoleMaster) {
        linkClockOffset =
            static_cast<int32_t>(p.masterMillis) - static_cast<int32_t>(millis());
      }
      if (linkConfirmedLocal && (p.flags & kLinkFlagConfirmed)) {
        linkFinalizePairing(p);
      }
    } else if (linkState == kLinkReady &&
               memcmp(p.srcMac, linkPeerMac, 6) == 0) {
      if (!linkRoleMaster) {
        linkClockOffset =
            static_cast<int32_t>(p.masterMillis) - static_cast<int32_t>(millis());
      }
      linkPartnerRssi = item.rssi;
      linkPartnerLastSeenMs = millis();
      // A paired peer sends no HELLO, so receiving one means the peer is still
      // trying to finalize and went looking after we already went quiet. Answer
      // with a confirmed HELLO (throttled) so it can latch too. Self-limiting:
      // once the peer is paired it stops sending and these replies stop.
      const uint32_t nowMs = millis();
      if (nowMs - lastLinkHelloMs >= 80) {
        lastLinkHelloMs = nowMs;
        linkSendHello();
      }
    }
    return;
  }

  // Remote-control command from the bridge chip. Accepted whenever remote is
  // active, independent of unit pairing (the bridge is not a paired peer). The
  // bridge burst-repeats each command (so it lands even while we are off on a
  // hopping tool's channel); p.code is the command sequence, so we act once.
  if (p.type == kLinkMsgCommand) {
    static uint16_t lastCmdSeq = 0;
    if (remoteActive && p.code != lastCmdSeq) {
      lastCmdSeq = p.code;
      const uint8_t op = static_cast<uint8_t>(p.reserved & 0xFF);
      const bool fileReply = op == kAxdCmdFileList || op == kAxdCmdFileGet ||
                             op == kAxdCmdFileGetReliable || op == kAxdCmdFileGetVerified || op == kAxdCmdFileDelete;
      if (fileReply && (p.flags & kLinkCommandWaitFileReady) &&
          (p.sessionId == 0 || !linkWaitForFileReceiver(p))) return;
      if (op == kAxdCmdFileGetVerified) {
        if (p.sessionId != 0) linkStreamFileVerified(static_cast<uint8_t>(p.reserved >> 8),
            p.sessionId, p.masterMillis, p.networks, p.bleCount);
      } else if (op == kAxdCmdFileGetReliable) {
        if (p.sessionId != 0) linkStreamFileReliable(static_cast<uint8_t>(p.reserved >> 8), p.sessionId);
      } else {
        linkDispatchCommand(op, static_cast<uint8_t>(p.reserved >> 8));
      }
    }
    return;
  }

  if (linkState != kLinkReady || memcmp(p.srcMac, linkPeerMac, 6) != 0) return;

  if (p.type == kLinkMsgSync) {
    if (!linkRoleMaster) {
      linkClockOffset =
          static_cast<int32_t>(p.masterMillis) - static_cast<int32_t>(millis());
      if (p.sessionId) linkSessionId = p.sessionId;
    }
  } else if (p.type == kLinkMsgTelem) {
    linkPartnerNetworks = p.networks;
    linkPartnerBle = p.bleCount;
    linkPartnerChannel = p.channel;
    linkPartnerRssi = item.rssi;
    linkPartnerLastSeenMs = millis();
  }
}

void linkDrainPackets() {
  while (linkPacketTail != linkPacketHead) {
    const LinkQueueItem item = linkPacketQueue[linkPacketTail];
    linkPacketTail = (linkPacketTail + 1) % kLinkPacketQueueSlots;
    linkHandlePacket(item);
  }
}

// ---- Remote control (phone -> orange bridge -> here over ESP-NOW) --------
// The bridge chip relays BLE command opcodes as kLinkMsgCommand frames; we run
// the matching tool -- the same entry points the serial shortcuts use. Status
// (counts + current view) is broadcast back so the bridge can notify the phone.

// Stream the current Wi-Fi scan list to the phone (screen -> bridge -> BLE), one
// AxdWifiResult per AP. Sent while homed on the rendezvous channel, where the
// bridge listens.
void linkStreamWifiResults() {
  const int n = wifiCount;
#ifdef AWOK_HEADLESS
  // The bridge runs the tool locally: notify the phone directly over BLE, one
  // result per notification, tagged as this chip's own output. No ESP-NOW hop.
  for (int i = 0; i < n; ++i) {
    uint8_t blob[11 + 32];
    uint8_t mac[6] = {0};
    parseBssid(wifiEntries[i].bssid, mac);
    blob[0] = static_cast<uint8_t>(i);
    blob[1] = static_cast<uint8_t>(n);
    blob[2] = static_cast<uint8_t>(static_cast<int8_t>(wifiEntries[i].rssi));
    blob[3] = static_cast<uint8_t>(wifiEntries[i].channel);
    blob[4] = static_cast<uint8_t>(wifiEntries[i].auth);
    memcpy(blob + 5, mac, 6);
    size_t sl = wifiEntries[i].ssid.length();
    if (sl > 32) sl = 32;
    memcpy(blob + 11, wifiEntries[i].ssid.c_str(), sl);
    bridgeNotifyResult(kSourceBridge, blob, 11 + sl);
    delay(30);
  }
  Serial.printf("[bridge] notified %d Wi-Fi result(s) to phone\n", n);
  return;
#else
  if (!linkEspNowReady) return;
  // A scan leaves the radio on an arbitrary channel; the bridge only listens on
  // the rendezvous channel, so home there before streaming or the phone sees a
  // partial (or empty) list. Safe: this is only called from idle (scan complete
  // or a ListWifi request), never mid-hop.
  esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
  for (int i = 0; i < n; ++i) {
    AxdWifiResult r;
    r.index = static_cast<uint8_t>(i);
    r.count = static_cast<uint8_t>(n);
    uint8_t mac[6];
    if (parseBssid(wifiEntries[i].bssid, mac)) memcpy(r.bssid, mac, 6);
    r.rssi = static_cast<int8_t>(wifiEntries[i].rssi);
    r.channel = static_cast<uint8_t>(wifiEntries[i].channel);
    r.auth = static_cast<uint8_t>(wifiEntries[i].auth);
    strncpy(r.ssid, wifiEntries[i].ssid.c_str(), sizeof(r.ssid) - 1);
    esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&r), sizeof(r));
    // Each frame becomes one BLE notification at the bridge; pace slower than the
    // phone's connection interval so the ATT tx queue never overflows and drops
    // APs. ~30 ms comfortably clears a 15 ms negotiated interval.
    delay(30);
  }
  Serial.printf("[remote] streamed %d Wi-Fi result(s) on ch %u\n", n, kLinkChannel);
#endif
}

void linkDispatchCommand(uint8_t op, uint8_t arg) {
  Serial.printf("[remote] command op=%u arg=%u\n", op, arg);
  switch (op) {
    // Recon
    case kAxdCmdWifiScan: startWifiScanContinuous(); break;
    case kAxdCmdBleScan: scanBle(); break;
    case kAxdCmdChannelMap:
      if (wifiCount) drawChannelMap(); else scanWifiForChannelMap();
      break;
    case kAxdCmdPacketMon: startPacketMon(); break;
    case kAxdCmdClients: startClientSniffer(); break;
    case kAxdCmdWpsScan: startWpsScan(); break;
    case kAxdCmdHiddenSsid: startHiddenReveal(); break;
    case kAxdCmdCameras: startCameraScan(); break;
    case kAxdCmdSecurityAudit: startSecurityAudit(); break;
    case kAxdCmdTrackers: startTrackerScan(); break;
    case kAxdCmdBleIntel: startBleIntel(); break;
    case kAxdCmdSpectrogram:
      if (!spectrogramActive) startSpectrogram();
      if (arg == 201) {
        spectrogramLockStep(-1);
      } else if (arg == 202) {
        spectrogramLockStep(1);
      } else if (arg == 203) {
        spectrogramCycleBand();
      } else if (arg == 204) {
        spectrogramToggleHop();
      } else if (arg >= 1 && arg <= 165) {
        spectrogramLockToChannel(arg);
      }
      break;
    case kAxdCmdHarvester: startHarvester(); break;
    case kAxdCmdProbeIntel: startProbeIntel(); break;
    case kAxdCmdWifi6Intel: startWifi6Intel(); break;
    case kAxdCmdSaved: drawSavedNetworks(); break;
    // Attacks (target-specific ones act on the on-device last selection)
    case kAxdCmdBeaconFlood: startBeaconFlood(); break;
    case kAxdCmdEvilPortal: startEvilPortal(); break;
    case kAxdCmdEvilTwin: startEvilTwin(); break;
    case kAxdCmdProbeLure: startProbeLure(); break;
    case kAxdCmdAuthFlood: startAuthFlood(); break;
    // Monitor
    case kAxdCmdDeauthWatch: startDeauthMonitor(); break;
    case kAxdCmdRogueWatch: startRogueWatch(); break;
    case kAxdCmdBleSpamWatch: startBleDetect(); break;
    case kAxdCmdKarmaWatch: startKarmaWatch(); break;
    case kAxdCmdBeaconWatch: startBeaconWatch(); break;
    case kAxdCmdAdvancedWatch: startAdvancedWatch(); break;
    case kAxdCmdDeauthForensics: startDeauthForensics(); break;
    // GPS / wardrive / misc
    case kAxdCmdWardriveStart: startWardrive(); break;
    case kAxdCmdWardriveStop: stopWardrive(); drawHome(); break;
    case kAxdCmdGps: drawGps(); break;
    case kAxdCmdLocator: startLocator(); break;
    case kAxdCmdStatus: drawStatus(); break;
    case kAxdCmdFiles: openFilesManager(); break;
    case kAxdCmdFileList: linkStreamFileList(); break;
    case kAxdCmdFileGet: linkStreamFileData(arg); break;
    case kAxdCmdFileDelete: linkDeleteFile(arg); break;
    case kAxdCmdFileAbort: linkAbortFileStream(); break;
    // Network selection + per-target actions
    case kAxdCmdListWifi: linkStreamWifiResults(); break;
    case kAxdCmdSelectWifi:
      if (arg < wifiCount) openWifiAudit(wifiEntries[arg], View::kWifi);
      break;
    case kAxdCmdDeauthSel:
      if (selectedWifi.bssid.length()) { openDeauthAttackSingle(); startDeauthAttack(); }
      break;
    case kAxdCmdGrabSel:
      if (selectedWifi.bssid.length()) startHandshakeCapture();
      break;
    case kAxdCmdTrackSel:
      if (selectedWifi.bssid.length()) beginWifiSignalMonitor();
      break;
    case kAxdCmdFleetHunt:
      if (selectedWifi.bssid.length()) startFleetHunt();
      break;
    case kAxdCmdTopology: startTopologyMap(); break;
    // Fleet control (phone or on-device buttons)
    case kAxdCmdFleetStart: fleetStartWardrive(); break;
    case kAxdCmdFleetJoin: fleetArm(); break;
    case kAxdCmdFleetStop: fleetLeave(); break;
    case kAxdCmdStopHome: stopActiveTools(); drawHome(); break;
    default: break;
  }
}

void linkBroadcastStatus() {
  broadcastWardriveDashboard();
  const bool wardriving = wardriveActive || linkWardriveActive || fleetWardriveOn ||
                          (currentView == View::kWardrive) || (currentView == View::kLinkWardrive);
  const uint32_t nets = wardriving
                            ? wardriveNetworks
                            : (wifiCount > 0 ? (uint32_t)wifiCount : wardriveNetworks);
  const uint32_t ble = wardriving
                           ? wardriveBleCount
                           : (bleCount > 0 ? (uint32_t)bleCount : wardriveBleCount);
  const uint8_t view = static_cast<uint8_t>(currentView);
#ifdef AWOK_HEADLESS
  // Bridge -> phone directly: [wifi u32][ble u32][tool u8][gpsFix u8][sats u8]
  // [lat f32][lon f32] (source tag prefixed by bridgeNotifyStatus). The app
  // reads the GPS tail when present so the headless bridge's fix shows on-phone.
  // ...then a fleet tail: [active u8][members u8][code u16][coordinator u8].
  uint8_t blob[24];
  memcpy(blob + 0, &nets, 4);
  memcpy(blob + 4, &ble, 4);
  blob[8] = view;
  blob[9] = gpsHasFix() ? 1 : 0;
  blob[10] = static_cast<uint8_t>(
      gps.satellites.isValid() ? gps.satellites.value() : 0);
  float lat = gps.location.isValid() ? static_cast<float>(gps.location.lat()) : 0.0f;
  float lon = gps.location.isValid() ? static_cast<float>(gps.location.lng()) : 0.0f;
  memcpy(blob + 11, &lat, 4);
  memcpy(blob + 15, &lon, 4);
  blob[19] = (fleetActive || fleetListening) ? 1 : 0;
  blob[20] = static_cast<uint8_t>(fleetMemberCount);
  uint16_t fcode = fleetCode;
  memcpy(blob + 21, &fcode, 2);
  blob[23] = (fleetCoordinator ? 0x01 : 0) |
             (fleetListening && !fleetActive ? 0x02 : 0) |
             (fleetWardriveOn ? 0x04 : 0);
  bridgeNotifyStatus(kSourceBridge, blob, sizeof(blob));
  return;
#else
  if (!linkEspNowReady) return;
  LinkPacket p;
  linkFillCommon(p, kLinkMsgTelem);
  p.networks = nets;
  p.bleCount = ble;
  p.channel = view;  // "tool id" for the phone UI
  p.flags = kLinkTelemStatus |
            ((fleetActive || fleetListening) ? kLinkTelemFleetActive : 0) |
            (fleetCoordinator ? kLinkTelemFleetCoordinator : 0) |
            (fleetListening && !fleetActive ? kLinkTelemFleetListening : 0) |
            (fleetWardriveOn ? kLinkTelemFleetRunning : 0) |
            (gpsHasFix() ? kLinkTelemGpsFix : 0);
  p.code = fleetCode;
  const uint8_t sats = static_cast<uint8_t>(
      gps.satellites.isValid() ? gps.satellites.value() : 0);
  p.reserved = static_cast<uint16_t>(static_cast<uint8_t>(fleetMemberCount)) |
               (static_cast<uint16_t>(sats) << 8);
  const float lat = gps.location.isValid() ? static_cast<float>(gps.location.lat()) : 0.0f;
  const float lon = gps.location.isValid() ? static_cast<float>(gps.location.lng()) : 0.0f;
  memcpy(&p.sessionId, &lat, sizeof(lat));
  memcpy(&p.masterMillis, &lon, sizeof(lon));
  esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&p), sizeof(p));
#endif
}

// During fleet scanning the ordinary idle status path is not active. Send the
// selected screen chip's status while every radio is back on the rendezvous
// channel so the bridge/web UI retains live fleet membership and state.
void linkMaybeBroadcastRemoteStatus(uint32_t now) {
  static uint32_t lastRemoteFleetStatusMs = 0;
  if (!remoteActive || now - lastRemoteFleetStatusMs < 1000) return;
  lastRemoteFleetStatusMs = now;
  linkBroadcastStatus();
}

// Always-on from boot: keep ESP-NOW listening so the phone can drive this chip
// via the bridge. Parks on the rendezvous channel; re-homed there when idle.
void remoteBegin() {
  if (!linkEnsureEspNow()) {
    Serial.println("[remote] esp-now init failed; bridge control unavailable");
    return;
  }
  esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
  remoteActive = true;
  Serial.printf("[remote] bridge control active on ch %u\n", kLinkChannel);
}

void linkStartDiscovery() {
  if (!linkEnsureEspNow()) {
    drawLinkWardrive();
    return;
  }
  linkState = kLinkDiscovering;
  linkPeerValid = false;
  linkConfirmedLocal = false;
  linkRoleMaster = true;
  linkCode = 0;
  linkSessionId = 0;
  linkClockOffset = 0;
  lastLinkHelloMs = 0;
  esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
  recordFirmwareAudit("link", "pair_start", "success", "discovering");
  drawLinkWardrive();
}

void linkConfirm() {
  if (linkState != kLinkAwaitConfirm) return;
  linkConfirmedLocal = true;
  linkSendHello();  // advertise the confirmed flag immediately
  drawLinkWardrive();
}

void linkCancelPairing() {
  linkState = kLinkOff;
  linkPeerValid = false;
  linkConfirmedLocal = false;
  linkCode = 0;
}

void linkUnpair() {
  recordFirmwareAudit("link", "unpair", "success", "operator");
  if (linkEspNowReady && linkPeerValid && esp_now_is_peer_exist(linkPeerMac)) {
    esp_now_del_peer(linkPeerMac);
  }
  linkState = kLinkOff;
  linkPeerValid = false;
  linkConfirmedLocal = false;
  linkCode = 0;
  linkSessionId = 0;
  linkClockOffset = 0;
  linkPartnerNetworks = 0;
  linkPartnerBle = 0;
  linkPartnerRssi = -127;
}

// ---- Split Wardrive ------------------------------------------------------

int linkPlanSize(LinkPlan plan) {
  switch (plan) {
    case kLinkPlan24: return kLink24ChannelCount;
    case kLinkPlan5: return kLink5ChannelCount;
    default: return kLink24ChannelCount + kLink5ChannelCount;
  }
}

uint8_t linkPlanAt(LinkPlan plan, int idx) {
  if (plan == kLinkPlan24) return kLink24Channels[idx];
  if (plan == kLinkPlan5) return kLink5Channels[idx];
  return idx < kLink24ChannelCount ? kLink24Channels[idx]
                                   : kLink5Channels[idx - kLink24ChannelCount];
}

// Fills `plan` with this unit's channel set and returns true when that set is
// alternate-dealt by role parity (false = this unit scans the whole set):
//  - Solo: every channel the local board supports (whole set).
//  - Mixed pair (one C5 + one 2.4-only classic): partition by band so nothing
//    overlaps and nothing is dropped — the C5 takes ALL of 5 GHz, the 2.4-only
//    unit takes ALL of 2.4 GHz (whole set, no deal).
//  - Same-capability pair: alternate-deal the shared plan (two C5s split the
//    full dual-band plan; two classics split 2.4 GHz).
bool linkAssignedPlan(LinkPlan& plan) {
  const bool localDual = AwokPins::kDualBand;
  if (linkState != kLinkReady) {  // solo
    plan = localDual ? kLinkPlanFull : kLinkPlan24;
    return false;
  }
  if (localDual != linkPeerDualBand) {  // mixed pair: exclusive band, whole set
    plan = localDual ? kLinkPlan5 : kLinkPlan24;
    return false;
  }
  plan = localDual ? kLinkPlanFull : kLinkPlan24;  // same capability: deal it
  return true;
}

void fleetAssignedPlan(LinkPlan& plan) {
  if (!AwokPins::kDualBand) {
    plan = kLinkPlan24;
  } else {
    // WROOM/classic workers own 2.4 GHz whenever present; C5 workers then own
    // and evenly split 5 GHz. An all-C5 fleet evenly splits the full plan.
    plan = fleetHasClassicWifiWorker() ? kLinkPlan5 : kLinkPlanFull;
  }
}

// Denominator shown on the Split Wardrive screen: this unit's set size.
int linkPlanCount() {
  LinkPlan plan;
  if (fleetActive) fleetAssignedPlan(plan); else linkAssignedPlan(plan);
  return linkPlanSize(plan);
}

uint8_t linkNextAssignedChannel() {
  LinkPlan plan;
  const bool dealPair = fleetActive ? false : linkAssignedPlan(plan);
  if (fleetActive) fleetAssignedPlan(plan);
  const int count = linkPlanSize(plan);
  int m = 1, slice = 0;  // deal the plan m ways; take slice `slice`
  if (fleetActive) {
    m = fleetMyWifiWorkerCount();
    slice = fleetMyWifiSlice();
  } else if (dealPair) {
    m = 2;
    slice = linkRoleMaster ? 0 : 1;
  }
  for (int tries = 0; tries < count; ++tries) {
    const int idx = linkChannelCursor % count;
    linkChannelCursor = (linkChannelCursor + 1) % count;
    if (m <= 1 || (idx % m) == slice) return linkPlanAt(plan, idx);
  }
  return linkPlanAt(plan, 0);
}

int linkAssignedChannelCount() {
  LinkPlan plan;
  const bool dealPair = fleetActive ? false : linkAssignedPlan(plan);
  if (fleetActive) fleetAssignedPlan(plan);
  const int count = linkPlanSize(plan);
  int m = 1, slice = 0;
  if (fleetActive) {
    m = fleetMyWifiWorkerCount();
    slice = fleetMyWifiSlice();
  } else if (dealPair) {
    m = 2;
    slice = linkRoleMaster ? 0 : 1;
  }
  if (m <= 1) return count;  // solo/mixed: whole set
  int n = 0;
  for (int i = 0; i < count; ++i)
    if ((i % m) == slice) ++n;
  return n;
}

void linkStartNextScan() {
  linkScanChannel = linkNextAssignedChannel();
  // async, show_hidden, active scan, dwell, single channel.
  WiFi.scanNetworks(true, true, false, kLinkWardriveDwellMs, linkScanChannel);
}

void linkIngestScan(int result) {
  for (int i = 0; i < result; ++i) {
    uint8_t* bssid = WiFi.BSSID(i);
    if (!bssid) continue;
    if (!gpsHasFix() || wardriveMacSeen(bssid)) continue;
    wardriveAddMac(bssid);
    if (fleetActive && !fleetCoordinator) {
      // Worker: queue the row for the coordinator to merge (one CSV).
      fleetQueueOutRow(bssid, WiFi.SSID(i), WiFi.encryptionType(i),
                       WiFi.channel(i), WiFi.RSSI(i), false);
    } else {
      // Coordinator/solo: write + stream directly.
      appendWardriveRow(WiFi.BSSIDstr(i), WiFi.SSID(i), WiFi.encryptionType(i),
                        WiFi.channel(i), WiFi.RSSI(i));
    }
    ++wardriveNetworks;
  }
}

void startLinkWardrive() {
  stopActiveTools();  // release the previous tool before allocating this drive
  if (!linkEnsureEspNow()) {
    drawLinkWardrive();
    return;
  }
  if (!fleetBeginRowBuffers()) return;
  wardriveNetworks = 0;
  wardriveBleCount = 0;  // dedicated Fleet BLE nodes still scan below
  wardriveScans = 0;
  wardriveResetDedup();
  wardriveStartMs = millis();
  resetWardriveSessionStats(true);
  linkChannelCursor = 0;
  linkInWindow = false;
  linkWindowScanStopped = false;
  lastLinkWardriveDrawMs = 0;
  lastLinkTelemMs = 0;
  lastLinkSyncMs = 0;
  signalMonitorActive = false;

  WiFi.disconnect(false, false);
  wardriveCsvReady = (!fleetActive || fleetCoordinator) ? openWardriveCsv() : false;
  linkWardriveActive = true;
  recordFirmwareAudit(
      "link", "split_wardrive_start",
      wardriveCsvReady ? "success" : "degraded",
      String(linkState == kLinkReady ? "paired" : "solo") + "; session=" +
          String(static_cast<unsigned long>(linkSessionId)) + "; channels=" +
          String(linkAssignedChannelCount()));
  Serial.printf("[link] split wardrive started (%s, %d channels)\n",
                linkState == kLinkReady ? "paired" : "solo",
                linkAssignedChannelCount());
  drawLinkWardrive();
  esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
  linkBroadcastStatus();
}

void stopLinkWardrive() {
  linkWardriveActive = false;
  linkInWindow = false;
  WiFi.scanDelete();
  fleetStopBleScanOnly();
  fleetReleaseRowBuffers();
  closeWardriveCsv();
  esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
  linkBroadcastStatus();
  // Keep Wi-Fi STA resident; powering it down breaks the next radio bring-up.
  Serial.printf("[link] split wardrive stopped; %lu Wi-Fi networks\n",
                static_cast<unsigned long>(wardriveNetworks));
}

void updateLinkWardrive(uint32_t now) {
  if (fleetImBleNode()) { updateFleetBleNode(now); return; }
  const bool paired = (linkState == kLinkReady) || fleetActive;

  if (paired) {
    const uint32_t lnow = linkNow();
    const bool windowDue = (lnow % kLinkRendezvousMs) < kLinkWindowMs;
    if (windowDue) {
      int r = WiFi.scanComplete();
      // If an in-flight scan is still running 60ms into the window, cancel it
      // so the rendezvous window can proceed on kLinkChannel.
      if (r == WIFI_SCAN_RUNNING && (lnow % kLinkRendezvousMs) > 60) {
        WiFi.scanDelete();
        r = WIFI_SCAN_FAILED;
      }
      if (r == WIFI_SCAN_RUNNING) {
        // Let the in-flight scan finish if it completes in the first 60ms
      } else {
        if (r >= 0) {
          linkIngestScan(r);
          WiFi.scanDelete();
          ++wardriveScans;
        }
        if (!linkInWindow) {
          esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
          linkInWindow = true;
          lastLinkTelemMs = 0;
          lastLinkSyncMs = 0;
        }
        if (now - lastLinkTelemMs >= 60) {
          lastLinkTelemMs = now;
          if (fleetActive) {
            if (fleetCoordinator) {
              fleetSendAcks();
              fleetCoordDrainRows();
              if (now - lastFleetInviteMs >= kFleetInviteIntervalMs) {
                lastFleetInviteMs = now;
                fleetBroadcastInvite();
              }
              if (now - lastFleetRosterMs >= kFleetRosterIntervalMs) {
                lastFleetRosterMs = now;
                fleetBroadcastRoster();
              }
            } else {
              fleetWorkerSendRows();
            }
          } else {
            linkSendTelem();
          }
        }
        linkMaybeBroadcastRemoteStatus(now);
        if (!fleetActive && linkRoleMaster && now - lastLinkSyncMs >= 90) {
          lastLinkSyncMs = now;
          linkSendSync();
        }
      }
      if (currentView == View::kLinkWardrive &&
          now - lastLinkWardriveDrawMs >= kLinkWardriveRedrawMs) {
        lastLinkWardriveDrawMs = now;
        drawLinkWardrive();
      }
      return;
    }
    if (linkInWindow) linkInWindow = false;  // window just closed
  }

  const int result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) {
    // scan in progress: nothing to do this pass
  } else if (result >= 0) {
    linkIngestScan(result);
    WiFi.scanDelete();
    ++wardriveScans;
    const uint32_t rem = kLinkRendezvousMs - (linkNow() % kLinkRendezvousMs);
    if (!paired || rem >= (kLinkWardriveDwellMs + 40)) {
      linkStartNextScan();
    }
  } else {
    const uint32_t rem = kLinkRendezvousMs - (linkNow() % kLinkRendezvousMs);
    if (!paired || rem >= (kLinkWardriveDwellMs + 40)) {
      linkStartNextScan();
    }
  }

  static uint32_t lastLinkFlushMs = 0;
  if (now - lastLinkFlushMs >= 2000) {
    lastLinkFlushMs = now;
    flushWardriveCsv();
  }

  if (currentView == View::kLinkWardrive &&
      now - lastLinkWardriveDrawMs >= kLinkWardriveRedrawMs) {
    lastLinkWardriveDrawMs = now;
    drawLinkWardrive();
  }
}

void updateLink() {
  if (linkState == kLinkOff && !linkWardriveActive && !remoteActive &&
      !fleetActive && !fleetListening)
    return;
  linkDrainPackets();
  const uint32_t now = millis();

  // Fleet housekeeping: apply any roster the recv callback captured, and (as
  // coordinator) keep inviting + re-broadcasting the roster.
  if (fleetRosterPending) {
    fleetRosterPending = false;
    fleetApplyRoster(fleetPendingRoster);
  }
  if (fleetActive) {
    fleetCoordinatorTick(now);
    fleetCoordDrainRows();
  }

  // Remote-control housekeeping: drain commands (done above), keep ESP-NOW alive,
  // re-home to the rendezvous channel when idle so the bridge can reach us, and
  // stream status back to the phone about once a second.
  if (remoteActive && linkState == kLinkOff && !linkWardriveActive) {
    static uint32_t lastRemoteStatusMs = 0;
    // Whenever no radio-owning tool is running (Home, or a finished scan's
    // results view), keep ESP-NOW up and homed on the rendezvous channel so the
    // phone can always reach us -- including to leave a view a BLE tool left us
    // in after it shut Wi-Fi down. Active hopping tools manage the radio
    // themselves; we only re-home between them.
    if (!toolBlocksSerialShortcuts() && !scanInProgress && !wifiScanContinuous) {
      if (WiFi.getMode() != WIFI_STA) linkEnsureEspNow();  // a BLE tool tore it down
      esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
    }
    if (now - lastRemoteStatusMs >= 1000) {
      lastRemoteStatusMs = now;
      linkBroadcastStatus();
    }
    return;
  }

  // Pairing chatter: broadcast HELLO on the link channel while we look for /
  // confirm a peer (no scanning happens during pairing, so the link is solid).
  if (linkState == kLinkDiscovering || linkState == kLinkAwaitConfirm) {
    if (now - lastLinkHelloMs >= kLinkHelloIntervalMs) {
      lastLinkHelloMs = now;
      linkSendHello();
    }
  }

  if (linkWardriveActive) updateLinkWardrive(now);
}

// ---- UI ------------------------------------------------------------------

void openLinkWardrive() {
  fleetMenuOpen = false;  // explicit Split entry; active Fleet still owns its screen
  currentView = View::kLinkWardrive;
  linkEnsureEspNow();  // state shown even if it fails
  drawLinkWardrive();
}

// Human-readable role for a member row on the fleet screen.
static const char* fleetRoleLabel(int idx) {
  if (idx == fleetBleNodeIndex) return "BLE";
  const bool dual = (fleetMembers[idx].caps & kFleetCapDualBand) != 0;
  if (idx == fleetCoordIndex)
    return dual ? (fleetHasClassicWifiWorker() ? "c5G" : "cALL") : "c2.4";
  return dual ? (fleetHasClassicWifiWorker() ? "5GHz" : "dual") : "2.4G";
}

// Fleet entry menu (no session yet): choose Start (become coordinator) or Join.
void drawFleetMenu() {
  currentView = View::kLinkWardrive;
  display.fillScreen(kBackground);
  drawHeader("FLEET", linkEspNowReady ? "choose a role" : "ESP-NOW unavailable");
  display.setTextSize(1); display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(scaleX(8), scaleY(52)); display.print("One coordinator owns the merged CSV.");
  display.setCursor(scaleX(8), scaleY(66)); display.print("Workers share Wi-Fi / BLE scanning.");
  drawSmallButton(8, 100, 224, 44, "Start as coordinator", kAccent);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(8), scaleY(152)); display.print("Create a fleet and begin wardriving.");
  drawSmallButton(8, 184, 224, 44, "Join as worker", kAccent);
  display.setCursor(scaleX(8), scaleY(236)); display.print("Wait for a coordinator to invite you.");
  drawSmallButton(4, 280, 112, 36, "Back", kMuted);
  drawSmallButton(124, 280, 112, 36, "Home", kAccent);
}

// Live fleet view: session code, members + roles + row counts, aggregate.
void drawFleetStatus() {
  currentView = View::kLinkWardrive;
  display.fillScreen(kBackground);
  const bool joining = fleetListening && !fleetActive;
  drawHeader("FLEET", fleetCoordinator ? "coordinator"
                       : joining        ? "joining..."
                                        : "member");

  if (joining) {
    display.setTextSize(2);
    display.setTextColor(kWarn, kBackground);
    display.setCursor(scaleX(6), scaleY(60));
    display.print("LISTENING");
    display.setTextSize(1);
    display.setTextColor(ILI9341_WHITE, kBackground);
    display.setCursor(scaleX(6), scaleY(96));
    display.print("Waiting for a coordinator invite.");
    display.setCursor(scaleX(6), scaleY(108));
    display.print("Start a fleet on another chip.");
    drawFooter("Leave", "Home");
    return;
  }

#ifdef AWOK_MINI_DISPLAY
  drawWardriveDashboardBody(String(fleetMemberCount) + " nodes | " + (fleetCoordinator ? "coordinator" : "worker"));
#else
  display.setTextSize(1);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(46));
  display.print("Code");
  display.setTextSize(2);
  display.setTextColor(kAccent, kBackground);
  display.setCursor(scaleX(6), scaleY(58));
  char code[8];
  snprintf(code, sizeof(code), "%04u", fleetCode);
  display.print(code);

  display.setTextSize(1);
  display.setTextColor(fleetWardriveOn ? kGood : kWarn, kBackground);
  display.setCursor(scaleX(120), scaleY(52));
  display.print(fleetWardriveOn ? (wardriveStorageState() == 3 ? "SD ERROR" : gpsHasFix() ? "RUNNING" : "NO FIX") : "READY");
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(scaleX(120), scaleY(68));
  display.printf("Nodes: %d", fleetMemberCount);

  int y = 92;
  for (int i = 0; i < fleetMemberCount && i < kFleetMaxNodes; ++i) {
    const bool me = (i == fleetMyIndex);
    display.setTextColor(me ? ILI9341_WHITE : kMuted, kBackground);
    display.setCursor(scaleX(6), scaleY(y));
    display.printf("M%d %-5s %02x%02x %lu%s", i, fleetRoleLabel(i),
                   fleetMembers[i].mac[4], fleetMembers[i].mac[5],
                   static_cast<unsigned long>(fleetMembers[i].rows),
                   me ? " *" : "");
    y += 12;
  }

  display.setTextColor(kAccent, kBackground);
  display.setCursor(scaleX(6), scaleY(y + 6));
  display.printf("Agg: %lu wifi  %lu ble",
                 static_cast<unsigned long>(wardriveNetworks),
                 static_cast<unsigned long>(wardriveBleCount));
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(scaleX(6), scaleY(186)); display.print(gpsTimestamp());
  display.setCursor(scaleX(6), scaleY(200));
  display.printf("%s %.2fkm %lu/min", wardriveElapsedText().c_str(), wardriveStats.distanceM / 1000.0,
                 (unsigned long)wardriveStats.perMinute());
  display.setCursor(scaleX(6), scaleY(214));
  display.printf("GPS %d sat | fix %u%% | %s", gpsSats(), wardriveStats.fixPercent(), gpsHasFix() ? "OK" : "LOST");
  display.setTextColor(wardriveStorageState() == 3 || wardriveStorageState() == 0 ? kBad : kGood, kBackground);
  display.setCursor(scaleX(6), scaleY(230)); display.print(wardriveRecordingLabel());
  display.setCursor(scaleX(6), scaleY(244));
  if (fleetCoordinator) display.printf("SD %lu rows | flushed %lu", (unsigned long)wardriveStats.rows, (unsigned long)wardriveStats.flushedRows);
  else display.print("Merged CSV lives on coordinator");
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(260)); display.print(gpsTimezoneLabel());
#endif

  if (fleetCoordinator) {
    drawThreeButtonFooter("Home", "Leave", fleetWardriveOn ? "Stop" : "Go");
  } else {
    drawFooter("Leave", "Home");
  }
}

void drawLinkWardrive() {
  if (fleetActive || fleetListening) { drawFleetStatus(); return; }
  if (fleetMenuOpen) { drawFleetMenu(); return; }
  currentView = View::kLinkWardrive;
  display.fillScreen(kBackground);

  if (linkWardriveActive) {
    const bool paired = linkState == kLinkReady;
    drawHeader("SPLIT WD", paired ? "linked drive" : "solo (unpaired)");
    String context;
    if (!paired) context = "Solo | ch " + String(linkScanChannel) + " | Wi-Fi only";
    else if (millis() - linkPartnerLastSeenMs > kLinkPeerTimeoutMs) context = "PARTNER LOST | scanning continues";
    else context = "Peer " + String(linkPartnerNetworks) + " AP | " + String(linkPartnerRssi) + "dBm | ch " + String(linkScanChannel);
    drawWardriveDashboardBody(context);
    drawFooter("Stop", "Home");
    return;
  }

  // Not driving: pairing / idle screens.
  if (linkState == kLinkAwaitConfirm) {
    drawHeader("LINK", "confirm this code matches");
    display.setTextColor(kMuted, kBackground);
    display.setCursor(scaleX(6), scaleY(60));
    display.print("Both units should show the same");
    display.setCursor(scaleX(6), scaleY(72));
    display.print("4-digit code. Confirm on each.");
    display.setTextSize(4);
    display.setTextColor(kAccent, kBackground);
    char code[8];
    snprintf(code, sizeof(code), "%04u", linkCode);
    display.setCursor(scaleX(64), scaleY(110));
    display.print(code);
    display.setTextSize(1);
    display.setTextColor(linkConfirmedLocal ? kGood : kWarn, kBackground);
    display.setCursor(scaleX(6), scaleY(168));
    display.print(linkConfirmedLocal ? "Confirmed here; waiting for partner."
                                     : "Press Confirm when codes match.");
    display.setTextColor(kMuted, kBackground);
    display.setCursor(scaleX(6), scaleY(188));
    display.printf("You are the %s (lower MAC wins).",
                   linkRoleMaster ? "master" : "slave");
    drawSmallButton(4, 280, 112, 36, "Cancel", kMuted);
    drawSmallButton(124, 280, 112, 36, "Confirm", kAccent);
    return;
  }

  if (linkState == kLinkDiscovering) {
    drawHeader("LINK", "searching for a partner");
    display.setTextSize(2);
    display.setTextColor(kWarn, kBackground);
    display.setCursor(scaleX(6), scaleY(60));
    display.print("PAIRING...");
    display.setTextSize(1);
    display.setTextColor(ILI9341_WHITE, kBackground);
    display.setCursor(scaleX(6), scaleY(96));
    display.print("Put the other unit into Pair too.");
    display.setCursor(scaleX(6), scaleY(110));
    display.print("Both broadcast on channel 1.");
    display.setTextColor(kMuted, kBackground);
    display.setCursor(scaleX(6), scaleY(134));
    display.print("A 4-digit code appears once they");
    display.setCursor(scaleX(6), scaleY(146));
    display.print("find each other.");
    drawSmallButton(4, 280, 232, 36, "Cancel pairing", kMuted);
    return;
  }

  if (linkState == kLinkReady) {
    drawHeader("LINK", "paired");
    display.setTextSize(2);
    display.setTextColor(kGood, kBackground);
    display.setCursor(scaleX(6), scaleY(56));
    display.print("PAIRED");
    display.setTextSize(1);
    display.setTextColor(ILI9341_WHITE, kBackground);
    display.setCursor(scaleX(6), scaleY(92));
    display.printf("Role: %s", linkRoleMaster ? "master" : "slave");
    display.setCursor(scaleX(6), scaleY(106));
    display.printf("Session: %lu", static_cast<unsigned long>(linkSessionId));
    display.setCursor(scaleX(6), scaleY(120));
    display.printf("My set: %d of %d channels", linkAssignedChannelCount(),
                   linkPlanCount());
    display.setTextColor(kMuted, kBackground);
    display.setCursor(scaleX(6), scaleY(144));
    display.print("Start launches the split wardrive.");
    display.setCursor(scaleX(6), scaleY(156));
    display.print("Each unit logs its own WiGLE CSV.");
    drawSmallButton(8, 216, 224, 44, "Start split wardrive", kAccent);
    drawSmallButton(4, 280, 112, 36, "Back", kMuted);
    drawSmallButton(124, 280, 112, 36, "Unpair", kWarn);
    return;
  }

  // kLinkOff — unpaired Split setup. Pairing remains separate from Fleet.
  drawHeader("SPLIT", linkEspNowReady ? "two boards / separate CSVs" : "ESP-NOW unavailable");
  display.setTextSize(1); display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(scaleX(8), scaleY(52)); display.print("Pair two boards to divide Wi-Fi");
  display.setCursor(scaleX(8), scaleY(66)); display.print("channels. Each board uses its GPS.");
  drawSmallButton(8, 100, 224, 44, "Pair boards", kAccent);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(8), scaleY(152)); display.print("Confirm the same code on both units.");
  drawSmallButton(8, 184, 224, 44, "Wi-Fi only / unpaired", kAccent);
  display.setCursor(scaleX(8), scaleY(236)); display.print("For Wi-Fi + BLE, choose Solo mode.");
  drawSmallButton(4, 280, 112, 36, "Back", kMuted);
  drawSmallButton(124, 280, 112, 36, "Home", kAccent);
}

void handleLinkWardriveTouch(int x, int y) {
  if (fleetActive || fleetListening) {
    // Existing active-session semantics: Home leaves Fleet running; Leave exits.
    if (fleetCoordinator && !(fleetListening && !fleetActive)) {
      if (gpsMenuHit(x, y, 4, 284, 72, 30)) drawHome();
      else if (gpsMenuHit(x, y, 84, 284, 72, 30)) { fleetLeave(); openDriveMenu(); }
      else if (gpsMenuHit(x, y, 164, 284, 72, 30)) {
        if (fleetWardriveOn) fleetStopWardrive(); else fleetStartWardrive();
        drawLinkWardrive();
      }
    } else {
      if (gpsMenuHit(x, y, 4, 284, 112, 30)) { fleetLeave(); openDriveMenu(); }
      else if (gpsMenuHit(x, y, 124, 284, 112, 30)) drawHome();
    }
    return;
  }
  if (fleetMenuOpen) {
    if (gpsMenuHit(x, y, 8, 100, 224, 44)) { fleetStartWardrive(); drawLinkWardrive(); }
    else if (gpsMenuHit(x, y, 8, 184, 224, 44)) { fleetArm(); drawLinkWardrive(); }
    else if (gpsMenuHit(x, y, 4, 280, 112, 36)) { fleetMenuOpen = false; openDriveMenu(); }
    else if (gpsMenuHit(x, y, 124, 280, 112, 36)) { fleetMenuOpen = false; drawHome(); }
    return;
  }
  if (linkWardriveActive) {
    if (gpsMenuHit(x, y, 4, 284, 112, 30)) { stopLinkWardrive(); drawLinkWardrive(); }
    else if (gpsMenuHit(x, y, 124, 284, 112, 30)) { stopLinkWardrive(); drawHome(); }
    return;
  }
  if (linkState == kLinkDiscovering) {
    if (gpsMenuHit(x, y, 4, 280, 232, 36)) { linkCancelPairing(); drawLinkWardrive(); }
    return;
  }
  if (linkState == kLinkAwaitConfirm) {
    if (gpsMenuHit(x, y, 4, 280, 112, 36)) { linkCancelPairing(); drawLinkWardrive(); }
    else if (gpsMenuHit(x, y, 124, 280, 112, 36)) linkConfirm();
    return;
  }
  if (gpsMenuHit(x, y, 4, 280, 112, 36)) { openDriveMenu(); return; }
  if (linkState == kLinkReady) {
    if (gpsMenuHit(x, y, 8, 216, 224, 44)) startLinkWardrive();
    else if (gpsMenuHit(x, y, 124, 280, 112, 36)) { linkUnpair(); drawLinkWardrive(); }
  } else {
    if (gpsMenuHit(x, y, 8, 100, 224, 44)) linkStartDiscovery();
    else if (gpsMenuHit(x, y, 8, 184, 224, 44)) startLinkWardrive();
    else if (gpsMenuHit(x, y, 124, 280, 112, 36)) drawHome();
  }
}
