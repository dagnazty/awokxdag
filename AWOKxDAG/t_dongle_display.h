#pragma once

#include <Adafruit_ST7735.h>
#include "board_pins.h"
#include "t_dongle_logo_data.h"

// The T-Dongle remains a BLE-driven bridge; its LCD only shows identity and
// link/storage state. Both LCD and SD use the same SPI bus with separate CS.
class AwokDongleDisplay {
 public:
  AwokDongleDisplay()
      : panel_(&SPI, AwokPins::kDisplayCs, AwokPins::kDisplayDc,
               AwokPins::kDisplayReset) {}

  void begin() {
    panel_.initR(INITR_MINI160x80);
    panel_.setSPISpeed(16000000);
    panel_.setRotation(3);  // 160 x 80 along the dongle's long edge
    panel_.invertDisplay(true);  // dark AxD logo on a light background
    panel_.fillScreen(ST7735_BLACK);
    panel_.drawXBitmap(0, 0, kDongleLogoBitmap, kDongleLogoWidth,
                       kDongleLogoHeight, ST7735_WHITE);
    panel_.drawFastVLine(63, 0, 80, ST7735_CYAN);
    panel_.setTextWrap(false);
    panel_.setTextSize(1);
    panel_.setTextColor(ST7735_CYAN);
    panel_.setCursor(68, 34);
    panel_.print("STARTING");
    pinMode(AwokPins::kBacklight, OUTPUT);
    digitalWrite(AwokPins::kBacklight,
                 AwokPins::kBacklightOn ? HIGH : LOW);
    started_ = true;
    Serial.printf("[display] T-Dongle LCD %dx%d ready\n", panel_.width(),
                  panel_.height());
  }

  void update(bool sdReady, bool bleReady, bool phoneConnected) {
    if (!started_ || (drawn_ && sdReady == sdReady_ &&
                      bleReady == bleReady_ && phoneConnected == phoneConnected_))
      return;
    sdReady_ = sdReady;
    bleReady_ = bleReady;
    phoneConnected_ = phoneConnected;
    drawn_ = true;

    panel_.fillRect(65, 0, 95, 80, ST7735_BLACK);
    panel_.setTextColor(ST7735_CYAN);
    panel_.setTextSize(2);
    panel_.setCursor(68, 3);
    panel_.print("AxD");
    panel_.setTextSize(1);
    panel_.setTextColor(ST7735_WHITE);
    panel_.setCursor(68, 24);
    panel_.print("BRIDGE");
    panel_.drawFastHLine(68, 36, 88, ST7735_BLUE);
    panel_.setTextColor(phoneConnected ? ST7735_GREEN : ST7735_CYAN);
    panel_.setCursor(68, 41);
    panel_.print(phoneConnected ? "BLE PHONE" : bleReady ? "BLE READY" : "BLE START");
    panel_.setTextColor(sdReady ? ST7735_GREEN : ST7735_YELLOW);
    panel_.setCursor(68, 53);
    panel_.print(sdReady ? "SD  READY" : "SD  MISSING");
    panel_.setTextColor(ST7735_WHITE);
    panel_.setCursor(68, 67);
    panel_.print("C5 2.4/5G");
  }

 private:
  Adafruit_ST7735 panel_;
  bool started_ = false;
  bool drawn_ = false;
  bool sdReady_ = false;
  bool bleReady_ = false;
  bool phoneConnected_ = false;
};
