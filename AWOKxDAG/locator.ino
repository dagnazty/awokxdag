// AWOKxDAG — RSSI locator / "fox hunt" (compiled as part of the sketch; see
// awok_common.h)
//
// Locks onto the selected AP (selectedWifi) and shows a large proximity meter
// with warmer/colder feedback so a rogue device can be tracked down on foot.
// Reuses sampleSelectedWifiSignal() from the main sketch.

constexpr uint32_t kLocatorSampleMs = 450;

bool locatorActive = false;
int32_t locatorRssi = -127;
int32_t locatorPrevRssi = -127;
int locatorTrend = 0;  // +1 warmer, -1 colder, 0 hold
int32_t locatorBest = -127;
uint32_t locatorHits = 0;
uint32_t locatorMisses = 0;
uint32_t lastLocatorSampleMs = 0;

void drawLocator() {
  currentView = View::kLocator;
  display.fillScreen(kBackground);
  drawHeader("LOCATOR",
             selectedWifi.ssid.length() ? selectedWifi.ssid : "<hidden>");

  const bool found = locatorRssi > -127;
  display.setTextSize(3);
  display.setTextColor(found ? signalColor(locatorRssi) : kMuted, kBackground);
  display.setCursor(30, 52);
  if (found) {
    display.printf("%ld", static_cast<long>(locatorRssi));
    display.setTextSize(1);
    display.print(" dBm");
  } else {
    display.print("--");
  }

  // Proximity bar: closer (stronger) fills more of the bar.
  const int barLeft = 12;
  const int barWidth = 216;
  display.drawRect(barLeft, 96, barWidth, 22, kMuted);
  if (found) {
    const int fill =
        map(constrain(locatorRssi, -90, -30), -90, -30, 0, barWidth - 2);
    display.fillRect(barLeft + 1, 97, fill, 20, signalColor(locatorRssi));
  }

  display.setTextSize(2);
  if (!found) {
    display.setTextColor(kWarn, kBackground);
    display.setCursor(60, 132);
    display.print("SEARCHING");
  } else if (locatorTrend > 0) {
    display.setTextColor(kGood, kBackground);
    display.setCursor(70, 132);
    display.print("WARMER");
  } else if (locatorTrend < 0) {
    display.setTextColor(kBad, kBackground);
    display.setCursor(78, 132);
    display.print("COLDER");
  } else {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(94, 132);
    display.print("HOLD");
  }

  display.setTextSize(1);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 168);
  display.print("BSSID: ");
  display.print(selectedWifi.bssid.length() ? selectedWifi.bssid : "unknown");
  display.setCursor(6, 182);
  display.printf("Channel %ld %sG",
                 static_cast<long>(selectedWifi.channel),
                 bandLabel(selectedWifi.channel));
  display.setCursor(6, 196);
  if (locatorBest > -127) {
    display.printf("Best: %ld dBm   seen %lu / missed %lu",
                   static_cast<long>(locatorBest),
                   static_cast<unsigned long>(locatorHits),
                   static_cast<unsigned long>(locatorMisses));
  } else {
    display.print("Move around to find the strongest spot.");
  }
  drawThreeButtonFooter("Back", "Reset", "Signal");
}

void startLocator() {
  locatorRssi = -127;
  locatorPrevRssi = -127;
  locatorTrend = 0;
  locatorBest = -127;
  locatorHits = 0;
  locatorMisses = 0;
  lastLocatorSampleMs = millis() - kLocatorSampleMs;
  locatorActive = true;
  Serial.printf("[locator] tracking %s\n", selectedWifi.bssid.c_str());
  drawLocator();
}

void stopLocator() { locatorActive = false; }

void updateLocator() {
  if (!locatorActive || currentView != View::kLocator) return;
  if (millis() - lastLocatorSampleMs < kLocatorSampleMs) return;
  lastLocatorSampleMs = millis();

  scanInProgress = true;
  bool found = false;
  const int32_t rssi = sampleSelectedWifiSignal(found);
  scanInProgress = false;

  locatorPrevRssi = locatorRssi;
  if (found) {
    locatorRssi = rssi;
    ++locatorHits;
    if (rssi > locatorBest) locatorBest = rssi;
    if (locatorPrevRssi > -127) {
      locatorTrend =
          (rssi > locatorPrevRssi + 1) ? 1 : (rssi < locatorPrevRssi - 1 ? -1 : 0);
    }
  } else {
    locatorRssi = -127;
    locatorTrend = 0;
    ++locatorMisses;
  }
  drawLocator();
}
