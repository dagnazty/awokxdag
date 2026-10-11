#pragma once
// Off-screen back buffer for the 240x320 Touch UI.
//
// Live views redraw on 0.5-1 s timers and each begins with a full-screen
// fillScreen(); drawing straight to the panel makes that clear-then-repaint
// visible as flicker. Render into a PSRAM back buffer instead and blit it to
// the panel once per frame (dirty-gated) so each refresh appears atomically.
//
// Every existing display.* call is standard Adafruit_GFX. The board profile
// selects ILI9341 (AWOK Dual/Classic Touch), ST7789 (NM-CYD-C5), or ST7796
// (HaleHound E32R35T), while this
// wrapper adds begin()/present(). When there is no PSRAM to hold the ~150 KB
// buffer it falls back to drawing straight to the panel.
#include <Adafruit_GFX.h>
#ifdef AWOK_ST7789_TOUCH
#include <Adafruit_ST7789.h>
#elif defined(AWOK_ST7796_TOUCH)
#include <Adafruit_ST7796S.h>
#else
#include <Adafruit_ILI9341.h>
#endif
#include <SPI.h>
#include <esp_heap_caps.h>

class AwokTouchDisplay : public Adafruit_GFX {
 public:
  AwokTouchDisplay(int8_t dc, int8_t cs, int8_t rst, SPIClass* bus = &SPI)
      : Adafruit_GFX(240, 320),
#ifdef AWOK_ST7789_TOUCH
        tft_(bus, cs, dc, rst) {}
#elif defined(AWOK_ST7796_TOUCH)
        tft_(bus, cs, dc, rst) {}
#else
        tft_(bus, dc, cs, rst) {}
#endif

  bool begin(uint32_t freq) {
#ifdef AWOK_ST7789_TOUCH
    tft_.init(240, 320, SPI_MODE0);
    tft_.setSPISpeed(freq);
    tft_.setRotation(0);
    // NM-CYD-C5 panels need inversion disabled and BGR MADCTL ordering.
    tft_.invertDisplay(false);
    const uint8_t madctl = ST77XX_MADCTL_MX | ST77XX_MADCTL_MY | 0x08;
    tft_.sendCommand(ST77XX_MADCTL, &madctl, 1);
#elif defined(AWOK_ST7796_TOUCH)
    tft_.init(320, 480);
    tft_.setSPISpeed(freq);
    tft_.setRotation(0);  // 180 degrees from the previous E32R35T image.
    tft_.invertDisplay(true);
#else
    tft_.begin(freq);
#ifdef AWOK_CYD_NATIVE_320X240
    // These 2.8-inch targets address the panel as 320x240 with MV clear. Adafruit's
    // rotation 1 supplies those bounds, then MADCTL 0x4c selects the CYD's
    // native address order (LovyanGFX rotation 6).  The 240x320 UI is turned
    // in software below; scaling it into 320x240 left 80 rows untouched.
    tft_.setRotation(1);
    const uint8_t cydMadctl = 0x4c;
    tft_.sendCommand(ILI9341_MADCTL, &cydMadctl, 1);
#else
    tft_.setRotation(0);  // buffer is raw 240x320; blit is always at rotation 0
#endif
#endif
#ifndef AWOK_CLASSIC_CYD
    if (!buffer_) {
      buffer_ = static_cast<uint16_t*>(heap_caps_malloc(
          size_t(WIDTH) * HEIGHT * sizeof(uint16_t),
          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
#endif
    buffered_ = buffer_ != nullptr;
    if (buffered_) {
      fillScreen(0);
    } else {
      Serial.println("[display] no PSRAM back buffer; drawing direct-to-panel");
      tft_.fillScreen(0);
    }
    return true;
  }

  // Push the back buffer to the panel; no-op unless something was drawn.
  void present(bool = true) {
    if (!buffered_ || !dirty_) return;
    tft_.drawRGBBitmap(0, 0, buffer_, WIDTH, HEIGHT);
    dirty_ = false;
  }

  void setRotation(uint8_t r) {
    Adafruit_GFX::setRotation(r);  // panel rotation is set in begin()
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (!buffered_) {
#ifdef AWOK_CYD_NATIVE_320X240
      if (x < 0 || y < 0 || x >= _width || y >= _height) return;
      tft_.drawPixel(HEIGHT - 1 - y, x, color);
#elif defined(AWOK_CLASSIC_CYD)
      if (x < 0 || y < 0 || x >= _width || y >= _height) return;
      const int16_t left = panelX(x);
      const int16_t top = panelY(y);
      tft_.fillRect(left, top, panelX(x + 1) - left,
                    panelY(y + 1) - top, color);
#else
      tft_.drawPixel(x, y, color);
#endif
      return;
    }
    if (x < 0 || y < 0 || x >= _width || y >= _height) return;
    int16_t t;
    switch (rotation) {
      case 1: t = x; x = WIDTH - 1 - y; y = t; break;
      case 2: x = WIDTH - 1 - x; y = HEIGHT - 1 - y; break;
      case 3: t = x; x = y; y = HEIGHT - 1 - t; break;
    }
    buffer_[int32_t(y) * WIDTH + x] = color;
    dirty_ = true;
  }

  void fillScreen(uint16_t color) override {
    if (!buffered_) {
      tft_.fillScreen(color);
      return;
    }
    const uint32_t n = uint32_t(WIDTH) * HEIGHT;
    for (uint32_t i = 0; i < n; ++i) buffer_[i] = color;
    dirty_ = true;
  }

  // Panel-native fast fills when in passthrough; buffered mode loops drawPixel
  // (RAM writes, so per-pixel is fine and keeps rotation handling in one place).
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    if (!buffered_) {
#ifdef AWOK_CYD_NATIVE_320X240
      tft_.fillRect(HEIGHT - 1 - y, x, 1, w, color);
#elif defined(AWOK_CLASSIC_CYD)
      tft_.fillRect(panelX(x), panelY(y),
                    panelX(x + w) - panelX(x),
                    panelY(y + 1) - panelY(y), color);
#else
      tft_.drawFastHLine(x, y, w, color);
#endif
      return;
    }
    for (int16_t i = 0; i < w; ++i) drawPixel(x + i, y, color);
  }

  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
    if (!buffered_) {
#ifdef AWOK_CYD_NATIVE_320X240
      tft_.fillRect(HEIGHT - y - h, x, h, 1, color);
#elif defined(AWOK_CLASSIC_CYD)
      tft_.fillRect(panelX(x), panelY(y),
                    panelX(x + 1) - panelX(x),
                    panelY(y + h) - panelY(y), color);
#else
      tft_.drawFastVLine(x, y, h, color);
#endif
      return;
    }
    for (int16_t i = 0; i < h; ++i) drawPixel(x, y + i, color);
  }

  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                uint16_t color) override {
    if (!buffered_) {
#ifdef AWOK_CYD_NATIVE_320X240
      tft_.fillRect(HEIGHT - y - h, x, h, w, color);
#elif defined(AWOK_CLASSIC_CYD)
      tft_.fillRect(panelX(x), panelY(y),
                    panelX(x + w) - panelX(x),
                    panelY(y + h) - panelY(y), color);
#else
      tft_.fillRect(x, y, w, h, color);
#endif
      return;
    }
    for (int16_t j = 0; j < h; ++j) drawFastHLine(x, y + j, w, color);
  }

 private:
#if defined(AWOK_CLASSIC_CYD) && !defined(AWOK_CYD_NATIVE_320X240)
  int16_t panelX(int16_t logicalX) const {
    return int32_t(logicalX) * tft_.width() / WIDTH;
  }
  int16_t panelY(int16_t logicalY) const {
    return int32_t(logicalY) * tft_.height() / HEIGHT;
  }
#endif
#ifdef AWOK_ST7789_TOUCH
  Adafruit_ST7789 tft_;
#elif defined(AWOK_ST7796_TOUCH)
  Adafruit_ST7796S tft_;
#else
  Adafruit_ILI9341 tft_;
#endif
  uint16_t* buffer_ = nullptr;
  bool buffered_ = false;
  bool dirty_ = true;
};
