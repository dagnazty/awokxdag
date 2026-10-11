// bridge_ble.ino — BLE GATT server for the headless orange bridge chip.
//
// Compiled ONLY under AWOK_HEADLESS (C5 or classic ESP32 bridge profiles). The bridge
// runs the same full firmware as the white chip, but with no screen; the phone
// drives it directly over BLE. Each command carries a target byte:
//   kTargetBridge -> run the tool locally on this chip (linkDispatchCommand)
//   kTargetScreen -> relay over ESP-NOW to the white screen chip (channel sweep)
// Telemetry/results from the bridge's own tools notify the phone directly; those
// relayed back from the white chip arrive over ESP-NOW and are forwarded too.
#ifdef AWOK_HEADLESS
#include <atomic>

static const char* kBridgeSvcUuid     = "a0d10000-1234-4a3c-8b21-000000000001";
static const char* kBridgeCmdUuid     = "a0d10000-1234-4a3c-8b21-000000000002";  // write
static const char* kBridgeStatusUuid  = "a0d10000-1234-4a3c-8b21-000000000003";  // notify
static const char* kBridgeResultsUuid = "a0d10000-1234-4a3c-8b21-000000000004";  // notify

NimBLECharacteristic* g_bridgeStatus = nullptr;
NimBLECharacteristic* g_bridgeResults = nullptr;
volatile bool g_bridgePhoneConnected = false;
static std::atomic<uint16_t> g_bridgeMtu{23};

// Relay a command to the white chip over ESP-NOW. The screen chip parks on the
// rendezvous channel when idle and hops across the plan while a tool runs, so:
//   1) burst on the rendezvous channel first -- an idle screen chip (the common
//      case: starting a tool, or anything issued from Home/a results view) gets
//      it instantly with no channel hopping at all;
//   2) then a short channel sweep as the fallback for a tool that is actively
//      hopping (e.g. Stop mid-scan).
// A per-command sequence number lets the screen act on it exactly once despite
// the burst + sweep repeats. Cutting 3 sweeps -> 2 and the dwell 3 ms -> 2 ms,
// plus the rendezvous burst, roughly halves the relay's on-air time and radio
// churn while keeping delivery reliable.
static const uint8_t kBridgeRvBurst = 4;      // rendezvous-channel sends first
static const uint8_t kBridgeSweeps = 2;       // fallback full-plan passes
static const uint32_t kBridgeChanDwellMs = 2; // per-channel dwell in a sweep
static uint16_t g_bridgeSeq = 0;

// Zero throughout the command burst/sweep. Only publish the nonce once the
// receiver is back on the rendezvous channel, so early file data cannot be lost.
static std::atomic<uint32_t> g_bridgeFileReadyToken{0};

static std::atomic<bool> g_bridgeReliableFile{false};
static std::atomic<uint32_t> g_bridgeFileActivityMs{0}, g_bridgeFileDoneSeq{0};
// Single producer (Wi-Fi callback), single consumer (Arduino main loop).
static AxdFileChunkMsg g_bridgeFileQueue[4];
static std::atomic<unsigned> g_bridgeFileHead{0}, g_bridgeFileTail{0};

bool bridgeFileTransferActive() {
  return g_bridgeReliableFile.load() && g_bridgePhoneConnected &&
         millis() - g_bridgeFileActivityMs.load() < 12000;
}

void bridgeQueueFileChunk(const AxdFileChunkMsg& chunk) {
  if (!g_bridgeReliableFile.load() || chunk.token != g_bridgeFileReadyToken.load() ||
      chunk.token == 0 || chunk.kind > 5 || chunk.seq == 0) return;
  const unsigned head = g_bridgeFileHead.load();
  const unsigned next = (head + 1) % 4;
  if (next == g_bridgeFileTail.load()) return;  // sender will retry; never ACK here
  g_bridgeFileQueue[head] = chunk;
  g_bridgeFileQueue[head].data[sizeof(chunk.data) - 1] = 0;
  g_bridgeFileHead.store(next);
  g_bridgeFileActivityMs.store(millis());
}

void bridgeServiceFileTransfer() {
  const unsigned tail = g_bridgeFileTail.load();
  if (tail == g_bridgeFileHead.load()) return;
  const AxdFileChunkMsg chunk = g_bridgeFileQueue[tail];
  g_bridgeFileTail.store((tail + 1) % 4);
  if (chunk.token != g_bridgeFileReadyToken.load() || !g_bridgePhoneConnected) return;
  if (chunk.kind == 1 || chunk.kind == 2 || chunk.kind == 4) g_bridgeFileDoneSeq.store(chunk.seq);
  char line[220];
  snprintf(line, sizeof(line), "$FILECHUNK,%lu,%lu,%lu,%u,%s",
           static_cast<unsigned long>(chunk.token), static_cast<unsigned long>(chunk.seq),
           static_cast<unsigned long>(chunk.totalBytes), chunk.kind, chunk.data);
  // BLE work is deliberately outside the Wi-Fi receive callback. The screen
  // retries if this notification is dropped or the browser cannot acknowledge.
  if (!bridgeNotifyResult(kSourceFiles, reinterpret_cast<const uint8_t*>(line), strlen(line))) {
    Serial.printf("[files] BLE enqueue failed: token=%lu seq=%lu bytes=%u mtu=%u\n",
                  static_cast<unsigned long>(chunk.token), static_cast<unsigned long>(chunk.seq),
                  static_cast<unsigned>(strlen(line) + 1), g_bridgeMtu.load());
  }
}

static void bridgeAckFileChunk(const uint8_t* data, size_t len) {
  if (len != 9) return;
  AxdFileChunkAck ack;
  for (int i = 0; i < 4; ++i) {
    ack.token |= static_cast<uint32_t>(data[1 + i]) << (8 * i);
    ack.seq |= static_cast<uint32_t>(data[5 + i]) << (8 * i);
  }
  if (ack.token == 0 || ack.seq == 0 || ack.token != g_bridgeFileReadyToken.load()) return;
  g_bridgeFileActivityMs.store(millis() - (ack.seq == g_bridgeFileDoneSeq.load() ? 10000 : 0));
  // No channel sweep for an ACK: both radios remain parked for this transfer.
  esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&ack), sizeof(ack));
}

bool bridgeFileReceiverReady(uint32_t token) {
  return token != 0 && g_bridgePhoneConnected &&
         g_bridgeFileReadyToken.load() == token;
}

static void bridgeRelayToScreen(uint8_t op, uint8_t arg, uint32_t fileToken,
                                uint32_t startSeq, uint32_t snapshotBytes, uint32_t checksum) {
  if (!linkEnsureEspNow()) return;
  g_bridgeFileReadyToken.store(0);
  g_bridgeReliableFile.store(op == kAxdCmdFileGetReliable || op == kAxdCmdFileGetVerified);
  g_bridgeFileDoneSeq.store(0);
  g_bridgeFileActivityMs.store(millis());
  g_bridgeSeq++;
  LinkPacket p;
  p.type = kLinkMsgCommand;
  p.code = g_bridgeSeq;
  p.reserved = static_cast<uint16_t>(op) | (static_cast<uint16_t>(arg) << 8);
  memcpy(p.srcMac, linkSelfMac, 6);
  const bool fileReply = op == kAxdCmdFileList || op == kAxdCmdFileGet ||
                         op == kAxdCmdFileGetReliable || op == kAxdCmdFileGetVerified || op == kAxdCmdFileDelete;
  if (fileReply) {
    p.flags |= kLinkCommandWaitFileReady;
    p.sessionId = (op == kAxdCmdFileGetReliable || op == kAxdCmdFileGetVerified) ? fileToken : 0;
    if (op == kAxdCmdFileGetVerified) {
      p.masterMillis = startSeq; p.networks = snapshotBytes; p.bleCount = checksum;
    }
    while (p.sessionId == 0) p.sessionId = esp_random();
  }

  esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
  for (uint8_t i = 0; i < kBridgeRvBurst; ++i) {
    esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&p), sizeof(p));
    delay(2);
  }
  for (uint8_t pass = 0; pass < kBridgeSweeps; ++pass) {
    for (int i = 0; i < kLink24ChannelCount; ++i) {
      esp_wifi_set_channel(kLink24Channels[i], WIFI_SECOND_CHAN_NONE);
      esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&p), sizeof(p));
      delay(kBridgeChanDwellMs);
    }
#ifndef AWOK_CLASSIC_ESP32
    for (int i = 0; i < kLink5ChannelCount; ++i) {
      esp_wifi_set_channel(kLink5Channels[i], WIFI_SECOND_CHAN_NONE);
      esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&p), sizeof(p));
      delay(kBridgeChanDwellMs);
    }
#endif
  }
  const esp_err_t parked = esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
  if (fileReply && parked == ESP_OK) g_bridgeFileReadyToken.store(p.sessionId);
}

// Push one status/result blob to the phone with a source tag prefixed.
void bridgeNotifyStatus(uint8_t source, const uint8_t* body, size_t len) {
  if (!g_bridgeStatus || !g_bridgePhoneConnected) return;
  uint8_t blob[1 + 32];
  blob[0] = source;
  size_t n = len < sizeof(blob) - 1 ? len : sizeof(blob) - 1;
  memcpy(blob + 1, body, n);
  g_bridgeStatus->setValue(blob, n + 1);
  g_bridgeStatus->notify(blob, n + 1);
}

bool bridgeNotifyResult(uint8_t source, const uint8_t* body, size_t len) {
  if (!g_bridgeResults || !g_bridgePhoneConnected) return false;
  uint8_t blob[1 + 240];  // fits full WiGLE CSV row or base64 file data chunk
  blob[0] = source;
  size_t n = len < sizeof(blob) - 1 ? len : sizeof(blob) - 1;
  memcpy(blob + 1, body, n);
  // notify() with no payload only schedules a characteristic update. Another
  // producer (Wi-Fi callback or loop) can replace that value before BLE reads it.
  // The explicit-payload overload copies this packet into its own NimBLE mbuf.
  return g_bridgeResults->notify(blob, n + 1);
}

// A command may kick off a multi-second blocking scan; running that (and the
// result notifications) inside the BLE host-task onWrite callback would stall
// the stack and drop most notifications. So onWrite only latches the command and
// loop() (the Arduino loopTask) services it -- the host task stays free to drain
// notifications, exactly like the screen chip's queue-then-process path.
volatile uint8_t g_bridgePendOp = 0, g_bridgePendArg = 0, g_bridgePendTarget = 0;
volatile bool g_bridgeCmdPending = false;
volatile uint32_t g_bridgePendFileToken = 0;
volatile uint32_t g_bridgePendStartSeq = 0, g_bridgePendSnapshotBytes = 0, g_bridgePendCrc = 0;

class BridgeCmdCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    const NimBLEAttValue v = c->getValue();
    if (v.length() == 0) return;
    if (v.data()[0] == kAxdCmdFileChunkAck) {
      bridgeAckFileChunk(v.data(), v.length());
      return;
    }
    g_bridgePendFileToken = 0;
    g_bridgePendStartSeq = g_bridgePendSnapshotBytes = g_bridgePendCrc = 0;
    if (v.data()[0] == kAxdCmdFileGetReliable || v.data()[0] == kAxdCmdFileGetVerified) {
      const bool verified = v.data()[0] == kAxdCmdFileGetVerified;
      if (v.length() != (verified ? 19 : 7)) return;
      uint32_t token = 0;
      for (int i = 0; i < 4; ++i) token |= static_cast<uint32_t>(v.data()[3 + i]) << (8 * i);
      if (token == 0) return;
      g_bridgePendFileToken = token;
      if (verified) {
        uint32_t start = 0, size = 0, crc = 0;
        for (int i = 0; i < 4; ++i) {
          start |= static_cast<uint32_t>(v.data()[7 + i]) << (8 * i);
          size |= static_cast<uint32_t>(v.data()[11 + i]) << (8 * i);
          crc |= static_cast<uint32_t>(v.data()[15 + i]) << (8 * i);
        }
        g_bridgePendStartSeq = start; g_bridgePendSnapshotBytes = size; g_bridgePendCrc = crc;
      }
    }
    g_bridgePendOp = v.data()[0];
    g_bridgePendArg = v.length() > 1 ? v.data()[1] : 0;
    g_bridgePendTarget = v.length() > 2
                             ? v.data()[2]
                             : static_cast<uint8_t>(kTargetBridge);
    g_bridgeCmdPending = true;  // serviced on the main loop
  }
};

// Called from loop(): run any latched command off the BLE host task.
void bridgeServiceCommand() {
  if (!g_bridgeCmdPending) return;
  g_bridgeCmdPending = false;
  const uint8_t op = g_bridgePendOp, arg = g_bridgePendArg;
  const uint32_t fileToken = g_bridgePendFileToken;
  const uint32_t startSeq = g_bridgePendStartSeq, snapshotBytes = g_bridgePendSnapshotBytes, checksum = g_bridgePendCrc;
  uint8_t target = g_bridgePendTarget;
  // SD card is physically on the Screen chip; always relay file commands to screen
  if (op == kAxdCmdFileList || op == kAxdCmdFileGet || op == kAxdCmdFileGetReliable || op == kAxdCmdFileGetVerified || op == kAxdCmdFileDelete ||
      op == kAxdCmdFileAbort || op == kAxdCmdFiles) {
    target = kTargetScreen;
  }
  Serial.printf("[bridge] cmd op=%u arg=%u target=%u\n", op, arg, target);
  if (target == kTargetScreen) {
    bridgeRelayToScreen(op, arg, fileToken, startSeq, snapshotBytes, checksum);
  } else {
    linkDispatchCommand(op, arg);  // run on this chip
  }
}

class BridgeServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
    g_bridgePhoneConnected = true;
    g_bridgeMtu.store(info.getMTU());
    s->updateConnParams(info.getConnHandle(), 12, 12, 0, 200);
    Serial.println("[bridge] phone connected");
  }
  void onMTUChange(uint16_t mtu, NimBLEConnInfo&) override {
    g_bridgeMtu.store(mtu);
    Serial.printf("[bridge] BLE MTU=%u, notification payload=%u\n", mtu, mtu - 3);
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override {
    g_bridgePhoneConnected = false;
    g_bridgeFileReadyToken.store(0);
    Serial.printf("[bridge] phone disconnected (%d); re-advertising\n", reason);
    NimBLEDevice::startAdvertising();
  }
};

void bridgeBleBegin() {
  if (!NimBLEDevice::isInitialized()) NimBLEDevice::init("AxD-Bridge");
  NimBLEDevice::setPower(3);
  NimBLEDevice::setMTU(247);  // fewer fragments -> faster result/CSV streaming
  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new BridgeServerCallbacks());
  NimBLEService* svc = server->createService(kBridgeSvcUuid);
  NimBLECharacteristic* cmd = svc->createCharacteristic(
      kBridgeCmdUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  cmd->setCallbacks(new BridgeCmdCallbacks());
  g_bridgeStatus = svc->createCharacteristic(
      kBridgeStatusUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  g_bridgeStatus->setValue("ready");
  g_bridgeResults = svc->createCharacteristic(
      kBridgeResultsUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  svc->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  // The 128-bit service UUID and full name do not fit together in a 31-byte
  // legacy advertisement. Keep the UUID in the advertisement and put the name
  // in the scan response so Web Bluetooth can discover both.
  adv->enableScanResponse(true);
  adv->addServiceUUID(svc->getUUID());
  adv->setName("AxD-Bridge");
  adv->start();
  Serial.println("[bridge] BLE GATT server up ('AxD-Bridge')");
}

#endif  // AWOK_HEADLESS
