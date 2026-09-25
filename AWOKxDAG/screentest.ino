// AWOKxDAG — panel / input diagnostic. Distinguishes a dead or rotated screen
// from a firmware drawing bug. Mini draws straight to the ST7735, bypassing
// MiniLayout; one step also blits through the firmware canvas for comparison.

enum ScreenTestStep : uint8_t {
  kScreenTestIntro = 0,
  kScreenTestRed,
  kScreenTestGreen,
  kScreenTestBlue,
  kScreenTestWhite,
  kScreenTestBlack,
  kScreenTestBars,
  kScreenTestChecker,
  kScreenTestFirmware,
  kScreenTestOrient,
  kScreenTestInput,
  kScreenTestBacklight,
  kScreenTestVerdict,
  kScreenTestCount
};

constexpr uint16_t kTestRed = 0xF800;
constexpr uint16_t kTestGreen = 0x07E0;
constexpr uint16_t kTestBlue = 0x001F;
constexpr uint16_t kTestWhite = 0xFFFF;
constexpr uint16_t kTestBlack = 0x0000;
constexpr uint16_t kTestYellow = 0xFFE0;
constexpr uint16_t kTestCyan = 0x07FF;
constexpr uint16_t kTestMagenta = 0xF81F;

#ifdef AWOK_MINI_DISPLAY
constexpr int kTestW = 128;
constexpr int kTestH = 128;
#else
constexpr int kTestW = kScreenWidth;
constexpr int kTestH = kScreenHeight;
#endif

uint8_t screenTestStep = 0;
uint32_t screenTestStepMs = 0;
uint8_t screenTestButtons = 0;
uint8_t screenTestCorners = 0;  // bit0 TL, 1 TR, 2 BL, 3 BR, 4 center
int screenTestMapX = -1, screenTestMapY = -1;
int screenTestRawX = 0, screenTestRawY = 0, screenTestRawZ = 0;

void screenTestFill(uint16_t color) {
#ifdef AWOK_MINI_DISPLAY
  display.diagnosticFill(color);
#else
  display.fillScreen(color);
#endif
}

void screenTestText(int x, int y, uint16_t color, const char* text,
                    uint16_t bg = kTestBlack) {
#ifdef AWOK_MINI_DISPLAY
  display.diagnosticText(x, y, color, text, bg);
#else
  display.setTextWrap(false);
  display.setTextSize(1);
  display.setTextColor(color, bg);
  display.setCursor(x, y);
  display.print(text);
#endif
}

void screenTestRect(int x, int y, int w, int h, uint16_t color, bool fill) {
#ifdef AWOK_MINI_DISPLAY
  display.diagnosticRect(x, y, w, h, color, fill);
#else
  if (fill) display.fillRect(x, y, w, h, color);
  else display.drawRect(x, y, w, h, color);
#endif
}

void finishScreenTest() {
  writeBacklightPercent(deviceSettings.brightnessPercent);
  backlightDimmed = false;
#ifdef AWOK_MINI_DISPLAY
  display.diagnosticEnd();
#endif
}

void stopScreenTest() {
  finishScreenTest();
  drawSettings();
}

void screenTestDrawChecker(uint16_t a, uint16_t b, int cell) {
#ifdef AWOK_MINI_DISPLAY
  display.diagnosticChecker(a, b, cell);
#else
  for (int y = 0; y < kTestH; y += cell)
    for (int x = 0; x < kTestW; x += cell)
      display.fillRect(x, y, cell, cell, ((x / cell) + (y / cell)) & 1 ? a : b);
#endif
}

void screenTestDrawBars() {
  const uint16_t colors[] = {kTestWhite, kTestYellow, kTestCyan, kTestGreen,
                             kTestMagenta, kTestRed, kTestBlue, kTestBlack};
  const int n = 8;
  const int stripe = kTestW / n;
  for (int i = 0; i < n; ++i) {
    const int x = i * stripe;
    const int w = (i == n - 1) ? (kTestW - x) : stripe;
    screenTestRect(x, 0, w, kTestH, colors[i], true);
  }
}

void screenTestDrawOrient() {
  screenTestFill(kTestBlack);
  screenTestText(2, 2, kTestGreen, "TL");
  screenTestText(kTestW - 14, 2, kTestGreen, "TR");
  screenTestText(2, kTestH - 10, kTestGreen, "BL");
  screenTestText(kTestW - 14, kTestH - 10, kTestGreen, "BR");
  screenTestText(kTestW / 2 - 24, kTestH / 2 - 8, kTestWhite, "AxD");
  screenTestText(kTestW / 2 - 18, kTestH / 2 + 4, kTestCyan, kVersion);
  screenTestText(4, kTestH / 2 + 20, kTestYellow, "TL must be top-left");
}

void screenTestDrawIntro() {
  screenTestFill(kTestBlack);
  screenTestText(4, 8, kTestCyan, "SCREEN TEST");
  screenTestText(4, 24, kTestWhite, "Solids: panel / cable");
  screenTestText(4, 36, kTestWhite, "Checker vs blit: GPU");
  screenTestText(4, 48, kTestWhite, "  vs firmware path");
#ifdef AWOK_MINI_DISPLAY
  screenTestText(4, 64, kTestWhite, "Buttons: GPIO map");
  screenTestText(4, 84, kTestYellow, "Center: next");
  screenTestText(4, 96, kTestYellow, "Left: exit");
#else
  screenTestText(4, 64, kTestWhite, "Touch: raw vs mapped");
  screenTestText(4, 84, kTestYellow, "Tap: next step");
  screenTestText(4, 96, kTestYellow, "Exit on intro/end");
#endif
  screenTestRect(4, kTestH - 28, 56, 22, kTestWhite, false);
  screenTestText(16, kTestH - 22, kTestWhite, "EXIT");
  screenTestRect(kTestW - 64, kTestH - 28, 56, 22, kTestCyan, false);
  screenTestText(kTestW - 50, kTestH - 22, kTestCyan, "NEXT");
}

void screenTestDrawInput() {
  screenTestFill(kTestBlack);
#ifdef AWOK_MINI_DISPLAY
  screenTestText(4, 4, kTestCyan, "BUTTON TEST");
  screenTestText(4, 16, kTestWhite, "Press every key");
  const char* names[] = {"Left", "Center", "Up", "Right", "Down"};
  for (int i = 0; i < 5; ++i) {
    const bool hit = screenTestButtons & (1 << i);
    char line[22];
    snprintf(line, sizeof(line), "%s %s", names[i], hit ? "OK" : "....");
    screenTestText(8, 36 + i * 12, hit ? kTestGreen : kTestWhite, line);
  }
  screenTestText(4, kTestH - 12, kTestYellow, "Center: next");
#else
  screenTestText(4, 4, kTestCyan, "TOUCH TEST");
  char line[40];
  snprintf(line, sizeof(line), "map %d,%d", screenTestMapX, screenTestMapY);
  screenTestText(4, 16, kTestWhite, line);
  snprintf(line, sizeof(line), "raw %d,%d z=%d", screenTestRawX, screenTestRawY,
           screenTestRawZ);
  screenTestText(4, 28, kTestWhite, line);
  const char* spots[] = {"TL", "TR", "BL", "BR", "CEN"};
  for (int i = 0; i < 5; ++i) {
    const bool hit = screenTestCorners & (1 << i);
    snprintf(line, sizeof(line), "%s %s", spots[i], hit ? "OK" : "--");
    screenTestText(4 + (i % 3) * 72, 44 + (i / 3) * 14,
                   hit ? kTestGreen : kTestWhite, line);
  }
  screenTestText(4, 76, kTestYellow, "Tap corners + center");
  screenTestText(4, 88, kTestYellow, "Then tap NEXT");
  if (screenTestMapX >= 0) {
    screenTestRect(screenTestMapX - 3, screenTestMapY - 3, 7, 7, kTestRed, true);
  }
  screenTestRect(kTestW - 64, kTestH - 28, 56, 22, kTestCyan, false);
  screenTestText(kTestW - 50, kTestH - 22, kTestCyan, "NEXT");
#endif
}

void screenTestDrawVerdict() {
  screenTestFill(kTestBlack);
  screenTestText(4, 6, kTestCyan, "HOW TO READ IT");
  screenTestText(4, 22, kTestWhite, "Bad solids: panel,");
  screenTestText(4, 34, kTestWhite, "flex, or backlight.");
  screenTestText(4, 50, kTestWhite, "Solids OK, blit bad:");
  screenTestText(4, 62, kTestWhite, "firmware pixel path.");
#ifdef AWOK_MINI_DISPLAY
  screenTestText(4, 78, kTestWhite, "Button never OK:");
  screenTestText(4, 90, kTestWhite, "GPIO or the switch.");
#else
  screenTestText(4, 78, kTestWhite, "raw moves, map off:");
  screenTestText(4, 90, kTestWhite, "touch calibration.");
  screenTestText(4, 106, kTestWhite, "raw stuck: controller");
#endif
  screenTestText(4, kTestH - 22, kTestYellow, "Tap: back to Settings");
}

void drawScreenTest() {
  currentView = View::kScreenTest;
  switch (screenTestStep) {
    case kScreenTestIntro:
      screenTestDrawIntro();
      break;
    case kScreenTestRed:
      screenTestFill(kTestRed);
      break;
    case kScreenTestGreen:
      screenTestFill(kTestGreen);
      break;
    case kScreenTestBlue:
      screenTestFill(kTestBlue);
      break;
    case kScreenTestWhite:
      screenTestFill(kTestWhite);
      break;
    case kScreenTestBlack:
      screenTestFill(kTestBlack);
      break;
    case kScreenTestBars:
      screenTestDrawBars();
      break;
    case kScreenTestChecker:
      screenTestDrawChecker(kTestWhite, kTestBlack, 16);
      screenTestText(4, 4, kTestRed, "PANEL", kTestWhite);
      break;
    case kScreenTestFirmware:
#ifdef AWOK_MINI_DISPLAY
      display.diagnosticFirmwareChecker(kTestWhite, kTestBlack, 16);
      screenTestText(4, 4, kTestBlue, "FIRMWARE", kTestWhite);
#else
      screenTestDrawChecker(kTestWhite, kTestBlack, 4);
      screenTestText(4, 4, kTestBlue, "GFX GRID", kTestBlack);
#endif
      break;
    case kScreenTestOrient:
      screenTestDrawOrient();
      break;
    case kScreenTestInput:
      screenTestDrawInput();
      break;
    case kScreenTestBacklight:
      screenTestFill(kTestWhite);
      screenTestText(4, kTestH / 2 - 8, kTestBlack, "BACKLIGHT SWEEP", kTestWhite);
      screenTestText(4, kTestH / 2 + 8, kTestBlack, "watch fade; tap next",
                     kTestWhite);
      break;
    case kScreenTestVerdict:
      screenTestDrawVerdict();
      break;
    default:
      break;
  }
}

void startScreenTest() {
  screenTestStep = kScreenTestIntro;
  screenTestStepMs = millis();
  screenTestButtons = 0;
  screenTestCorners = 0;
  screenTestMapX = -1;
  screenTestMapY = -1;
  writeBacklightPercent(100);
  Serial.println("[screentest] start — tap through colors, then input");
  drawScreenTest();
}

void screenTestAdvance() {
  if (screenTestStep + 1 >= kScreenTestCount) {
    stopScreenTest();
    return;
  }
  ++screenTestStep;
  screenTestStepMs = millis();
  if (screenTestStep != kScreenTestBacklight) {
    writeBacklightPercent(deviceSettings.brightnessPercent);
  }
  Serial.printf("[screentest] step %u\n", unsigned(screenTestStep));
  drawScreenTest();
}

void handleScreenTestTouch(int x, int y) {
  // The screen test draws in the panel's physical space (kTestW/kTestH); taps
  // arrive in the 240x320 design grid, so scale them up to match its zones.
  x = scaleX(x);
  y = scaleY(y);
  if (screenTestStep == kScreenTestIntro) {
    if (x < kTestW / 2) {
      stopScreenTest();
      return;
    }
    screenTestAdvance();
    return;
  }
  if (screenTestStep == kScreenTestInput) {
#ifndef AWOK_MINI_DISPLAY
    if (y >= kTestH - 32 && x >= kTestW - 70) {
      screenTestAdvance();
      return;
    }
    const int margin = 40;
    if (x < margin && y < margin) screenTestCorners |= 1;
    if (x >= kTestW - margin && y < margin) screenTestCorners |= 2;
    if (x < margin && y >= kTestH - margin) screenTestCorners |= 4;
    if (x >= kTestW - margin && y >= kTestH - margin) screenTestCorners |= 8;
    if (x > kTestW / 2 - 30 && x < kTestW / 2 + 30 &&
        y > kTestH / 2 - 30 && y < kTestH / 2 + 30)
      screenTestCorners |= 16;
    screenTestMapX = x;
    screenTestMapY = y;
    drawScreenTest();
#endif
    return;
  }
  if (screenTestStep == kScreenTestVerdict) {
    stopScreenTest();
    return;
  }
  screenTestAdvance();
}

void updateScreenTest() {
  if (currentView != View::kScreenTest) return;
  if (screenTestStep == kScreenTestBacklight) {
    const int phase = static_cast<int>(((millis() - screenTestStepMs) / 400) % 6);
    const int percents[] = {100, 80, 60, 40, 20, 0};
    writeBacklightPercent(percents[phase]);
  }
#ifdef AWOK_MINI_DISPLAY
  if (screenTestStep == kScreenTestInput) {
    const uint8_t before = screenTestButtons;
    if (digitalRead(AwokPins::kButtonLeft) == LOW) screenTestButtons |= 1;
    if (digitalRead(AwokPins::kButtonCenter) == LOW) screenTestButtons |= 2;
    if (digitalRead(AwokPins::kButtonUp) == LOW) screenTestButtons |= 4;
    if (digitalRead(AwokPins::kButtonRight) == LOW) screenTestButtons |= 8;
    if (digitalRead(AwokPins::kButtonDown) == LOW) screenTestButtons |= 16;
    if (screenTestButtons != before) drawScreenTest();
  }
#elif defined(PANCAKE_CAP_TOUCH)
  if (screenTestStep == kScreenTestInput) {
    uint16_t panelX = 0, panelY = 0;
    if (touch.read(panelX, panelY)) {
      screenTestRawX = panelX;   // raw ST7796 panel coordinates (0..319/0..479)
      screenTestRawY = panelY;
      screenTestRawZ = 0;        // capacitive touch reports no pressure
    }
  }
#else
  if (screenTestStep == kScreenTestInput && touch.touched()) {
    TS_Point point = touch.getPoint();
    screenTestRawX = point.x;
    screenTestRawY = point.y;
    screenTestRawZ = point.z;
  }
#endif
}
