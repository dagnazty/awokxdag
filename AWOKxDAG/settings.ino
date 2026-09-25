// AWOKxDAG — device settings (GPS baud, backlight). Stored in NVS separately
// from saved networks. Reached from Status → Settings.

const uint32_t kBacklightTimeoutOptions[] = {0, 15000, 30000, 60000, 120000,
                                             300000};
constexpr int kBacklightTimeoutOptionCount = static_cast<int>(
    sizeof(kBacklightTimeoutOptions) / sizeof(kBacklightTimeoutOptions[0]));
const uint8_t kBrightnessOptions[] = {20, 40, 60, 80, 100};
constexpr int kBrightnessOptionCount =
    static_cast<int>(sizeof(kBrightnessOptions) / sizeof(kBrightnessOptions[0]));
// Keep the existing NVS record and View IDs; these are UI-only subpages.
int settingsGroup = 0;  // root, Display, GPS & Time, Behavior, Diagnostics
int settingsEdit = -1, settingsChoice = -1;
bool settingsResetConfirm = false;
String settingsNotice;
const char* const kSettingsGroups[] = {"SETTINGS", "DISPLAY", "GPS & TIME", "BEHAVIOR", "DIAGNOSTICS"};
const char* const kSettingsNames[] = {"SCREEN SLEEP", "BRIGHTNESS", "GPS BAUD", "BOOT SPLASH", "ACTIVE CONFIRM", "RAW NMEA"};
char attackConfirmLabel[24] = {};
uint32_t attackConfirmMs = 0;

bool gpsBaudIsKnown(unsigned long baud) {
  for (int i = 0; i < kGpsBaudOptionCount; ++i) {
    if (kGpsBaudOptions[i] == baud) return true;
  }
  return false;
}

bool settingFlag(uint8_t bit) { return (deviceSettings.flags & bit) != 0; }

void setSettingFlag(uint8_t bit, bool on) {
  if (on) deviceSettings.flags |= bit;
  else deviceSettings.flags = static_cast<uint8_t>(deviceSettings.flags & ~bit);
}

const char* backlightTimeoutLabel(uint32_t ms) {
  if (ms == 0) return "Off";
  if (ms == 15000) return "15s";
  if (ms == 30000) return "30s";
  if (ms == 60000) return "1m";
  if (ms == 120000) return "2m";
  if (ms == 300000) return "5m";
  return "?";
}

bool confirmActiveTest(const char* label) {
  if (!settingFlag(kSettingConfirmAttacks)) return true;
  if (attackConfirmLabel[0] && strcmp(attackConfirmLabel, label) == 0 &&
      millis() - attackConfirmMs < 4000) {
    attackConfirmLabel[0] = 0;
    return true;
  }
  strncpy(attackConfirmLabel, label, sizeof(attackConfirmLabel) - 1);
  attackConfirmLabel[sizeof(attackConfirmLabel) - 1] = 0;
  attackConfirmMs = millis();
  Serial.printf("[settings] tap again to start %s\n", label);
  return false;
}

void drawConfirmBanner() {
  if (!attackConfirmLabel[0]) return;
  if (millis() - attackConfirmMs >= 4000) {
    attackConfirmLabel[0] = 0;
    return;
  }
  display.setTextSize(1);
  display.setTextColor(kBad, kBackground);
  display.setCursor(6, 258);
  display.print("Tap again: ");
  display.print(attackConfirmLabel);
}

void deviceSettingsDefaults(DeviceSettingsRecord& out) {
  out = {};
  out.version = kDeviceSettingsVersion;
  out.gpsBaud = AwokPins::kGpsBaud;
  out.backlightTimeoutMs = 0;
  out.brightnessPercent = 100;
}

void writeBacklightPercent(int percent) {
  const int pin = AwokPins::kBacklight;
  pinMode(pin, OUTPUT);
  // Full on/off stay on GPIO so LEDC is not attached at boot (it fragments
  // the large internal heap block Wi-Fi needs).
  if (percent <= 0) {
    digitalWrite(pin, AwokPins::kBacklightOn ? LOW : HIGH);
    return;
  }
  if (percent >= 100) {
    digitalWrite(pin, AwokPins::kBacklightOn ? HIGH : LOW);
    return;
  }
  const int duty = constrain(percent, 20, 99) * 255 / 100;
  analogWrite(pin, AwokPins::kBacklightOn ? duty : (255 - duty));
}

void setBacklightLit(bool on) {
  backlightDimmed = !on;
  writeBacklightPercent(on ? deviceSettings.brightnessPercent : 0);
}

void noteActivity() {
  lastActivityMs = millis();
  if (backlightDimmed) setBacklightLit(true);
}

void updateBacklightSleep() {
  if (currentView == View::kScreenTest) return;
  if (deviceSettings.backlightTimeoutMs == 0 || backlightDimmed) return;
  if (millis() - lastActivityMs >= deviceSettings.backlightTimeoutMs) {
    setBacklightLit(false);
  }
}

bool saveDeviceSettings() {
  Preferences preferences;
  if (!preferences.begin("awokxdag", false)) {
    lastSettingsWriteOk = false;
    return false;
  }
  deviceSettings.version = kDeviceSettingsVersion;
  const bool written =
      preferences.putBytes("settings", &deviceSettings, sizeof(deviceSettings)) ==
      sizeof(deviceSettings);
  preferences.end();
  lastSettingsWriteOk = written;
  if (written) {
    recordFirmwareAudit(
        "configuration", "settings", "success",
        "gps_baud=" + String(deviceSettings.gpsBaud) +
            "; backlight_ms=" + String(deviceSettings.backlightTimeoutMs) +
            "; brightness=" + String(deviceSettings.brightnessPercent) +
            "; flags=" + String(deviceSettings.flags));
  } else {
    recordFirmwareAudit("configuration", "settings", "failed",
                        "persistent storage write failed");
  }
  return written;
}

void loadDeviceSettings() {
  deviceSettingsDefaults(deviceSettings);
  lastSettingsWriteOk = true;
  Preferences preferences;
  if (!preferences.begin("awokxdag", true)) return;
  if (!preferences.isKey("settings")) {
    preferences.end();
    return;
  }
  DeviceSettingsRecord loaded = {};
  const bool ok =
      preferences.getBytesLength("settings") == sizeof(loaded) &&
      preferences.getBytes("settings", &loaded, sizeof(loaded)) == sizeof(loaded) &&
      loaded.version == kDeviceSettingsVersion;
  preferences.end();
  if (!ok) {
    Serial.println("[settings] invalid NVS record; using defaults");
    lastSettingsWriteOk = false;
    return;
  }
  if (!gpsBaudIsKnown(loaded.gpsBaud)) loaded.gpsBaud = AwokPins::kGpsBaud;
  bool timeoutOk = false;
  for (int i = 0; i < kBacklightTimeoutOptionCount; ++i) {
    if (kBacklightTimeoutOptions[i] == loaded.backlightTimeoutMs) timeoutOk = true;
  }
  if (!timeoutOk) loaded.backlightTimeoutMs = 0;
  if (loaded.brightnessPercent < 20 || loaded.brightnessPercent > 100) {
    loaded.brightnessPercent = 100;
  }
  deviceSettings = loaded;
  gpsRawEcho = settingFlag(kSettingNmeaEcho);
  Serial.printf("[settings] baud=%lu timeout=%lums brightness=%u%% flags=0x%02x\n",
                deviceSettings.gpsBaud,
                static_cast<unsigned long>(deviceSettings.backlightTimeoutMs),
                unsigned(deviceSettings.brightnessPercent),
                unsigned(deviceSettings.flags));
}

void resetDeviceSettings() {
  deviceSettingsDefaults(deviceSettings);
  gpsRawEcho = false;
  attackConfirmLabel[0] = 0;
  applyGpsBaud(deviceSettings.gpsBaud, false);
  setBacklightLit(true);
  saveDeviceSettings();
}

void toggleSettingFlag(uint8_t bit) {
  setSettingFlag(bit, !settingFlag(bit));
  if (bit == kSettingNmeaEcho) gpsRawEcho = settingFlag(kSettingNmeaEcho);
  saveDeviceSettings();
}

String settingsValueLabel(int item) {
  if (item == 0) return deviceSettings.backlightTimeoutMs ? String(backlightTimeoutLabel(deviceSettings.backlightTimeoutMs)) : "Never";
  if (item == 1) return String(deviceSettings.brightnessPercent) + "%";
  if (item == 2) return String(deviceSettings.gpsBaud);
  if (item == 3) return settingFlag(kSettingSkipSplash) ? "Skip" : "Show";
  if (item == 4) return settingFlag(kSettingConfirmAttacks) ? "Confirm" : "Immediate";
  return settingFlag(kSettingNmeaEcho) ? "On" : "Off";
}

int settingsChoiceCount(int item) {
  if (item == 0) return kBacklightTimeoutOptionCount;
  if (item == 1) return kBrightnessOptionCount;
  if (item == 2) return kGpsBaudOptionCount;
  return 2;
}

String settingsChoiceLabel(int item, int choice) {
  if (item == 0) return choice == 0 ? "Never" : String(backlightTimeoutLabel(kBacklightTimeoutOptions[choice]));
  if (item == 1) return String(kBrightnessOptions[choice]) + "%";
  if (item == 2) return String(kGpsBaudOptions[choice]);
  if (item == 3) return choice == 0 ? "Show at startup" : "Skip at startup";
  if (item == 4) return choice == 0 ? "Require second tap" : "Start immediately";
  return choice == 0 ? "Off" : "On / USB Serial";
}

int settingsCurrentChoice(int item) {
  if (item >= 3) {
    if (item == 3) return settingFlag(kSettingSkipSplash) ? 1 : 0;
    if (item == 4) return settingFlag(kSettingConfirmAttacks) ? 0 : 1;
    return settingFlag(kSettingNmeaEcho) ? 1 : 0;
  }
  for (int i = 0; i < settingsChoiceCount(item); ++i) {
    if (item == 0 && deviceSettings.backlightTimeoutMs == kBacklightTimeoutOptions[i]) return i;
    if (item == 1 && deviceSettings.brightnessPercent == kBrightnessOptions[i]) return i;
    if (item == 2 && deviceSettings.gpsBaud == kGpsBaudOptions[i]) return i;
  }
  return -1;  // retain a valid non-preset value until an explicit choice is made
}

void beginSettingsEdit(int item) {
  if (item < 0 || item > 5) return;
  settingsEdit = item;
  settingsChoice = settingsCurrentChoice(item);
  settingsNotice = "";
  drawSettings();
}

void applySettingsChoice() {
  if (settingsEdit < 0 || settingsEdit > 5 || settingsChoice < 0 ||
      settingsChoice >= settingsChoiceCount(settingsEdit)) return;
  if (settingsEdit == 0) deviceSettings.backlightTimeoutMs = kBacklightTimeoutOptions[settingsChoice];
  else if (settingsEdit == 1) deviceSettings.brightnessPercent = kBrightnessOptions[settingsChoice];
  else if (settingsEdit == 2) {
    if (deviceSettings.gpsBaud != kGpsBaudOptions[settingsChoice])
      applyGpsBaud(kGpsBaudOptions[settingsChoice], false);
  }
  else if (settingsEdit == 3) setSettingFlag(kSettingSkipSplash, settingsChoice == 1);
  else if (settingsEdit == 4) {
    setSettingFlag(kSettingConfirmAttacks, settingsChoice == 0);
    attackConfirmLabel[0] = 0;
  } else {
    setSettingFlag(kSettingNmeaEcho, settingsChoice == 1);
    gpsRawEcho = settingFlag(kSettingNmeaEcho);
  }
  if (settingsEdit <= 1) { noteActivity(); setBacklightLit(true); }
  const bool saved = saveDeviceSettings();
  settingsNotice = saved ? "Saved on this device." : "RAM only. Retry save from Settings.";
  settingsEdit = -1;
  drawSettings();
}

bool settingsHit(int x, int y, int left, int top, int width, int height) {
  return x >= left && x < left + width && y >= top && y < top + height;
}

void openSettings() {
  settingsGroup = 0;
  settingsEdit = -1;
  settingsResetConfirm = false;
  settingsNotice = "";
  drawSettings();
}

void drawSettings() {
  currentView = View::kSettings;
  display.fillScreen(kBackground);
  const char* title = settingsResetConfirm ? "RESET SETTINGS?" : settingsEdit >= 0 ? kSettingsNames[settingsEdit] : kSettingsGroups[settingsGroup];
  drawHeader(title, lastSettingsWriteOk ? (settingsEdit >= 0 ? "choose a value, then Save" : "stored on this device") : "SAVE FAILED - changes in RAM only");
  display.setTextSize(1); display.setTextColor(kMuted, kBackground);
  if (settingsResetConfirm) {
    display.setCursor(8, 54); display.print("Restore device preferences:");
    display.setCursor(8, 80); display.print("Brightness 100% / screen sleep Never");
    display.setCursor(8, 96); display.print("GPS baud " + String(AwokPins::kGpsBaud));
    display.setCursor(8, 112); display.print("Splash Show / active tools Immediate");
    display.setCursor(8, 128); display.print("Raw NMEA Off");
    display.setCursor(8, 146); display.print("Keeps saved networks, files,");
    display.setCursor(8, 160); display.print("and the GPS-selected timezone.");
    drawSmallButton(8, 176, 224, 44, "Restore defaults", kBad);
    drawSmallButton(4, 280, 232, 36, "Cancel", kMuted);
    return;
  }
  if (settingsEdit >= 0) {
    display.setCursor(8, 52); display.print("Current: " + settingsValueLabel(settingsEdit));
    display.setCursor(8, 70); display.print("Changes apply only when you Save.");
    const int count = settingsChoiceCount(settingsEdit);
    for (int i = 0; i < count; ++i) {
      const int x = count > 2 ? 8 + (i % 2) * 116 : 8;
      const int y = 102 + (count > 2 ? i / 2 : i) * 54;
      drawSmallButton(x, y, count > 2 ? 108 : 224, 44,
          String(i == settingsChoice ? "> " : "") + settingsChoiceLabel(settingsEdit, i),
          i == settingsChoice ? kGood : kAccent);
    }
    drawSmallButton(4, 280, 112, 36, "Cancel", kMuted);
    if (settingsChoice >= 0) drawSmallButton(124, 280, 112, 36, "Save", kAccent);
    return;
  }
  if (settingsGroup == 0) {
    for (int i = 1; i <= 4; ++i)
      drawSmallButton(8, 46 + (i - 1) * 44, 224, 40, kSettingsGroups[i], kAccent);
    drawSmallButton(8, 222, 224, 40, "Restore defaults...", kWarn);
  } else if (settingsGroup == 1) {
    drawSmallButton(8, 52, 224, 44, "Screen sleep: " + settingsValueLabel(0), kAccent);
    drawSmallButton(8, 104, 224, 44, "Brightness: " + settingsValueLabel(1), kAccent);
    drawSmallButton(8, 156, 224, 44, "Boot splash: " + settingsValueLabel(3), kAccent);
  } else if (settingsGroup == 2) {
    drawSmallButton(8, 52, 224, 44, "GPS baud: " + settingsValueLabel(2), kAccent);
    display.setTextColor(kAccent, kBackground);
    display.setCursor(8, 118); display.print("TIMEZONE / DST: AUTOMATIC FROM GPS");
    display.setTextColor(gpsHasFix() ? kGood : kWarn, kBackground);
    display.setCursor(8, 138); display.print(gpsReceptionLabel());
    display.setTextColor(ILI9341_WHITE, kBackground);
    display.setCursor(8, 158); display.print(gpsTimestamp());
    display.setCursor(8, 174); display.print(gpsTimezoneLabel());
    display.setTextColor(kMuted, kBackground);
    display.setCursor(8, 194); display.print(gpsLocalZone >= 0 ? clipped(String(AwokTime::kZones[gpsLocalZone].name), 37) : "Waiting for GPS location.");
    display.setCursor(8, 214); display.print("Last known zone survives fix loss.");
  } else if (settingsGroup == 3) {
    drawSmallButton(8, 52, 224, 44, "Active tools: " + settingsValueLabel(4), kAccent);
    display.setTextColor(kMuted, kBackground);
    display.setCursor(8, 124); display.print("Choose whether active tools need");
    display.setCursor(8, 140); display.print("a second tap before starting.");
  } else {
    drawSmallButton(8, 52, 224, 44, "GPS receiver diagnostics", kAccent);
    drawSmallButton(8, 104, 224, 44, "Raw NMEA: " + settingsValueLabel(5), kAccent);
    drawSmallButton(8, 156, 224, 44, "Screen / input test", kAccent);
    display.setTextColor(kMuted, kBackground);
    display.setCursor(8, 224); display.print("Raw GPS sentences go to USB Serial.");
  }
  display.setTextSize(1); display.setTextColor(lastSettingsWriteOk ? kMuted : kWarn, kBackground);
  display.setCursor(8, 268); display.print(clipped(settingsNotice, 37));
  drawSmallButton(4, 280, 112, 36, "Back", kMuted);
  drawSmallButton(124, 280, 112, 36, lastSettingsWriteOk ? "Home" : "Retry save", kAccent);
}

void handleSettingsTouch(int x, int y) {
  if (settingsResetConfirm) {
    if (settingsHit(x, y, 4, 280, 232, 36)) settingsResetConfirm = false;
    else if (settingsHit(x, y, 8, 176, 224, 44)) {
      resetDeviceSettings();
      settingsResetConfirm = false;
      settingsNotice = lastSettingsWriteOk ? "Defaults restored." : "Defaults in RAM only. Retry save.";
    } else return;
    drawSettings(); return;
  }
  if (settingsEdit >= 0) {
    if (settingsHit(x, y, 4, 280, 112, 36)) { settingsEdit = -1; drawSettings(); return; }
    if (settingsHit(x, y, 124, 280, 112, 36)) { applySettingsChoice(); return; }
    const int count = settingsChoiceCount(settingsEdit);
    for (int i = 0; i < count; ++i) {
      const int left = count > 2 ? 8 + (i % 2) * 116 : 8;
      const int top = 102 + (count > 2 ? i / 2 : i) * 54;
      if (settingsHit(x, y, left, top, count > 2 ? 108 : 224, 44)) {
        settingsChoice = i; drawSettings(); return;
      }
    }
    return;
  }
  if (settingsHit(x, y, 4, 280, 112, 36)) {
    if (settingsGroup == 0) drawHome();  // Settings is a Home destination now
    else { settingsGroup = 0; settingsNotice = ""; drawSettings(); }
    return;
  }
  if (settingsHit(x, y, 124, 280, 112, 36)) {
    if (lastSettingsWriteOk) drawHome();
    else { saveDeviceSettings(); settingsNotice = lastSettingsWriteOk ? "Saved on this device." : "Save failed. Changes remain in RAM."; drawSettings(); }
    return;
  }
  if (settingsGroup == 0) {
    for (int group = 1; group <= 4; ++group) {
      if (settingsHit(x, y, 8, 46 + (group - 1) * 44, 224, 40)) {
        settingsGroup = group; settingsNotice = ""; drawSettings(); return;
      }
    }
    if (settingsHit(x, y, 8, 222, 224, 40)) { settingsResetConfirm = true; drawSettings(); }
    return;
  }
  if (settingsGroup == 1) {
    if (settingsHit(x, y, 8, 52, 224, 44)) beginSettingsEdit(0);
    else if (settingsHit(x, y, 8, 104, 224, 44)) beginSettingsEdit(1);
    else if (settingsHit(x, y, 8, 156, 224, 44)) beginSettingsEdit(3);
  } else if (settingsGroup == 2) {
    if (settingsHit(x, y, 8, 52, 224, 44)) beginSettingsEdit(2);
  } else if (settingsGroup == 3) {
    if (settingsHit(x, y, 8, 52, 224, 44)) beginSettingsEdit(4);
  } else {
    if (settingsHit(x, y, 8, 52, 224, 44)) { gpsDiagnosticsFromSettings = true; drawGpsDiagnostics(); }
    else if (settingsHit(x, y, 8, 104, 224, 44)) beginSettingsEdit(5);
    else if (settingsHit(x, y, 8, 156, 224, 44)) startScreenTest();
  }
}

void updateSettingsPage() {
  // Refresh the automatic time summary without resetting a pending choice.
  static uint32_t lastDrawMs = 0;
  if (currentView != View::kSettings || settingsGroup != 2 || settingsEdit >= 0 ||
      settingsResetConfirm || millis() - lastDrawMs < 1000) return;
  lastDrawMs = millis();
  drawSettings();
}
