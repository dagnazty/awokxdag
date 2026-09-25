// AWOKxDAG — GPS interface + wardriving (compiled as part of the sketch; see awok_common.h)

// ---- GPS interface ------------------------------------------------------

TinyGPSPlus gps;
HardwareSerial gpsSerial(AwokPins::kGpsUart);
bool gpsStarted = false;
bool gpsRawEcho = false;
bool gpsDataSeen = false;
uint32_t gpsLastDataMs = 0;
bool gpsDiagnosticsFromSettings = false;
int gpsMenuPage = 0;  // 0 overview, 1 diagnostics, 2 drive picker
String gpsMenuNotice;

// Baud options to cycle through on-device; the confirmed default is first.
const unsigned long kGpsBaudOptions[] = {115200, 9600, 38400, 57600, 4800};
constexpr int kGpsBaudOptionCount =
    static_cast<int>(sizeof(kGpsBaudOptions) / sizeof(kGpsBaudOptions[0]));
int gpsBaudIndex = 0;
unsigned long gpsCurrentBaud = AwokPins::kGpsBaud;
char gpsLineBuf[100];
int gpsLineLen = 0;
char gpsLastSentence[28] = "(none)";
// passedChecksum() at the last baud change, so the screen can show whether the
// *current* baud is producing valid sentences.
uint32_t gpsBaudBaselinePassed = 0;
bool gpsClockSynced = false;
uint32_t gpsLastClockSyncMs = 0;

// Store the zone name, not its table index (indices can change on data updates).
int gpsLocalZone = -1;
bool gpsZoneLookedUp = false;
uint32_t gpsLastZoneLookupMs = 0;
int gpsAppliedOffset = 32767;
char gpsPosixZone[24] = "UTC0";

const char* gpsClockTimezone() { return gpsPosixZone; }

static void updateGpsTimezone() {
  const uint32_t nowMs = millis();
  if (gps.location.isValid() && gps.location.age() < 5000 &&
      (!gpsZoneLookedUp || nowMs - gpsLastZoneLookupMs >= 30000)) {
    gpsZoneLookedUp = true;
    gpsLastZoneLookupMs = nowMs;
    const int zone = AwokTime::zoneAt(gps.location.lat(), gps.location.lng());
    if (zone >= 0 && zone != gpsLocalZone) {
      gpsLocalZone = zone;
      Preferences preferences;
      if (preferences.begin("awok-tz", false)) {
        preferences.putString("zone", AwokTime::kZones[zone].name);
        preferences.end();
      }
      Serial.printf("[gps] local timezone: %s\n", AwokTime::kZones[zone].name);
    }
  }
  int minutes = 0;
  bool dst = false;
  if (!AwokTime::offsetAt(gpsLocalZone, int64_t(time(nullptr)), minutes, dst) ||
      minutes == gpsAppliedOffset) return;
  // libc/FAT uses the current local offset, while epoch seconds stay absolute
  // for TLS/NTP. Re-evaluated each loop, including at DST transitions without GPS.
  const int absolute = abs(minutes);
  snprintf(gpsPosixZone, sizeof(gpsPosixZone), "LOC%s%d:%02d",
           minutes >= 0 ? "-" : "+", absolute / 60, absolute % 60);
  setenv("TZ", gpsPosixZone, 1);
  tzset();
  gpsAppliedOffset = minutes;
}

// Convert a validated Gregorian UTC date to Unix seconds without mktime(),
// whose result depends on the process timezone. All supported GPS years are
// positive, so this era decomposition is identical on both ESP32 toolchains.
static time_t gpsUtcEpoch(int year, unsigned month, unsigned day,
                          unsigned hour, unsigned minute, unsigned second) {
  year -= month <= 2;
  const int era = year / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned shiftedMonth =
      month > 2 ? month - 3 : month + 9;
  const unsigned dayOfYear = (153 * shiftedMonth + 2) / 5 + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 -
                            yearOfEra / 100 + dayOfYear;
  const int64_t days = static_cast<int64_t>(era) * 146097 + dayOfEra - 719468;
  return static_cast<time_t>(days * 86400 + hour * 3600 + minute * 60 + second);
}

static bool gpsDateTimeFreshAndSane() {
  if (!gps.date.isValid() || !gps.time.isValid() ||
      gps.date.age() >= 5000 || gps.time.age() >= 5000) return false;
  const int year = gps.date.year();
  const unsigned month = gps.date.month();
  const unsigned day = gps.date.day();
  const unsigned hour = gps.time.hour();
  const unsigned minute = gps.time.minute();
  const unsigned second = gps.time.second();
  if (year < 2020 || year > 2099 || month < 1 || month > 12 ||
      hour > 23 || minute > 59 || second > 59) return false;
  static const uint8_t daysPerMonth[] =
      {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  unsigned maximumDay = daysPerMonth[month - 1];
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  if (month == 2 && leap) ++maximumDay;
  return day >= 1 && day <= maximumDay;
}

// GPS NMEA date/time is UTC. Use it to seed and discipline the ESP system
// clock, which in turn supplies TLS validation and FAT file timestamps. GPS is
// authoritative whenever it is fresh; network time remains a fallback for
// devices that have no fix when an HTTPS upload starts.
static void syncSystemClockFromGps() {
  if (!gpsDateTimeFreshAndSane()) return;
  const int year = gps.date.year();
  const unsigned month = gps.date.month();
  const unsigned day = gps.date.day();
  const unsigned hour = gps.time.hour();
  const unsigned minute = gps.time.minute();
  const unsigned second = gps.time.second();
  const uint32_t nowMs = millis();
  if (gpsClockSynced && nowMs - gpsLastClockSyncMs < 60000) return;

  const time_t epoch = gpsUtcEpoch(year, month, day, hour, minute, second);
  if (epoch <= 1577836800) return;  // reject 2020-01-01 and older

  const time_t oldEpoch = time(nullptr);
  struct timeval tv = {};
  tv.tv_sec = epoch;
  tv.tv_usec = static_cast<suseconds_t>(gps.time.centisecond()) * 10000;
  if (settimeofday(&tv, nullptr) != 0) {
    Serial.printf("[gps] system clock sync failed: errno=%d\n", errno);
    return;
  }
  gpsClockSynced = true;
  gpsLastClockSyncMs = nowMs;
  long long correction = static_cast<long long>(epoch) -
                         static_cast<long long>(oldEpoch);
  if (correction < 0) correction = -correction;
  if (oldEpoch < 1577836800 || correction > 2) {
    Serial.println("[gps] clock synchronized from GPS");
  }
}

void applyGpsBaud(unsigned long baud, bool persist) {
  int index = 0;
  for (int i = 0; i < kGpsBaudOptionCount; ++i) {
    if (kGpsBaudOptions[i] == baud) {
      index = i;
      break;
    }
  }
  gpsBaudIndex = index;
  gpsCurrentBaud = kGpsBaudOptions[gpsBaudIndex];
  deviceSettings.gpsBaud = gpsCurrentBaud;
  if (gpsStarted) gpsSerial.end();
  gpsSerial.begin(gpsCurrentBaud, SERIAL_8N1, AwokPins::kGpsRx,
                  AwokPins::kGpsTx);
  gpsStarted = true;
  gpsBaudBaselinePassed = gps.passedChecksum();
  gpsDataSeen = false;
  gpsLastDataMs = 0;
  Serial.printf("[gps] UART%d rx=%d tx=%d @ %lu baud\n", AwokPins::kGpsUart,
                AwokPins::kGpsRx, AwokPins::kGpsTx, gpsCurrentBaud);
  if (persist) saveDeviceSettings();
}

void initGps() {
  Preferences preferences;
  if (preferences.begin("awok-tz", true)) {
    const String saved = preferences.getString("zone", "");
    gpsLocalZone = AwokTime::zoneByName(saved.c_str());
    preferences.end();
  }
  const unsigned long baud =
      gpsBaudIsKnown(deviceSettings.gpsBaud) ? deviceSettings.gpsBaud
                                             : AwokPins::kGpsBaud;
  applyGpsBaud(baud, false);
}

void cycleGpsBaud() {
  applyGpsBaud(kGpsBaudOptions[(gpsBaudIndex + 1) % kGpsBaudOptionCount], true);
}

void updateGps() {
  if (!gpsStarted) return;
  while (gpsSerial.available()) {
    const char c = static_cast<char>(gpsSerial.read());
    gpsDataSeen = true;
    gpsLastDataMs = millis();
    gps.encode(c);
    if (gpsRawEcho) Serial.write(c);
    if (c == '\n' || c == '\r') {
      if (gpsLineLen > 0) {
        const int n = min(gpsLineLen, static_cast<int>(sizeof(gpsLastSentence)) - 1);
        memcpy(gpsLastSentence, gpsLineBuf, n);
        gpsLastSentence[n] = 0;
        gpsLineLen = 0;
      }
    } else if (gpsLineLen < static_cast<int>(sizeof(gpsLineBuf)) - 1) {
      gpsLineBuf[gpsLineLen++] = c;
    }
  }
  syncSystemClockFromGps();
  updateGpsTimezone();
}

bool gpsHasFix() {
  return gps.location.isValid() && gps.location.age() < 5000;
}

int gpsSats() {
  return gps.satellites.isValid() ? gps.satellites.value() : 0;
}

uint32_t gpsCharsProcessed() { return gps.charsProcessed(); }

// Comma-prefixed "lat,lon,alt" for appending to a CSV row, or empty fields
// when there is no fix.
String gpsCsvFields() {
  if (!gpsHasFix()) return ",,,";
  String out = ",";
  out += String(gps.location.lat(), 6);
  out += ',';
  out += String(gps.location.lng(), 6);
  out += ',';
  out += String(gps.altitude.meters(), 1);
  return out;
}

// Local calendar time for every log and WiGLE FirstSeen. GPS supplies absolute
// time; the latest valid position chooses timezone and DST rules. Keep the last
// zone during fix loss. Until both clock and zone are known, label uptime rather
// than silently writing UTC as though it were local time.
static time_t gpsTimestampEpoch() {
  if (gpsDateTimeFreshAndSane())
    return gpsUtcEpoch(gps.date.year(), gps.date.month(), gps.date.day(),
                       gps.time.hour(), gps.time.minute(), gps.time.second());
  return time(nullptr);
}

String gpsTimestamp() {
  struct tm local = {};
  int minutes = 0;
  bool dst = false;
  if (AwokTime::localTime(gpsLocalZone, gpsTimestampEpoch(), local, minutes, dst)) {
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
    return String(buffer);
  }
  return String("uptime+") + String(millis());
}

String gpsTimezoneLabel() {
  int minutes = 0;
  bool dst = false;
  if (!AwokTime::offsetAt(gpsLocalZone, int64_t(gpsTimestampEpoch()), minutes, dst))
    return "Local time: waiting for GPS";
  char offset[32];
  snprintf(offset, sizeof(offset), "GMT%c%02d:%02d %s", minutes < 0 ? '-' : '+',
           abs(minutes) / 60, abs(minutes) % 60, dst ? "DST" : "standard");
  return String(offset);
}

WardriveSessionStats wardriveStats;

void resetWardriveSessionStats(bool linked) {
  wardriveStats = WardriveSessionStats();
  wardriveStats.mode = fleetActive ? (fleetCoordinator ? 2 : 3) : linked ? 1 : 0;
  do { wardriveStats.id = esp_random(); } while (!wardriveStats.id);
}

void updateWardriveSessionStats() {
  if (!(wardriveActive || linkWardriveActive)) return;
  const uint32_t elapsed = millis() - wardriveStartMs;
  wardriveStats.elapsedMs = elapsed;
  wardriveStats.count(elapsed, wardriveNetworks + wardriveBleCount);
  const uint32_t dt = elapsed - wardriveStats.sampledMs;
  if (dt < 1000) return;
  wardriveStats.sampledMs = elapsed;
  const bool fix = gpsHasFix();
  if (fix && dt <= 5000) wardriveStats.fixMs += dt;
  const bool quality = fix && gps.hdop.isValid() && gps.hdop.age() < 5000 && gps.hdop.hdop() <= 5.0;
  if (!quality || dt > 5000) { wardriveStats.lastPosition = false; return; }
  const double lat = gps.location.lat(), lon = gps.location.lng();
  const bool moving = gps.speed.isValid() && gps.speed.age() < 5000 && gps.speed.kmph() >= 2.0;
  if (wardriveStats.lastPosition && moving) {
    const double step = TinyGPSPlus::distanceBetween(wardriveStats.lastLat, wardriveStats.lastLon, lat, lon);
    const double maxStep = 30.0 + gps.speed.mps() * (dt / 1000.0) * 2.0;
    if (step >= 1.0 && step <= maxStep) wardriveStats.distanceM += step;
  }
  wardriveStats.lastLat = lat; wardriveStats.lastLon = lon;
  wardriveStats.lastPosition = true;
}

// 0 unavailable, 1 waiting for rows/flush, 2 flushed, 3 write failure, 4 worker relay.
int wardriveStorageState() {
  if (wardriveStats.mode == 3) return 4;
  if (wardriveStats.writeError) return 3;
  if (!wardriveCsvReady && (wardriveActive || linkWardriveActive || !wardriveStats.bytes)) return 0;
  return wardriveStats.didFlush ? 2 : 1;
}

String wardriveRecordingLabel() {
  const int state = wardriveStorageState();
  if (state == 3) return "SD WRITE FAILED";
  if (state == 0) return "NO SD RECORDING";
  if (!(wardriveActive || linkWardriveActive)) return "STOPPED";
  if (state == 4) return gpsHasFix() ? "RELAY TO COORDINATOR" : "WAITING FOR GPS";
  if (!gpsHasFix()) return wardriveStats.mode == 2 && wardriveStats.rows ? "RECORDING | LOCAL GPS LOST" : "WAITING FOR GPS";
  return wardriveStats.rows ? "RECORDING" : "WAITING FOR SIGHTINGS";
}

String wardriveElapsedText() {
  const uint32_t seconds = wardriveStats.elapsedMs / 1000;
  char text[20];
  snprintf(text, sizeof(text), "%02lu:%02lu:%02lu", (unsigned long)(seconds / 3600),
           (unsigned long)(seconds / 60 % 60), (unsigned long)(seconds % 60));
  return String(text);
}

// Called only from existing rendezvous/status opportunities; never retunes a radio.
void broadcastWardriveDashboard() {
  if (!wardriveStats.id) return;
  static uint32_t lastSentMs = 0;
  if (millis() - lastSentMs < 1000) return;
  lastSentMs = millis();
  AxdWardriveStatusMsg msg;
#ifdef AWOK_HEADLESS
  constexpr unsigned source = 0;
#else
  constexpr unsigned source = 1;
#endif
  const unsigned mode = wardriveStats.mode;
  const uint32_t flushAge = wardriveStats.didFlush ? (millis() - wardriveStats.lastFlushMs) / 1000 : 0xffffffffU;
  // v1: source,session,active,seconds,metres,fix%,wifi,ble,rate,sats,hdop,
  // storage,rows,bytes,flushAge,localTime,zone,file,mode,fix,speed,nodes.
  const int n = snprintf(msg.data, sizeof(msg.data),
      "$WDSTAT,1,%u,%lu,%u,%lu,%.0f,%u,%lu,%lu,%lu,%d,%.1f,%d,%lu,%lu,%lu,%s,%s,%s,%u,%u,%.1f,%d",
      source, (unsigned long)wardriveStats.id, unsigned(wardriveActive || linkWardriveActive),
      (unsigned long)(wardriveStats.elapsedMs / 1000), wardriveStats.distanceM, wardriveStats.fixPercent(),
      (unsigned long)wardriveNetworks, (unsigned long)wardriveBleCount,
      (unsigned long)wardriveStats.perMinute(), gpsSats(),
      gps.hdop.isValid() && gps.hdop.age() < 5000 ? gps.hdop.hdop() : -1.0,
      wardriveStorageState(), (unsigned long)wardriveStats.rows, (unsigned long)wardriveStats.bytes,
      (unsigned long)flushAge, gpsTimestamp().c_str(), gpsTimezoneLabel().c_str(), wardriveCsvName().c_str(),
      mode, unsigned(gpsHasFix()), gps.speed.isValid() && gps.speed.age() < 5000 ? gps.speed.kmph() : 0.0,
      fleetActive ? fleetMemberCount : linkState == kLinkReady ? 2 : 1);
  if (n <= 0 || size_t(n) >= sizeof(msg.data)) return;
  Serial.println(msg.data);
#ifdef AWOK_HEADLESS
  bridgeNotifyResult(kSourceWardriveStatus, reinterpret_cast<const uint8_t*>(msg.data), n);
#else
  if (linkEspNowReady) esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&msg), sizeof(msg));
#endif
}

// ---- GPS status + wardriving --------------------------------------------

const char* wigleAuth(wifi_auth_mode_t auth) {
  switch (auth) {
    case WIFI_AUTH_OPEN:
      return "[ESS]";
    case WIFI_AUTH_WEP:
      return "[WEP][ESS]";
    case WIFI_AUTH_WPA_PSK:
      return "[WPA-PSK-CCMP+TKIP][ESS]";
    case WIFI_AUTH_WPA2_PSK:
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "[WPA2-PSK-CCMP][ESS]";
    case WIFI_AUTH_WPA3_PSK:
    case WIFI_AUTH_WPA2_WPA3_PSK:
    case WIFI_AUTH_WPA3_EXT_PSK:
      return "[WPA3-SAE-CCMP][ESS]";
    case WIFI_AUTH_ENTERPRISE:
      return "[WPA2-EAP-CCMP][ESS]";
    case WIFI_AUTH_OWE:
      return "[OWE][ESS]";
    default:
      return "[ESS]";
  }
}

static inline uint32_t macHash1(const uint8_t* mac) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < 6; ++i) {
    h = (h ^ mac[i]) * 16777619u;
  }
  return h;
}

static inline uint32_t macHash2(const uint8_t* mac) {
  uint32_t h = 0x811c9dc5u;
  for (int i = 0; i < 6; ++i) {
    h = (h * 33) ^ mac[i];
  }
  return h;
}

constexpr size_t kWardriveBloomBits = kWardriveBloomFilterBytes * 8;

bool wardriveMacSeen(const uint8_t* mac) {
  const uint32_t h1 = macHash1(mac);
  const uint32_t h2 = macHash2(mac);
  const uint32_t b1 = h1 % kWardriveBloomBits;
  const uint32_t b2 = (h1 + h2) % kWardriveBloomBits;
  const uint32_t b3 = (h1 + 2 * h2) % kWardriveBloomBits;
  const uint32_t b4 = (h1 + 3 * h2) % kWardriveBloomBits;
  return (wardriveBloom[b1 >> 3] & (1 << (b1 & 7))) &&
         (wardriveBloom[b2 >> 3] & (1 << (b2 & 7))) &&
         (wardriveBloom[b3 >> 3] & (1 << (b3 & 7))) &&
         (wardriveBloom[b4 >> 3] & (1 << (b4 & 7)));
}

void wardriveAddMac(const uint8_t* mac) {
  const uint32_t h1 = macHash1(mac);
  const uint32_t h2 = macHash2(mac);
  const uint32_t b1 = h1 % kWardriveBloomBits;
  const uint32_t b2 = (h1 + h2) % kWardriveBloomBits;
  const uint32_t b3 = (h1 + 2 * h2) % kWardriveBloomBits;
  const uint32_t b4 = (h1 + 3 * h2) % kWardriveBloomBits;
  wardriveBloom[b1 >> 3] |= (1 << (b1 & 7));
  wardriveBloom[b2 >> 3] |= (1 << (b2 & 7));
  wardriveBloom[b3 >> 3] |= (1 << (b3 & 7));
  wardriveBloom[b4 >> 3] |= (1 << (b4 & 7));
  ++wardriveMacCount;
}

void wardriveResetDedup() {
  memset(wardriveBloom, 0, sizeof(wardriveBloom));
  wardriveMacCount = 0;
}

// Open a FRESH CSV for this wardrive run: /awokxdag/wardrive-NNNN.csv, using the
// first index not already on the card, so every Start (solo, link, or fleet)
// writes a new file instead of appending to one growing log.
bool openWardriveCsv() {
  closeWardriveCsv();
  g_wardriveCsvPath = "";
  if (!ensureSdCard()) return false;
  for (int n = 1; n <= 9999; ++n) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%s/wardrive-%04d.csv", kSdDirectory, n);
    if (!SD.exists(buf)) { g_wardriveCsvPath = String(buf); break; }
  }
  if (g_wardriveCsvPath.length() == 0) {
    Serial.println("[wardrive] no free session filename (0001-9999)");
    return false;
  }
  g_wardriveFile = SD.open(g_wardriveCsvPath.c_str(), FILE_WRITE);
  if (!g_wardriveFile) {
    sdReady = false;
    wardriveCsvReady = false;
    return false;
  }
  // Build both header lines without temporary heap Strings. A failed/short
  // header write must never leave a ready session that appends headerless rows.
  char header[512];
  const int headerLength = snprintf(
      header, sizeof(header),
      "WigleWifi-1.6,appRelease=AxD,model=%s,release=%s,device=AxD,"
      "display=ILI9341,board=%s,brand=AxD\r\n"
      "MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,"
      "CurrentLongitude,AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type\r\n",
      AwokPins::kChipLabel, kVersion, AwokPins::kBoardLabel);
  const bool headerFits = headerLength > 0 &&
                         static_cast<size_t>(headerLength) < sizeof(header);
  const size_t written = headerFits
      ? g_wardriveFile.write(reinterpret_cast<const uint8_t*>(header), headerLength)
      : 0;
  g_wardriveFile.flush();
  if (!headerFits || written != static_cast<size_t>(headerLength) ||
      g_wardriveFile.size() != static_cast<size_t>(headerLength)) {
    Serial.println("[wardrive] CSV header write failed; SD logging disabled");
    closeWardriveCsv();
    SD.remove(g_wardriveCsvPath.c_str());  // only this newly created, incomplete session
    g_wardriveCsvPath = "";
    sdReady = false;
    return false;
  }
  wardriveCsvReady = true;
  wardriveStats.bytes = g_wardriveFile.size();
  Serial.printf("[wardrive] logging to %s\n", g_wardriveCsvPath.c_str());
  return true;
}

void closeWardriveCsv() {
  if (g_wardriveFile) {
    flushWardriveCsv();
    g_wardriveFile.close();
  }
  wardriveCsvReady = false;
}

void flushWardriveCsv() {
  if (wardriveCsvReady && g_wardriveFile) {
    g_wardriveFile.flush();
    if (g_wardriveFile.getWriteError()) { wardriveStats.writeError = true; wardriveCsvReady = false; }
    else {
      wardriveStats.flushedRows = wardriveStats.rows;
      wardriveStats.lastFlushMs = millis();
      wardriveStats.didFlush = true;
    }
  }
}

// Basename of the current run's CSV for on-screen display (empty before start).
String wardriveCsvName() {
  if (g_wardriveCsvPath.length() == 0) return String("wardrive.csv");
  return g_wardriveCsvPath.substring(g_wardriveCsvPath.lastIndexOf('/') + 1);
}

int wigleFrequencyMhz(int channel) {
  if (channel == 14) return 2484;
  if (channel >= 1 && channel <= 13) return 2407 + channel * 5;
  if (channel >= 32 && channel <= 177) return 5000 + channel * 5;
  return 0;  // BLE and unknown/non-Wi-Fi channels
}

// Shared tail of a WiGLE row (timestamp, channel/frequency, RSSI, GPS, type).
void appendWigleTail(File& file, int channel, int rssi, const char* type) {
  file.print(gpsTimestamp());
  file.print(',');
  file.print(channel);
  file.print(',');
  file.print(wigleFrequencyMhz(channel));
  file.print(',');
  file.print(rssi);
  file.print(',');
  file.print(String(gps.location.lat(), 6));
  file.print(',');
  file.print(String(gps.location.lng(), 6));
  file.print(',');
  file.print(String(gps.altitude.meters(), 1));
  file.print(',');
  file.print(String(gps.hdop.isValid() ? gps.hdop.hdop() * 5.0 : 0.0, 1));
  file.print(",,0,");
  file.println(type);
}

// The WiGLE row tail as a String (same fields/order as appendWigleTail): so the
// full row can be both written to SD and streamed to the phone.
String wardriveWigleTail(int channel, int rssi, const char* type) {
  return gpsTimestamp() + "," + String(channel) + "," +
         String(wigleFrequencyMhz(channel)) + "," + String(rssi) + "," +
         String(gps.location.lat(), 6) + "," + String(gps.location.lng(), 6) +
         "," + String(gps.altitude.meters(), 1) + "," +
         String(gps.hdop.isValid() ? gps.hdop.hdop() * 5.0 : 0.0, 1) +
         ",,0," + type;
}

// Persist a wardrive row to SD via persistent open file handle, and (on the
// headless bridge) push it to the phone so the app can build/download the WiGLE
// CSV even with no SD.
static void wardriveEmitRow(const char* line, size_t len) {
#ifdef AWOK_HEADLESS
  if (g_bridgePhoneConnected)
    bridgeNotifyResult(kSourceWardrive,
                       reinterpret_cast<const uint8_t*>(line),
                       len);
#endif
  if (!wardriveCsvReady || !g_wardriveFile) return;
  const size_t written = g_wardriveFile.println(line);
  if (written != len + 2 || g_wardriveFile.getWriteError()) {
    wardriveStats.writeError = true;
    Serial.println("[wardrive] SD row write failed; recording stopped");
    closeWardriveCsv();
    return;
  }
  ++wardriveStats.rows;
  wardriveStats.bytes += written;
}

static void wardriveEmitRow(const String& line) {
  wardriveEmitRow(line.c_str(), line.length());
}

// Coordinator: merge a WiGLE row received from a fleet member. Dedups by id
// (Wi-Fi BSSID / BLE address), builds the line from the member's own GPS carried
// in the frame, and emits it through the same SD+phone sink as local rows. The
// FirstSeen timestamp uses the coordinator's clock (rows stream in near real
// time and the fleet is co-located, so it is within a second of the sighting).
void wardriveEmitPeerRow(const FleetWardriveRow& r) {
  if (wardriveMacSeen(r.id)) return;
  wardriveAddMac(r.id);
  char line[256];
  const char* authStr = r.isBle ? "[BLE]" : wigleAuth((wifi_auth_mode_t)r.auth);
  const char* typeStr = r.isBle ? "BLE" : "WIFI";
  String escapedName = csvField(String(r.name));
  String ts = gpsTimestamp();
  int n = snprintf(line, sizeof(line),
                   "%02X:%02X:%02X:%02X:%02X:%02X,%s,%s,%s,%u,%d,%d,%.6f,%.6f,%d.0,0.0,,0,%s",
                   r.id[0], r.id[1], r.id[2], r.id[3], r.id[4], r.id[5],
                   escapedName.c_str(), authStr, ts.c_str(),
                   (unsigned)r.channel, wigleFrequencyMhz(r.channel), (int)r.rssi,
                   r.lat, r.lon, (int)r.alt, typeStr);
  if (n > 0 && static_cast<size_t>(n) < sizeof(line)) {
    wardriveEmitRow(line, static_cast<size_t>(n));
  }
  if (r.isBle) ++wardriveBleCount; else ++wardriveNetworks;
}

void appendWardriveRow(const String& bssid, const String& ssid,
                       wifi_auth_mode_t auth, int channel, int rssi) {
  wardriveEmitRow(bssid + "," + csvField(ssid) + "," + wigleAuth(auth) + "," +
                  wardriveWigleTail(channel, rssi, "WIFI"));
}

void appendWardriveBleRow(const String& address, const String& name, int rssi) {
  wardriveEmitRow(address + "," + csvField(name) + ",[BLE]," +
                  wardriveWigleTail(0, rssi, "BLE"));
}

// NimBLE scan callback (runs in the BLE task). Enqueues a POD BleHit for the
// wardrive loop; no SD or String heap churn happens here.
class WardriveBleCallbacks : public NimBLEScanCallbacks {
 public:
  void onResult(const NimBLEAdvertisedDevice* device) override {
    const int next = (bleHitHead + 1) % kBleHitQueueSlots;
    if (next == bleHitTail) return;  // queue full: drop
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

WardriveBleCallbacks wardriveBleCallbacks;

// Time-multiplex scheduler for wardrive; drawWardrive reads its phase.
RadioScheduler wardriveSched;

bool gpsMenuHit(int x, int y, int left, int top, int width, int height) {
  return x >= left && x < left + width && y >= top && y < top + height;
}

const char* gpsReceptionLabel() {
  if (!gpsStarted || !gpsDataSeen) return "NO GPS DATA";
  if (millis() - gpsLastDataMs >= 5000) return "DATA STALE";
  return gpsHasFix() ? "FIX ACQUIRED" : "SEARCHING";
}

void drawGps() {
  gpsDiagnosticsFromSettings = false;
  gpsMenuPage = 0;
  currentView = View::kGps;
  display.fillScreen(kBackground);
  drawHeader("GPS", "location and local time");
  const bool fix = gpsHasFix();
  const String sats = gps.satellites.isValid() && gps.satellites.age() < 5000 ? String(gpsSats()) : "--";
  const String hdop = gps.hdop.isValid() && gps.hdop.age() < 5000 ? String(gps.hdop.hdop(), 1) : "--";
  const String speed = fix && gps.speed.isValid() && gps.speed.age() < 5000 ? String(gps.speed.kmph(), 1) + " km/h" : "--";
  const String altitude = fix && gps.altitude.isValid() && gps.altitude.age() < 5000 ? String(gps.altitude.meters(), 1) + " m" : "--";
  const String local = gpsTimestamp();
#ifdef AWOK_MINI_DISPLAY
  display.dashboardLine(0, gpsReceptionLabel(), fix ? kGood : kWarn);
  display.dashboardLine(1, (local.length() == 19 ? local.substring(11) + " local" : "Time: waiting GPS").c_str(), kAccent);
  display.dashboardLine(2, gpsTimezoneLabel().c_str(), kMuted);
  display.dashboardLine(3, (fix ? "Lat " + String(gps.location.lat(), 6) : "Lat --").c_str(), ILI9341_WHITE);
  display.dashboardLine(4, (fix ? "Lon " + String(gps.location.lng(), 6) : "Lon --").c_str(), ILI9341_WHITE);
  display.dashboardLine(5, ("Speed " + speed).c_str(), ILI9341_WHITE);
  display.dashboardLine(6, ("Alt " + altitude).c_str(), kMuted);
  display.dashboardLine(7, ("Satellites " + sats).c_str(), kMuted);
  display.dashboardLine(8, ("HDOP " + hdop).c_str(), kMuted);
  drawSmallButton(4, 280, 72, 36, "Home", kMuted);
  drawSmallButton(84, 280, 72, 36, "Drive", kAccent);
  drawSmallButton(164, 280, 72, 36, "Diag", kAccent);
#else
  display.setTextSize(2); display.setTextColor(fix ? kGood : kWarn, kBackground);
  display.setCursor(scaleX(8), scaleY(52)); display.print(gpsReceptionLabel());
  display.setTextSize(1); display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(scaleX(8), scaleY(88)); display.print("Satellites: " + sats + "   HDOP: " + hdop);
  display.setCursor(scaleX(8), scaleY(106)); display.print("Speed: " + speed + "   Alt: " + altitude);
  display.setTextColor(kAccent, kBackground);
  display.setCursor(scaleX(8), scaleY(134)); display.print("LOCAL TIME");
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(scaleX(8), scaleY(148)); display.print(local);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(8), scaleY(162)); display.print(gpsTimezoneLabel());
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(scaleX(8), scaleY(190)); display.print(fix ? "Lat: " + String(gps.location.lat(), 6) : "Lat: -- (waiting for fix)");
  display.setCursor(scaleX(8), scaleY(204)); display.print(fix ? "Lon: " + String(gps.location.lng(), 6) : "Lon: -- (waiting for fix)");
  drawSmallButton(8, 228, 224, 42, "Diagnostics", kAccent);
  drawSmallButton(4, 280, 112, 36, "Home", kMuted);
  drawSmallButton(124, 280, 112, 36, "Drive modes", kAccent);
#endif
}

void drawGpsDiagnostics() {
  gpsMenuPage = 1;
  currentView = View::kGps;
  display.fillScreen(kBackground);
  drawHeader("GPS DIAG", "receiver / wiring / raw data");
  const uint32_t passed = gps.passedChecksum();
  const uint32_t passedHere = passed >= gpsBaudBaselinePassed ? passed - gpsBaudBaselinePassed : passed;
  display.setTextSize(1); display.setTextColor(kAccent, kBackground);
  display.setCursor(scaleX(8), scaleY(50)); display.print(gpsReceptionLabel());
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(scaleX(8), scaleY(68)); display.printf("UART%d  RX %d  TX %d", AwokPins::kGpsUart, AwokPins::kGpsRx, AwokPins::kGpsTx);
  display.setCursor(scaleX(8), scaleY(84)); display.printf("Chars %lu | valid %lu", (unsigned long)gps.charsProcessed(), (unsigned long)passed);
  display.setCursor(scaleX(8), scaleY(100)); display.printf("This baud %lu | bad %lu", (unsigned long)passedHere, (unsigned long)gps.failedChecksum());
  display.setCursor(scaleX(8), scaleY(116)); display.print("Last: " + clipped(String(gpsLastSentence), 30));
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(8), scaleY(140));
  if (!gpsDataSeen || millis() - gpsLastDataMs >= 5000) display.print("Check power, wiring, and baud rate.");
  else if (!passedHere) display.print("Data received; try another baud.");
  else if (!gpsHasFix()) display.print("Valid data; move to open sky.");
  else display.print("Receiver has a valid position fix.");
  display.setCursor(scaleX(8), scaleY(156)); display.print("Raw NMEA is sent to USB Serial.");
  drawSmallButton(8, 180, 224, 40, "Baud: " + String(gpsCurrentBaud) + " / change", kAccent);
  drawSmallButton(8, 228, 224, 40, String("Raw NMEA: ") + (gpsRawEcho ? "On" : "Off"), kAccent);
  drawSmallButton(4, 280, 112, 36, "Back", kMuted);
  drawSmallButton(124, 280, 112, 36, "Home", kAccent);
}

const char* driveModeState() {
  if (fleetListening && !fleetActive) return "Fleet: waiting to join";
  if (fleetActive) return fleetWardriveOn ? "Fleet: running" : "Fleet: stopped / still joined";
  if (wardriveActive) return "Solo: running";
  if (linkWardriveActive) return linkState == kLinkReady && millis() - linkPartnerLastSeenMs > kLinkPeerTimeoutMs ? "Split: partner lost" : "Split: running";
  if (linkState == kLinkDiscovering || linkState == kLinkAwaitConfirm) return "Split: pairing in progress";
  if (linkState == kLinkReady) return "Split: paired / stopped";
  return "No drive running";
}

void drawDriveMenu() {
  gpsMenuPage = 2;
  currentView = View::kGps;
  display.fillScreen(kBackground);
  drawHeader("DRIVE MODES", driveModeState());
  drawSmallButton(8, 48, 224, 44, wardriveActive ? "Solo / resume screen" : "Solo wardrive", kAccent);
  drawSmallButton(8, 116, 224, 44, "Split / two boards", kAccent);
  drawSmallButton(8, 184, 224, 44, "Fleet / multiple boards", kAccent);
  display.setTextSize(1); display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(8), scaleY(98)); display.print("One board | Wi-Fi + BLE");
  display.setCursor(scaleX(8), scaleY(166)); display.print("Pair boards | Wi-Fi | separate CSVs");
  display.setCursor(scaleX(8), scaleY(234)); display.print("Coordinator + workers | merged CSV");
  display.setTextColor(kWarn, kBackground);
  display.setCursor(scaleX(8), scaleY(256)); display.print(clipped(gpsMenuNotice, 37));
  drawSmallButton(4, 280, 112, 36, "Back", kMuted);
  drawSmallButton(124, 280, 112, 36, "Home", kAccent);
}

void openDriveMenu() { gpsMenuNotice = ""; drawDriveMenu(); }

void selectDriveMode(int mode) {
  gpsMenuNotice = "";
  if ((fleetActive || fleetListening) && mode != 2) gpsMenuNotice = "Leave Fleet before changing mode.";
  else if (wardriveActive && mode != 0) gpsMenuNotice = "Stop Solo before changing mode.";
  else if (linkWardriveActive && !fleetActive && mode != 1) gpsMenuNotice = "Stop Split before changing mode.";
  else if ((linkState == kLinkDiscovering || linkState == kLinkAwaitConfirm) && mode != 1) gpsMenuNotice = "Cancel Split pairing first.";
  else if (linkState == kLinkReady && mode == 2 && !fleetActive) gpsMenuNotice = "Unpair Split before joining Fleet.";
  if (gpsMenuNotice.length()) { drawDriveMenu(); return; }
  if (mode == 0) {
    if (wardriveActive) drawWardrive(); else startWardrive();
  } else if (mode == 1) openLinkWardrive();
  else if (mode == 2) {
    if (!fleetActive && !fleetListening) {
      linkEnsureEspNow();
      fleetMenuOpen = true;
    }
    drawLinkWardrive();
  }
}

void redrawGpsPage() {
  if (gpsMenuPage == 1) drawGpsDiagnostics();
  else if (gpsMenuPage == 2) drawDriveMenu();
  else drawGps();
}

void handleGpsTouch(int x, int y) {
  if (gpsMenuPage == 0) {
#ifdef AWOK_MINI_DISPLAY
    if (gpsMenuHit(x, y, 4, 280, 72, 36)) drawHome();
    else if (gpsMenuHit(x, y, 84, 280, 72, 36)) openDriveMenu();
    else if (gpsMenuHit(x, y, 164, 280, 72, 36)) drawGpsDiagnostics();
#else
    if (gpsMenuHit(x, y, 8, 228, 224, 42)) drawGpsDiagnostics();
    else if (gpsMenuHit(x, y, 4, 280, 112, 36)) drawHome();
    else if (gpsMenuHit(x, y, 124, 280, 112, 36)) openDriveMenu();
#endif
  } else if (gpsMenuPage == 1) {
    if (gpsMenuHit(x, y, 8, 180, 224, 40)) { cycleGpsBaud(); drawGpsDiagnostics(); }
    else if (gpsMenuHit(x, y, 8, 228, 224, 40)) { toggleSettingFlag(kSettingNmeaEcho); drawGpsDiagnostics(); }
    else if (gpsMenuHit(x, y, 4, 280, 112, 36)) {
      if (gpsDiagnosticsFromSettings) { gpsDiagnosticsFromSettings = false; drawSettings(); }
      else drawGps();
    }
    else if (gpsMenuHit(x, y, 124, 280, 112, 36)) drawHome();
  } else {
    if (gpsMenuHit(x, y, 8, 48, 224, 44)) selectDriveMode(0);
    else if (gpsMenuHit(x, y, 8, 116, 224, 44)) selectDriveMode(1);
    else if (gpsMenuHit(x, y, 8, 184, 224, 44)) selectDriveMode(2);
    else if (gpsMenuHit(x, y, 4, 280, 112, 36)) drawGps();
    else if (gpsMenuHit(x, y, 124, 280, 112, 36)) drawHome();
  }
}

void drawWardriveTile(int x, int y, const char* label, uint32_t value) {
  display.fillRoundRect(scaleX(x), scaleY(y), scaleX(111), scaleY(52), 5, kPanel);
  display.setTextSize(1); display.setTextColor(kMuted, kPanel);
  display.setCursor(scaleX(x + 7), scaleY(y + 6)); display.print(label);
  display.setTextSize(value > 999999 ? 1 : 2); display.setTextColor(ILI9341_WHITE, kPanel);
  display.setCursor(scaleX(x + 7), scaleY(y + 24)); display.print(value);
}

// The same session information is used by solo, Split, and Fleet dashboards.
void drawWardriveDashboardBody(const String& context) {
  const int storage = wardriveStorageState();
  const uint16_t statusColor = storage == 0 || storage == 3 ? kBad : gpsHasFix() ? kGood : kWarn;
  const uint32_t flushAge = wardriveStats.didFlush ? (millis() - wardriveStats.lastFlushMs) / 1000 : 0;
  String local = gpsTimestamp();
#ifdef AWOK_MINI_DISPLAY
  // Native 128px summary: critical information stays visible above real footer
  // actions instead of becoming a long, scrolling portrait document.
  display.dashboardLine(0, wardriveRecordingLabel().c_str(), statusColor);
  display.dashboardLine(1, (local.length() == 19 ? local.substring(11) + " local" : "Time: waiting GPS").c_str(), kAccent);
  char line[48];
  snprintf(line, sizeof(line), "WiFi %lu BLE %lu", (unsigned long)wardriveNetworks, (unsigned long)wardriveBleCount);
  display.dashboardLine(2, line, ILI9341_WHITE);
  snprintf(line, sizeof(line), "%s %.2fkm", wardriveElapsedText().c_str(), wardriveStats.distanceM / 1000.0);
  display.dashboardLine(3, line, ILI9341_WHITE);
  snprintf(line, sizeof(line), "GPS %d sat fix %u%%", gpsSats(), wardriveStats.fixPercent());
  display.dashboardLine(4, line, gpsHasFix() ? kGood : kWarn);
  snprintf(line, sizeof(line), "%lu/min recent", (unsigned long)wardriveStats.perMinute());
  display.dashboardLine(5, line, kAccent);
  snprintf(line, sizeof(line), storage == 4 ? "Relay to coordinator" : "SD %lu rows %luKB",
           (unsigned long)wardriveStats.rows, (unsigned long)(wardriveStats.bytes / 1024));
  display.dashboardLine(6, line, statusColor);
  snprintf(line, sizeof(line), wardriveStats.didFlush ? "Flushed %lus ago" : "No flush yet", (unsigned long)flushAge);
  display.dashboardLine(7, storage == 4 || wardriveStats.mode == 1 ? context.c_str() : line, kMuted);
  display.dashboardLine(8, storage == 4 ? "Worker: no local CSV" : wardriveCsvName().c_str(), kMuted);
#else
  display.setTextSize(1); display.setTextColor(statusColor, kBackground);
  display.setCursor(scaleX(6), scaleY(49)); display.print(wardriveRecordingLabel());
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(scaleX(6), scaleY(65)); display.print(local);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(scaleX(6), scaleY(79)); display.print(gpsTimezoneLabel());
  drawWardriveTile(6, 94, "WI-FI", wardriveNetworks);
  drawWardriveTile(123, 94, "BLE", wardriveBleCount);
  display.setTextSize(1); display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(scaleX(6), scaleY(156));
  display.printf("%s  %.2f km  %lu/min", wardriveElapsedText().c_str(), wardriveStats.distanceM / 1000.0,
                 (unsigned long)wardriveStats.perMinute());
  display.setTextColor(gpsHasFix() ? kGood : kWarn, kBackground);
  display.setCursor(scaleX(6), scaleY(176));
  display.printf("GPS %s | %d satellites", gpsHasFix() ? "FIX" : "NO FIX", gpsSats());
  display.setCursor(scaleX(6), scaleY(190));
  const double hdop = gps.hdop.isValid() && gps.hdop.age() < 5000 ? gps.hdop.hdop() : -1;
  if (hdop >= 0) display.printf("HDOP %.1f | fix coverage %u%%", hdop, wardriveStats.fixPercent());
  else display.printf("HDOP -- | fix coverage %u%%", wardriveStats.fixPercent());
  display.fillRoundRect(scaleX(6), scaleY(207), scaleX(228), scaleY(52), 4, kPanel);
  display.setTextColor(statusColor, kPanel); display.setCursor(scaleX(12), scaleY(213));
  if (storage == 4) display.print("Relay to coordinator");
  else display.printf("SD %lu rows | %lu KB", (unsigned long)wardriveStats.rows, (unsigned long)(wardriveStats.bytes / 1024));
  display.setTextColor(kMuted, kPanel); display.setCursor(scaleX(12), scaleY(228));
  if (storage == 3) display.print("Write failed - check SD card");
  else if (storage == 0) display.print("No file recording available");
  else if (storage == 4) display.print("Coordinator owns the merged CSV");
  else if (wardriveStats.didFlush) display.printf("Flushed %lu rows | %lus ago", (unsigned long)wardriveStats.flushedRows, (unsigned long)flushAge);
  else display.print("Waiting for first flush");
  display.setCursor(scaleX(12), scaleY(243));
  display.print(storage == 4 ? "Worker: no local CSV" : clipped(wardriveCsvName(), 35));
  display.setTextColor(kMuted, kBackground); display.setCursor(scaleX(6), scaleY(266)); display.print(clipped(context, 37));
#endif
}

void drawWardrive() {
  currentView = View::kWardrive;
  display.fillScreen(kBackground);
  drawHeader("WARDRIVE", radiosCoexist ? "Wi-Fi + BLE session" : "Wi-Fi session");
  const String phase = radiosCoexist ? (wardriveSched.phase == RadioPhase::kBle ? "BLE window" : "Wi-Fi window") : "Wi-Fi only";
  drawWardriveDashboardBody(phase + " | scans " + String(wardriveScans));
  drawFooter("Stop", "Home");
}

// Wi-Fi window hooks for the dual-radio scheduler: kick an async passive AP
// scan, and tear the scan down before Wi-Fi is released for the BLE window.
static void wardriveEnterWifi() {
  WiFi.disconnect(false, false);
  esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
  linkBroadcastStatus();
  WiFi.scanNetworks(true, true, false, 120);
}
static void wardriveExitWifi() { WiFi.scanDelete(); }

void startWardrive() {
  stopActiveTools();  // release the previous tool before bringing up both radios
  wardriveNetworks = 0;
  wardriveBleCount = 0;
  wardriveScans = 0;
  wardriveResetDedup();
  bleHitHead = 0;
  bleHitTail = 0;
  wardriveStartMs = millis();
  resetWardriveSessionStats(false);
  lastWardriveDrawMs = 0;
  signalMonitorActive = false;

  wardriveCsvReady = openWardriveCsv();

  wardriveSched = RadioScheduler();
  wardriveSched.enterWifi = wardriveEnterWifi;
  wardriveSched.exitWifi = wardriveExitWifi;
  wardriveSched.bleCallbacks = &wardriveBleCallbacks;  // passive scan
  // Wi-Fi window ends when the scan completes; cap high so a slow dual-band
  // sweep is never cut off mid-scan (which would log zero APs).
  // Long Wi-Fi dwell (many scan passes) between short BLE windows gives Wi-Fi
  // most airtime. Both controllers stay initialized throughout the session.
  wardriveSched.wifiWindowMs = 30000;
  wardriveSched.bleWindowMs = 8000;
  if (!radioSchedulerBegin(wardriveSched)) {
    closeWardriveCsv();
    return;
  }

  Serial.println(radiosCoexist
                     ? "[wardrive] started (Wi-Fi + BLE, time-shared)"
                     : "[wardrive] started (Wi-Fi only)");
  wardriveActive = true;
  drawWardrive();
  esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
  linkBroadcastStatus();
}

void stopWardrive() {
  if (!wardriveActive && !wardriveSched.active) return;
  wardriveActive = false;
  Serial.println("[wardrive] stopping; closing CSV before radio shutdown");
  // Only loopTask writes the CSV; radio callbacks enqueue observations. Save
  // accepted rows before touching the host/controller shutdown path.
  closeWardriveCsv();
  Serial.println("[wardrive] CSV closed; stopping radios");
  radioSchedulerEnd(wardriveSched);
  esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
  linkBroadcastStatus();
  Serial.printf("[wardrive] stopped; %lu Wi-Fi, %lu BLE\n",
                static_cast<unsigned long>(wardriveNetworks),
                static_cast<unsigned long>(wardriveBleCount));
}

void updateWardrive() {
  if (!wardriveActive) return;
  radioSchedulerTick(wardriveSched);

  // Drain BLE advertisements (the queue only fills during a BLE window): log
  // each new address once, when a fix is present.
  while (bleHitTail != bleHitHead) {
    const BleHit& hit = bleHitQueue[bleHitTail];
    uint8_t mac[6];
    if (gpsHasFix() && parseBssid(String(hit.addr), mac) &&
        !wardriveMacSeen(mac)) {
      wardriveAddMac(mac);
      appendWardriveBleRow(String(hit.addr), String(hit.name), hit.rssi);
      ++wardriveBleCount;
    }
    bleHitTail = (bleHitTail + 1) % kBleHitQueueSlots;
  }

  // Service the Wi-Fi AP scan only during the Wi-Fi window and only until it
  // completes; the scheduler then ends the window (a dual-band scan can take
  // longer than a fixed slice, so ending on completion is what actually lets
  // APs get logged). Wi-Fi is torn down during BLE windows, so scanComplete()
  // must not be polled then.
  if (wardriveSched.phase == RadioPhase::kWifi && !wardriveSched.wifiScanDone) {
    const int result = WiFi.scanComplete();
    if (result == WIFI_SCAN_RUNNING) {
      // scan in progress: nothing to do this pass
    } else if (result >= 0) {
      for (int i = 0; i < result; ++i) {
        uint8_t* bssid = WiFi.BSSID(i);
        if (!bssid) continue;
        if (gpsHasFix() && !wardriveMacSeen(bssid)) {
          wardriveAddMac(bssid);
          appendWardriveRow(WiFi.BSSIDstr(i), WiFi.SSID(i),
                            WiFi.encryptionType(i), WiFi.channel(i),
                            WiFi.RSSI(i));
          ++wardriveNetworks;
        }
      }
      WiFi.scanDelete();
      ++wardriveScans;
      // Between scan passes, briefly home to the rendezvous channel so the
      // screen chip delivers live AP & BLE counts to the bridge/phone.
      esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
      linkBroadcastStatus();
      WiFi.scanNetworks(true, true, false, 120);
    } else {
      // not started / failed: kick off a scan
      WiFi.scanNetworks(true, true, false, 120);
    }
  }
  static uint32_t lastWardriveFlushMs = 0;
  if (millis() - lastWardriveFlushMs >= 2000) {
    lastWardriveFlushMs = millis();
    flushWardriveCsv();
  }
  if (currentView == View::kWardrive &&
      millis() - lastWardriveDrawMs >= kWardriveRedrawMs) {
    lastWardriveDrawMs = millis();
    drawWardrive();
  }
}
