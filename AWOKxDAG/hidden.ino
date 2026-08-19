// AWOKxDAG — hidden-SSID reveal (compiled as part of the sketch; see
// awok_common.h)
//
// Tracks APs beaconing a hidden (blank) SSID, then fills in their real name
// when a probe response or association request for that BSSID carries the SSID
// element (which happens whenever a client (re)connects). Passive.
// hiddenRevealActive lives in the main sketch (read by the input tab).

constexpr int kMaxHidden = 24;
constexpr int kHiddenHitQueueSlots = 24;
constexpr uint32_t kHiddenHopIntervalMs = 300;
constexpr uint32_t kHiddenRedrawMs = 700;
// HiddenHit and HiddenEntry are declared in awok_common.h.

HiddenHit hiddenQueue[kHiddenHitQueueSlots];
volatile int hiddenHead = 0;
volatile int hiddenTail = 0;
HiddenEntry hiddenEntries[kMaxHidden];
int hiddenCount = 0;
int hiddenRevealedCount = 0;
int hiddenHopIndex = 0;
uint32_t lastHiddenHopMs = 0;
uint32_t lastHiddenDrawMs = 0;

// True when the SSID element at [offset] is present and non-empty (not blank
// and not all-null padding).
bool ssidPresent(const uint8_t* payload, int length, int offset,
                 uint8_t& outLen, char* out) {
  if (offset + 2 > length) return false;
  if (payload[offset] != 0x00) return false;  // must be the SSID element
  const uint8_t len = payload[offset + 1];
  if (len == 0 || len > 32 || offset + 2 + len > length) return false;
  bool allZero = true;
  for (int i = 0; i < len; ++i) {
    if (payload[offset + 2 + i] != 0) {
      allZero = false;
      break;
    }
  }
  if (allZero) return false;
  outLen = len;
  memcpy(out, payload + offset + 2, len);
  out[len] = 0;
  return true;
}

void hiddenSnifferCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* packet =
      static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* payload = packet->payload;
  const int length = packet->rx_ctrl.sig_len;
  if (length < 30) return;
  const uint8_t subtype = payload[0] & 0xF0;

  uint8_t kind = 0xFF;
  char ssid[33];
  uint8_t ssidLen = 0;
  ssid[0] = 0;

  if (subtype == 0x80) {  // beacon: is the SSID hidden?
    if (length < 38) return;
    if (payload[36] != 0x00) return;
    if (!ssidPresent(payload, length, 36, ssidLen, ssid)) {
      kind = 0;  // blank/null SSID => hidden AP
      ssidLen = 0;
      ssid[0] = 0;
    } else {
      return;  // named beacon: not our concern here
    }
  } else if (subtype == 0x50) {  // probe response: SSID IE at 36
    if (ssidPresent(payload, length, 36, ssidLen, ssid)) kind = 1;
  } else if (subtype == 0x00) {  // association request: SSID IE at 28
    if (ssidPresent(payload, length, 28, ssidLen, ssid)) kind = 1;
  }
  if (kind == 0xFF) return;

  const int next = (hiddenHead + 1) % kHiddenHitQueueSlots;
  if (next == hiddenTail) return;
  HiddenHit& hit = hiddenQueue[hiddenHead];
  memcpy(hit.bssid, payload + 16, 6);
  hit.channel = packet->rx_ctrl.channel;
  hit.rssi = packet->rx_ctrl.rssi;
  hit.kind = kind;
  hit.ssidLen = ssidLen;
  memcpy(hit.ssid, ssid, ssidLen + 1);
  hiddenHead = next;
}

int hiddenIndexOf(const uint8_t* bssid) {
  for (int i = 0; i < hiddenCount; ++i) {
    bool equal = true;
    for (int j = 0; j < 6; ++j) {
      if (hiddenEntries[i].bssid[j] != bssid[j]) {
        equal = false;
        break;
      }
    }
    if (equal) return i;
  }
  return -1;
}

void mergeHiddenHit(const HiddenHit& hit) {
  int index = hiddenIndexOf(hit.bssid);
  if (hit.kind == 0) {  // hidden beacon: track the BSSID
    if (index < 0) {
      if (hiddenCount >= kMaxHidden) return;
      index = hiddenCount++;
      hiddenEntries[index] = HiddenEntry();
      memcpy(hiddenEntries[index].bssid, hit.bssid, 6);
    }
    hiddenEntries[index].channel = hit.channel;
    hiddenEntries[index].rssi = hit.rssi;
  } else if (index >= 0 && !hiddenEntries[index].revealed) {
    // reveal only for a BSSID we already know is hidden
    hiddenEntries[index].ssid = String(hit.ssid);
    hiddenEntries[index].revealed = true;
    ++hiddenRevealedCount;
  }
}

void drawHiddenReveal() {
  currentView = View::kHiddenReveal;
  display.fillScreen(kBackground);
  drawHeader("HIDDEN REVEAL",
             String(hiddenRevealedCount) + "/" + String(hiddenCount) +
                 " revealed | ch " + String(kDeauthHopChannels[hiddenHopIndex]));
  display.setTextSize(1);
  const int rows = min(hiddenCount, kVisibleRows);
  for (int i = 0; i < rows; ++i) {
    const int y = 48 + i * 22;
    display.setTextColor(hiddenEntries[i].revealed ? kGood : ILI9341_WHITE,
                         kBackground);
    display.setCursor(5, y);
    display.print(hiddenEntries[i].revealed
                      ? clipped(hiddenEntries[i].ssid, 22)
                      : String("(hidden)"));
    display.setTextColor(kMuted, kBackground);
    display.setCursor(5, y + 11);
    display.printf("%s ch%-3d", macToString(hiddenEntries[i].bssid).c_str(),
                   static_cast<int>(hiddenEntries[i].channel));
  }
  if (hiddenCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(30, 145);
    display.print("Listening for hidden APs...");
  }
  drawFooter("Back", "Back");
}

void startHiddenReveal() {
  hiddenCount = 0;
  hiddenRevealedCount = 0;
  hiddenHead = 0;
  hiddenTail = 0;
  hiddenHopIndex = 0;
  lastHiddenHopMs = millis();
  lastHiddenDrawMs = 0;
  signalMonitorActive = false;

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&hiddenSnifferCallback);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(kDeauthHopChannels[0], WIFI_SECOND_CHAN_NONE);

  hiddenRevealActive = true;
  Serial.println("[hidden] reveal started");
  drawHiddenReveal();
}

void stopHiddenReveal() {
  hiddenRevealActive = false;
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true, false);
  Serial.printf("[hidden] stopped; %d/%d revealed\n", hiddenRevealedCount,
                hiddenCount);
}

void updateHiddenReveal() {
  if (!hiddenRevealActive || currentView != View::kHiddenReveal) return;
  while (hiddenTail != hiddenHead) {
    mergeHiddenHit(hiddenQueue[hiddenTail]);
    hiddenTail = (hiddenTail + 1) % kHiddenHitQueueSlots;
  }
  const uint32_t now = millis();
  if (now - lastHiddenHopMs >= kHiddenHopIntervalMs) {
    lastHiddenHopMs = now;
    hiddenHopIndex = (hiddenHopIndex + 1) % kDeauthHopChannelCount;
    esp_wifi_set_channel(kDeauthHopChannels[hiddenHopIndex],
                         WIFI_SECOND_CHAN_NONE);
  }
  if (now - lastHiddenDrawMs >= kHiddenRedrawMs) {
    lastHiddenDrawMs = now;
    drawHiddenReveal();
  }
}
