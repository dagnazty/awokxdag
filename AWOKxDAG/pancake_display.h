#pragma once
// Pancake C5 display: a 3.5" ST7796 320x480 panel, driven at native resolution.
//
// This is a plain 320x480 Adafruit_GFX back buffer -- the same buffered pattern
// as touch_display.h, just the full panel size. There is NO image scaling: the
// UI is laid out natively at 320x480 (every layout coordinate is scaled from the
// 240x320 design grid at the *source* level via scaleX()/scaleY() in
// awok_common.h), so text and geometry are drawn by Adafruit_GFX directly and
// stay crisp. present() blits the buffer to the panel 1:1.
//
// There is no Adafruit ST7796 library on the Arduino registry, so the panel
// driver is a small Adafruit_SPITFT subclass here -- the same base class
// Adafruit_ILI9341 is built on. Only an ST7796 init sequence and setAddrWindow
// differ; the SPI plumbing, writePixels and colour handling are inherited.
#include <Adafruit_GFX.h>
#include <Adafruit_SPITFT.h>
#include <SPI.h>
#include <esp_heap_caps.h>

// ---- Minimal ST7796 320x480 panel driver (Adafruit_SPITFT subclass) --------
class AwokST7796 : public Adafruit_SPITFT {
 public:
  AwokST7796(SPIClass* spi, int8_t dc, int8_t cs, int8_t rst)
      : Adafruit_SPITFT(kPanelW, kPanelH, spi, cs, dc, rst) {}

  void begin(uint32_t freq) {
    if (freq == 0) freq = 27000000;  // matches the ILI9341 Touch build
    initSPI(freq, SPI_MODE0);
    // ST7796S bring-up (extension command set, gamma, power); values are the
    // widely-used ST7796 init shared by TFT_eSPI/LovyanGFX for this panel.
    static const uint8_t kGammaPlus[] = {0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15,
                                         0x2F, 0x54, 0x42, 0x3C, 0x17, 0x14,
                                         0x18, 0x1B};
    static const uint8_t kGammaMinus[] = {0xE0, 0x09, 0x0B, 0x06, 0x04, 0x03,
                                          0x2B, 0x43, 0x42, 0x3B, 0x16, 0x14,
                                          0x17, 0x1B};
    static const uint8_t kOutputAdjust[] = {0x40, 0x8A, 0x00, 0x00, 0x29,
                                            0x19, 0xA5, 0x33};
    static const uint8_t kDispFnCtrl[] = {0x80, 0x02, 0x3B};
    // Each sendCommand() opens and closes its own SPI transaction, so the init
    // sequence is not wrapped in an outer startWrite()/endWrite() (that would
    // nest transactions). present() wraps its raw writeCommand/writePixels.
    sendCommand(0x01);  // Software reset
    delay(120);
    sendCommand(0x11);  // Sleep out
    delay(120);
    cmd(0xF0, 0xC3);            // Enable extension command 2, part I
    cmd(0xF0, 0x96);            // Enable extension command 2, part II
    cmd(0x36, kMadctlPortrait); // MADCTL
    cmd(0x3A, 0x55);            // COLMOD: 16-bit/pixel
    cmd(0xB4, 0x01);            // Column inversion
    sendCommand(0xB6, kDispFnCtrl, sizeof(kDispFnCtrl));  // Display fn control
    sendCommand(0xE8, kOutputAdjust, sizeof(kOutputAdjust));
    cmd(0xC1, 0x06);            // Power control 2
    cmd(0xC2, 0xA7);            // Power control 3
    cmd(0xC5, 0x18);            // VCOM control
    delay(120);
    sendCommand(0xE0, kGammaPlus, sizeof(kGammaPlus));
    sendCommand(0xE1, kGammaMinus, sizeof(kGammaMinus));
    delay(120);
    cmd(0xF0, 0x3C);  // Disable extension command 2, part I
    cmd(0xF0, 0x69);  // Disable extension command 2, part II
    delay(120);
    sendCommand(0x21);  // Inversion on (Pancake panel: TFT_INVERSION_ON)
    sendCommand(0x29);  // Display on
    _width = kPanelW;
    _height = kPanelH;
  }

  // Standard MIPI DCS window commands (identical bytes to ILI9341/ST7796).
  void setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) override {
    const uint16_t x2 = x + w - 1, y2 = y + h - 1;
    writeCommand(0x2A);  // Column address set
    SPI_WRITE16(x);
    SPI_WRITE16(x2);
    writeCommand(0x2B);  // Page address set
    SPI_WRITE16(y);
    SPI_WRITE16(y2);
    writeCommand(0x2C);  // Memory write
  }

  static constexpr int16_t kPanelW = 320;
  static constexpr int16_t kPanelH = 480;
  // MADCTL for the native portrait orientation: MX (0x40) | BGR (0x08).
  // If the image is mirrored/rotated or colours are swapped on real hardware,
  // this is the byte to adjust.
  static constexpr uint8_t kMadctlPortrait = 0x48;

 private:
  // Command followed by a single data byte (the common case in the init table).
  void cmd(uint8_t command, uint8_t data) { sendCommand(command, &data, 1); }
};

// ---- Native 320x480 back buffer (drop-in for AwokTouchDisplay) -------------
class AwokPancakeDisplay : public Adafruit_GFX {
 public:
  AwokPancakeDisplay(int8_t dc, int8_t cs, int8_t rst)
      : Adafruit_GFX(kW, kH), panel_(&SPI, dc, cs, rst) {}

  bool begin(uint32_t freq) {
    panel_.begin(freq);
    if (!buffer_) {
      buffer_ = static_cast<uint16_t*>(heap_caps_malloc(
          size_t(kW) * kH * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    buffered_ = buffer_ != nullptr;
    if (buffered_) {
      fillScreen(0);
    } else {
      Serial.println("[display] no PSRAM back buffer; drawing direct-to-panel");
      panel_.fillScreen(0);
    }
    return true;
  }

  // Blit the back buffer to the panel; no-op unless something was drawn.
  void present(bool = true) {
    if (!buffered_ || !dirty_) return;
    panel_.startWrite();
    panel_.setAddrWindow(0, 0, kW, kH);
    for (int y = 0; y < kH; ++y) {
      panel_.writePixels(buffer_ + size_t(y) * kW, kW, true, false);
    }
    panel_.endWrite();
    dirty_ = false;
  }

  void setRotation(uint8_t r) {
    Adafruit_GFX::setRotation(r);  // buffer is the canvas; UI uses rotation 0
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (!buffered_) {
      panel_.drawPixel(x, y, color);
      return;
    }
    if (x < 0 || y < 0 || x >= _width || y >= _height) return;
    int16_t t;
    switch (rotation) {
      case 1: t = x; x = kW - 1 - y; y = t; break;
      case 2: x = kW - 1 - x; y = kH - 1 - y; break;
      case 3: t = x; x = y; y = kH - 1 - t; break;
    }
    buffer_[int32_t(y) * kW + x] = color;
    dirty_ = true;
  }

  void fillScreen(uint16_t color) override {
    if (!buffered_) {
      panel_.fillScreen(color);
      return;
    }
    const uint32_t n = uint32_t(kW) * kH;
    for (uint32_t i = 0; i < n; ++i) buffer_[i] = color;
    dirty_ = true;
  }

  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    if (!buffered_) {
      panel_.drawFastHLine(x, y, w, color);
      return;
    }
    for (int16_t i = 0; i < w; ++i) drawPixel(x + i, y, color);
  }

  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
    if (!buffered_) {
      panel_.drawFastVLine(x, y, h, color);
      return;
    }
    for (int16_t i = 0; i < h; ++i) drawPixel(x, y + i, color);
  }

  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                uint16_t color) override {
    if (!buffered_) {
      panel_.fillRect(x, y, w, h, color);
      return;
    }
    for (int16_t j = 0; j < h; ++j) drawFastHLine(x, y + j, w, color);
  }

 private:
  static constexpr int16_t kW = AwokST7796::kPanelW;
  static constexpr int16_t kH = AwokST7796::kPanelH;
  AwokST7796 panel_;
  uint16_t* buffer_ = nullptr;
  bool buffered_ = false;
  bool dirty_ = true;
};
