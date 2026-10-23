// AWOKxDAG — RSSI locator / "fox hunt" (compiled as part of the sketch; see
// awok_common.h)
//
// Locks onto the selected AP (selectedWifi) and shows a large proximity meter
// with warmer/colder feedback so a rogue device can be tracked down on foot.
// Reuses sampleSelectedWifiSignal() from the main sketch.

constexpr uint32_t kLocatorSampleMs = 450;

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
  display.setCursor(scaleX(30), scaleY(52));
  if (found) {
    display.printf("%ld", static_cast<long>(locatorRssi));
    display.setTextSize(1);
    display.print(" dBm");
  } else {
    display.print("--");
  }

  // Proximity bar: closer (stronger) fills more of the bar.
#ifdef AWOK_MINI_DISPLAY
  display.bar(96, "Proximity", found ? constrain(locatorRssi, -90, -30) + 90 : 0, 60);
#else
  const int barLeft = scaleX(12);
  const int barWidth = scaleX(216);
  display.drawRect(barLeft, scaleY(96), barWidth, scaleY(22), kMuted);
  if (found) {
    const int fill =
        map(constrain(locatorRssi, -90, -30), -90, -30, 0, barWidth - 2);
    display.fillRect(barLeft + 1, scaleY(97), fill, scaleY(20),
                     signalColor(locatorRssi));
  }

#endif
  display.setTextSize(2);
  if (!found) {
    display.setTextColor(kWarn, kBackground);
    display.setCursor(scaleX(60), scaleY(132));
    display.print("SEARCHING");
  } else if (locatorTrend > 0) {
    display.setTextColor(kGood, kBackground);
    display.setCursor(scaleX(70), scaleY(132));
    display.print("WARMER");
  } else if (locatorTrend < 0) {
    display.setTextColor(kBad, kBackground);
    display.setCursor(scaleX(78), scaleY(132));
    display.print("COLDER");
  } else {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(scaleX(94), scaleY(132));
    display.print("HOLD");
  }

  display.setTextSize(1);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(168));
  display.print("BSSID: ");
  display.print(selectedWifi.bssid.length() ? selectedWifi.bssid : "unknown");
  display.setCursor(scaleX(6), scaleY(182));
  display.printf("Channel %ld %sG",
                 static_cast<long>(selectedWifi.channel),
                 bandLabel(selectedWifi.channel));
  display.setCursor(scaleX(6), scaleY(196));
  if (locatorBest > -127) {
    display.printf("Best: %ld dBm   seen %lu / missed %lu",
                   static_cast<long>(locatorBest),
                   static_cast<unsigned long>(locatorHits),
                   static_cast<unsigned long>(locatorMisses));
  } else {
    display.print("Move around to find the strongest spot.");
  }
  drawThreeButtonFooter("Back", "Reset", "Fleet");
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

// ---- Fleet Hunter (Multi-node Target Trilateration & Radar) ------------
constexpr uint32_t kHuntSampleIntervalMs = 500;
constexpr int kMaxHuntPoints = 16;

struct HuntPoint {
  float lat;
  float lon;
  int8_t rssi;
  uint8_t nodeIndex;
  uint32_t timestampMs;
};

HuntPoint huntPoints[kMaxHuntPoints] = {};
int huntPointCount = 0;
int huntPointHead = 0;

bool huntSolved = false;
float huntTargetLat = 0.0f;
float huntTargetLon = 0.0f;
float huntTargetDist = 0.0f;
float huntTargetBearing = 0.0f;
float huntTargetConf = 0.0f;
int8_t huntLatestRssi = -127;
int8_t huntBestRssi = -127;
uint32_t lastHuntSampleMs = 0;
uint32_t lastHuntTelemetryMs = 0;

static const char* compassHeadingStr(float deg) {
  while (deg < 0.0f) deg += 360.0f;
  while (deg >= 360.0f) deg -= 360.0f;
  if (deg >= 337.5f || deg < 22.5f) return "N";
  if (deg < 67.5f) return "NE";
  if (deg < 112.5f) return "E";
  if (deg < 157.5f) return "SE";
  if (deg < 202.5f) return "S";
  if (deg < 247.5f) return "SW";
  if (deg < 292.5f) return "W";
  return "NW";
}

// Convert RSSI to estimated distance in meters using log-distance path loss
// d = 10 ^ ((-40 - RSSI) / (10 * n)), n = 2.5 path loss exponent
static float rssiToDistanceMeters(int8_t rssi) {
  if (rssi > -20) rssi = -20;
  if (rssi < -100) rssi = -100;
  float exp = (-40.0f - static_cast<float>(rssi)) / 25.0f;
  float dist = powf(10.0f, exp);
  if (dist < 1.0f) dist = 1.0f;
  if (dist > 250.0f) dist = 250.0f;
  return dist;
}

void huntAddPoint(float lat, float lon, int8_t rssi, uint8_t nodeIndex) {
  if (lat == 0.0f || lon == 0.0f) return;
  huntPoints[huntPointHead].lat = lat;
  huntPoints[huntPointHead].lon = lon;
  huntPoints[huntPointHead].rssi = rssi;
  huntPoints[huntPointHead].nodeIndex = nodeIndex;
  huntPoints[huntPointHead].timestampMs = millis();
  huntPointHead = (huntPointHead + 1) % kMaxHuntPoints;
  if (huntPointCount < kMaxHuntPoints) ++huntPointCount;
}

void huntSolveTrilateration() {
  if (huntPointCount == 0) {
    huntSolved = false;
    return;
  }

  // Weighted Centroid Localization: w_i = 1 / (d_i ^ 2)
  float sumW = 0.0f;
  float sumWLat = 0.0f;
  float sumWLon = 0.0f;

  for (int i = 0; i < huntPointCount; ++i) {
    float dist = rssiToDistanceMeters(huntPoints[i].rssi);
    float w = 1.0f / (dist * dist);
    sumW += w;
    sumWLat += w * huntPoints[i].lat;
    sumWLon += w * huntPoints[i].lon;
  }

  if (sumW <= 0.0f) {
    huntSolved = false;
    return;
  }

  huntTargetLat = sumWLat / sumW;
  huntTargetLon = sumWLon / sumW;
  huntSolved = true;

  // Reference observer position (current device location or latest observation)
  float myLat = gps.location.isValid() ? static_cast<float>(gps.location.lat()) : 0.0f;
  float myLon = gps.location.isValid() ? static_cast<float>(gps.location.lng()) : 0.0f;
  if (myLat == 0.0f || myLon == 0.0f) {
    int latestIdx = (huntPointHead - 1 + kMaxHuntPoints) % kMaxHuntPoints;
    myLat = huntPoints[latestIdx].lat;
    myLon = huntPoints[latestIdx].lon;
  }

  // Distance from reference observer to target
  float dLat = (huntTargetLat - myLat) * 111132.0f;
  float midLatRad = ((huntTargetLat + myLat) * 0.5f) * (PI / 180.0f);
  float dLon = (huntTargetLon - myLon) * (111132.0f * cosf(midLatRad));
  huntTargetDist = sqrtf(dLat * dLat + dLon * dLon);

  // Bearing from observer to target (forward azimuth)
  float lat1 = myLat * (PI / 180.0f);
  float lat2 = huntTargetLat * (PI / 180.0f);
  float dLambda = (huntTargetLon - myLon) * (PI / 180.0f);
  float y = sinf(dLambda) * cosf(lat2);
  float x = cosf(lat1) * sinf(lat2) - sinf(lat1) * cosf(lat2) * cosf(dLambda);
  float bearing = atan2f(y, x) * (180.0f / PI);
  while (bearing < 0.0f) bearing += 360.0f;
  while (bearing >= 360.0f) bearing -= 360.0f;
  huntTargetBearing = bearing;

  // Confidence radius (weighted RMSE spread)
  if (huntPointCount == 1) {
    huntTargetConf = rssiToDistanceMeters(huntPoints[0].rssi) * 0.35f;
  } else {
    float sumDistErrSq = 0.0f;
    for (int i = 0; i < huntPointCount; ++i) {
      float ptLat = (huntPoints[i].lat - huntTargetLat) * 111132.0f;
      float ptLon = (huntPoints[i].lon - huntTargetLon) * (111132.0f * cosf(midLatRad));
      float ptDist = sqrtf(ptLat * ptLat + ptLon * ptLon);
      float estDist = rssiToDistanceMeters(huntPoints[i].rssi);
      float err = ptDist - estDist;
      sumDistErrSq += err * err;
    }
    huntTargetConf = sqrtf(sumDistErrSq / huntPointCount);
  }
  if (huntTargetConf < 2.0f) huntTargetConf = 2.0f;
  if (huntTargetConf > 100.0f) huntTargetConf = 100.0f;
}

void huntEmitTelemetry() {
  if (millis() - lastHuntTelemetryMs < 800) return;
  lastHuntTelemetryMs = millis();

  char bssidStr[20];
  snprintf(bssidStr, sizeof(bssidStr), "%s", selectedWifi.bssid.length() ? selectedWifi.bssid.c_str() : "00:00:00:00:00:00");
  char ssidStr[33];
  snprintf(ssidStr, sizeof(ssidStr), "%s", selectedWifi.ssid.length() ? selectedWifi.ssid.c_str() : "<hidden>");

  Serial.printf("$HUNT,%s,%s,%.6f,%.6f,%.1f,%.1f,%.1f,%d,%u\n",
                bssidStr, ssidStr,
                huntTargetLat, huntTargetLon,
                huntTargetDist, huntTargetBearing,
                huntTargetConf,
                huntLatestRssi,
                huntPointCount);

#ifdef AWOK_HEADLESS
  if (g_bridgePhoneConnected) {
    char rowBuf[128];
    int len = snprintf(rowBuf, sizeof(rowBuf), "$HUNT,%s,%s,%.6f,%.6f,%.1f,%.1f,%.1f,%d,%u",
                       bssidStr, ssidStr,
                       huntTargetLat, huntTargetLon,
                       huntTargetDist, huntTargetBearing,
                       huntTargetConf,
                       huntLatestRssi,
                       huntPointCount);
    if (len > 0) {
      bridgeNotifyResult(kSourceHunt, reinterpret_cast<const uint8_t*>(rowBuf), len);
    }
  }
#else
  if (linkEspNowReady) {
    FleetHuntResult res;
    parseBssid(selectedWifi.bssid, res.bssid);
    res.rssi = huntLatestRssi;
    res.points = static_cast<uint8_t>(huntPointCount);
    res.lat = huntTargetLat;
    res.lon = huntTargetLon;
    res.distanceM = huntTargetDist;
    res.bearingDeg = huntTargetBearing;
    res.confidenceM = huntTargetConf;
    strncpy(res.ssid, selectedWifi.ssid.c_str(), sizeof(res.ssid) - 1);
    esp_now_send(kLinkBroadcastAddr, reinterpret_cast<const uint8_t*>(&res), sizeof(res));
  }
#endif
}

void drawFleetHunt() {
  currentView = View::kFleetHunt;
  display.fillScreen(kBackground);
  drawHeader("FLEET HUNTER",
             selectedWifi.ssid.length() ? selectedWifi.ssid : "<hidden>");

#ifdef AWOK_MINI_DISPLAY
  const int cx = 64;
  const int cy = 50;
  const int radius = 32;

  // Radar scope
  display.drawCircle(cx, cy, radius, kMuted);
  display.drawCircle(cx, cy, radius * 2 / 3, kMuted);
  display.drawCircle(cx, cy, radius / 3, kMuted);
  display.drawFastHLine(cx - radius, cy, 2 * radius + 1, kMuted);
  display.drawFastVLine(cx, cy - radius, 2 * radius + 1, kMuted);

  // Compass labels
  display.setTextSize(1);
  display.setTextColor(kWarn, kBackground);
  display.setCursor(cx - 2, cy - radius - 8); display.print("N");

  if (huntSolved) {
    float rad = (huntTargetBearing - 90.0f) * (PI / 180.0f);
    int blipDist = constrain(static_cast<int>(huntTargetDist * (radius / 60.0f)), 4, radius - 2);
    int bx = cx + static_cast<int>(cosf(rad) * blipDist);
    int by = cy + static_cast<int>(sinf(rad) * blipDist);
    display.drawLine(cx, cy, bx, by, kAccent);
    display.fillCircle(bx, by, 3, kGood);
  }
  display.fillCircle(cx, cy, 2, kForeground);

  display.setTextSize(1);
  display.setTextColor(kForeground, kBackground);
  display.setCursor(scaleX(2), scaleY(90));
  if (huntSolved) {
    display.printf("%.1fm %ddeg %s", huntTargetDist, static_cast<int>(huntTargetBearing), compassHeadingStr(huntTargetBearing));
    display.setCursor(scaleX(2), scaleY(100));
    display.printf("%.4f,%.4f", huntTargetLat, huntTargetLon);
    display.setCursor(scaleX(2), scaleY(110));
    display.printf("Pts:%d %ddBm", huntPointCount, huntLatestRssi);
  } else {
    display.setCursor(scaleX(10), scaleY(96));
    display.setTextColor(kWarn, kBackground);
    display.print(gpsHasFix() ? "COLLECTING..." : "NO GPS FIX");
    display.setCursor(scaleX(2), scaleY(110));
    display.setTextColor(kMuted, kBackground);
    display.printf("RSSI: %d dBm", huntLatestRssi);
  }
#else
  const int cx = scaleX(120);
  const int cy = scaleY(110);
  const int radius = scaleX(54);  // uniform scale keeps the radar circular

  // Radar scope rings
  display.drawCircle(cx, cy, radius, kMuted);
  display.drawCircle(cx, cy, radius * 2 / 3, kMuted);
  display.drawCircle(cx, cy, radius / 3, kMuted);
  display.drawFastHLine(cx - radius, cy, 2 * radius + 1, kMuted);
  display.drawFastVLine(cx, cy - radius, 2 * radius + 1, kMuted);

  // Compass cardinal marks
  display.setTextSize(1);
  display.setTextColor(kWarn, kBackground);
  display.setCursor(cx - 3, cy - radius - 9); display.print("N");
  display.setTextColor(kMuted, kBackground);
  display.setCursor(cx - 3, cy + radius + 3); display.print("S");
  display.setCursor(cx - radius - 9, cy - 3); display.print("W");
  display.setCursor(cx + radius + 4, cy - 3); display.print("E");

  if (huntSolved) {
    float rad = (huntTargetBearing - 90.0f) * (PI / 180.0f);
    int blipDist = constrain(static_cast<int>(huntTargetDist * (radius / 75.0f)), 6, radius - 3);
    int bx = cx + static_cast<int>(cosf(rad) * blipDist);
    int by = cy + static_cast<int>(sinf(rad) * blipDist);

    // Bearing ray
    display.drawLine(cx, cy, bx, by, kAccent);
    // Target blip and confidence ring
    display.fillCircle(bx, by, 4, kGood);
    display.drawCircle(bx, by, 6, kGood);
    int confR = constrain(static_cast<int>(huntTargetConf * (radius / 75.0f)), 3, 16);
    display.drawCircle(bx, by, confR, kWarn);
  }
  // Local observer at center
  display.fillCircle(cx, cy, 3, kForeground);

  // Metrics readout
  if (huntSolved) {
    display.setTextSize(2);
    display.setTextColor(kGood, kBackground);
    display.setCursor(scaleX(20), scaleY(180));
    display.printf("%.1fm  %d\xF7 %s",
                   huntTargetDist,
                   static_cast<int>(huntTargetBearing),
                   compassHeadingStr(huntTargetBearing));

    display.setTextSize(1);
    display.setTextColor(kForeground, kBackground);
    display.setCursor(scaleX(14), scaleY(204));
    display.printf("Coords: %.6f, %.6f", huntTargetLat, huntTargetLon);

    display.setCursor(scaleX(14), scaleY(218));
    display.setTextColor(kMuted, kBackground);
    display.printf("Conf: \xF1%.1fm | %ddBm | %d pt(s)",
                   huntTargetConf,
                   huntLatestRssi,
                   huntPointCount);
  } else {
    display.setTextSize(2);
    display.setTextColor(kWarn, kBackground);
    display.setCursor(scaleX(44), scaleY(184));
    display.print(gpsHasFix() ? "COLLECTING..." : "NO GPS FIX");

    display.setTextSize(1);
    display.setTextColor(kMuted, kBackground);
    display.setCursor(scaleX(20), scaleY(212));
    display.print(gpsHasFix() ? "Sampling multi-node signal..." : "Waiting for satellites for trilateration");
  }

  drawThreeButtonFooter("Back", "Reset", "Locator");
#endif
}

void startFleetHunt() {
  huntPointCount = 0;
  huntPointHead = 0;
  huntSolved = false;
  huntTargetLat = 0.0f;
  huntTargetLon = 0.0f;
  huntTargetDist = 0.0f;
  huntTargetBearing = 0.0f;
  huntTargetConf = 0.0f;
  huntLatestRssi = -127;
  huntBestRssi = -127;
  lastHuntSampleMs = millis() - kHuntSampleIntervalMs;
  lastHuntTelemetryMs = 0;
  fleetHuntActive = true;
  Serial.printf("[hunt] started fleet hunt on %s\n", selectedWifi.bssid.c_str());
  drawFleetHunt();
}

void stopFleetHunt() {
  fleetHuntActive = false;
  Serial.println("[hunt] stopped fleet hunt");
}

void huntIngestObservation(const FleetHuntObservation& obs) {
  uint8_t targetMac[6];
  if (!parseBssid(selectedWifi.bssid, targetMac)) return;
  if (memcmp(obs.bssid, targetMac, 6) != 0) return;
  if (obs.lat == 0.0f || obs.lon == 0.0f) return;

  huntLatestRssi = obs.rssi;
  if (obs.rssi > huntBestRssi) huntBestRssi = obs.rssi;
  huntAddPoint(obs.lat, obs.lon, obs.rssi, obs.nodeIndex);
  huntSolveTrilateration();
  huntEmitTelemetry();
  if (currentView == View::kFleetHunt) {
    drawFleetHunt();
  }
}

void huntOnObservationFrame(const uint8_t* data) {
  FleetHuntObservation obs;
  memcpy(&obs, data, sizeof(obs));
  if (!fleetHuntActive) return;
  huntIngestObservation(obs);
}

void updateFleetHunt() {
  if (!fleetHuntActive) return;
  if (millis() - lastHuntSampleMs < kHuntSampleIntervalMs) return;
  lastHuntSampleMs = millis();

  scanInProgress = true;
  bool found = false;
  const int32_t rssi = sampleSelectedWifiSignal(found);
  scanInProgress = false;

  if (found) {
    huntLatestRssi = static_cast<int8_t>(rssi);
    if (rssi > huntBestRssi) huntBestRssi = static_cast<int8_t>(rssi);

    float myLat = gps.location.isValid() ? static_cast<float>(gps.location.lat()) : 0.0f;
    float myLon = gps.location.isValid() ? static_cast<float>(gps.location.lng()) : 0.0f;
    if (myLat != 0.0f && myLon != 0.0f) {
      huntAddPoint(myLat, myLon, huntLatestRssi, 0);
      huntSolveTrilateration();
    }
  }

  huntEmitTelemetry();

  if (currentView == View::kFleetHunt) {
    drawFleetHunt();
  }
}

