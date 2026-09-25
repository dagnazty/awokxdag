// AWOKxDAG Network Tools. Original integration inspired by Evil-M5Project's
// scanHosts, scanPorts, CCTV, printer, SIP OPTIONS and UPnP mapping tools by
// 7h30th3r0n3. See THIRD_PARTY_NOTICES.md. No upstream scanner body is copied.
// One in-flight probe; nonblocking sockets; bounded tables and responses.

constexpr int kNetCapacity = kResultCapacity;
constexpr uint16_t kNetSipLocalPort = 49160;
constexpr size_t kNetResponseBytes = 8192;
constexpr uint32_t kNetConnectMs = 450;
constexpr uint32_t kNetResponseMs = 2500;
const uint16_t kNetPorts[] = {21,22,23,25,53,80,110,139,143,443,445,554,631,1883,3306,3389,8080,8443,9100};
const uint16_t kNetCameraPorts[] = {554,8554,80,8000,8080,8899};
const uint16_t kNetPrinterPorts[] = {9100,631,515};
const char* const kNetServiceLabels[] = {"TCP Ports", "LAN Cameras", "Printers", "SIP Services", "UPnP Mappings"};
const char* const kNetServiceDescriptions[] = {"Common ports on discovered hosts", "RTSP / ONVIF service evidence", "Printer service candidates", "SIP OPTIONS service evidence", "Read gateway port mappings"};
const NetJob kNetServiceJobs[] = {NetJob::Ports, NetJob::Cameras, NetJob::Printers, NetJob::Sip, NetJob::Upnp};
constexpr int kNetMenuRows = 4;
constexpr int kNetResultRows = 3;
NetHost* netHosts = nullptr;
NetResult* netResults = nullptr;
char* netResponse = nullptr;
bool netOpen = false;
bool netSetupReturnUpload = false;
NetJob netJob = NetJob::None;
NetJob netLastJob = NetJob::None;
NetStage netStage = NetStage::Idle;
String netSsid, netPassword, netEdit, netStatus;
bool netEditPassword = false;
AwokKeyboard::Mode netKeyMode = AwokKeyboard::Lower;
AwokKeyboard::Tap netKeyTap;
int netMenuPage = 0, netPage = 0, netApPage = 0;
int netMenuSection = 0;  // 0 overview, 1 services, 2 results/upload
int netHostsPage = 0, netServicesPage = 0, netResultsHost = -1;
bool netResultsActions = false;
bool netSetupReturnResults = false;
int netHostCount = 0, netResultCount = 0, netSelectedHost = -1, netSelectedResult = 0;
bool netShowHosts = true, netLimited = false, netSubnetLimited = false;
uint32_t netFirst = 0, netLast = 0, netCursor = 0, netDeadline = 0, netLastDraw = 0;
uint32_t netCompleted = 0, netTimeouts = 0, netRefused = 0, netErrors = 0;
uint32_t netSessionIp = 0, netSessionMask = 0;
int netHostCursor = 0, netHostEnd = 0, netPortCursor = 0;
int netSocket = -1;
String netRequest;
size_t netSent = 0, netReceived = 0;
WiFiUDP netUdp;
String netSipId;
int netUpnpStep = 0, netMappingIndex = 0;
NetworkParse::Url netUpnpUrl;
String netUpnpService;
NetSummary netHostSummary, netResultSummary;
bool netHostsLimited = false;

static void netFreeWorkspace() {
  heap_caps_free(netHosts);
  heap_caps_free(netResults);
  heap_caps_free(netResponse);
  netHosts = nullptr; netResults = nullptr; netResponse = nullptr;
}

static void* netWorkspaceCalloc(size_t count, size_t size) {
  if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) > 0) {
    void* external = heap_caps_calloc(
        count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (external) return external;
  }
  return heap_caps_calloc(count, size,
                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

bool netEnsureWorkspace() {
  if (netHosts && netResults && netResponse) return true;
  netFreeWorkspace();
  netHosts = static_cast<NetHost*>(
      netWorkspaceCalloc(kNetCapacity, sizeof(NetHost)));
  netResults = static_cast<NetResult*>(
      netWorkspaceCalloc(kNetCapacity, sizeof(NetResult)));
  netResponse = static_cast<char*>(netWorkspaceCalloc(kNetResponseBytes + 1, 1));
  if (netHosts && netResults && netResponse) return true;
  netFreeWorkspace();
  return false;
}

// The TLS stack needs two 16 KiB record buffers plus certificate workspace.
// Network Tools normally retains about 20 KiB for discovery results. Uploads
// do not need those tables, so release them before the handshake and recreate
// them lazily when another LAN tool starts.
void netReleaseWorkspaceForUpload() {
  netFreeWorkspace();
  netHostCount = netResultCount = 0;
  netSelectedHost = -1; netSelectedResult = 0;
  netHostSummary = NetSummary{}; netResultSummary = NetSummary{};
  netLastJob = NetJob::None;
  netHostsLimited = netLimited = netSubnetLimited = false;
  netShowHosts = true; netPage = netHostsPage = netServicesPage = 0;
  netResultsHost = -1; netResultsActions = false;
}

void netSnapshot() {
  NetSummary& summary = netLastJob == NetJob::Hosts ? netHostSummary : netResultSummary;
  summary.job = netLastJob;
  snprintf(summary.status, sizeof(summary.status), "%s", netStatus.c_str());
  summary.checked = netCompleted; summary.timeouts = netTimeouts;
  summary.refused = netRefused; summary.errors = netErrors;
  summary.limited = netLimited; summary.subnetLimited = netSubnetLimited;
  summary.hostsLimited = netHostsLimited;
}


bool networkToolsOpen() { return netOpen; }
uint32_t netIpNumber(IPAddress ip) {
  return (uint32_t(ip[0]) << 24) | (uint32_t(ip[1]) << 16) | (uint32_t(ip[2]) << 8) | ip[3];
}
IPAddress netIpAddress(uint32_t ip) { return IPAddress(ip >> 24, ip >> 16, ip >> 8, ip); }
String netIpText(uint32_t ip) { return netIpAddress(ip).toString(); }
bool netExpired() { return int32_t(millis() - netDeadline) >= 0; }
void netWipe(String& value) {
  for (size_t i = 0; i < value.length(); ++i) value.setCharAt(i, 0);
  value = "";
}
const char* netJobName(NetJob job) {
  switch (job) {
    case NetJob::Join: return "Connect";
    case NetJob::Hosts: return "Hosts";
    case NetJob::Ports: return "TCP ports";
    case NetJob::Cameras: return "Camera services";
    case NetJob::Printers: return "Printer candidates";
    case NetJob::Sip: return "SIP services";
    case NetJob::Upnp: return "UPnP mappings";
    default: return "Network Tools";
  }
}
void netCloseSocket() {
  if (netSocket >= 0) { close(netSocket); netSocket = -1; }
  netRequest = ""; netReceived = netSent = 0;
}
void closeNetworkTools() {
  if (!netOpen) return;
  if (netJob == NetJob::Join)
    recordFirmwareAudit("network", "connect", "cancelled", "leaving Network Tools");
  else if (netJob != NetJob::None)
    netFinish("Cancelled (partial)");
  netCloseSocket(); netUdp.stop();
  netJob = NetJob::None; netStage = NetStage::Idle;
  WiFi.setAutoReconnect(false);
  shutdownWifi();
  netWipe(netPassword); netWipe(netEdit); netSsid = "";
  netFreeWorkspace();
  netOpen = false;
  netSetupReturnUpload = false;
}
// Preserve readable large labels when they fit; long IP/service rows and
// connection controls use the small font instead of overflowing Touch buttons.
void netButton(int x, int y, int w, int h, const String& label) {
  if (label.length() * 12 <= size_t(w - 12)) drawButton(x, y, w, h, label);
  else drawSmallButton(x, y, w, h, clipped(label, (w - 12) / 6), kAccent);
}
void netFooter(const char* left, const char* right) {
  netButton(6, 284, 106, 30, left);
  netButton(128, 284, 106, 30, right);
}
void netText(int y, const String& value) {
  display.setTextSize(1); display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(y));
#ifdef AWOK_MINI_DISPLAY
  display.print(value);
#else
  display.print(clipped(value, 37));
#endif
}
String netConnectionLabel() {
  if (netJob == NetJob::Join) return "Connecting...";
  return WiFi.status() == WL_CONNECTED ? "Wi-Fi " + WiFi.localIP().toString() : "Wi-Fi disconnected";
}
bool netHostSelectionValid(int index) {
  return netHosts && index >= 0 && index < netHostCount;
}
void netCard(int row, const String& title, const String& detail) {
  const int y = 58 + row * 50;
#ifdef AWOK_MINI_DISPLAY
  display.button(8, y, 224, 44, (title + " | " + detail).c_str(), kAccent);
#else
  display.drawRoundRect(scaleX(8), scaleY(y), scaleX(224), scaleY(44), 5, kAccent);
  display.setTextSize(title.length() <= 17 ? 2 : 1);
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(scaleX(16), scaleY(y + 5)); display.print(clipped(title, 34));
  display.setTextSize(1); display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(16), scaleY(y + 29)); display.print(clipped(detail, 34));
#endif
}
void netPager(const char* back, int page, int pages, bool allowBack) {
  if (allowBack) drawSmallButton(4, 280, 72, 36, back, kMuted);
  if (page > 0) drawSmallButton(84, 280, 72, 36, "Prev", kAccent);
  if (page + 1 < pages) drawSmallButton(164, 280, 72, 36, "Next", kAccent);
}
void netRememberResultsPage() {
  if (netShowHosts) netHostsPage = netPage; else netServicesPage = netPage;
}
void netOpenResults(bool hosts) {
  netRememberResultsPage();
  netShowHosts = hosts; netPage = hosts ? netHostsPage : netServicesPage;
  netResultsActions = false;
  netStatus = hosts ? netHostSummary.status : netResultSummary.status;
  drawNetworkResults();
}
void netOpenSetup(bool fromResults) {
  netSetupReturnUpload = false; netSetupReturnResults = fromResults;
  drawNetworkSetup();
}
void netReturnFromSetup() {
  if (netSetupReturnUpload) drawWardriveUpload();
  else if (netSetupReturnResults) drawNetworkResults();
  else drawNetworkMenu();
}
void drawNetworkMenu() {
  currentView = View::kNetworkMenu;
  display.fillScreen(kBackground);
  drawHeader(netMenuSection == 1 ? "LAN SERVICES" : netMenuSection == 2 ? "RESULTS / UPLOAD" : "NETWORK TOOLS", netConnectionLabel());
  if (netMenuSection == 0) {
    netMenuPage = 0;
    netCard(0, "Connection", WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "Choose Wi-Fi and join first");
    netCard(1, "Hosts", String(netHostCount) + " found | discover / select");
    netCard(2, "Services", "Ports / cameras / printers / more");
    netCard(3, "Results & Upload", "Previous findings / wardrive CSV");
  } else if (netMenuSection == 1) {
    netMenuPage = max(0, min(netMenuPage, 1));
    netText(44, netMenuPage ? "Scope: connected gateway" : "Scope: all discovered hosts");
    for (int row = 0; row < kNetMenuRows; ++row) {
      const int index = netMenuPage * kNetMenuRows + row;
      if (index >= 5) break;
      netCard(row, kNetServiceLabels[index], kNetServiceDescriptions[index]);
    }
  } else {
    netMenuPage = 0;
    netCard(0, "Last hosts", String(netHostCount) + " hosts | inspect / export");
    netCard(1, "Last services", String(netResultCount) + " results | inspect / export");
    netCard(2, "Wardrive Upload", "Choose CSV, network, destination");
    netText(222, "Uploads start only with Upload.");
    netText(238, "Upload clears previous LAN results.");
  }
  if (netMenuSection != 2) netText(264, netStatus);
  netPager("Back", netMenuPage, netMenuSection == 1 ? 2 : 1, true);
}
void openNetworkTools() {
  signalMonitorActive = false;
  if (netOpen) { drawNetworkMenu(); return; }
  releaseBleMemory();
  if (!netEnsureWorkspace()) {
    showRadioError("Network Tools: low memory"); return;
  }
  netOpen = true; netHostCount = netResultCount = 0; netMenuPage = netPage = 0;
  netSelectedHost = -1; netJob = netLastJob = NetJob::None;
  netMenuSection = 0; netHostsPage = netServicesPage = 0; netResultsHost = -1;
  netResultsActions = netSetupReturnResults = netSetupReturnUpload = false;
  netHostSummary = NetSummary{}; netResultSummary = NetSummary{}; netHostsLimited = false;
  netLimited = netSubnetLimited = false; netStatus = "";
  netSsid = selectedWifi.ssid;
  drawNetworkMenu();
}
void drawNetworkSetup() {
  currentView = View::kNetworkSetup;
  display.fillScreen(kBackground);
  drawHeader("CONNECT WI-FI", netConnectionLabel());
  if (netJob == NetJob::Join) {
    netText(66, "Joining " + netSsid);
    netText(92, "Waiting for network (up to 20s).");
    netText(118, "Password cleared after this attempt.");
    drawSmallButton(8, 218, 224, 44, "Cancel connection", kMuted);
    return;
  }
  netCard(0, "SSID", netSsid.length() ? netSsid : "Select to enter network name");
  netCard(1, "Password", netPassword.length() ? "********" : "Empty (open network)");
  netCard(2, "Choose scanned AP", "Pick from scan / rescan networks");
  netCard(3, "Join network", "Credentials kept in RAM only");
  netText(264, netStatus);
  drawSmallButton(4, 280, 112, 36, "Back", kMuted);
  if (WiFi.status() == WL_CONNECTED) drawSmallButton(124, 280, 112, 36, "Disconnect", kAccent);
}
void drawNetworkEditor() {
  currentView = View::kNetworkEdit;
  const int limit = netEditPassword ? 63 : 32;
  String preview;
  if (netEditPassword) {
    for (size_t i = 0; i < netEdit.length(); ++i) preview += '*';
    // T9 taps cycle the last character in place; show it in the clear while it
    // is still being worked on so the layout is usable, then mask it on commit.
    if (netKeyTap.index >= 0 && netEdit.length())
      preview.setCharAt(preview.length() - 1, netEdit[netEdit.length() - 1]);
  } else preview = netEdit;
#ifdef AWOK_MINI_DISPLAY
  display.keyboard(netEditPassword ? "WI-FI PASSWORD" : "NETWORK SSID",
                   preview.c_str(), netEdit.length(), limit, netKeyMode, netKeyTap.index >= 0);
#else
  display.fillScreen(kBackground);
  drawHeader(netEditPassword ? "PASSWORD" : "NETWORK SSID", "Repeat tap: cycle   # next letter");
  display.fillRoundRect(scaleX(6), scaleY(45), scaleX(228), scaleY(28), 5, kPanel);
  display.drawRoundRect(scaleX(6), scaleY(45), scaleX(228), scaleY(28), 5, kAccent);
  display.setTextWrap(false);
  display.setTextSize(2);
  display.setTextColor(ILI9341_WHITE, kPanel);
  display.setCursor(scaleX(12), scaleY(51));
  // Show the tail rather than clipping the new characters off the right.
  if (preview.length() > 17) preview = preview.substring(preview.length() - 17);
  display.print(preview);
  display.print(netKeyTap.index >= 0 ? '^' : '_');
  display.setTextSize(1);
  display.setTextColor(netEdit.length() == size_t(limit) ? kWarn : kMuted, kBackground);
  display.setCursor(scaleX(8), scaleY(79));
  display.printf("%u/%d%s", unsigned(netEdit.length()), limit,
                 netEdit.length() == size_t(limit) ? "  Full" : "");
  display.setCursor(scaleX(148), scaleY(79));
  display.printf("%s  %s", AwokKeyboard::modeName(netKeyMode),
                 netKeyTap.index >= 0 ? "Cycling" : "Ready");
  // key() rects are design-space (so hit() matches design-space touch); draw
  // each scaled up, centring the native-size label within the scaled rect.
  for (int i = 0; i < AwokKeyboard::kSlots; ++i) {
    const auto key = AwokKeyboard::key(i, netKeyMode);
    if (!key.valid()) continue;
    const int kx = scaleX(key.x), ky = scaleY(key.y);
    const int kw = scaleX(key.x + key.w) - kx, kh = scaleY(key.y + key.h) - ky;
    const uint16_t color = key.action == AwokKeyboard::Done ? kGood :
        key.action == AwokKeyboard::Delete ? kWarn :
        key.action == AwokKeyboard::Cancel ? kMuted : kAccent;
    display.fillRoundRect(kx, ky, kw, kh, 3, kPanel);
    display.drawRoundRect(kx, ky, kw, kh, 3, color);
    const bool group = key.sublabel[0];
    const int size = strlen(key.label) == 1 ? 2 : 1;
    display.setTextSize(size);
    display.setTextColor(ILI9341_WHITE, kPanel);
    display.setCursor(kx + (kw - int(strlen(key.label)) * 6 * size) / 2,
                      ky + (group ? scaleY(4) : (kh - 8 * size) / 2));
    display.print(key.label);
    if (group) {
      display.setTextSize(1);
      display.setTextColor(color, kPanel);
      display.setCursor(kx + (kw - int(strlen(key.sublabel)) * 6) / 2,
                        ky + scaleY(26));
      display.print(key.sublabel);
    }
  }
#endif
}
void drawNetworkAps() {
  currentView = View::kNetworkAps;
  display.fillScreen(kBackground);
  drawHeader("CHOOSE NETWORK", netConnectionLabel());
  const int pages = max(1, (wifiCount + kNetMenuRows - 1) / kNetMenuRows);
  netApPage = max(0, min(netApPage, pages - 1));
  netText(44, String(wifiCount) + " APs | " + String(netApPage + 1) + "/" + String(pages));
  for (int row = 0; row < kNetMenuRows; ++row) {
    const int index = netApPage * kNetMenuRows + row;
    if (index >= wifiCount) break;
    netCard(row, wifiEntries[index].ssid.length() ? wifiEntries[index].ssid : "<hidden>",
            wifiEntries[index].ssid.length() ? "Select, then enter key and Join" : "Enter hidden SSID manually");
  }
  if (!wifiCount) netText(78, "No APs yet. Scan to find networks.");
  drawSmallButton(4, 280, 56, 36, "Back", kMuted);
  if (netApPage > 0) drawSmallButton(64, 280, 52, 36, "Prev", kAccent);
  if (netApPage + 1 < pages) drawSmallButton(124, 280, 52, 36, "Next", kAccent);
  drawSmallButton(180, 280, 56, 36, "Scan", kAccent);
}
int netResultPages() {
  return max(1, ((netShowHosts ? netHostCount : netResultCount) + kNetResultRows - 1) / kNetResultRows);
}
void drawNetworkResults() {
  currentView = View::kNetworkResults;
  display.fillScreen(kBackground);
  const int count = netShowHosts ? netHostCount : netResultCount;
  const int pages = netResultPages();
  netPage = max(0, min(netPage, pages - 1));
  netRememberResultsPage();
  const bool running = netJob != NetJob::None;
  drawHeader(netResultsActions ? "RESULT ACTIONS" : netShowHosts ? "LAN HOSTS" : "LAN RESULTS", netConnectionLabel());
  if (netResultsActions && !running) {
    netCard(0, netShowHosts ? "Service results" : "Host list", netShowHosts ? String(netResultCount) + " retained service results" : String(netHostCount) + " discovered hosts");
    netCard(1, "Save CSV", netShowHosts ? "Save this host snapshot to SD" : "Save this service snapshot to SD");
    netCard(2, "Discover hosts", "New discovery replaces old results");
    netCard(3, "Connection", "Join / change network");
    netText(264, netStatus);
    drawSmallButton(4, 280, 112, 36, "Back", kMuted);
    drawSmallButton(124, 280, 112, 36, "Tools", kAccent);
    return;
  }
  netText(44, String(count) + " found | " + String(netPage + 1) + "/" + String(pages) + (running ? " | scanning" : " | stopped"));
  for (int row = 0; row < kNetResultRows; ++row) {
    const int index = netPage * kNetResultRows + row;
    if (index >= count) break;
    netCard(row, netShowHosts ? netIpText(netHosts[index].ip) : netIpText(netResults[index].ip) + ":" + String(netResults[index].port),
            netShowHosts ? "MAC " + macToString(netHosts[index].mac) : String(netResults[index].kind));
  }
  if (!count) {
    netText(76, running ? "Waiting for responses..." : "No retained entries.");
    netText(98, running ? "Stop keeps partial results." : netShowHosts ? "Discover hosts to populate this list." : "Choose Services to run a check.");
    if (!running) drawSmallButton(8, 134, 224, 44, netShowHosts ? "Discover hosts" : "Choose services", kAccent);
  }
  const NetSummary& summary = netShowHosts ? netHostSummary : netResultSummary;
  netText(210, netStatus);
  netText(220, String(running ? netCompleted : summary.checked) + " checked; " + String(running ? netTimeouts : summary.timeouts) + " timeout; " + String(running ? netErrors : summary.errors) + " err");
  netText(230, (running ? netLimited : summary.limited) ? "Result cap reached; export is partial" : (running ? netSubnetLimited || netHostsLimited : summary.subnetLimited || summary.hostsLimited) ? "Host discovery limited; partial scope" : "");
  drawSmallButton(8, 242, 224, 36, running ? "Stop scan" : "Actions / Save CSV", running ? kWarn : kAccent);
  netPager(!netShowHosts && netHostSelectionValid(netResultsHost) ? "Host" : "Tools", netPage, pages, !running);
}
void drawNetworkHost() {
  if (!netHostSelectionValid(netSelectedHost)) { netOpenResults(true); return; }
  currentView = View::kNetworkHost;
  display.fillScreen(kBackground);
  drawHeader("HOST SERVICES", netConnectionLabel());
  netText(44, "Target: " + netIpText(netHosts[netSelectedHost].ip));
  for (int i = 0; i < 4; ++i) netCard(i, kNetServiceLabels[i], "Check only this host");
  netText(264, "MAC: " + macToString(netHosts[netSelectedHost].mac));
  drawSmallButton(4, 280, 112, 36, "Hosts", kMuted);
  drawSmallButton(124, 280, 112, 36, "Tools", kAccent);
}
void drawNetworkDetail() {
  if (!netResults || netSelectedResult < 0 || netSelectedResult >= netResultCount) { drawNetworkResults(); return; }
  currentView = View::kNetworkDetail;
  NetResult& item = netResults[netSelectedResult];
  display.fillScreen(kBackground);
  drawHeader("SERVICE DETAIL", netConnectionLabel());
  netText(48, netIpText(item.ip) + ":" + String(item.port) + " " + item.kind);
  const String detail(item.detail);
  for (unsigned i = 0; i < detail.length(); i += 36) netText(72 + (i / 36) * 16, detail.substring(i, i + 36));
  netText(244, "Evidence only; identity unverified.");
  drawSmallButton(4, 280, 232, 36, "Back to results", kMuted);
}
bool netSaveCsv() {
  if (!sdReady) return false;
  if (netJob != NetJob::None && (netShowHosts == (netJob == NetJob::Hosts))) netSnapshot();
  const NetSummary& summary = netShowHosts ? netHostSummary : netResultSummary;
  const char* path = netShowHosts ? "/awokxdag/latest_lan_hosts.csv" : "/awokxdag/latest_network_services.csv";
  const String temp = String(path) + ".tmp";
  SD.remove(temp.c_str()); File file = SD.open(temp.c_str(), FILE_WRITE);
  if (!file) return false;
  file.println("uptime_ms,scan,status,network_ssid,local_ip,ip,mac,port,kind,evidence,result_limit,subnet_limit,checked,timeouts,refused,errors,host_discovery_limited");
  const int count = netShowHosts ? netHostCount : netResultCount;
  // Keep an explicit summary row even when nothing responded.
  for (int i = -1; i < count; ++i) {
    file.print(millis()); file.print(',');
    file.print(csvField(netJobName(summary.job))); file.print(',');
    file.print(csvField(summary.status)); file.print(','); file.print(csvField(summary.ssid)); file.print(',');
    file.print(netIpText(summary.ip)); file.print(',');
    if (i < 0) file.print(",,,summary,");
    else if (netShowHosts) {
      file.print(netIpText(netHosts[i].ip)); file.print(','); file.print(macToString(netHosts[i].mac));
      file.print(",,ARP,ARP cache observation");
    } else {
      const NetResult& item = netResults[i]; file.print(netIpText(item.ip)); file.print(",,");
      file.print(item.port); file.print(','); file.print(csvField(item.kind)); file.print(','); file.print(csvField(item.detail));
    }
    file.print(','); file.print(summary.limited ? "true" : "false"); file.print(',');
    file.print(summary.subnetLimited ? "true" : "false"); file.print(','); file.print(summary.checked); file.print(',');
    file.print(summary.timeouts); file.print(','); file.print(summary.refused); file.print(','); file.print(summary.errors); file.print(','); file.print(summary.hostsLimited ? "true" : "false"); file.println();
  }
  bool ok = !file.getWriteError(); file.close();
  if (!ok) { SD.remove(temp.c_str()); return false; }
  SD.remove(path); return SD.rename(temp.c_str(), path);
}
void netAdd(uint32_t ip, uint16_t port, const char* kind, const String& evidence) {
  for (int i = 0; i < netResultCount; ++i)
    if (netResults[i].ip == ip && netResults[i].port == port && !strcmp(netResults[i].kind, kind) && evidence == netResults[i].detail) return;
  if (netResultCount >= kNetCapacity) { netLimited = true; return; }
  NetResult& out = netResults[netResultCount++]; out.ip = ip; out.port = port;
  snprintf(out.kind, sizeof(out.kind), "%s", kind);
  String clean = evidence; clean.replace('\r', ' '); clean.replace('\n', ' ');
  snprintf(out.detail, sizeof(out.detail), "%s", clean.c_str());
}
void netFinish(const String& message) {
  const NetJob completed = netJob;
  netCloseSocket(); netUdp.stop(); netJob = NetJob::None; netStage = NetStage::Idle;
  netStatus = message;
  if (completed == NetJob::Hosts) netHostsLimited = netLimited || netSubnetLimited || message.startsWith("Cancelled") || message.startsWith("Connection");
  netSnapshot();
  recordFirmwareAudit("network", netJobName(completed), message.c_str(), String(netCompleted) + " probes; " + String(netResultCount) + " service results");
  const bool saved = netSaveCsv();
  netStatus += saved ? " | SD saved" : sdReady ? " | SD write failed" : " | no SD";
  netSnapshot();
  drawNetworkResults();
}
void netDisconnect() {
  netCloseSocket(); netUdp.stop(); netJob = NetJob::None; netStage = NetStage::Idle;
  WiFi.setAutoReconnect(false); shutdownWifi(); netWipe(netPassword); netWipe(netEdit);
  netHostCount = netResultCount = 0; netSelectedHost = -1;
  netHostSummary = NetSummary{}; netResultSummary = NetSummary{};
  netHostsLimited = false; netStatus = "Disconnected";
  netPage = netHostsPage = netServicesPage = 0; netResultsHost = -1; netResultsActions = false;
}
void netJoin() {
  if (!netSsid.length() || netSsid.length() > 32 || (netPassword.length() && netPassword.length() < 8)) {
    netStatus = "SSID 1-32; WPA key 8-63 or empty"; drawNetworkSetup(); return;
  }
  netHostCount = netResultCount = 0; netSelectedHost = -1;
  netHostSummary = NetSummary{}; netResultSummary = NetSummary{};
  netLastJob = NetJob::None; netHostsLimited = false;
  netPage = netHostsPage = netServicesPage = 0; netResultsHost = -1; netResultsActions = false;
  WiFi.persistent(false);
  if (!ensureWifiStation()) { netStatus = "Wi-Fi initialization failed"; drawNetworkSetup(); return; }
  WiFi.setAutoReconnect(false); esp_wifi_set_storage(WIFI_STORAGE_RAM);
  WiFi.disconnect(false, false);
  WiFi.begin(netSsid.c_str(), netPassword.c_str()); netWipe(netPassword);
  netJob = NetJob::Join; netDeadline = millis() + 20000; netStatus = "Connecting...";
  recordFirmwareAudit("network", "connect", "started", "user-selected network");
  drawNetworkSetup();
}
err_t netArpOnTcpip(tcpip_api_call_data* base) {
  NetArpCall* query = reinterpret_cast<NetArpCall*>(base);
  esp_netif_t* iface = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (!iface) return ERR_IF;
  netif* ni = static_cast<netif*>(esp_netif_get_netif_impl(iface));
  if (!ni) return ERR_IF;
  ip4_addr_t address; address.addr = htonl(query->ip);
  if (query->send) return etharp_request(ni, &address);
  eth_addr* mac = nullptr; const ip4_addr_t* found = nullptr;
  query->found = etharp_find_addr(ni, &address, &mac, &found) >= 0 && mac;
  if (query->found) memcpy(query->mac, mac->addr, 6);
  return ERR_OK;
}
void netStartJob(NetJob job, int host) {
  if (netJob != NetJob::None) return;
  if (!netEnsureWorkspace()) {
    netStatus = "Low memory; cannot start network tool";
    drawNetworkMenu();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) { netStatus = "Connect before scanning"; netOpenSetup(currentView == View::kNetworkResults || currentView == View::kNetworkHost); return; }
  netSessionIp = netIpNumber(WiFi.localIP()); netSessionMask = netIpNumber(WiFi.subnetMask());
  if (!NetworkParse::range(netSessionIp, netSessionMask, netFirst, netLast)) {
    netStatus = "Unsupported subnet (/31, /32 or mask)"; netOpenSetup(currentView == View::kNetworkResults || currentView == View::kNetworkHost); return;
  }
  if (job != NetJob::Hosts && job != NetJob::Upnp && netHostCount &&
      (netHostSummary.ip != netSessionIp || netHostSummary.mask != netSessionMask ||
       strcmp(netHostSummary.ssid, WiFi.SSID().c_str()))) {
    netHostCount = 0; netSelectedHost = -1;
  }
  if (job != NetJob::Hosts && job != NetJob::Upnp && !netHostCount) {
    netOpenResults(true); netStatus = "Discover hosts before service checks"; drawNetworkResults(); return;
  }
  if (host >= 0 && !netHostSelectionValid(host)) {
    netOpenResults(true); netStatus = "Host list changed; select a host"; drawNetworkResults(); return;
  }
  netRememberResultsPage(); netResultsActions = false; netResultsHost = host;
  if (job == NetJob::Hosts) netHostsPage = netServicesPage = 0;
  else netServicesPage = 0;
  NetSummary& summary = job == NetJob::Hosts ? netHostSummary : netResultSummary;
  summary = NetSummary{}; summary.ip = netSessionIp; summary.mask = netSessionMask;
  snprintf(summary.ssid, sizeof(summary.ssid), "%s", WiFi.SSID().c_str());
  netJob = netLastJob = job; netCompleted = netTimeouts = netRefused = netErrors = 0;
  netPage = 0; netLimited = false; netResultCount = 0;
  if (job == NetJob::Hosts) { netResultSummary = NetSummary{}; netHostsLimited = false; }
  else netSubnetLimited = netHostSummary.subnetLimited;
  netShowHosts = job == NetJob::Hosts;
  netStatus = String("Scanning ") + netJobName(job);
  if (job == NetJob::Hosts) {
    netHostCount = 0; netSelectedHost = -1; netSubnetLimited = netLast - netFirst + 1 > 1024;
    if (netSubnetLimited) {
      netFirst = max(netFirst, (netSessionIp & 0xffffff00U) + 1);
      netLast = min(netLast, (netSessionIp & 0xffffff00U) + 254);
    }
    netCursor = netFirst; netStage = NetStage::ArpSend;
  } else if (job == NetJob::Upnp) {
    netSubnetLimited = false; netMappingIndex = 0; netUpnpStep = 0;
    if (!netUdp.begin(0)) { netFinish("UDP initialization failed"); return; }
    const char* request = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n\r\n";
    if (!netUdp.beginPacket(IPAddress(239,255,255,250), 1900) || netUdp.write((const uint8_t*)request, strlen(request)) != strlen(request) || !netUdp.endPacket()) {
      netFinish("SSDP send failed"); return;
    }
    netStage = NetStage::SsdpWait; netDeadline = millis() + 3500;
  } else {
    netHostCursor = host >= 0 ? host : 0; netHostEnd = host >= 0 ? host + 1 : netHostCount;
    netPortCursor = 0; netStage = NetStage::Probe;
    if (job == NetJob::Sip && !netUdp.begin(kNetSipLocalPort)) { netFinish("UDP initialization failed"); return; }
  }
  recordFirmwareAudit("network", netJobName(job), "started", host >= 0 ? netIpText(netHosts[host].ip) : "connected LAN");
  netSnapshot();
  drawNetworkResults();
}
uint16_t netCurrentPort() {
  if (netJob == NetJob::Ports) return kNetPorts[netPortCursor];
  if (netJob == NetJob::Cameras) return kNetCameraPorts[netPortCursor];
  if (netJob == NetJob::Printers) return kNetPrinterPorts[netPortCursor];
  return 5060;
}
void netNextProbe() {
  netCloseSocket(); ++netCompleted;
  const int count = netJob == NetJob::Ports ? sizeof(kNetPorts)/sizeof(kNetPorts[0]) : netJob == NetJob::Cameras ? sizeof(kNetCameraPorts)/sizeof(kNetCameraPorts[0]) : netJob == NetJob::Printers ? 3 : 1;
  if (++netPortCursor >= count) { netPortCursor = 0; ++netHostCursor; }
  if (netHostCursor >= netHostEnd) netFinish("Complete (timeouts are inconclusive)");
  else netStage = NetStage::Probe;
}
void netTcpStart(uint32_t ip, uint16_t port, const String& request) {
  netCloseSocket();
  netRequest = request; netResponse[0] = 0;
  netSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (netSocket < 0 || fcntl(netSocket, F_SETFL, O_NONBLOCK) < 0) {
    ++netErrors;
    if (netJob == NetJob::Upnp) netFinish("Socket allocation failed"); else netNextProbe();
    return;
  }
  sockaddr_in address = {}; address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(ip); address.sin_port = htons(port);
  int result = connect(netSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address));
  if (result < 0 && errno != EINPROGRESS && errno != EWOULDBLOCK) {
    if (errno == ECONNREFUSED) ++netRefused; else ++netErrors;
    if (netJob == NetJob::Upnp) netFinish("Router connection failed"); else netNextProbe();
    return;
  }
  netStage = NetStage::Connecting; netDeadline = millis() + kNetConnectMs;
}
String netHttpRequest(const char* method, const NetworkParse::Url& url, const String& body, const String& action) {
  String request = String(method) + " " + url.path + " HTTP/1.1\r\nHost: " + netIpText(url.ip) + ":" + String(url.port) + "\r\nConnection: close\r\n";
  if (body.length()) {
    request += "Content-Type: text/xml; charset=\"utf-8\"\r\nContent-Length: " + String(body.length()) + "\r\n";
    if (action.length()) request += "SOAPAction: \"" + action + "\"\r\n";
  }
  return request + "\r\n" + body;
}
void netRequestMapping() {
  const String body = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:GetGenericPortMappingEntry xmlns:u=\"" + netUpnpService + "\"><NewPortMappingIndex>" + String(netMappingIndex) + "</NewPortMappingIndex></u:GetGenericPortMappingEntry></s:Body></s:Envelope>";
  netTcpStart(netUpnpUrl.ip, netUpnpUrl.port, netHttpRequest("POST", netUpnpUrl, body, netUpnpService + "#GetGenericPortMappingEntry"));
}
bool netResolveControl(const char* value, const NetworkParse::Url& base, NetworkParse::Url& out) {
  String decoded(value); decoded.replace("&amp;", "&");
  if (decoded.startsWith("http://")) {
    if (!NetworkParse::url(decoded.c_str(), out)) return false;
  } else {
    if (decoded.indexOf(":") >= 0 || decoded.startsWith("//")) return false;
    String path(base.path);
    if (decoded.startsWith("/")) path = decoded;
    else path = path.substring(0, path.lastIndexOf('/') + 1) + decoded;
    const String absolute = "http://" + netIpText(base.ip) + ":" + String(base.port) + path;
    if (!NetworkParse::url(absolute.c_str(), out)) return false;
  }
  // The mapping viewer only follows literal-IP URLs on the connected gateway.
  return out.ip == netIpNumber(WiFi.gatewayIP());
}
void netUpnpResponse() {
  char* body = nullptr; size_t size = 0;
  if (!NetworkParse::httpBody(netResponse, netReceived, body, size)) { ++netErrors; netFinish("Invalid/truncated router response"); return; }
  int code = 0; sscanf(netResponse, "HTTP/%*s %d", &code);
  if (netUpnpStep == 1) {
    if (code != 200) { netFinish("Router description HTTP " + String(code)); return; }
    NetworkParse::Url base = netUpnpUrl;
    char value[320];
    if (NetworkParse::xml(body, "URLBase", value, sizeof(value))) {
      if (!NetworkParse::url(value, base) || base.ip != netIpNumber(WiFi.gatewayIP())) {
        netFinish("Unsupported router URLBase"); return;
      }
    }
    const char* cursor = body;
    // Heap workspace avoids adding a large temporary to the Arduino loop stack.
    char* section = static_cast<char*>(malloc(2048));
    if (!section) { netFinish("Low memory parsing router"); return; }
    bool found = false;
    while (NetworkParse::block(cursor, "service", section, 2048)) {
      char service[100];
      if (!NetworkParse::xml(section, "serviceType", service, sizeof(service))) continue;
      if (strcmp(service, "urn:schemas-upnp-org:service:WANIPConnection:1") &&
          strcmp(service, "urn:schemas-upnp-org:service:WANIPConnection:2") &&
          strcmp(service, "urn:schemas-upnp-org:service:WANPPPConnection:1")) continue;
      if (!NetworkParse::xml(section, "controlURL", value, sizeof(value)) ||
          !netResolveControl(value, base, netUpnpUrl)) continue;
      netUpnpService = service; found = true; break;
    }
    free(section);
    if (!found) { netFinish("No supported IGD control service"); return; }
    netUpnpStep = 2; netRequestMapping(); return;
  }
  char value[128];
  if (NetworkParse::xml(body, "errorCode", value, sizeof(value))) {
    if (!strcmp(value, "713")) netFinish("Complete: end of mapping table");
    else netFinish("Router SOAP error " + String(value));
    return;
  }
  if (code != 200) { netFinish("Mapping HTTP " + String(code)); return; }
  char internal[24], external[12], internalPort[12], protocol[12], enabled[12];
  if (!NetworkParse::xml(body, "NewInternalClient", internal, sizeof(internal)) ||
      !NetworkParse::xml(body, "NewExternalPort", external, sizeof(external)) ||
      !NetworkParse::xml(body, "NewInternalPort", internalPort, sizeof(internalPort)) ||
      !NetworkParse::xml(body, "NewProtocol", protocol, sizeof(protocol)) ||
      !NetworkParse::xml(body, "NewEnabled", enabled, sizeof(enabled))) {
    ++netErrors; netFinish("Incomplete mapping response"); return;
  }
  char* end; unsigned long port = strtoul(external, &end, 10);
  if (!*external || *end || port > 65535 || !port) { netFinish("Invalid mapping port"); return; }
  String detail = String(protocol) + " external " + external + " -> " + internal + ":" + internalPort + " enabled=" + enabled;
  if (NetworkParse::xml(body, "NewPortMappingDescription", value, sizeof(value))) detail += " " + String(value);
  netAdd(netIpNumber(WiFi.gatewayIP()), port, "UPnP mapping", detail);
  ++netCompleted;
  if (++netMappingIndex >= min(64, kNetCapacity)) { netLimited = true; netFinish("Mapping limit reached (partial)"); return; }
  netRequestMapping();
}
void netReadProbeResponse() {
  if (netJob == NetJob::Upnp) { netUpnpResponse(); return; }
  const uint32_t ip = netHosts[netHostCursor].ip; const uint16_t port = netCurrentPort();
  if (netJob == NetJob::Cameras && (port == 554 || port == 8554)) {
    int code = 0;
    if (sscanf(netResponse, "RTSP/1.0 %d", &code) == 1 && code >= 100 && code <= 599) {
      char server[90] = {};
      NetworkParse::header(netResponse, "Server", server, sizeof(server));
      netAdd(ip, port, "RTSP", "RTSP status " + String(code) + "; camera candidate " + server);
    }
  } else if (netJob == NetJob::Cameras) {
    char* body = nullptr; size_t size = 0;
    if (NetworkParse::httpBody(netResponse, netReceived, body, size)) {
      if (strstr(body, "onvif.org") && (strstr(body, "GetDeviceInformationResponse") || strstr(body, "NotAuthorized"))) {
        char maker[60] = {}, model[60] = {};
        NetworkParse::xml(body, "Manufacturer", maker, sizeof(maker));
        NetworkParse::xml(body, "Model", model, sizeof(model));
        netAdd(ip, port, "ONVIF", *maker || *model ? String(maker) + " " + model : "ONVIF authentication required");
      }
    } else ++netErrors;
  }
  netNextProbe();
}
void netBeginProbe() {
  uint32_t ip = netHosts[netHostCursor].ip; uint16_t port = netCurrentPort();
  netStatus = String(netJobName(netJob)) + " " + netIpText(ip) + ":" + String(port);
  if (netJob == NetJob::Sip) {
    netSipId = String(esp_random(), HEX) + "@awokxdag";
    String request = "OPTIONS sip:" + netIpText(ip) + " SIP/2.0\r\nVia: SIP/2.0/UDP " + WiFi.localIP().toString() + ":" + String(kNetSipLocalPort) + ";branch=z9hG4bK" + String(esp_random(), HEX) + ";rport\r\nFrom: <sip:scan@awokxdag>;tag=1\r\nTo: <sip:" + netIpText(ip) + ">\r\nCall-ID: " + netSipId + "\r\nCSeq: 1 OPTIONS\r\nMax-Forwards: 1\r\nContent-Length: 0\r\n\r\n";
    if (!netUdp.beginPacket(netIpAddress(ip), 5060) || netUdp.write((const uint8_t*)request.c_str(), request.length()) != request.length() || !netUdp.endPacket()) {
      ++netErrors; netNextProbe(); return;
    }
    netStage = NetStage::SipWait; netDeadline = millis() + 700; return;
  }
  String request;
  if (netJob == NetJob::Cameras) {
    if (port == 554 || port == 8554) request = "OPTIONS rtsp://" + netIpText(ip) + ":" + String(port) + "/ RTSP/1.0\r\nCSeq: 1\r\nUser-Agent: AxD\r\n\r\n";
    else {
      NetworkParse::Url url; url.ip = ip; url.port = port;
      strcpy(url.path, "/onvif/device_service");
      const String body = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\"><s:Body><GetDeviceInformation xmlns=\"http://www.onvif.org/ver10/device/wsdl\"/></s:Body></s:Envelope>";
      request = netHttpRequest("POST", url, body, "http://www.onvif.org/ver10/device/wsdl/GetDeviceInformation");
      request.replace("text/xml; charset=\"utf-8\"", "application/soap+xml; charset=\"utf-8\"; action=\"http://www.onvif.org/ver10/device/wsdl/GetDeviceInformation\"");
    }
  }
  netTcpStart(ip, port, request);
}
void netUpdateTcp() {
  if (netSocket < 0) return;
  if (netStage == NetStage::Connecting) {
    fd_set writes, errors; FD_ZERO(&writes); FD_ZERO(&errors);
    FD_SET(netSocket, &writes); FD_SET(netSocket, &errors); timeval zero = {};
    const int ready = select(netSocket + 1, nullptr, &writes, &errors, &zero);
    if (ready < 0) {
      ++netErrors;
      if (netJob == NetJob::Upnp) netFinish("Socket error"); else netNextProbe();
      return;
    }
    if (ready) {
      int error = 0; socklen_t length = sizeof(error);
      if (getsockopt(netSocket, SOL_SOCKET, SO_ERROR, &error, &length) < 0) error = errno;
      if (error) {
        if (error == ECONNREFUSED) ++netRefused; else ++netErrors;
        if (netJob == NetJob::Upnp) netFinish("Router connection failed"); else netNextProbe();
        return;
      }
      if (!netRequest.length()) {
        const uint16_t port = netCurrentPort();
        const char* kind = netJob == NetJob::Ports ? "TCP open" : port == 9100 ? "Raw print?" : port == 631 ? "IPP?" : "LPD?";
        netAdd(netHosts[netHostCursor].ip, port, kind, netJob == NetJob::Ports ? "TCP connection accepted; service not verified" : "Printer candidate by open port; no print job sent");
        netNextProbe(); return;
      }
      netStage = NetStage::Sending; netDeadline = millis() + kNetResponseMs;
    }
  }
  if (netStage == NetStage::Sending) {
    int written = send(netSocket, netRequest.c_str() + netSent, min(size_t(1024), netRequest.length() - netSent), 0);
    if (written > 0) netSent += written;
    else if (written < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
      ++netErrors;
      if (netJob == NetJob::Upnp) netFinish("Router send failed"); else netNextProbe();
      return;
    }
    if (netSent == netRequest.length()) { netStage = NetStage::Reading; netDeadline = millis() + kNetResponseMs; }
  }
  if (netStage == NetStage::Reading) {
    const int received = recv(netSocket, netResponse + netReceived, min(size_t(1024), kNetResponseBytes - netReceived), 0);
    if (received > 0) {
      netReceived += received; netResponse[netReceived] = 0;
      if (memchr(netResponse, 0, netReceived)) {
        ++netErrors;
        if (netJob == NetJob::Upnp) netFinish("Invalid router response"); else netNextProbe();
        return;
      }
      // RTSP OPTIONS needs only the response headers; servers may keep it open.
      if (netJob == NetJob::Cameras && (netCurrentPort() == 554 || netCurrentPort() == 8554) && strstr(netResponse, "\r\n\r\n")) { netReadProbeResponse(); return; }
      // HTTP Content-Length allows completion without waiting for peer close.
      char value[32]; char* body = strstr(netResponse, "\r\n\r\n");
      char encoding[32];
      if (body && !NetworkParse::header(netResponse, "Transfer-Encoding", encoding, sizeof(encoding)) && NetworkParse::header(netResponse, "Content-Length", value, sizeof(value))) {
        char* end; unsigned long len = strtoul(value, &end, 10);
        if (*value && !*end && len <= netReceived - size_t(body + 4 - netResponse)) { netReadProbeResponse(); return; }
      }
      if (netReceived == kNetResponseBytes) {
        ++netErrors;
        if (netJob == NetJob::Upnp) netFinish("Router response exceeds 8 KiB"); else netNextProbe();
        return;
      }
    } else if (received == 0) { netReadProbeResponse(); return; }
    else if (errno != EAGAIN && errno != EWOULDBLOCK) {
      ++netErrors;
      if (netJob == NetJob::Upnp) netFinish("Router receive failed"); else netNextProbe();
      return;
    }
  }
  if (netExpired()) {
    ++netTimeouts;
    if (netJob == NetJob::Upnp) netFinish("Router timeout (partial)"); else netNextProbe();
  }
}
void updateNetworkNavigation() {
  static uint32_t lastDraw = 0;
  static String lastConnection;
  if (!netOpen || netJob != NetJob::None || millis() - lastDraw < 1000) return;
  lastDraw = millis();
  const String connection = netConnectionLabel();
  if (connection == lastConnection) return;
  lastConnection = connection;
  // Only redraw on a connection change; periodic redraws reset Mini's position
  // within a wrapped action label while the user is reading it.
  if (currentView == View::kNetworkMenu) drawNetworkMenu();
  else if (currentView == View::kNetworkSetup) drawNetworkSetup();
  else if (currentView == View::kNetworkResults) drawNetworkResults();
  else if (currentView == View::kNetworkHost) drawNetworkHost();
  else if (currentView == View::kNetworkDetail) drawNetworkDetail();
}
void updateNetworkTools() {
  updateNetworkNavigation();
  if (currentView == View::kNetworkEdit && netKeyTap.expire(millis())) drawNetworkEditor();
  if (!netOpen || netJob == NetJob::None) return;
  if (netJob == NetJob::Join) {
    if (WiFi.status() == WL_CONNECTED) {
      netJob = NetJob::None; netStatus = "Connected: " + WiFi.localIP().toString();
      recordFirmwareAudit("network", "connect", "success", WiFi.localIP().toString());
      drawNetworkSetup();
    } else if (netExpired()) {
      netDisconnect(); netStatus = "Connection timed out; re-enter key";
      recordFirmwareAudit("network", "connect", "timeout", ""); drawNetworkSetup();
    }
    return;
  }
  if (WiFi.status() != WL_CONNECTED || netIpNumber(WiFi.localIP()) != netSessionIp || netIpNumber(WiFi.subnetMask()) != netSessionMask) {
    netFinish("Connection changed/lost (partial)");
    netHostCount = 0; netSelectedHost = netResultsHost = -1;
    drawNetworkResults(); return;
  }
  if (netStage == NetStage::ArpSend) {
    if (netCursor == netSessionIp) ++netCursor;
    if (netCursor > netLast) { netFinish("Complete; silent hosts may be absent"); return; }
    NetArpCall query = {}; query.ip = netCursor; query.send = true;
    if (tcpip_api_call(netArpOnTcpip, &query.call) != ERR_OK) ++netErrors;
    netDeadline = millis() + 200; netStage = NetStage::ArpWait;
    netStatus = "ARP " + netIpText(netCursor) + " / " + String(netLast - netFirst + 1);
  } else if (netStage == NetStage::ArpWait && netExpired()) {
    NetArpCall query = {}; query.ip = netCursor;
    if (tcpip_api_call(netArpOnTcpip, &query.call) != ERR_OK) ++netErrors;
    else if (query.found) {
      if (netHostCount < kNetCapacity) {
        NetHost& host = netHosts[netHostCount++]; host.ip = netCursor; memcpy(host.mac, query.mac, 6);
      } else netLimited = true;
    } else ++netTimeouts;
    ++netCompleted; ++netCursor; netStage = NetStage::ArpSend;
  } else if (netStage == NetStage::Probe) netBeginProbe();
  else if (netStage == NetStage::SsdpWait || netStage == NetStage::SipWait) {
    int size = netUdp.parsePacket();
    if (size > 0) {
      uint32_t sender = netIpNumber(netUdp.remoteIP());
      const uint16_t senderPort = netUdp.remotePort();
      int got = netUdp.read(netResponse, kNetResponseBytes);
      netUdp.clear();
      if (got > 0 && size <= int(kNetResponseBytes) && !memchr(netResponse, 0, got)) {
        netResponse[got] = 0;
        if (netStage == NetStage::SsdpWait && sender == netIpNumber(WiFi.gatewayIP())) {
          char location[320]; NetworkParse::Url url;
          if (!strncmp(netResponse, "HTTP/1.1 200", 12) && NetworkParse::header(netResponse, "LOCATION", location, sizeof(location)) && NetworkParse::url(location, url) && url.ip == sender) {
            netUdp.stop(); netUpnpUrl = url; netUpnpStep = 1;
            netTcpStart(url.ip, url.port, netHttpRequest("GET", url, "", ""));
          }
        } else if (netStage == NetStage::SipWait && sender == netHosts[netHostCursor].ip && senderPort == 5060) {
          int code = 0; char id[96], cseq[40];
          if (sscanf(netResponse, "SIP/2.0 %d", &code) == 1 && code >= 100 && code <= 699 && NetworkParse::header(netResponse, "Call-ID", id, sizeof(id)) && netSipId == id && NetworkParse::header(netResponse, "CSeq", cseq, sizeof(cseq)) && !strcmp(cseq, "1 OPTIONS")) {
            char server[100] = {}; NetworkParse::header(netResponse, "Server", server, sizeof(server));
            netAdd(sender, 5060, "SIP", "OPTIONS status " + String(code) + " " + server);
            netNextProbe();
          }
        }
      }
    }
    if ((netStage == NetStage::SsdpWait || netStage == NetStage::SipWait) && netExpired()) {
      ++netTimeouts;
      if (netJob == NetJob::Upnp) netFinish("No supported gateway SSDP response"); else netNextProbe();
    }
  } else netUpdateTcp();
  if (currentView == View::kNetworkResults && millis() - netLastDraw >= 600) { netLastDraw = millis(); drawNetworkResults(); }
}
void handleNetworkTouch(int x, int y) {
  if (currentView == View::kWardriveUpload ||
      currentView == View::kWardriveUploadFiles) {
    handleWardriveUploadTouch(x, y);
    return;
  }
  if (currentView == View::kNetworkEdit) {
    const int index = AwokKeyboard::hit(x, y, netKeyMode);
    if (index < 0) return;
    const auto key = AwokKeyboard::key(index, netKeyMode);
    if (key.action != AwokKeyboard::Character) netKeyTap.commit();
    switch (key.action) {
      case AwokKeyboard::Cancel:
        netWipe(netEdit);
        drawNetworkSetup();
        return;
      case AwokKeyboard::Done:
        if (netEditPassword) { netWipe(netPassword); netPassword = netEdit; }
        else { if (netSsid != netEdit) netWipe(netPassword); netSsid = netEdit; }
        netWipe(netEdit);
        drawNetworkSetup();
        return;
      case AwokKeyboard::ChangeMode:
        netKeyMode = static_cast<AwokKeyboard::Mode>((netKeyMode + 1) % 4);
        break;
      case AwokKeyboard::Next:
        break;  // commits immediately, for consecutive letters on the same key
      case AwokKeyboard::Delete:
        if (netEdit.length()) netEdit.remove(netEdit.length() - 1);
        break;
      case AwokKeyboard::Character: {
        bool replace = false;
        const char value = netKeyTap.press(index, netKeyMode, millis(),
            netEdit.length() < (netEditPassword ? 63U : 32U), replace);
        if (value) {
          if (replace && netEdit.length()) netEdit.setCharAt(netEdit.length() - 1, value);
          else netEdit += value;
        }
        break;
      }
    }
    drawNetworkEditor();
    return;
  }
  if (currentView == View::kNetworkSetup) { handleNetworkSetupTouch(x, y); return; }
  if (currentView == View::kNetworkAps) { handleNetworkApsTouch(x, y); return; }
  if (currentView == View::kNetworkMenu) { handleNetworkMenuTouch(x, y); return; }
  if (currentView == View::kNetworkResults) { handleNetworkResultsTouch(x, y); return; }
  if (currentView == View::kNetworkHost) { handleNetworkHostTouch(x, y); return; }
  if (currentView == View::kNetworkDetail && gpsMenuHit(x, y, 4, 280, 232, 36)) drawNetworkResults();
}
int netCardHit(int x, int y, int count) {
  for (int row = 0; row < count; ++row)
    if (gpsMenuHit(x, y, 8, 58 + row * 50, 224, 44)) return row;
  return -1;
}
void handleNetworkSetupTouch(int x, int y) {
  if (netJob == NetJob::Join) {
    if (gpsMenuHit(x, y, 8, 218, 224, 44)) { netDisconnect(); drawNetworkSetup(); }
    return;
  }
  if (gpsMenuHit(x, y, 4, 280, 112, 36)) { netReturnFromSetup(); return; }
  if (gpsMenuHit(x, y, 124, 280, 112, 36) && WiFi.status() == WL_CONNECTED) { netDisconnect(); drawNetworkSetup(); return; }
  const int row = netCardHit(x, y, kNetMenuRows);
  if (row == 0 || row == 1) {
    netEditPassword = row == 1; netEdit = netEditPassword ? netPassword : netSsid;
    netKeyMode = AwokKeyboard::Lower; netKeyTap.commit(); drawNetworkEditor();
  } else if (row == 2) { netApPage = 0; drawNetworkAps(); }
  else if (row == 3) netJoin();
}
void handleNetworkApsTouch(int x, int y) {
  const int pages = max(1, (wifiCount + kNetMenuRows - 1) / kNetMenuRows);
  if (gpsMenuHit(x, y, 4, 280, 56, 36)) drawNetworkSetup();
  else if (gpsMenuHit(x, y, 64, 280, 52, 36) && netApPage > 0) { --netApPage; drawNetworkAps(); }
  else if (gpsMenuHit(x, y, 124, 280, 52, 36) && netApPage + 1 < pages) { ++netApPage; drawNetworkAps(); }
  else if (gpsMenuHit(x, y, 180, 280, 56, 36)) {
    netDisconnect(); scanWifi();
    if (lastWifiScanOk) drawNetworkAps();
    else { netStatus = "AP scan failed"; drawNetworkSetup(); }
  } else {
    const int row = netCardHit(x, y, kNetMenuRows);
    const int index = netApPage * kNetMenuRows + row;
    if (row < 0 || index >= wifiCount) return;
    if (netSsid != wifiEntries[index].ssid) netWipe(netPassword);
    netSsid = wifiEntries[index].ssid;
    netStatus = netSsid.length() ? "Enter key, then Join" : "Enter hidden SSID, then Join";
    drawNetworkSetup();
  }
}
void handleNetworkMenuTouch(int x, int y) {
  if (netJob != NetJob::None) return;
  if (gpsMenuHit(x, y, 4, 280, 72, 36)) {
    if (netMenuSection) { netMenuSection = netMenuPage = 0; drawNetworkMenu(); }
    else { closeNetworkTools(); drawReconMenu(); }
    return;
  }
  if (netMenuSection == 1) {
    if (gpsMenuHit(x, y, 84, 280, 72, 36) && netMenuPage > 0) { --netMenuPage; drawNetworkMenu(); return; }
    if (gpsMenuHit(x, y, 164, 280, 72, 36) && netMenuPage < 1) { ++netMenuPage; drawNetworkMenu(); return; }
  }
  const int row = netCardHit(x, y, kNetMenuRows);
  if (row < 0) return;
  if (netMenuSection == 0) {
    if (row == 0) netOpenSetup(false);
    else if (row == 1) { netResultsHost = -1; netOpenResults(true); }
    else { netMenuSection = row == 2 ? 1 : 2; netMenuPage = 0; drawNetworkMenu(); }
  } else if (netMenuSection == 1) {
    const int index = netMenuPage * kNetMenuRows + row;
    if (index < 5) netStartJob(kNetServiceJobs[index], -1);
  } else if (row < 2) { netResultsHost = -1; netOpenResults(row == 0); }
  else if (row == 2) { netSetupReturnResults = false; openWardriveUpload(); }
}
void handleNetworkResultsTouch(int x, int y) {
  const bool running = netJob != NetJob::None;
  if (netResultsActions && !running) {
    if (gpsMenuHit(x, y, 4, 280, 112, 36)) { netResultsActions = false; drawNetworkResults(); return; }
    if (gpsMenuHit(x, y, 124, 280, 112, 36)) { netResultsActions = false; drawNetworkMenu(); return; }
    const int row = netCardHit(x, y, kNetMenuRows);
    if (row == 0) netOpenResults(!netShowHosts);
    else if (row == 1) { netStatus = netSaveCsv() ? "Snapshot saved" : "SD save failed / missing"; drawNetworkResults(); }
    else if (row == 2) netStartJob(NetJob::Hosts, -1);
    else if (row == 3) netOpenSetup(true);
    return;
  }
  if (gpsMenuHit(x, y, 8, 242, 224, 36)) {
    if (running) netFinish("Cancelled (partial)");
    else { netResultsActions = true; drawNetworkResults(); }
    return;
  }
  if (gpsMenuHit(x, y, 4, 280, 72, 36) && !running) {
    if (!netShowHosts && netHostSelectionValid(netResultsHost)) { netSelectedHost = netResultsHost; drawNetworkHost(); }
    else drawNetworkMenu();
    return;
  }
  if (gpsMenuHit(x, y, 84, 280, 72, 36) && netPage > 0) { --netPage; drawNetworkResults(); return; }
  if (gpsMenuHit(x, y, 164, 280, 72, 36) && netPage + 1 < netResultPages()) { ++netPage; drawNetworkResults(); return; }
  if (running) return;
  const int count = netShowHosts ? netHostCount : netResultCount;
  if (!count && gpsMenuHit(x, y, 8, 134, 224, 44)) {
    if (netShowHosts) netStartJob(NetJob::Hosts, -1);
    else { netMenuSection = 1; netMenuPage = 0; drawNetworkMenu(); }
    return;
  }
  const int row = netCardHit(x, y, kNetResultRows);
  const int index = netPage * kNetResultRows + row;
  if (row < 0 || index >= count) return;
  netRememberResultsPage();
  if (netShowHosts) { netSelectedHost = index; drawNetworkHost(); }
  else { netSelectedResult = index; drawNetworkDetail(); }
}
void handleNetworkHostTouch(int x, int y) {
  if (netJob != NetJob::None) return;
  if (gpsMenuHit(x, y, 4, 280, 112, 36)) { netOpenResults(true); return; }
  if (gpsMenuHit(x, y, 124, 280, 112, 36)) { drawNetworkMenu(); return; }
  const int row = netCardHit(x, y, 4);
  if (row >= 0) {
    if (!netHostSelectionValid(netSelectedHost)) { netOpenResults(true); return; }
    netStartJob(kNetServiceJobs[row], netSelectedHost);
  }
}

void handleNetworkSerial() {
  // Existing serial shortcuts cannot switch radios out from under a LAN job.
  // Serial h always exits; passwords are entered through the masked device UI.
  if (Serial.available()) noteActivity();
  while (Serial.available()) {
    const char ch = Serial.read();
    if (ch == 'h' || ch == 'H') { closeNetworkTools(); drawHome(); return; }
  }
}
