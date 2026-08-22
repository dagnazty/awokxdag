// AWOKxDAG — BLE Tracker Scan (compiled as part of the sketch; see awok_common.h)
//
// Continuous passive BLE scan that flags personal item trackers: Apple Find My
// / AirTags broadcasting in the separated ("offline finding") state, Tile tags,
// and Samsung SmartTags. Each address is tracked over time; a tracker seen
// repeatedly across a long enough span while you move is flagged as possibly
// FOLLOWING you -- the planted-tracker privacy case. Passive: never transmits.

TrackerEntry trackerEntries[kMaxTrackers];
int trackerCount = 0;
TrackerHit trackerHitQueue[kTrackerHitQueueSlots];
volatile int trackerHitHead = 0;
volatile int trackerHitTail = 0;
uint32_t trackerScanStartMs = 0;
uint32_t lastTrackerDrawMs = 0;
int trackerFollowingCount = 0;

const char* trackerKindName(uint8_t kind) {
  switch (kind) {
    case 1: return "AirTag";
    case 2: return "Tile";
    case 3: return "SmartTag";
    default: return "-";
  }
}

// Classify an advertisement as a known tracker; 0 = not a tracker.
uint8_t trackerClassify(const NimBLEAdvertisedDevice* device) {
  if (device->haveManufacturerData()) {
    const std::string mfg = device->getManufacturerData();
    if (mfg.length() >= 3) {
      const uint16_t company = static_cast<uint8_t>(mfg[0]) |
                               (static_cast<uint16_t>(
                                    static_cast<uint8_t>(mfg[1]))
                                << 8);
      // Apple offline-finding (Find My) advert: company 0x004C, type 0x12.
      // This is the separated-from-owner state an AirTag/Find My accessory
      // uses, i.e. the one worth flagging -- not every nearby Apple device.
      if (company == 0x004C && static_cast<uint8_t>(mfg[2]) == 0x12) return 1;
    }
  }
  for (int i = 0; i < device->getServiceUUIDCount(); ++i) {
    const NimBLEUUID uuid = device->getServiceUUID(i);
    if (uuid == NimBLEUUID(static_cast<uint16_t>(0xFEED)) ||
        uuid == NimBLEUUID(static_cast<uint16_t>(0xFEEC))) {
      return 2;  // Tile
    }
  }
  for (int i = 0; i < device->getServiceDataCount(); ++i) {
    const NimBLEUUID uuid = device->getServiceDataUUID(i);
    if (uuid == NimBLEUUID(static_cast<uint16_t>(0xFEED)) ||
        uuid == NimBLEUUID(static_cast<uint16_t>(0xFEEC))) {
      return 2;  // Tile (service data)
    }
    if (uuid == NimBLEUUID(static_cast<uint16_t>(0xFD5A))) {
      return 3;  // Samsung SmartTag / SmartThings Find
    }
  }
  return 0;
}

class TrackerScanCallbacks : public NimBLEScanCallbacks {
 public:
  void onResult(const NimBLEAdvertisedDevice* device) override {
    const uint8_t kind = trackerClassify(device);
    if (!kind) return;
    const int next = (trackerHitHead + 1) % kTrackerHitQueueSlots;
    if (next == trackerHitTail) return;  // queue full: drop
    TrackerHit& hit = trackerHitQueue[trackerHitHead];
    const std::string addr = device->getAddress().toString();
    const int n = min(static_cast<int>(addr.length()),
                      static_cast<int>(sizeof(hit.addr)) - 1);
    memcpy(hit.addr, addr.c_str(), n);
    hit.addr[n] = 0;
    hit.rssi = device->getRSSI();
    hit.kind = kind;
    trackerHitHead = next;
  }
};

TrackerScanCallbacks trackerScanCallbacks;

int trackerIndexOf(const char* addr) {
  for (int i = 0; i < trackerCount; ++i) {
    if (trackerEntries[i].addr.equals(addr)) return i;
  }
  return -1;
}

void mergeTrackerHit(const TrackerHit& hit) {
  const uint32_t now = millis();
  int index = trackerIndexOf(hit.addr);
  if (index < 0) {
    if (trackerCount >= kMaxTrackers) return;
    index = trackerCount++;
    trackerEntries[index] = TrackerEntry();
    trackerEntries[index].addr = String(hit.addr);
    trackerEntries[index].firstSeenMs = now;
  }
  TrackerEntry& entry = trackerEntries[index];
  entry.kind = hit.kind;
  entry.rssi = hit.rssi;
  entry.lastSeenMs = now;
  ++entry.sightings;
  entry.following = (entry.lastSeenMs - entry.firstSeenMs) >= kTrackerFollowMs &&
                    entry.sightings >= kTrackerMinSightings;
}

void sortTrackers() {
  // Following trackers first, then by strongest signal.
  for (int i = 0; i < trackerCount - 1; ++i) {
    for (int j = i + 1; j < trackerCount; ++j) {
      const bool swap =
          (trackerEntries[j].following && !trackerEntries[i].following) ||
          (trackerEntries[j].following == trackerEntries[i].following &&
           trackerEntries[j].rssi > trackerEntries[i].rssi);
      if (swap) {
        TrackerEntry temporary = trackerEntries[i];
        trackerEntries[i] = trackerEntries[j];
        trackerEntries[j] = temporary;
      }
    }
  }
}

bool exportTrackersToSd() {
  if (!ensureSdCard()) return false;
  const String temporaryPath = String(kTrackerCsvPath) + ".tmp";
  SD.remove(temporaryPath.c_str());
  File file = SD.open(temporaryPath.c_str(), FILE_WRITE);
  if (!file) {
    sdReady = false;
    return false;
  }
  file.println(
      "uptime_ms,address,kind,rssi,first_seen_ms,last_seen_ms,sightings,"
      "following,latitude,longitude,altitude_m");
  const uint32_t uptime = millis();
  const String location = gpsCsvFields();
  for (int i = 0; i < trackerCount; ++i) {
    file.print(uptime);
    file.print(',');
    file.print(csvField(trackerEntries[i].addr));
    file.print(',');
    file.print(trackerKindName(trackerEntries[i].kind));
    file.print(',');
    file.print(trackerEntries[i].rssi);
    file.print(',');
    file.print(trackerEntries[i].firstSeenMs);
    file.print(',');
    file.print(trackerEntries[i].lastSeenMs);
    file.print(',');
    file.print(trackerEntries[i].sightings);
    file.print(',');
    file.print(trackerEntries[i].following ? "yes" : "no");
    file.println(location);
  }
  file.flush();
  const bool ok = file.getWriteError() == 0;
  file.close();
  if (!ok) {
    SD.remove(temporaryPath.c_str());
    return false;
  }
  SD.remove(kTrackerCsvPath);
  if (!SD.rename(temporaryPath.c_str(), kTrackerCsvPath)) return false;
  Serial.printf("[tracker] wrote %d row(s) to %s\n", trackerCount,
                kTrackerCsvPath);
  return true;
}

void drawTrackerScan() {
  currentView = View::kTrackerScan;
  display.fillScreen(kBackground);
  sortTrackers();
  trackerFollowingCount = 0;
  for (int i = 0; i < trackerCount; ++i) {
    if (trackerEntries[i].following) ++trackerFollowingCount;
  }
  drawHeader("BLE TRACKERS",
             trackerFollowingCount
                 ? "ALERT: " + String(trackerFollowingCount) + " may follow you"
                 : String(trackerCount) + " tracker(s) nearby");
  display.setTextSize(1);
  const int rows = min(trackerCount, kVisibleRows);
  const uint32_t now = millis();
  for (int i = 0; i < rows; ++i) {
    const int y = 48 + i * 22;
    display.setTextColor(trackerEntries[i].following ? kBad : ILI9341_WHITE,
                         kBackground);
    display.setCursor(5, y);
    display.printf("%-8s %s", trackerKindName(trackerEntries[i].kind),
                   trackerEntries[i].addr.c_str());
    display.setTextColor(trackerEntries[i].following ? kBad : kMuted,
                         kBackground);
    display.setCursor(5, y + 11);
    const uint32_t span =
        (now - trackerEntries[i].firstSeenMs) / 1000;  // seconds seen
    display.printf("%4ld dBm  %lus  x%lu %s",
                   static_cast<long>(trackerEntries[i].rssi),
                   static_cast<unsigned long>(span),
                   static_cast<unsigned long>(trackerEntries[i].sightings),
                   trackerEntries[i].following ? "FOLLOW" : "");
  }
  if (trackerCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(20, 145);
    display.print("Scanning for item trackers...");
  }
  drawFooter("Back", lastTrackerCsvOk ? "Saved" : "Save");
}

void startTrackerScan() {
  trackerCount = 0;
  trackerHitHead = 0;
  trackerHitTail = 0;
  trackerFollowingCount = 0;
  trackerScanStartMs = millis();
  lastTrackerDrawMs = 0;
  lastTrackerCsvOk = false;
  signalMonitorActive = false;

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&trackerScanCallbacks, false);
  scan->setActiveScan(false);
  scan->setInterval(80);
  scan->setWindow(80);
  scan->clearResults();
  scan->start(0, false, true);

  trackerScanActive = true;
  Serial.println("[tracker] BLE tracker scan started");
  drawTrackerScan();
}

void stopTrackerScan() {
  trackerScanActive = false;
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->stop();
  scan->clearResults();
  lastTrackerCsvOk = exportTrackersToSd();
  Serial.printf("[tracker] stopped; %d tracker(s)\n", trackerCount);
}

void updateTrackerScan() {
  if (!trackerScanActive || currentView != View::kTrackerScan) return;
  while (trackerHitTail != trackerHitHead) {
    mergeTrackerHit(trackerHitQueue[trackerHitTail]);
    trackerHitTail = (trackerHitTail + 1) % kTrackerHitQueueSlots;
  }
  const uint32_t now = millis();
  if (now - lastTrackerDrawMs >= kTrackerRedrawMs) {
    lastTrackerDrawMs = now;
    drawTrackerScan();
  }
}
