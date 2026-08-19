// AWOKxDAG — BLE spam / advertisement-flood detector (compiled as part of the
// sketch; see awok_common.h)
//
// Continuous passive BLE scan. Classifies each advertisement by the vendor
// payload that BLE spammers abuse (Apple continuity 0x004C, Microsoft Swift
// Pair 0x0006, Samsung 0x0075, Google Fast Pair service 0xFE2C) and raises an
// alert when the spam-advert rate spikes -- the defensive counterpart to a
// spammer. Passive: it never transmits.

constexpr uint32_t kBleDetectWindowMs = 2000;   // rate window
constexpr uint32_t kBleDetectRedrawMs = 500;
constexpr uint32_t kBleDetectSpamThreshold = 12;  // spam adverts per window

volatile uint32_t bleDetectTotal = 0;
volatile uint32_t bleDetectSpam = 0;
volatile uint32_t bleDetectWindowSpam = 0;
volatile uint32_t bleDetectWindowTotal = 0;
volatile uint8_t bleDetectLastVendor = 0;  // 0 none,1 apple,2 ms,3 samsung,4 google
uint32_t bleDetectPeakRate = 0;
uint32_t bleDetectStartMs = 0;
uint32_t lastBleDetectWindowMs = 0;
uint32_t lastBleDetectDrawMs = 0;
uint32_t bleDetectRate = 0;
bool bleDetectAlert = false;

const char* bleVendorName(uint8_t v) {
  switch (v) {
    case 1: return "Apple";
    case 2: return "Microsoft";
    case 3: return "Samsung";
    case 4: return "Google";
    default: return "-";
  }
}

// Classify an advertisement's manufacturer/service payload; 0 = not a known
// spam vendor.
uint8_t bleClassify(const NimBLEAdvertisedDevice* device) {
  if (device->haveManufacturerData()) {
    const std::string mfg = device->getManufacturerData();
    if (mfg.length() >= 2) {
      const uint16_t company = static_cast<uint8_t>(mfg[0]) |
                               (static_cast<uint16_t>(
                                    static_cast<uint8_t>(mfg[1]))
                                << 8);
      if (company == 0x004C) return 1;  // Apple
      if (company == 0x0006) return 2;  // Microsoft
      if (company == 0x0075) return 3;  // Samsung
    }
  }
  if (device->haveServiceData()) {
    for (int i = 0; i < device->getServiceDataCount(); ++i) {
      if (device->getServiceDataUUID(i) ==
          NimBLEUUID(static_cast<uint16_t>(0xFE2C))) {
        return 4;  // Google Fast Pair
      }
    }
  }
  return 0;
}

class BleDetectCallbacks : public NimBLEScanCallbacks {
 public:
  void onResult(const NimBLEAdvertisedDevice* device) override {
    ++bleDetectTotal;
    ++bleDetectWindowTotal;
    const uint8_t vendor = bleClassify(device);
    if (vendor) {
      ++bleDetectSpam;
      ++bleDetectWindowSpam;
      bleDetectLastVendor = vendor;
    }
  }
};

BleDetectCallbacks bleDetectCallbacks;

void drawBleDetect() {
  currentView = View::kBleSpamWatch;
  display.fillScreen(kBackground);
  drawHeader("BLE SPAM WATCH",
             bleDetectAlert ? "ALERT: spam flood nearby"
                            : "listening for advert floods");
  display.setTextSize(2);
  display.setTextColor(bleDetectAlert ? kBad : kGood, kBackground);
  display.setCursor(6, 52);
  display.print(bleDetectAlert ? "SPAM SEEN" : "CLEAR");

  display.setTextSize(1);
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(6, 90);
  display.printf("Spam adverts/win: %lu",
                 static_cast<unsigned long>(bleDetectRate));
  display.setCursor(6, 104);
  display.printf("Total adverts: %lu",
                 static_cast<unsigned long>(bleDetectTotal));
  display.setCursor(6, 118);
  display.printf("Spam adverts: %lu",
                 static_cast<unsigned long>(bleDetectSpam));
  display.setCursor(6, 132);
  display.printf("Peak/win: %lu   thr %lu",
                 static_cast<unsigned long>(bleDetectPeakRate),
                 static_cast<unsigned long>(kBleDetectSpamThreshold));

  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 152);
  display.print("Last spam vendor: ");
  display.print(bleVendorName(bleDetectLastVendor));

  display.drawFastHLine(6, 172, 228, kPanel);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 182);
  display.print("Passive; nothing transmitted. Many");
  display.setCursor(6, 194);
  display.print("random-address vendor adverts in a");
  display.setCursor(6, 206);
  display.print("short window indicate a spammer.");
  drawFooter("Home", "Reset");
}

void startBleDetect() {
  bleDetectTotal = 0;
  bleDetectSpam = 0;
  bleDetectWindowSpam = 0;
  bleDetectWindowTotal = 0;
  bleDetectLastVendor = 0;
  bleDetectPeakRate = 0;
  bleDetectRate = 0;
  bleDetectAlert = false;
  bleDetectStartMs = millis();
  lastBleDetectWindowMs = millis();
  lastBleDetectDrawMs = 0;
  signalMonitorActive = false;

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&bleDetectCallbacks, false);
  scan->setActiveScan(false);
  scan->setInterval(80);
  scan->setWindow(80);  // near-continuous listening
  scan->clearResults();
  scan->start(0, false, true);

  bleDetectActive = true;
  Serial.println("[bledetect] BLE spam watch started");
  drawBleDetect();
}

void stopBleDetect() {
  bleDetectActive = false;
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->stop();
  scan->clearResults();
  Serial.println("[bledetect] stopped");
}

void updateBleDetect() {
  if (!bleDetectActive || currentView != View::kBleSpamWatch) return;
  const uint32_t now = millis();
  if (now - lastBleDetectWindowMs >= kBleDetectWindowMs) {
    lastBleDetectWindowMs = now;
    bleDetectRate = bleDetectWindowSpam;
    if (bleDetectRate > bleDetectPeakRate) bleDetectPeakRate = bleDetectRate;
    bleDetectAlert = bleDetectRate >= kBleDetectSpamThreshold;
    bleDetectWindowSpam = 0;
    bleDetectWindowTotal = 0;
  }
  if (now - lastBleDetectDrawMs >= kBleDetectRedrawMs) {
    lastBleDetectDrawMs = now;
    drawBleDetect();
  }
}
