// AWOKxDAG — Wi-Fi 6 / 802.11ax OFDMA & BSS Color Intelligence
// (compiled as part of the sketch; see awok_common.h)
//
// Passive sniffer that inspects 802.11ax High Efficiency (HE) capabilities,
// BSS Color codes (1-63), channel width (80/160 MHz), and spatial reuse
// parameters across 2.4 GHz and 5 GHz bands without transmitting.
// Emits $AXINTEL telemetry over BLE/Serial and exports to SD.

ToolBuffer<Wifi6ApEntry, kMaxWifi6Aps> wifi6Aps;
int wifi6ApCount = 0;

static constexpr int kWifi6QueueSlots = AwokPins::kDualBand ? 32 : 16;
static ToolQueue<Wifi6Hit, kWifi6QueueSlots> wifi6Queue;

int wifi6HopIndex = 0;
uint32_t lastWifi6HopMs = 0;
uint32_t lastWifi6DrawMs = 0;
uint32_t wifi6StartMs = 0;

#ifdef AWOK_MINI_DISPLAY
constexpr int kWifi6RowsPerPage = 4;
#else
constexpr int kWifi6RowsPerPage = 10;
#endif

int wifi6PageCount() {
  return max(1, (wifi6ApCount + kWifi6RowsPerPage - 1) / kWifi6RowsPerPage);
}

void clearWifi6Intel() {
  wifi6ApCount = 0;
  wifi6Page = 0;
}

static const uint8_t kWifi6_24Channels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
static constexpr int kWifi6_24Count = sizeof(kWifi6_24Channels) / sizeof(kWifi6_24Channels[0]);

#ifndef AWOK_CLASSIC_ESP32
static const uint8_t kWifi6_5Channels[] = {
    36,  40,  44,  48,  52,  56,  60,  64,  100, 104, 108, 112, 116,
    120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165};
static constexpr int kWifi6_5Count = sizeof(kWifi6_5Channels) / sizeof(kWifi6_5Channels[0]);
#else
static const uint8_t kWifi6_5Channels[] = {0};
static constexpr int kWifi6_5Count = 0;
#endif

static int wifi6TotalHopChannels() {
#ifndef AWOK_CLASSIC_ESP32
  return kWifi6_24Count + kWifi6_5Count;
#else
  return kWifi6_24Count;
#endif
}

static uint8_t wifi6HopChannelAt(int idx) {
  if (idx < kWifi6_24Count) return kWifi6_24Channels[idx];
#ifndef AWOK_CLASSIC_ESP32
  if (idx < kWifi6_24Count + kWifi6_5Count) {
    return kWifi6_5Channels[idx - kWifi6_24Count];
  }
#endif
  return 1;
}

void IRAM_ATTR wifi6PromiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  const uint32_t generation = wifi6Queue.generation();
  if (!generation || type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* packet =
      static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* payload = packet->payload;
  const int length = packet->rx_ctrl.sig_len;
  if (length < 38) return;

  const uint8_t frameControl = payload[0];
  // Filter for Beacon (0x80) or Probe Response (0x50)
  if ((frameControl & 0xF0) != 0x80 && (frameControl & 0xF0) != 0x50) return;

  Wifi6Hit hit = {};
  memcpy(hit.bssid, payload + 16, 6);
  hit.rssi = packet->rx_ctrl.rssi;
  hit.channel = packet->rx_ctrl.channel;
  hit.generation = 4; // Default to 802.11n (Wi-Fi 4)
  hit.bssColor = 0;
  hit.colorDisabled = false;
  hit.channelWidth = 20;
  hit.ssid[0] = '\0';

  // Management frame body starts at offset 24 + 12 (fixed params) = 36
  int offset = 36;
  while (offset + 2 <= length) {
    const uint8_t id = payload[offset];
    const uint8_t elen = payload[offset + 1];
    if (offset + 2 + elen > length) break;
    const uint8_t* val = payload + offset + 2;

    if (id == 0 && elen > 0) {
      // SSID
      const int copyLen = min(static_cast<int>(elen), 32);
      memcpy(hit.ssid, val, copyLen);
      hit.ssid[copyLen] = '\0';
    } else if (id == 3 && elen >= 1) {
      // DS Parameter Set
      hit.channel = val[0];
    } else if (id == 45) {
      // HT Capabilities (802.11n / Wi-Fi 4)
      if (hit.generation < 4) hit.generation = 4;
    } else if (id == 61 && elen >= 2) {
      // HT Operation
      if (val[1] & 0x04) hit.channelWidth = 40;
    } else if (id == 191) {
      // VHT Capabilities (802.11ac / Wi-Fi 5)
      if (hit.generation < 5) hit.generation = 5;
    } else if (id == 192 && elen >= 2) {
      // VHT Operation
      const uint8_t chWidth = val[0];
      if (chWidth == 1) hit.channelWidth = 80;
      else if (chWidth == 2 || chWidth == 3) hit.channelWidth = 160;
    } else if (id == 255 && elen >= 2) {
      // Extension IE
      const uint8_t extId = val[0];
      if (extId == 35) {
        // HE Capabilities (802.11ax / Wi-Fi 6)
        hit.generation = 6;
      } else if (extId == 36 && elen >= 5) {
        // HE Operation (802.11ax / Wi-Fi 6)
        hit.generation = 6;
        // Byte 1: BSS Color (bits 0-5), Default PE (bit 6), TWT Required (bit 7)
        hit.bssColor = val[1] & 0x3F;
        // Byte 2: Partial BSS Color (bit 1), BSS Color Disabled (bit 2)
        hit.colorDisabled = (val[2] & 0x04) != 0;
        if (elen >= 7 && (val[3] & 0x02)) {
          if (hit.channelWidth < 80) hit.channelWidth = 80;
        }
      }
    }
    offset += 2 + elen;
  }

  wifi6Queue.push(hit, generation);
}

static void wifi6StreamTelemetry(const Wifi6ApEntry& ap) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5]);

  char telem[140];
  snprintf(telem, sizeof(telem), "$AXINTEL,%s,%s,%u,%u,%u,%u,%d",
           macStr,
           (ap.ssid[0] ? ap.ssid : "<hidden>"),
           ap.channel,
           ap.generation,
           ap.bssColor,
           ap.channelWidth,
           ap.rssi);
  Serial.println(telem);
#ifdef AWOK_HEADLESS
  bridgeNotifyResult(kSourceWifi6Intel, reinterpret_cast<const uint8_t*>(telem), strlen(telem));
#endif
}

void wifi6ProcessHit(const Wifi6Hit& hit) {
  int foundIdx = -1;
  for (int i = 0; i < wifi6ApCount; ++i) {
    if (memcmp(wifi6Aps[i].bssid, hit.bssid, 6) == 0) {
      foundIdx = i;
      break;
    }
  }

  if (foundIdx >= 0) {
    wifi6Aps[foundIdx].rssi = hit.rssi;
    wifi6Aps[foundIdx].channel = hit.channel;
    wifi6Aps[foundIdx].lastSeenMs = millis();
    if (hit.ssid[0] && !wifi6Aps[foundIdx].ssid[0]) {
      strncpy(wifi6Aps[foundIdx].ssid, hit.ssid, sizeof(wifi6Aps[foundIdx].ssid) - 1);
    }
    if (hit.generation > wifi6Aps[foundIdx].generation) {
      wifi6Aps[foundIdx].generation = hit.generation;
    }
    if (hit.bssColor > 0) {
      wifi6Aps[foundIdx].bssColor = hit.bssColor;
      wifi6Aps[foundIdx].colorDisabled = hit.colorDisabled;
    }
    if (hit.channelWidth > wifi6Aps[foundIdx].channelWidth) {
      wifi6Aps[foundIdx].channelWidth = hit.channelWidth;
    }
  } else if (wifi6ApCount < kMaxWifi6Aps) {
    Wifi6ApEntry& e = wifi6Aps[wifi6ApCount++];
    memcpy(e.bssid, hit.bssid, 6);
    strncpy(e.ssid, hit.ssid, sizeof(e.ssid) - 1);
    e.channel = hit.channel;
    e.generation = hit.generation;
    e.bssColor = hit.bssColor;
    e.colorDisabled = hit.colorDisabled;
    e.channelWidth = hit.channelWidth;
    e.rssi = hit.rssi;
    e.lastSeenMs = millis();
    wifi6StreamTelemetry(e);
  }
}

bool exportWifi6IntelToSd() {
  if (!wifi6Aps) return lastWifi6IntelCsvOk;
  if (!ensureSdCard()) return false;
  const String temporaryPath = String(kWifi6IntelCsvPath) + ".tmp";
  SD.remove(temporaryPath.c_str());
  File file = SD.open(temporaryPath.c_str(), FILE_WRITE);
  if (!file) {
    sdReady = false;
    return false;
  }
  file.println("uptime_ms,bssid,ssid,channel,band,generation,bss_color,color_disabled,channel_width_mhz,rssi,latitude,longitude,altitude_m");
  const uint32_t uptime = millis();
  const String location = gpsCsvFields();

  for (int i = 0; i < wifi6ApCount; ++i) {
    const Wifi6ApEntry& ap = wifi6Aps[i];
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5]);
    const char* band = (ap.channel <= 14) ? "2.4GHz" : "5GHz";
    const char* genStr = (ap.generation == 6) ? "Wi-Fi 6 (ax)"
                       : (ap.generation == 5) ? "Wi-Fi 5 (ac)" : "Wi-Fi 4 (n)";

    file.printf("%lu,%s,\"%s\",%u,%s,%s,%u,%u,%u,%d%s\n",
                static_cast<unsigned long>(uptime),
                macStr,
                ap.ssid,
                ap.channel,
                band,
                genStr,
                ap.bssColor,
                ap.colorDisabled ? 1 : 0,
                ap.channelWidth,
                ap.rssi,
                location.c_str());
  }
  file.flush();
  const bool ok = file.getWriteError() == 0;
  file.close();
  if (!ok) {
    SD.remove(temporaryPath.c_str());
    return false;
  }
  SD.remove(kWifi6IntelCsvPath);
  if (!SD.rename(temporaryPath.c_str(), kWifi6IntelCsvPath)) return false;
  Serial.printf("[wifi6] wrote %d entries to %s\n", wifi6ApCount, kWifi6IntelCsvPath);
  return true;
}

static uint16_t wifi6ColorToRgb(uint8_t colorId) {
  if (colorId == 0) return 0x7BEF; // Muted gray
  const uint8_t h = (colorId * 23) % 192;
  return rgb565(h + 60, 255 - (h / 2), (h * 3) % 255);
}

void drawWifi6Intel() {
  currentView = View::kWifi6Intel;
  display.fillScreen(kBackground);

  int axCount = 0, acCount = 0, nCount = 0;
  uint64_t colorsSeen = 0;
  for (int i = 0; i < wifi6ApCount; ++i) {
    if (wifi6Aps[i].generation == 6) axCount++;
    else if (wifi6Aps[i].generation == 5) acCount++;
    else nCount++;
    if (wifi6Aps[i].bssColor > 0 && wifi6Aps[i].bssColor <= 63) {
      colorsSeen |= (1ULL << wifi6Aps[i].bssColor);
    }
  }
  int distinctColors = 0;
  for (int i = 1; i <= 63; ++i) {
    if (colorsSeen & (1ULL << i)) distinctColors++;
  }

  const int pages = wifi6PageCount();
  if (wifi6Page >= pages) wifi6Page = pages - 1;
  if (wifi6Page < 0) wifi6Page = 0;

  char sub[80];
  if (pages > 1) {
    snprintf(sub, sizeof(sub), "AX:%d AC:%d N:%d | pg %d/%d", axCount, acCount, nCount, wifi6Page + 1, pages);
  } else {
    snprintf(sub, sizeof(sub), "AX:%d AC:%d N:%d | Colors:%d", axCount, acCount, nCount, distinctColors);
  }
  drawHeader("WI-FI 6 INTEL", sub);

#ifdef AWOK_MINI_DISPLAY
  display.setTextSize(1);
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(scaleX(2), scaleY(46));
  display.printf("AX:%-2d AC:%-2d N:%-2d", axCount, acCount, nCount);
  display.setCursor(scaleX(2), scaleY(58));
  display.printf("BSS Colors: %d/63", distinctColors);

  const int startIdx = wifi6Page * kWifi6RowsPerPage;
  const int visible = min(kWifi6RowsPerPage, wifi6ApCount - startIdx);
  for (int i = 0; i < visible; ++i) {
    const auto& ap = wifi6Aps[startIdx + i];
    const int y = 70 + i * 11;
    display.setTextColor(ap.generation == 6 ? ILI9341_GREEN : (ap.generation == 5 ? ILI9341_CYAN : 0x7BEF), kBackground);
    display.setCursor(scaleX(2), scaleY(y));
    display.printf("%-1.1s Ch%-2u %-7.7s", (ap.generation == 6 ? "6" : "5"), ap.channel, ap.ssid[0] ? ap.ssid : "<hid>");
  }
  if (pages > 1) {
    drawFourButtonFooter("Back", "Prev", "Next", lastWifi6IntelCsvOk ? "Saved" : "Save");
  } else {
    drawThreeButtonFooter("Back", "Clear", lastWifi6IntelCsvOk ? "Saved" : "Save");
  }
  return;
#endif

  // Touch screen display (240x320)
  display.setTextSize(1);
  display.setTextColor(0x7BEF, kBackground);
  display.setCursor(scaleX(10), scaleY(42));
  display.print("GEN  CH  COLOR   WIDTH  RSSI  SSID");

  display.drawFastHLine(scaleX(8), scaleY(52), scaleX(224), 0x3186);

  constexpr int kRowY = 56;
  constexpr int kRowH = 22;

  if (wifi6ApCount == 0) {
    display.setTextColor(0x7BEF, kBackground);
    display.setCursor(scaleX(20), scaleY(120));
    display.print("Listening for 802.11ax...");
    display.setCursor(scaleX(20), scaleY(134));
    display.print("Dual-band Beacons & Probes");
  } else {
    const int startIdx = wifi6Page * kWifi6RowsPerPage;
    for (int r = 0; r < kWifi6RowsPerPage; ++r) {
      const int idx = startIdx + r;
      if (idx >= wifi6ApCount) break;
      const auto& ap = wifi6Aps[idx];
      const int y = kRowY + r * kRowH;

      // Gen Badge
      uint16_t genCol = ILI9341_GREEN;
      const char* genLabel = "6";
      if (ap.generation == 5) { genCol = ILI9341_CYAN; genLabel = "5"; }
      else if (ap.generation == 4) { genCol = 0x7BEF; genLabel = "4"; }

      display.fillRect(scaleX(8), scaleY(y), scaleX(14), scaleY(13), genCol);
      display.setTextColor(ILI9341_BLACK, genCol);
      display.setCursor(scaleX(12), scaleY(y + 3));
      display.print(genLabel);

      // Channel
      display.setTextColor(ILI9341_WHITE, kBackground);
      display.setCursor(scaleX(28), scaleY(y + 3));
      display.printf("%-2u", ap.channel);

      // BSS Color Pill
      if (ap.bssColor > 0) {
        const uint16_t cCol = wifi6ColorToRgb(ap.bssColor);
        display.drawRoundRect(scaleX(46), scaleY(y), scaleX(32), scaleY(13), 3, cCol);
        display.setTextColor(cCol, kBackground);
        display.setCursor(scaleX(50), scaleY(y + 3));
        display.printf("C%02u", ap.bssColor);
      } else {
        display.setTextColor(0x7BEF, kBackground);
        display.setCursor(scaleX(50), scaleY(y + 3));
        display.print("--");
      }

      // Width
      display.setTextColor(0x8410, kBackground);
      display.setCursor(scaleX(84), scaleY(y + 3));
      display.printf("%3uM", ap.channelWidth);

      // RSSI
      display.setTextColor(ap.rssi >= -65 ? ILI9341_GREEN : (ap.rssi >= -80 ? ILI9341_YELLOW : ILI9341_RED), kBackground);
      display.setCursor(scaleX(120), scaleY(y + 3));
      display.printf("%3d", ap.rssi);

      // SSID
      display.setTextColor(ILI9341_WHITE, kBackground);
      display.setCursor(scaleX(150), scaleY(y + 3));
      const char* s = ap.ssid[0] ? ap.ssid : "<hidden>";
      char buf[14];
      strncpy(buf, s, sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = '\0';
      display.print(buf);
    }
  }

  if (pages > 1) {
    drawFourButtonFooter("Back", "Prev", "Next", lastWifi6IntelCsvOk ? "Saved" : "Export");
  } else {
    drawThreeButtonFooter("Back", "Clear", lastWifi6IntelCsvOk ? "Saved" : "Export");
  }
}

void startWifi6Intel() {
  stopActiveTools();
  if (!ensureWifiStation(true)) return;

  if (!wifi6Aps.allocate() || !wifi6Queue.begin()) {
    wifi6Queue.release();
    wifi6Aps.release();
    showToolMemoryError("Wi-Fi 6 Intel");
    return;
  }
  wifi6ApCount = 0;
  wifi6HopIndex = 0;
  lastWifi6HopMs = millis();
  lastWifi6DrawMs = 0;
  wifi6StartMs = millis();
  lastWifi6IntelCsvOk = false;
  wifi6Page = 0;

  esp_wifi_set_promiscuous(false);
  esp_wifi_set_channel(wifi6HopChannelAt(0), WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous_rx_cb(wifi6PromiscuousCallback);
  esp_wifi_set_promiscuous(true);

  wifi6IntelActive = true;
  Serial.println("[wifi6] started 802.11ax inspection");
  drawWifi6Intel();
}

void stopWifi6Intel() {
  if (!wifi6IntelActive) return;
  wifi6IntelActive = false;
  wifi6Queue.pause();
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  Wifi6Hit hit;
  while (wifi6Queue.pop(hit)) wifi6ProcessHit(hit);
  lastWifi6IntelCsvOk = exportWifi6IntelToSd();
  wifi6Queue.release();
  wifi6Aps.release();
  wifi6ApCount = 0;
  logMemory("Wi-Fi 6 buffers released");
  Serial.println("[wifi6] stopped");
}

void updateWifi6Intel() {
  if (!wifi6IntelActive || currentView != View::kWifi6Intel) return;

  const uint32_t now = millis();

  // Drain queue
  Wifi6Hit hit;
  while (wifi6Queue.pop(hit)) wifi6ProcessHit(hit);

  // Channel hopping
  if (now - lastWifi6HopMs >= kWifi6HopIntervalMs) {
    lastWifi6HopMs = now;
    const int total = wifi6TotalHopChannels();
    wifi6HopIndex = (wifi6HopIndex + 1) % total;
    esp_wifi_set_channel(wifi6HopChannelAt(wifi6HopIndex), WIFI_SECOND_CHAN_NONE);
  }

  // UI redraw
  if (now - lastWifi6DrawMs >= kWifi6RedrawMs) {
    lastWifi6DrawMs = now;
    drawWifi6Intel();
  }
}
