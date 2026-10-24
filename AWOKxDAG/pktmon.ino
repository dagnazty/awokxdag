// AWOKxDAG — packet monitor (compiled as part of the sketch; see awok_common.h)
//
// Promiscuous capture of every 802.11 frame on the hopped channels, written to
// a pcap on SD with a live management/data/control breakdown. Reuses the pcap
// helpers and capture ring buffer from the handshake module (they never run at
// the same time).

constexpr char kPktmonPcapPath[] = "/awokxdag/pktmon.pcap";
constexpr uint32_t kPktmonHopIntervalMs = 300;
constexpr uint32_t kPktmonRedrawMs = 500;

// pktmonActive lives in the main sketch's global block (defined before the
// input tab, which reads it in the serial guard).
File pktmonFile;
bool pktmonFileOpen = false;
uint32_t pktMgmt = 0;
uint32_t pktData = 0;
uint32_t pktCtrl = 0;
uint32_t pktTotal = 0;
uint32_t pktWritten = 0;
int pktmonHopIndex = 0;
uint32_t lastPktmonHopMs = 0;
uint32_t lastPktmonDrawMs = 0;
uint32_t pktmonStartMs = 0;

static bool pktmonGraph = false;
static int pktGraphChIdx = 0;
static uint32_t lastPktRowMs = 0;
static uint32_t lastPktSliceTotal = 0;
static uint32_t pktGraphBaseTotal = 0;
volatile int8_t pktSlicePeakRssi = -127;
constexpr int kPktGraphRows = 160;
constexpr int kPktGraphCenter = 120;
constexpr uint32_t kPktGraphRowMs = 80;
constexpr uint32_t kPktGraphRedrawMs = 100;
constexpr float kPktRateGain = 8.0f;
static int8_t pktDispRssi = -127;
constexpr int kPktFftN = 128;
constexpr int kPktBins = 64;
constexpr uint32_t kPktSubBinUs = (kPktGraphRowMs * 1000UL) / kPktFftN;
static volatile uint16_t pktSubBin[kPktFftN] = {0};
static volatile uint32_t pktRowStartUs = 0;
static uint8_t pktRowSpec[kPktGraphRows][kPktBins] = {{0}};
static int pktRowHead = -1;
static float pktFftRe[kPktFftN];
static float pktFftIm[kPktFftN];
static float pktHamming[kPktFftN];
static uint8_t pktXtoBin[kScreenWidth];
static uint16_t pktPalette[101];
static bool pktLutReady = false;

// Runs in the Wi-Fi task: tally by frame type and enqueue a copy for the loop.
void pktmonCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  // WIFI_PKT_MISC supplies receive metadata only, with no frame payload.
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA && type != WIFI_PKT_CTRL)
    return;
  const wifi_promiscuous_pkt_t* packet =
      static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const int length = packet->rx_ctrl.sig_len;
  if (length < 4) return;
  const int8_t rssi = packet->rx_ctrl.rssi;
  if (rssi > pktSlicePeakRssi) pktSlicePeakRssi = rssi;
  if (pktmonGraph) {
    const uint32_t idx = (micros() - pktRowStartUs) / kPktSubBinUs;
    if (idx < (uint32_t)kPktFftN) pktSubBin[idx]++;
  }
  if (type == WIFI_PKT_MGMT) {
    ++pktMgmt;
  } else if (type == WIFI_PKT_DATA) {
    ++pktData;
  } else {
    ++pktCtrl;
  }
  ++pktTotal;

  const int next = (captureHead + 1) % kCaptureQueueSlots;
  if (next == captureTail) return;  // queue full: drop the payload, keep count
  CaptureFrame& slot = captureQueue[captureHead];
  const int copyLen = length > kCaptureSlotBytes ? kCaptureSlotBytes : length;
  memcpy(slot.data, packet->payload, copyLen);
  slot.len = static_cast<uint16_t>(copyLen);
  slot.origLen = static_cast<uint16_t>(length);
  const uint32_t nowUs = micros();
  slot.tsSec = nowUs / 1000000UL;
  slot.tsUsec = nowUs % 1000000UL;
  slot.isEapol = false;
  captureHead = next;
}

bool openPktmonPcap() {
  if (!ensureSdCard()) return false;
  SD.remove(kPktmonPcapPath);
  pktmonFile = SD.open(kPktmonPcapPath, FILE_WRITE);
  if (!pktmonFile) {
    sdReady = false;
    return false;
  }
  writePcapLe32(pktmonFile, 0xA1B2C3D4);
  writePcapLe16(pktmonFile, 2);
  writePcapLe16(pktmonFile, 4);
  writePcapLe32(pktmonFile, 0);
  writePcapLe32(pktmonFile, 0);
  writePcapLe32(pktmonFile, kCaptureSlotBytes);
  writePcapLe32(pktmonFile, 105);  // LINKTYPE_IEEE802_11
  pktmonFile.flush();
  pktmonFileOpen = pktmonFile.getWriteError() == 0;
  return pktmonFileOpen;
}

void writePktmonFrame(const CaptureFrame& frame) {
  if (!pktmonFileOpen) return;
  writePcapLe32(pktmonFile, frame.tsSec);
  writePcapLe32(pktmonFile, frame.tsUsec);
  writePcapLe32(pktmonFile, frame.len);
  writePcapLe32(pktmonFile, frame.origLen);
  pktmonFile.write(frame.data, frame.len);
  ++pktWritten;
}

static void pktBuildLut() {
  for (int lv = 0; lv <= 100; ++lv) {
    int i = lv * 127 / 100;
    int r, g, b;
    if (i < 32) { r = i / 2; g = 0; b = i; }
    else if (i < 64) { r = i / 2; g = 0; b = 63 - i; }
    else if (i < 96) { r = 31; g = (i - 64) * 2; b = 0; }
    else { r = 31; g = 63; b = i - 96; }
    pktPalette[lv] = (uint16_t)((r << 11) | (g << 5) | b);
  }
  for (int i = 0; i < kPktFftN; ++i)
    pktHamming[i] = 0.54f - 0.46f * cosf(2.0f * 3.14159265f * i / (kPktFftN - 1));
  for (int x = 0; x < kScreenWidth; ++x) {
    int d = x >= kPktGraphCenter ? x - kPktGraphCenter : kPktGraphCenter - x;
    int bin = d * (kPktBins - 1) / kPktGraphCenter;
    if (bin < 0) bin = 0;
    if (bin >= kPktBins) bin = kPktBins - 1;
    pktXtoBin[x] = (uint8_t)bin;
  }
  pktLutReady = true;
}

static void pktFft() {
  const int n = kPktFftN;
  int j = 0;
  for (int i = 1; i < n; ++i) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      float tr = pktFftRe[i]; pktFftRe[i] = pktFftRe[j]; pktFftRe[j] = tr;
      float ti = pktFftIm[i]; pktFftIm[i] = pktFftIm[j]; pktFftIm[j] = ti;
    }
  }
  for (int len = 2; len <= n; len <<= 1) {
    float ang = -2.0f * 3.14159265f / len;
    float wr = cosf(ang), wi = sinf(ang);
    for (int i = 0; i < n; i += len) {
      float cr = 1.0f, ci = 0.0f;
      for (int k = 0; k < len / 2; ++k) {
        int a = i + k, b = a + len / 2;
        float tr = pktFftRe[b] * cr - pktFftIm[b] * ci;
        float ti = pktFftRe[b] * ci + pktFftIm[b] * cr;
        pktFftRe[b] = pktFftRe[a] - tr; pktFftIm[b] = pktFftIm[a] - ti;
        pktFftRe[a] += tr; pktFftIm[a] += ti;
        float ncr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = ncr;
      }
    }
  }
}

static void pktResetGraph() {
  pktRowHead = -1;
  lastPktSliceTotal = pktTotal;
  pktGraphBaseTotal = pktTotal;
  lastPktRowMs = millis();
  pktRowStartUs = micros();
  pktSlicePeakRssi = -127;
  pktDispRssi = -127;
  for (int i = 0; i < kPktFftN; ++i) pktSubBin[i] = 0;
  for (int r = 0; r < kPktGraphRows; ++r)
    for (int b = 0; b < kPktBins; ++b) pktRowSpec[r][b] = 0;
}

static void pktPushRow() {
  float cum = 0.0f;
  for (int i = 0; i < kPktFftN; ++i) {
    cum += (float)pktSubBin[i];
    pktFftRe[i] = cum;
    pktSubBin[i] = 0;
  }
  pktRowStartUs = micros();
  const uint32_t slice = (uint32_t)cum;
  float mean = 0.0f;
  for (int i = 0; i < kPktFftN; ++i) mean += pktFftRe[i];
  mean /= kPktFftN;
  for (int i = 0; i < kPktFftN; ++i) {
    pktFftRe[i] = (pktFftRe[i] - mean) * pktHamming[i];
    pktFftIm[i] = 0.0f;
  }
  pktFft();
  float mag[kPktBins];
  float mx = 1e-6f;
  for (int bin = 0; bin < kPktBins; ++bin) {
    mag[bin] = sqrtf(pktFftRe[bin] * pktFftRe[bin] + pktFftIm[bin] * pktFftIm[bin]);
    if (mag[bin] > mx) mx = mag[bin];
  }
  float bright = kPktRateGain * (float)slice;
  if (bright > 100.0f) bright = 100.0f;
  const float lmx = log2f(1.0f + mx);
  pktRowHead = (pktRowHead + 1) % kPktGraphRows;
  for (int bin = 0; bin < kPktBins; ++bin) {
    const float shape = lmx > 0.0f ? log2f(1.0f + mag[bin]) / lmx : 0.0f;
    int lvl = (int)(bright * shape + 0.5f);
    if (lvl < 0) lvl = 0;
    if (lvl > 100) lvl = 100;
    pktRowSpec[pktRowHead][bin] = (uint8_t)lvl;
  }
  if (pktSlicePeakRssi > -127) pktDispRssi = pktSlicePeakRssi;
  pktSlicePeakRssi = -127;
}

void drawPacketMonGraph() {
  currentView = View::kPacketMon;
  if (!pktLutReady) pktBuildLut();
  display.fillScreen(kBackground);
  const uint8_t gch = kDeauthHopChannels[pktGraphChIdx];
  drawHeader("PACKET MONITOR", "live graph");
  display.setTextSize(1);
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(6, 44);
  display.printf("Ch:%d %sG   Pkt:%lu   Pk:%ddBm",
                 gch, bandLabel(gch),
                 static_cast<unsigned long>(pktTotal - pktGraphBaseTotal),
                 pktDispRssi > -127 ? pktDispRssi : 0);

  const int traceTop = 56, traceBot = 118, wfTop = 126, wfBot = kFooterTop;
  display.fillRect(0, traceTop, kScreenWidth, traceBot - traceTop, pktPalette[0]);
  if (pktRowHead >= 0) {
    const uint8_t* cur = pktRowSpec[pktRowHead];
    for (int x = 0; x < kScreenWidth; ++x) {
      const int v = cur[pktXtoBin[x]];
      const int h = v * (traceBot - traceTop) / 100;
      if (h > 0) display.drawFastVLine(x, traceBot - h, h, pktPalette[v]);
    }
  }
  display.drawFastHLine(0, traceBot + 2, kScreenWidth, kMuted);

  if (pktRowHead >= 0) {
    const int rows = wfBot - wfTop;
    for (int row = 0; row < rows; ++row) {
      const int y = wfTop + row;
      const uint8_t* spec =
          pktRowSpec[((pktRowHead - row) % kPktGraphRows + kPktGraphRows) % kPktGraphRows];
      for (int x = 0; x < kScreenWidth; ++x) {
        display.drawPixel(x, y, pktPalette[spec[pktXtoBin[x]]]);
      }
    }
  }
  drawFourButtonFooter("Home", "Ch-", "Ch+", "Stats");
}

bool pktmonGraphActive() { return pktmonGraph; }

void pktmonToggleGraph() {
  pktmonGraph = !pktmonGraph;
  if (pktmonGraph) {
    pktGraphChIdx = pktmonHopIndex;
    esp_wifi_set_channel(kDeauthHopChannels[pktGraphChIdx], WIFI_SECOND_CHAN_NONE);
    pktResetGraph();
    lastPktmonDrawMs = 0;
    drawPacketMonGraph();
  } else {
    lastPktmonHopMs = millis();
    lastPktmonDrawMs = 0;
    drawPacketMon();
  }
}

void pktmonGraphStep(int dir) {
  pktGraphChIdx = (pktGraphChIdx + dir + kDeauthHopChannelCount) % kDeauthHopChannelCount;
  esp_wifi_set_channel(kDeauthHopChannels[pktGraphChIdx], WIFI_SECOND_CHAN_NONE);
  pktResetGraph();
  drawPacketMonGraph();
}

void drawPacketMon() {
  currentView = View::kPacketMon;
  display.fillScreen(kBackground);
  drawHeader("PACKET MONITOR",
             pktmonFileOpen ? "capturing to pktmon.pcap" : "live counts only");
  display.setTextSize(2);
  display.setTextColor(kAccent, kBackground);
  display.setCursor(scaleX(6), scaleY(52));
  display.printf("%lu pkts", static_cast<unsigned long>(pktTotal));

  display.setTextSize(1);
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(scaleX(6), scaleY(90));
  display.printf("Mgmt: %lu", static_cast<unsigned long>(pktMgmt));
  display.setCursor(scaleX(6), scaleY(104));
  display.printf("Data: %lu", static_cast<unsigned long>(pktData));
  display.setCursor(scaleX(6), scaleY(118));
  display.printf("Ctrl: %lu", static_cast<unsigned long>(pktCtrl));

  const uint32_t elapsed = (millis() - pktmonStartMs) / 1000;
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(138));
  display.printf("Rate: %lu pkt/s",
                 static_cast<unsigned long>(elapsed ? pktTotal / elapsed : 0));
  display.setCursor(scaleX(6), scaleY(152));
  display.printf("Channel %d %sG   %lus",
                 kDeauthHopChannels[pktmonHopIndex],
                 bandLabel(kDeauthHopChannels[pktmonHopIndex]),
                 static_cast<unsigned long>(elapsed));

  display.drawFastHLine(scaleX(6), scaleY(172), scaleX(228), kPanel);
  display.setTextColor(pktmonFileOpen ? kAccent : kWarn, kBackground);
  display.setCursor(scaleX(6), scaleY(182));
  display.printf("Written: %lu frame(s)",
                 static_cast<unsigned long>(pktWritten));
  display.setCursor(scaleX(6), scaleY(196));
  display.print(pktmonFileOpen ? "SD: pktmon.pcap (open in Wireshark)"
                               : "SD unavailable; counts only");
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(216));
  display.print("Hops 2.4/5 GHz; drops payload if the");
  display.setCursor(scaleX(6), scaleY(228));
  display.print("SD write cannot keep up (counts stay).");
  drawFooter("Home", "Graph");
}

void startPacketMon() {
  pktMgmt = 0;
  pktData = 0;
  pktCtrl = 0;
  pktTotal = 0;
  pktWritten = 0;
  captureHead = 0;
  captureTail = 0;
  pktmonHopIndex = 0;
  lastPktmonHopMs = millis();
  lastPktmonDrawMs = 0;
  pktmonStartMs = millis();
  pktmonGraph = false;
  pktSlicePeakRssi = -127;
  pktResetGraph();
  signalMonitorActive = false;

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&pktmonCallback);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(kDeauthHopChannels[0], WIFI_SECOND_CHAN_NONE);

  pktmonFileOpen = openPktmonPcap();
  pktmonActive = true;
  Serial.println("[pktmon] started");
  drawPacketMon();
}

void stopPacketMon() {
  pktmonActive = false;
  esp_wifi_set_promiscuous(false);
  if (pktmonFileOpen) {
    pktmonFile.flush();
    pktmonFile.close();
    pktmonFileOpen = false;
  }
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true, false);
  Serial.printf("[pktmon] stopped; %lu captured, %lu written\n",
                static_cast<unsigned long>(pktTotal),
                static_cast<unsigned long>(pktWritten));
}

void updatePacketMon() {
  if (!pktmonActive || currentView != View::kPacketMon) return;
  while (captureTail != captureHead) {
    writePktmonFrame(captureQueue[captureTail]);
    captureTail = (captureTail + 1) % kCaptureQueueSlots;
  }
  const uint32_t now = millis();
  if (pktmonGraph) {
    if (now - lastPktRowMs >= kPktGraphRowMs) {
      lastPktRowMs = now;
      esp_wifi_set_channel(kDeauthHopChannels[pktGraphChIdx], WIFI_SECOND_CHAN_NONE);
      pktPushRow();
    }
    if (now - lastPktmonDrawMs >= kPktGraphRedrawMs) {
      lastPktmonDrawMs = now;
      if (pktmonFileOpen) pktmonFile.flush();
      drawPacketMonGraph();
    }
    return;
  }
  if (now - lastPktmonHopMs >= kPktmonHopIntervalMs) {
    lastPktmonHopMs = now;
    pktmonHopIndex = (pktmonHopIndex + 1) % kDeauthHopChannelCount;
    esp_wifi_set_channel(kDeauthHopChannels[pktmonHopIndex],
                         WIFI_SECOND_CHAN_NONE);
  }
  if (now - lastPktmonDrawMs >= kPktmonRedrawMs) {
    lastPktmonDrawMs = now;
    if (pktmonFileOpen) pktmonFile.flush();
    drawPacketMon();
  }
}
