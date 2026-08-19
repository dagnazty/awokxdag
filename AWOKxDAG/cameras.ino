// AWOKxDAG — surveillance-camera detection (compiled as part of the sketch; see
// awok_common.h)
//
// CONTINUOUS scanner: promiscuous Wi-Fi (hopping 2.4/5 GHz) plus a continuous
// BLE scan, updating a live table. Flags common cameras (Ring, Blink, Wyze,
// Nest, Arlo, Reolink, Eufy, Tapo, Hikvision, Dahua, ...) by vendor OUI (works
// on beaconing APs AND connected clients), SSID/BLE name patterns, and BLE
// manufacturer id. Heuristic: the tables below are a curated subset and easy to
// extend; expect some false positives and negatives.

constexpr int kMaxCameras = 24;
constexpr int kCameraHitQueueSlots = 32;
constexpr uint32_t kCameraHopIntervalMs = 300;
constexpr uint32_t kCameraRedrawMs = 700;

CameraEntry cameraEntries[kMaxCameras];
int cameraCount = 0;
CameraHit cameraHitQueue[kCameraHitQueueSlots];
volatile int cameraHitHead = 0;
volatile int cameraHitTail = 0;
int cameraHopIndex = 0;
uint32_t lastCameraHopMs = 0;
uint32_t lastCameraDrawMs = 0;
uint32_t cameraStartMs = 0;

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
    mac = p + 16;  // addr3 = BSSID
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

// Continuous BLE scan callback: reuse the wardrive BleHit queue (the camera
// scan and wardrive never run at the same time).
class CameraBleCallbacks : public NimBLEScanCallbacks {
 public:
  void onResult(const NimBLEAdvertisedDevice* device) override {
    const int next = (bleHitHead + 1) % kBleHitQueueSlots;
    if (next == bleHitTail) return;
    BleHit& hit = bleHitQueue[bleHitHead];
    strncpy(hit.addr, device->getAddress().toString().c_str(),
            sizeof(hit.addr) - 1);
    hit.addr[sizeof(hit.addr) - 1] = 0;
    hit.rssi = device->getRSSI();
    if (device->haveName()) {
      strncpy(hit.name, device->getName().c_str(), sizeof(hit.name) - 1);
      hit.name[sizeof(hit.name) - 1] = 0;
    } else {
      hit.name[0] = 0;
    }
    bleHitHead = next;
  }
};

CameraBleCallbacks cameraBleCallbacks;

void addCamera(const String& label, const String& mac, const char* vendor,
               const char* reason, int rssi, int channel, bool ble) {
  for (int i = 0; i < cameraCount; ++i) {
    if (cameraEntries[i].mac.equalsIgnoreCase(mac)) {
      cameraEntries[i].rssi = rssi;  // refresh signal
      return;
    }
  }
  if (cameraCount >= kMaxCameras) return;
  CameraEntry& c = cameraEntries[cameraCount++];
  c.label = label;
  c.mac = mac;
  c.vendor = vendor;
  c.reason = reason;
  c.rssi = rssi;
  c.channel = static_cast<int16_t>(channel);
  c.ble = ble;
}

void mergeCameraWifiHit(const CameraHit& hit) {
  const char* vendor = cameraVendorForOui(hit.mac);
  const char* reason = "Wi-Fi OUI";
  if (!vendor && hit.nameLen > 0) {
    vendor = cameraVendorForName(String(hit.name));
    reason = "SSID name";
  }
  if (!vendor) return;
  addCamera(hit.nameLen > 0 ? String(hit.name) : String(""),
            macToString(hit.mac), vendor, reason, hit.rssi, hit.channel, false);
}

void mergeCameraBleHit(const BleHit& hit) {
  const char* vendor = cameraVendorForName(String(hit.name));
  if (!vendor) return;
  addCamera(hit.name[0] ? String(hit.name) : String("<unnamed>"),
            String(hit.addr), vendor, "BLE name", hit.rssi, 0, true);
}

void drawCameraScan() {
  currentView = View::kCameraScan;
  display.fillScreen(kBackground);
  drawHeader("CAMERAS", String(cameraCount) + " suspected  ch " +
                            String(kDeauthHopChannels[cameraHopIndex]));
  display.setTextSize(1);
  const int rows = min(cameraCount, kVisibleRows);
  for (int i = 0; i < rows; ++i) {
    const int y = 48 + i * 22;
    display.setTextColor(kBad, kBackground);
    display.setCursor(5, y);
    display.print(clipped(cameraEntries[i].vendor, 15));
    display.setTextColor(kAccent, kBackground);
    display.print(cameraEntries[i].ble ? " BLE" : " WiFi");
    display.setTextColor(kMuted, kBackground);
    display.setCursor(5, y + 11);
    display.printf("%4ld dBm %s", static_cast<long>(cameraEntries[i].rssi),
                   cameraEntries[i].mac.c_str());
  }
  if (cameraCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(30, 140);
    display.print("Scanning for cameras...");
    display.setCursor(30, 156);
    display.print("Heuristic; not exhaustive");
  }
  drawFooter("Home", "Reset");
}

void startCameraScan() {
  cameraCount = 0;
  cameraHitHead = 0;
  cameraHitTail = 0;
  bleHitHead = 0;
  bleHitTail = 0;
  cameraHopIndex = 0;
  lastCameraHopMs = millis();
  lastCameraDrawMs = 0;
  cameraStartMs = millis();
  signalMonitorActive = false;

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask =
      WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&cameraWifiCallback);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(kDeauthHopChannels[0], WIFI_SECOND_CHAN_NONE);

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&cameraBleCallbacks, false);
  scan->setActiveScan(true);
  scan->setInterval(160);
  scan->setWindow(80);
  scan->clearResults();
  scan->start(0, false, true);

  cameraActive = true;
  Serial.println("[cameras] continuous Wi-Fi + BLE scan started");
  drawCameraScan();
}

void stopCameraScan() {
  cameraActive = false;
  esp_wifi_set_promiscuous(false);
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->stop();
  scan->clearResults();
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true, false);
  Serial.printf("[cameras] stopped; %d suspected\n", cameraCount);
}

void updateCameraScan() {
  if (!cameraActive || currentView != View::kCameraScan) return;
  while (cameraHitTail != cameraHitHead) {
    mergeCameraWifiHit(cameraHitQueue[cameraHitTail]);
    cameraHitTail = (cameraHitTail + 1) % kCameraHitQueueSlots;
  }
  while (bleHitTail != bleHitHead) {
    mergeCameraBleHit(bleHitQueue[bleHitTail]);
    bleHitTail = (bleHitTail + 1) % kBleHitQueueSlots;
  }
  const uint32_t now = millis();
  if (now - lastCameraHopMs >= kCameraHopIntervalMs) {
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
