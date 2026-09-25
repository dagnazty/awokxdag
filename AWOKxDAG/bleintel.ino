// AWOKxDAG — BLE Ecosystem Intel & Continuity Decoder
// (compiled as part of the sketch; see awok_common.h)
//
// Continuous passive BLE scan that decodes proprietary vendor ecosystem
// payloads: Apple Continuity (AirPods battery levels/charging, AirDrop, Nearby Info,
// Find My), Android / Google Fast Pair (model ID and state), Microsoft Swift Pair,
// and Samsung Continuity / SmartThings.
// Passive: never transmits.

ToolBuffer<BleIntelEntry, kMaxBleIntel> bleIntelEntries;
int bleIntelCount = 0;
ToolQueue<BleIntelHit, kBleIntelHitQueueSlots> bleIntelHitQueue;
uint32_t bleIntelStartMs = 0;
uint32_t lastBleIntelDrawMs = 0;

const char* bleIntelEcosystemName(uint8_t eco) {
  switch (eco) {
    case kBleEcoApple: return "Apple";
    case kBleEcoGoogle: return "Google";
    case kBleEcoMicrosoft: return "Microsoft";
    case kBleEcoSamsung: return "Samsung";
    default: return "Unknown";
  }
}

uint16_t bleIntelEcosystemColor(uint8_t eco) {
  switch (eco) {
    case kBleEcoApple: return ILI9341_CYAN;
    case kBleEcoGoogle: return ILI9341_YELLOW;
    case kBleEcoMicrosoft: return 0x5D1F;  // soft light blue
    case kBleEcoSamsung: return ILI9341_MAGENTA;
    default: return ILI9341_WHITE;
  }
}

const char* appleAudioModelName(uint8_t modelId) {
  switch (modelId) {
    case 0x02: return "AirPods 1";
    case 0x0F: return "AirPods 2";
    case 0x13: return "AirPods 3";
    case 0x0E: return "AirPods Pro 1";
    case 0x14: return "AirPods Pro 2";
    case 0x0A: return "AirPods Max";
    case 0x10: return "Powerbeats Pro";
    case 0x11: return "Beats Solo Pro";
    case 0x12: return "Beats Studio Buds";
    case 0x17: return "Beats Fit Pro";
    default: return "AirPods/Beats";
  }
}

// Classify an advertisement as a supported ecosystem device.
bool bleClassifyIntel(const NimBLEAdvertisedDevice* device, BleIntelHit& hit) {
  hit = BleIntelHit();
  const std::string addrStr = device->getAddress().toString();
  const int n = min(static_cast<int>(addrStr.length()),
                    static_cast<int>(sizeof(hit.addr)) - 1);
  memcpy(hit.addr, addrStr.c_str(), n);
  hit.addr[n] = 0;
  hit.rssi = device->getRSSI();

  // 1. Apple Continuity (Company ID 0x004C)
  if (device->haveManufacturerData()) {
    const std::string mfg = device->getManufacturerData();
    if (mfg.length() >= 3) {
      const uint16_t company = static_cast<uint8_t>(mfg[0]) |
                               (static_cast<uint16_t>(static_cast<uint8_t>(mfg[1])) << 8);
      if (company == 0x004C) {
        hit.ecosystem = kBleEcoApple;
        const uint8_t type = static_cast<uint8_t>(mfg[2]);

        if (type == 0x07) {
          // Proximity Pairing (AirPods / Beats)
          const uint8_t modelId = (mfg.length() >= 5) ? static_cast<uint8_t>(mfg[4]) : 0;
          strncpy(hit.deviceType, appleAudioModelName(modelId), sizeof(hit.deviceType) - 1);

          if (mfg.length() >= 8) {
            const uint8_t rawL = static_cast<uint8_t>(mfg[6]) & 0x0F;
            const uint8_t rawR = (static_cast<uint8_t>(mfg[6]) >> 4) & 0x0F;
            const uint8_t rawC = static_cast<uint8_t>(mfg[7]) & 0x0F;
            const bool chgL = (static_cast<uint8_t>(mfg[7]) & 0x10) != 0;
            const bool chgR = (static_cast<uint8_t>(mfg[7]) & 0x20) != 0;
            const bool chgC = (static_cast<uint8_t>(mfg[7]) & 0x40) != 0;

            if (rawL <= 10) hit.batteryLeft = rawL * 10;
            if (rawR <= 10) hit.batteryRight = rawR * 10;
            if (rawC <= 10) hit.batteryCase = rawC * 10;

            char lBuf[8] = "--", rBuf[8] = "--", cBuf[8] = "--";
            if (hit.batteryLeft >= 0) snprintf(lBuf, sizeof(lBuf), "%d%%%s", hit.batteryLeft, chgL ? "+" : "");
            if (hit.batteryRight >= 0) snprintf(rBuf, sizeof(rBuf), "%d%%%s", hit.batteryRight, chgR ? "+" : "");
            if (hit.batteryCase >= 0) snprintf(cBuf, sizeof(cBuf), "%d%%%s", hit.batteryCase, chgC ? "+" : "");

            snprintf(hit.details, sizeof(hit.details), "L:%s R:%s C:%s", lBuf, rBuf, cBuf);
          } else {
            snprintf(hit.details, sizeof(hit.details), "Pairing Audio");
          }
          return true;
        } else if (type == 0x05) {
          strncpy(hit.deviceType, "AirDrop", sizeof(hit.deviceType) - 1);
          snprintf(hit.details, sizeof(hit.details), "Active Transfer");
          return true;
        } else if (type == 0x10) {
          strncpy(hit.deviceType, "Nearby Info", sizeof(hit.deviceType) - 1);
          snprintf(hit.details, sizeof(hit.details), "Continuity Action");
          return true;
        } else if (type == 0x12) {
          strncpy(hit.deviceType, "Find My", sizeof(hit.deviceType) - 1);
          snprintf(hit.details, sizeof(hit.details), "Offline Beacon");
          return true;
        } else if (type == 0x09) {
          strncpy(hit.deviceType, "AirPlay Target", sizeof(hit.deviceType) - 1);
          snprintf(hit.details, sizeof(hit.details), "Audio / Video");
          return true;
        } else if (type == 0x0B) {
          strncpy(hit.deviceType, "Apple Watch", sizeof(hit.deviceType) - 1);
          snprintf(hit.details, sizeof(hit.details), "Watch Tethering");
          return true;
        } else if (type == 0x0C) {
          strncpy(hit.deviceType, "Handoff", sizeof(hit.deviceType) - 1);
          snprintf(hit.details, sizeof(hit.details), "Active App State");
          return true;
        } else {
          strncpy(hit.deviceType, "Apple Device", sizeof(hit.deviceType) - 1);
          snprintf(hit.details, sizeof(hit.details), "Continuity 0x%02X", type);
          return true;
        }
      } else if (company == 0x0006) {
        // Microsoft Swift Pair
        hit.ecosystem = kBleEcoMicrosoft;
        if (mfg.length() >= 3 && static_cast<uint8_t>(mfg[2]) == 0x03) {
          strncpy(hit.deviceType, "Swift Pair", sizeof(hit.deviceType) - 1);
          snprintf(hit.details, sizeof(hit.details), "Peripheral In Range");
        } else {
          strncpy(hit.deviceType, "Microsoft", sizeof(hit.deviceType) - 1);
          snprintf(hit.details, sizeof(hit.details), "Beacon");
        }
        return true;
      } else if (company == 0x0075) {
        // Samsung
        hit.ecosystem = kBleEcoSamsung;
        strncpy(hit.deviceType, "Samsung Galaxy", sizeof(hit.deviceType) - 1);
        snprintf(hit.details, sizeof(hit.details), "Continuity Beacon");
        return true;
      }
    }
  }

  // 2. Google Fast Pair (0xFE2C) & Samsung SmartThings (0xFD5A) via Service Data
  for (int i = 0; i < device->getServiceDataCount(); ++i) {
    const NimBLEUUID uuid = device->getServiceDataUUID(i);
    if (uuid == NimBLEUUID(static_cast<uint16_t>(0xFE2C))) {
      hit.ecosystem = kBleEcoGoogle;
      strncpy(hit.deviceType, "Fast Pair", sizeof(hit.deviceType) - 1);
      const std::string sdata = device->getServiceData(i);
      if (sdata.length() >= 3) {
        snprintf(hit.details, sizeof(hit.details), "Model: %02X%02X%02X",
                 static_cast<uint8_t>(sdata[0]),
                 static_cast<uint8_t>(sdata[1]),
                 static_cast<uint8_t>(sdata[2]));
      } else {
        snprintf(hit.details, sizeof(hit.details), "Pairing Active");
      }
      return true;
    }
    if (uuid == NimBLEUUID(static_cast<uint16_t>(0xFD5A))) {
      hit.ecosystem = kBleEcoSamsung;
      strncpy(hit.deviceType, "SmartTag", sizeof(hit.deviceType) - 1);
      snprintf(hit.details, sizeof(hit.details), "SmartThings Find");
      return true;
    }
  }

  // 3. Service UUID matching
  for (int i = 0; i < device->getServiceUUIDCount(); ++i) {
    const NimBLEUUID uuid = device->getServiceUUID(i);
    if (uuid == NimBLEUUID(static_cast<uint16_t>(0xFE2C))) {
      hit.ecosystem = kBleEcoGoogle;
      strncpy(hit.deviceType, "Fast Pair", sizeof(hit.deviceType) - 1);
      snprintf(hit.details, sizeof(hit.details), "Ready to Pair");
      return true;
    }
  }

  return false;
}

class BleIntelCallbacks : public NimBLEScanCallbacks {
 public:
  void onResult(const NimBLEAdvertisedDevice* device) override {
    const uint32_t generation = bleIntelHitQueue.generation();
    if (!generation) return;
    BleIntelHit hit;
    if (!bleClassifyIntel(device, hit)) return;
    bleIntelHitQueue.push(hit, generation);
  }
};

BleIntelCallbacks bleIntelCallbacks;

int bleIntelIndexOf(const char* addr) {
  for (int i = 0; i < bleIntelCount; ++i) {
    if (bleIntelEntries[i].addr.equals(addr)) return i;
  }
  return -1;
}

void mergeBleIntelHit(const BleIntelHit& hit) {
  const uint32_t now = millis();
  int index = bleIntelIndexOf(hit.addr);
  if (index < 0) {
    if (bleIntelCount >= kMaxBleIntel) return;
    index = bleIntelCount++;
    bleIntelEntries[index] = BleIntelEntry();
    bleIntelEntries[index].addr = String(hit.addr);
    bleIntelEntries[index].firstSeenMs = now;
  }
  BleIntelEntry& entry = bleIntelEntries[index];
  entry.ecosystem = hit.ecosystem;
  entry.deviceType = String(hit.deviceType);
  entry.details = String(hit.details);
  entry.rssi = hit.rssi;
  entry.batteryLeft = hit.batteryLeft;
  entry.batteryRight = hit.batteryRight;
  entry.batteryCase = hit.batteryCase;
  entry.lastSeenMs = now;
  ++entry.sightings;

  // Stream telemetry: $BLEINTEL,mac,ecosystem,deviceType,rssi,batteryL,batteryR,batteryCase,details
  char telem[160];
  snprintf(telem, sizeof(telem), "$BLEINTEL,%s,%s,%s,%d,%d,%d,%d,%s",
           hit.addr,
           bleIntelEcosystemName(hit.ecosystem),
           hit.deviceType,
           hit.rssi,
           hit.batteryLeft,
           hit.batteryRight,
           hit.batteryCase,
           hit.details);
  Serial.println(telem);
#ifdef AWOK_HEADLESS
  bridgeNotifyResult(kSourceBleIntel, reinterpret_cast<const uint8_t*>(telem), strlen(telem));
#endif
}

void sortBleIntel() {
  // Sort by strongest RSSI
  for (int i = 0; i < bleIntelCount - 1; ++i) {
    for (int j = i + 1; j < bleIntelCount; ++j) {
      if (bleIntelEntries[j].rssi > bleIntelEntries[i].rssi) {
        BleIntelEntry temporary = bleIntelEntries[i];
        bleIntelEntries[i] = bleIntelEntries[j];
        bleIntelEntries[j] = temporary;
      }
    }
  }
}

bool exportBleIntelToSd() {
  if (!bleIntelEntries) return lastBleIntelCsvOk;
  if (!ensureSdCard()) return false;
  const String temporaryPath = String(kBleIntelCsvPath) + ".tmp";
  SD.remove(temporaryPath.c_str());
  File file = SD.open(temporaryPath.c_str(), FILE_WRITE);
  if (!file) {
    sdReady = false;
    return false;
  }
  file.println(
      "uptime_ms,address,ecosystem,device_type,rssi,battery_left,battery_right,"
      "battery_case,details,first_seen_ms,last_seen_ms,sightings,latitude,longitude,altitude_m");
  const uint32_t uptime = millis();
  const String location = gpsCsvFields();
  for (int i = 0; i < bleIntelCount; ++i) {
    file.print(uptime);
    file.print(',');
    file.print(csvField(bleIntelEntries[i].addr));
    file.print(',');
    file.print(bleIntelEcosystemName(bleIntelEntries[i].ecosystem));
    file.print(',');
    file.print(csvField(bleIntelEntries[i].deviceType));
    file.print(',');
    file.print(bleIntelEntries[i].rssi);
    file.print(',');
    file.print(bleIntelEntries[i].batteryLeft);
    file.print(',');
    file.print(bleIntelEntries[i].batteryRight);
    file.print(',');
    file.print(bleIntelEntries[i].batteryCase);
    file.print(',');
    file.print(csvField(bleIntelEntries[i].details));
    file.print(',');
    file.print(bleIntelEntries[i].firstSeenMs);
    file.print(',');
    file.print(bleIntelEntries[i].lastSeenMs);
    file.print(',');
    file.print(bleIntelEntries[i].sightings);
    file.println(location);
  }
  file.flush();
  const bool ok = file.getWriteError() == 0;
  file.close();
  if (!ok) {
    SD.remove(temporaryPath.c_str());
    return false;
  }
  SD.remove(kBleIntelCsvPath);
  if (!SD.rename(temporaryPath.c_str(), kBleIntelCsvPath)) return false;
  Serial.printf("[bleintel] wrote %d row(s) to %s\n", bleIntelCount, kBleIntelCsvPath);
  return true;
}

void drawBleIntel() {
  currentView = View::kBleIntel;
  display.fillScreen(kBackground);
  sortBleIntel();
  drawHeader("BLE INTEL", String(bleIntelCount) + " device(s) decoded" +
                              reconResultPageLabel(bleIntelCount));
  const int startIdx = reconResultPage * kMenuPerPage;
  const int rows = min(kMenuPerPage, bleIntelCount - startIdx);
  for (int i = 0; i < rows; ++i) {
    const int idx = startIdx + i;
    String title = bleIntelEntries[idx].deviceType + " " + bleIntelEntries[idx].addr;
    char det[48];
    snprintf(det, sizeof(det), "%ld dBm  %s",
             static_cast<long>(bleIntelEntries[idx].rssi),
             bleIntelEntries[idx].details.c_str());
    drawMenuCard(kMenuFirstY + i * kMenuRowPitch, title, det,
                 bleIntelEcosystemColor(bleIntelEntries[idx].ecosystem));
  }
  if (bleIntelCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(20, 145);
    display.print("Scanning BLE ecosystem frames...");
  }
  drawReconResultFooter("Back", lastBleIntelCsvOk ? "Saved" : "Save", bleIntelCount);
}

void startBleIntel() {
  stopActiveTools();
  if (!ensureBleReady(false)) return;
  if (!bleIntelEntries.allocate() || !bleIntelHitQueue.begin()) {
    bleIntelHitQueue.release();
    bleIntelEntries.release();
    releaseBleMemory();
    showToolMemoryError("BLE Intel");
    return;
  }
  bleIntelCount = 0;
  reconResultPage = 0;
  bleIntelStartMs = millis();
  lastBleIntelDrawMs = 0;
  lastBleIntelCsvOk = false;
  signalMonitorActive = false;

  NimBLEScan* scan = NimBLEDevice::getScan();
  configureBleScan(scan, &bleIntelCallbacks, false, 80, 80, 0);
  if (!scan->start(0, false, true)) {
    bleIntelHitQueue.release();
    bleIntelEntries.release();
    releaseBleMemory();
    showRadioError("BLE scan could not start");
    return;
  }

  bleIntelActive = true;
  Serial.println("[bleintel] BLE ecosystem intel scanner started");
  drawBleIntel();
}

void stopBleIntel() {
  if (!bleIntelActive) return;
  bleIntelActive = false;
  bleIntelHitQueue.pause();
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->stop();
  scan->clearResults();
  releaseBleMemory();
  BleIntelHit hit;
  while (bleIntelHitQueue.pop(hit)) mergeBleIntelHit(hit);
  lastBleIntelCsvOk = exportBleIntelToSd();
  Serial.printf("[bleintel] stopped; %d device(s)\n", bleIntelCount);
  bleIntelHitQueue.release();
  bleIntelEntries.release();
  bleIntelCount = 0;
  logMemory("BLE Intel buffers released");
}

void updateBleIntel() {
  if (!bleIntelActive || currentView != View::kBleIntel) return;
  BleIntelHit hit;
  while (bleIntelHitQueue.pop(hit)) mergeBleIntelHit(hit);
  const uint32_t now = millis();
  if (now - lastBleIntelDrawMs >= kBleIntelRedrawMs) {
    lastBleIntelDrawMs = now;
    drawBleIntel();
  }
}
