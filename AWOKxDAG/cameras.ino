// AWOKxDAG — surveillance-camera detection (compiled as part of the sketch; see
// awok_common.h)
//
// CONTINUOUS scanner: promiscuous Wi-Fi (hopping 2.4/5 GHz) plus BLE windows.
// All entries are suspected devices. LOW/MED/HIGH describe how specific and
// corroborated the radio clues are, not the probability of a camera.

constexpr int kMaxCameras = kResultCapacity;
constexpr int kCameraHitQueueSlots = AwokPins::kDualBand ? 32 : 16;
constexpr int kCameraBleHitQueueSlots = AwokPins::kDualBand ? 32 : 16;
constexpr uint32_t kCameraHopIntervalMs = 300;
constexpr uint32_t kCameraRedrawMs = 700;
constexpr uint32_t kCameraRetentionMs = 90000;

CameraEntry cameraEntries[kMaxCameras];
int cameraCount = 0;
CameraHit cameraHitQueue[kCameraHitQueueSlots];
volatile int cameraHitHead = 0;
volatile int cameraHitTail = 0;
CameraBleHit cameraBleHitQueue[kCameraBleHitQueueSlots];
volatile int cameraBleHitHead = 0;
volatile int cameraBleHitTail = 0;
int cameraHopIndex = 0;
uint32_t lastCameraHopMs = 0;
uint32_t lastCameraDrawMs = 0;
uint32_t cameraStartMs = 0;
uint32_t lastCameraPruneMs = 0;

struct CameraOui {
  uint8_t oui[3];
  const char* vendor;
};

const CameraOui kCameraOuis[] = {
    {{0x0C, 0x47, 0xC9}, "Amazon (Ring/Blink)"},
    {{0x44, 0x65, 0x0D}, "Amazon (Ring/Blink)"},
    {{0x74, 0xC2, 0x46}, "Amazon (Ring/Blink)"},
    {{0x68, 0x37, 0xE9}, "Amazon (Ring/Blink)"},
    {{0xF0, 0x81, 0x73}, "Amazon (Ring/Blink)"},
    {{0x34, 0xD2, 0x70}, "Amazon (Ring/Blink)"},
    {{0x2C, 0xAA, 0x8E}, "Wyze"},
    {{0x7C, 0x78, 0xB2}, "Wyze"},
    {{0xD0, 0x3F, 0x27}, "Wyze"},
    {{0x64, 0x16, 0x66}, "Google Nest"},
    {{0x18, 0xB4, 0x30}, "Google Nest"},
    {{0x6C, 0xAD, 0xF8}, "Google Nest"},
    {{0x3C, 0xEF, 0x8C}, "Dahua"},
    {{0x90, 0x02, 0xA9}, "Dahua"},
    {{0x44, 0x19, 0xB6}, "Hikvision"},
    {{0xC0, 0x56, 0xE3}, "Hikvision"},
    {{0xBC, 0xAD, 0x28}, "Hikvision"},
    {{0xEC, 0x71, 0xDB}, "Reolink"},
    {{0x9C, 0x3D, 0xCF}, "Arlo/Netgear"},
    {{0x00, 0x1F, 0x54}, "Arlo/Netgear"},
};
constexpr int kCameraOuiCount =
    static_cast<int>(sizeof(kCameraOuis) / sizeof(kCameraOuis[0]));

struct CameraPattern {
  const char* needle;
  const char* vendor;
};

const CameraPattern kCameraNamePatterns[] = {
    {"ring", "Ring"},         {"blink", "Blink"},
    {"wyze", "Wyze"},         {"arlo", "Arlo"},
    {"reolink", "Reolink"},   {"eufy", "Eufy"},
    {"tapo", "TP-Link Tapo"}, {"nest", "Nest"},
    {"amcrest", "Amcrest"},   {"ezviz", "EZVIZ"},
    {"ipcam", "IP camera"},   {"ipc-", "IP camera"},
    {"-cam", "camera"},       {"camera", "camera"},
};
constexpr int kCameraNamePatternCount =
    static_cast<int>(sizeof(kCameraNamePatterns) /
                     sizeof(kCameraNamePatterns[0]));

const char* cameraVendorForOui(const uint8_t* mac) {
  if (mac[0] & 0x02) return nullptr;  // randomized MAC: no vendor assignment
  for (int i = 0; i < kCameraOuiCount; ++i) {
    if (mac[0] == kCameraOuis[i].oui[0] && mac[1] == kCameraOuis[i].oui[1] &&
        mac[2] == kCameraOuis[i].oui[2]) {
      return kCameraOuis[i].vendor;
    }
  }
  return nullptr;
}

const char* cameraVendorForName(const String& name) {
  if (name.length() == 0) return nullptr;
  String lower = name;
  lower.toLowerCase();
  for (int i = 0; i < kCameraNamePatternCount; ++i) {
    if (lower.indexOf(kCameraNamePatterns[i].needle) >= 0) {
      return kCameraNamePatterns[i].vendor;
    }
  }
  return nullptr;
}

// Wi-Fi promiscuous callback: enqueue any device MAC (+ SSID for beacons).
void cameraWifiCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
  const wifi_promiscuous_pkt_t* packet =
      static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* p = packet->payload;
  const int len = packet->rx_ctrl.sig_len;
  if (len < 24) return;
  const uint8_t frameControl = p[0];
  const uint8_t frameType = (frameControl >> 2) & 0x03;

  const uint8_t* mac;
  const char* ssid = nullptr;
  int ssidLen = 0;
  if (frameType == 0x00) {  // management
    const uint8_t subtype = frameControl & 0xF0;
    // A probe request has a wildcard BSSID; its transmitter is addr2.
    mac = subtype == 0x40 ? p + 10 : p + 16;
    if ((subtype == 0x80 || subtype == 0x50) && len >= 38) {
      const uint8_t id = p[36];
      const uint8_t l = p[37];
      if (id == 0 && l > 0 && l <= 32 && len >= 38 + l) {
        ssid = reinterpret_cast<const char*>(p + 38);
        ssidLen = l;
      }
    }
  } else {  // data: addr2 = transmitter (catches connected client cameras)
    mac = p + 10;
  }
  if (mac[0] & 0x01) return;  // skip group/broadcast

  const int next = (cameraHitHead + 1) % kCameraHitQueueSlots;
  if (next == cameraHitTail) return;
  CameraHit& hit = cameraHitQueue[cameraHitHead];
  memcpy(hit.mac, mac, 6);
  hit.rssi = packet->rx_ctrl.rssi;
  hit.channel = packet->rx_ctrl.channel;
  const int n = ssidLen > 32 ? 32 : ssidLen;
  if (ssid && n > 0) memcpy(hit.name, ssid, n);
  hit.name[n] = 0;
  hit.nameLen = static_cast<uint8_t>(n);
  cameraHitHead = next;
}

uint8_t cameraRavenUuidMask(const NimBLEUUID& uuid) {
  for (uint16_t service = 0x3100; service <= 0x3500; service += 0x0100) {
    if (uuid == NimBLEUUID(service))
      return CameraSignals::ravenServiceBit(service);
  }
  return 0;
}

// Keep camera-specific BLE evidence together in its own callback queue. The
// shared wardrive queue only retains name/address and loses service/mfg data.
class CameraBleCallbacks : public NimBLEScanCallbacks {
 public:
  void onResult(const NimBLEAdvertisedDevice* device) override {
    const int next = (cameraBleHitHead + 1) % kCameraBleHitQueueSlots;
    if (next == cameraBleHitTail) return;
    CameraBleHit& hit = cameraBleHitQueue[cameraBleHitHead];
    strncpy(hit.addr, device->getAddress().toString().c_str(),
            sizeof(hit.addr) - 1);
    hit.addr[sizeof(hit.addr) - 1] = 0;
    hit.rssi = device->getRSSI();
    hit.publicAddress = device->getAddressType() == 0;
    hit.xuntong = false;
    hit.ravenMask = 0;
    if (device->haveName()) {
      strncpy(hit.name, device->getName().c_str(), sizeof(hit.name) - 1);
      hit.name[sizeof(hit.name) - 1] = 0;
    } else {
      hit.name[0] = 0;
    }
    // A BLE advertisement may contain multiple manufacturer entries.
    for (int i = 0; i < device->getManufacturerDataCount(); ++i) {
      const std::string data = device->getManufacturerData(i);
      if (data.size() >= 2 && static_cast<uint8_t>(data[0]) == 0xC8 &&
          static_cast<uint8_t>(data[1]) == 0x09) hit.xuntong = true;
    }
    for (int i = 0; i < device->getServiceUUIDCount(); ++i)
      hit.ravenMask |= cameraRavenUuidMask(device->getServiceUUID(i));
    for (int i = 0; i < device->getServiceDataCount(); ++i)
      hit.ravenMask |= cameraRavenUuidMask(device->getServiceDataUUID(i));
    cameraBleHitHead = next;
  }
};

CameraBleCallbacks cameraBleCallbacks;

void addCamera(const String& label, const String& mac, const char* vendor,
               uint16_t evidence, int rssi, int channel, bool ble) {
  for (int i = 0; i < cameraCount; ++i) {
    if (cameraEntries[i].ble == ble &&
        cameraEntries[i].mac.equalsIgnoreCase(mac)) {
      CameraEntry& c = cameraEntries[i];
      const CameraSignals::Confidence oldLevel =
          CameraSignals::confidence(c.evidence);
      c.evidence |= evidence;
      if (CameraSignals::confidence(c.evidence) > oldLevel ||
          (evidence & (CameraSignals::kFlockOui | CameraSignals::kAxonOui |
                       CameraSignals::kFlockName | CameraSignals::kAxonName |
                       CameraSignals::kFlockSsid | CameraSignals::kRavenService)))
        c.vendor = vendor;
      c.reason = CameraSignals::evidenceLabel(c.evidence);
      c.rssi = rssi;
      c.lastSeenMs = millis();
      if (label.length()) c.label = label;
      return;
    }
  }
  if (cameraCount >= kMaxCameras) return;
  CameraEntry& c = cameraEntries[cameraCount++];
  c.label = label;
  c.mac = mac;
  c.vendor = vendor;
  c.evidence = evidence;
  c.reason = CameraSignals::evidenceLabel(evidence);
  c.rssi = rssi;
  c.channel = static_cast<int16_t>(channel);
  c.ble = ble;
  c.lastSeenMs = millis();
}

void pruneCameraEntries(uint32_t nowMs) {
  for (int i = 0; i < cameraCount;) {
    if (!CameraSignals::sightingExpired(nowMs, cameraEntries[i].lastSeenMs,
                                        kCameraRetentionMs)) {
      ++i;
      continue;
    }
    for (int j = i + 1; j < cameraCount; ++j)
      cameraEntries[j - 1] = cameraEntries[j];
    --cameraCount;
    cameraEntries[cameraCount] = CameraEntry();
  }
}

void mergeCameraWifiHit(const CameraHit& hit) {
  const char* vendor = nullptr;
  uint16_t evidence = 0;
  if (CameraSignals::flockOui(hit.mac)) {
    vendor = "Flock Safety";
    evidence |= CameraSignals::kFlockOui;
  } else if (CameraSignals::axonOui(hit.mac)) {
    vendor = "Axon";
    evidence |= CameraSignals::kAxonOui;
  } else if ((vendor = cameraVendorForOui(hit.mac))) {
    evidence |= CameraSignals::kVendorOui;
  }
  if (hit.nameLen > 0) {
    if (CameraSignals::flockSsid(hit.name)) {
      vendor = "Flock Safety";
      evidence |= CameraSignals::kFlockSsid;
    } else if (CameraSignals::axonName(hit.name)) {
      vendor = "Axon";
      evidence |= CameraSignals::kAxonName;
    } else if (const char* named = cameraVendorForName(String(hit.name))) {
      if (!vendor || strcmp(vendor, named) == 0) {
        if (!vendor) vendor = named;
        evidence |= CameraSignals::kGenericName;
      }
    }
  }
  if (!evidence) return;
  addCamera(hit.nameLen > 0 ? String(hit.name) : String(""),
            macToString(hit.mac), vendor, evidence, hit.rssi, hit.channel, false);
}

void mergeCameraBleHit(const CameraBleHit& hit) {
  uint8_t mac[6] = {0};
  const bool hasPublicOui = hit.publicAddress && parseBssid(String(hit.addr), mac);
  const char* vendor = nullptr;
  uint16_t evidence = 0;
  if (hasPublicOui && CameraSignals::flockOui(mac)) {
    vendor = "Flock Safety";
    evidence |= CameraSignals::kFlockOui;
  } else if (hasPublicOui && CameraSignals::axonOui(mac)) {
    vendor = "Axon";
    evidence |= CameraSignals::kAxonOui;
  } else if (hasPublicOui && (vendor = cameraVendorForOui(mac))) {
    evidence |= CameraSignals::kVendorOui;
  }
  if (CameraSignals::flockName(hit.name)) {
    vendor = "Flock Safety";
    evidence |= CameraSignals::kFlockName;
  } else if (CameraSignals::axonName(hit.name)) {
    vendor = "Axon";
    evidence |= CameraSignals::kAxonName;
  } else if (const char* named = cameraVendorForName(String(hit.name))) {
    if (!vendor || strcmp(vendor, named) == 0) {
      if (!vendor) vendor = named;
      evidence |= CameraSignals::kGenericName;
    }
  }
  if (hit.xuntong) {
    if (!vendor) vendor = "Xuntong / Flock?";
    evidence |= CameraSignals::kXuntong;
  }
  if (hit.ravenMask) {
    if (!vendor) vendor = "Raven?";
    evidence |= CameraSignals::kRavenService;
    if (CameraSignals::bitCount(hit.ravenMask) >= 2)
      evidence |= CameraSignals::kRavenServices;
  }
  if (!evidence) return;
  addCamera(hit.name[0] ? String(hit.name) : String("<unnamed>"),
            String(hit.addr), vendor, evidence, hit.rssi, 0, true);
}

void drawCameraScan() {
  currentView = View::kCameraScan;
  display.fillScreen(kBackground);
  drawHeader("CAMERAS", String(cameraCount) + " suspected  ch " +
                            String(kDeauthHopChannels[cameraHopIndex]) +
                            (radiosCoexist ? "" : "  (Wi-Fi only)") +
                            reconResultPageLabel(cameraCount));
  const int startIdx = reconResultPage * kMenuPerPage;
  const int rows = min(kMenuPerPage, cameraCount - startIdx);
  for (int i = 0; i < rows; ++i) {
    const int idx = startIdx + i;
    const CameraEntry& c = cameraEntries[idx];
    const CameraSignals::Confidence level = CameraSignals::confidence(c.evidence);
    String title = clipped(c.vendor, 15) + "? " +
                   CameraSignals::confidenceLabel(level) +
                   (c.ble ? " BLE" : " WiFi");
    char det[56];
    snprintf(det, sizeof(det), "%ld dBm  %s  %s",
             static_cast<long>(c.rssi), c.reason.c_str(), c.mac.c_str());
    drawMenuCard(kMenuFirstY + i * kMenuRowPitch, title, det,
                 level == CameraSignals::kHigh ? kWarn :
                 level == CameraSignals::kMedium ? kAccent : kMuted);
  }
  if (cameraCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(30, 140);
    display.print("Scanning for cameras...");
    display.setCursor(30, 156);
    display.print("Heuristic; not exhaustive");
    if (!radiosCoexist) {
      display.setCursor(30, 172);
      display.print("BLE off: memory/startup check");
    }
  }
  drawReconResultFooter("Home", "Reset", cameraCount);
}

// Wi-Fi window hooks: (re)arm promiscuous capture on the current hop channel,
// and drop out of promiscuous before Wi-Fi is released for the BLE window.
static void cameraEnterWifi() {
  WiFi.disconnect(false, false);
  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask =
      WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&cameraWifiCallback);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(kDeauthHopChannels[cameraHopIndex],
                       WIFI_SECOND_CHAN_NONE);
}
static void cameraExitWifi() { esp_wifi_set_promiscuous(false); }

RadioScheduler cameraSched;

void startCameraScan() {
  cameraCount = 0;
  reconResultPage = 0;
  cameraHitHead = 0;
  cameraHitTail = 0;
  cameraBleHitHead = 0;
  cameraBleHitTail = 0;
  cameraHopIndex = 0;
  lastCameraHopMs = millis();
  lastCameraDrawMs = 0;
  lastCameraPruneMs = millis();
  cameraStartMs = millis();
  signalMonitorActive = false;

  cameraSched = RadioScheduler();
  cameraSched.enterWifi = cameraEnterWifi;
  cameraSched.exitWifi = cameraExitWifi;
  cameraSched.bleCallbacks = &cameraBleCallbacks;
  cameraSched.bleActiveScan = true;  // preserve names in scan responses
  if (!radioSchedulerBegin(cameraSched)) return;
  if (cameraSched.bleAvailable) {
    // Report advertisers that do not answer an active scan promptly. NimBLE's
    // default wait is 10.24 s, longer than our six-second BLE window.
    NimBLEDevice::getScan()->setScanResponseTimeout(200);
  }

  Serial.println(radiosCoexist
                     ? "[cameras] Wi-Fi + BLE scan started (time-shared)"
                     : "[cameras] Wi-Fi-only scan started");
  cameraActive = true;
  drawCameraScan();
}

void stopCameraScan() {
  cameraActive = false;
  radioSchedulerEnd(cameraSched);
  Serial.printf("[cameras] stopped; %d suspected\n", cameraCount);
}

void updateCameraScan() {
  if (!cameraActive || currentView != View::kCameraScan) return;
  radioSchedulerTick(cameraSched);
  const uint32_t now = millis();
  if (now - lastCameraPruneMs >= 1000) {
    lastCameraPruneMs = now;
    pruneCameraEntries(now);
  }
  while (cameraHitTail != cameraHitHead) {
    mergeCameraWifiHit(cameraHitQueue[cameraHitTail]);
    cameraHitTail = (cameraHitTail + 1) % kCameraHitQueueSlots;
  }
  while (cameraBleHitTail != cameraBleHitHead) {
    mergeCameraBleHit(cameraBleHitQueue[cameraBleHitTail]);
    cameraBleHitTail = (cameraBleHitTail + 1) % kCameraBleHitQueueSlots;
  }
  // Channel-hop only during the Wi-Fi window (Wi-Fi is down otherwise).
  if (cameraSched.phase == RadioPhase::kWifi &&
      now - lastCameraHopMs >= kCameraHopIntervalMs) {
    lastCameraHopMs = now;
    cameraHopIndex = (cameraHopIndex + 1) % kDeauthHopChannelCount;
    esp_wifi_set_channel(kDeauthHopChannels[cameraHopIndex],
                         WIFI_SECOND_CHAN_NONE);
  }
  if (now - lastCameraDrawMs >= kCameraRedrawMs) {
    lastCameraDrawMs = now;
    drawCameraScan();
  }
}
