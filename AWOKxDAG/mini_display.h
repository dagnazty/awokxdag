#pragma once
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#ifdef AWOK_CARDPUTER_V11
#include <M5Cardputer.h>
#endif
#include <new>
#include "board_pins.h"
#include "mini_layout.h"
#include "mini_pixels.h"
#include "keyboard_layout.h"

class MiniCanvas : public Adafruit_GFX {
 public:
  MiniCanvas() : Adafruit_GFX(MiniPixels::width, MiniPixels::height) {}
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    pixels.setIndex(x, y, MiniPixels::colorIndex(color));
  }
  void fillScreen(uint16_t color) override { pixels.fill(color); }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    const int right = std::min(MiniPixels::width, int(x) + w);
    const int bottom = std::min(MiniPixels::height, int(y) + h);
    const uint8_t index = MiniPixels::colorIndex(color);
    for (int row = std::max(0, int(y)); row < bottom; ++row)
      for (int col = std::max(0, int(x)); col < right; ++col)
        pixels.setIndex(col, row, index);
  }
  MiniPixels pixels;
};

class AwokMiniDisplay : public Adafruit_GFX {
 public:
  AwokMiniDisplay() : Adafruit_GFX(MiniPixels::width, MiniPixels::height)
#ifndef AWOK_CARDPUTER_V11
      ,
      panel_(&SPI, AwokPins::kDisplayCs, AwokPins::kDisplayDc,
             AwokPins::kDisplayReset) {}
#else
      {}
#endif
  bool begin() {
#ifdef AWOK_CARDPUTER_V11
    M5Cardputer.begin();
    panel_.setRotation(1);
    panel_.setBrightness(255);
#else
    panel_.initR(INITR_144GREENTAB);
    panel_.setSPISpeed(20000000);
    panel_.setRotation(0);
    digitalWrite(AwokPins::kBacklight, AwokPins::kBacklightOn ? HIGH : LOW);
#endif
    canvas_ = new (std::nothrow) MiniCanvas();
    if (!canvas_) {
      panel_.fillScreen(ST7735_BLACK);
      panel_.setCursor(2, 20); panel_.setTextColor(ST7735_WHITE);
      panel_.println("Mini: low memory");
      Serial.println("[mini] native canvas allocation failed");
      return false;
    }
    canvas_->setTextWrap(false);
    return true;
  }
  // Existing screen code supplies text and controls to the native layout.
  // Decorative portrait primitives are replaced by the compact renderer.
  void drawPixel(int16_t, int16_t, uint16_t) override {}
  void fillRect(int16_t, int16_t, int16_t, int16_t, uint16_t) override {}
  void drawFastHLine(int16_t, int16_t, int16_t, uint16_t) override {}
  void drawFastVLine(int16_t, int16_t, int16_t, uint16_t) override {}
  void fillScreen(uint16_t) override {
    keyboardActive_ = false;
    dashboardActive_ = false;
    directActive_ = false;
    memset(dashboardLines_, 0, sizeof(dashboardLines_));
    memset(keyboardPreview_, 0, sizeof(keyboardPreview_));
    layout.clear(); dirty_ = true;
  }
  using Print::write;
  size_t write(uint8_t c) override {
    if (c == '\n') { cursor_x = 0; cursor_y += textsize_y * 8; }
    else if (c != '\r') {
      layout.character(cursor_x, cursor_y, textsize_x * 6, char(c), textcolor);
      cursor_x += textsize_x * 6;
    }
    dirty_ = true;
    return 1;
  }
  void header(const char* title, const char* detail) {
    layout.header(title);
#ifdef AWOK_CARDPUTER_V11
    if (*detail) {
      auto* item = layout.add(0, -1);
      if (item) strncpy(item->label, detail, MiniLayout::textBytes - 1);
    }
#else
    strncpy(detail_, detail, sizeof(detail_) - 1);
    detail_[sizeof(detail_) - 1] = 0;
#endif
    dirty_ = true;
  }
  void button(int x, int y, int w, int h, const char* label, uint16_t color) {
    layout.button(x, y, w, h, label, color); dirty_ = true;
  }
  void selectableRows(int count) { layout.rowCount = count; }
  void gps(int state) { gpsState_ = state; dirty_ = true; }
  void version(const char* text) { version_ = text; }
  Adafruit_GFX* directCanvas() { return canvas_; }
  void directBegin() {
    directActive_ = true;
    keyboardActive_ = dashboardActive_ = false;
    if (canvas_) canvas_->fillScreen(ST7735_BLACK);
    dirty_ = true;
  }
  void bar(int y, const char* label, int value, int maximum) {
    auto* item = layout.add(0, y);
    if (item) {
      strncpy(item->label, label, MiniLayout::textBytes - 1);
      item->kind = MiniLayout::bar; item->value = value;
      item->maximum = std::max(1, maximum);
    }
    dirty_ = true;
  }
  void graph(int y, const int16_t* samples, int count) {
    auto* item = layout.add(0, y);
    if (item) {
      item->kind = MiniLayout::graph;
      layout.sampleCount = std::min(30, count);
      std::copy_n(samples, layout.sampleCount, layout.samples);
    }
    dirty_ = true;
  }
  // Boot splash: draw a monochrome XBM (set bit = lit) straight into the canvas,
  // centered, and push it to the panel now. Bypasses the layout document, which
  // only models header/rows/footer; the next present() rebuilds normally.
  void splash(const uint8_t* bitmap, int w, int h) {
    if (!canvas_) return;
    canvas_->fillScreen(ST7735_BLACK);
    canvas_->drawXBitmap((MiniPixels::width - w) / 2,
                         std::max(0, (MiniPixels::height - h) / 2), bitmap, w, h,
                         ST7735_WHITE);
    blitCanvas();
    dirty_ = redraw_ = true;  // force a full rebuild on the next present()
  }
  void keyboard(const char* title, const char* preview, int length, int limit,
                AwokKeyboard::Mode mode, bool pending) {
    if (!keyboardActive_) {
      keyboardFocus_ = 1;  // 2 / abc
#ifdef AWOK_CARDPUTER_V11
      editorAction_ = 2;
#endif
    }
    keyboardActive_ = true;
    keyboardMode_ = mode;
    keyboardPending_ = pending;
    keyboardLength_ = length;
    keyboardLimit_ = limit;
    strncpy(keyboardTitle_, title, sizeof(keyboardTitle_) - 1);
    const size_t count = strlen(preview);
    // Always keep the insertion point visible. Passwords arrive masked.
    const size_t visible = sizeof(keyboardPreview_) - 2;
    strncpy(keyboardPreview_, preview + (count > visible ? count - visible : 0),
            sizeof(keyboardPreview_) - 1);
    if (!AwokKeyboard::key(keyboardFocus_, mode).valid()) keyboardFocus_ = 0;
    dirty_ = true;
  }
  void dashboardLine(int row, const char* text, uint16_t color) {
    if (row < 0 || row >= 9) return;
    dashboardActive_ = true;
    strncpy(dashboardLines_[row], text, sizeof(dashboardLines_[row]) - 1);
    dashboardLines_[row][sizeof(dashboardLines_[row]) - 1] = 0;
    dashboardColors_[row] = color;
    dirty_ = true;
  }
  int dashboardButton(int focus) const {
    int count = 0;
    for (int i = 0; i < layout.count; ++i)
      if (layout.items[i].action() && layout.items[i].targetY >= 278 && count++ == focus) return i;
    return -1;
  }
  bool selection(int& x, int& y) const {
    if (directActive_) return false;
    if (dashboardActive_) {
      const int i = dashboardButton(dashboardFocus_);
      if (i < 0) return false;
      x = layout.items[i].targetX; y = layout.items[i].targetY; return true;
    }
    if (!keyboardActive_) return layout.selection(x, y);
#ifdef AWOK_CARDPUTER_V11
    return false;  // Cardputer editor actions are handled by physical keys.
#else
    const auto key = AwokKeyboard::key(keyboardFocus_, keyboardMode_);
    x = key.x + key.w / 2;
    y = key.y + key.h / 2;
    return key.valid();
#endif
  }
  void navigate(int direction, bool jump = false) {
    if (directActive_) return;
    if (dashboardActive_) {
      if (direction < 0 && dashboardFocus_ > 0) --dashboardFocus_;
      else if (direction > 0 && dashboardButton(dashboardFocus_ + 1) >= 0) ++dashboardFocus_;
      redraw_ = true; return;
    }
    if (keyboardActive_) {
#ifdef AWOK_CARDPUTER_V11
      if (direction < 0) editorAction_ = std::max(0, editorAction_ - 1);
      else if (direction > 0) editorAction_ = std::min(2, editorAction_ + 1);
#else
      keyboardFocus_ = AwokKeyboard::move(keyboardFocus_, direction, jump,
                                          keyboardMode_);
#endif
      redraw_ = true;
      return;
    }
    if (jump) layout.jump(direction > 0); else layout.move(direction);
    redraw_ = true;
  }
  // Direct panel drawing for the screen test. Bypasses MiniLayout so a bad UI
  // path cannot be mistaken for a dead ST7735. present() is a no-op until
  // diagnosticEnd().
  bool inDiagnostic() const { return diagnostic_; }
  void diagnosticEnd() {
    diagnostic_ = false;
    dirty_ = redraw_ = true;
  }
  void diagnosticFill(uint16_t color) {
    diagnostic_ = true;
    panel_.fillScreen(color);
  }
  void diagnosticRect(int x, int y, int w, int h, uint16_t color, bool fill) {
    diagnostic_ = true;
    if (fill) panel_.fillRect(x, y, w, h, color);
    else panel_.drawRect(x, y, w, h, color);
  }
  void diagnosticHLine(int x, int y, int w, uint16_t color) {
    diagnostic_ = true;
    panel_.drawFastHLine(x, y, w, color);
  }
  void diagnosticVLine(int x, int y, int h, uint16_t color) {
    diagnostic_ = true;
    panel_.drawFastVLine(x, y, h, color);
  }
  void diagnosticText(int x, int y, uint16_t color, const char* text,
                      uint16_t bg = 0x0000) {
    diagnostic_ = true;
    panel_.setTextWrap(false);
    panel_.setTextSize(1);
    panel_.setTextColor(color, bg);
    panel_.setCursor(x, y);
    panel_.print(text);
  }
  void diagnosticChecker(uint16_t a, uint16_t b, int cell) {
    diagnostic_ = true;
    for (int y = 0; y < MiniPixels::height; y += cell)
      for (int x = 0; x < MiniPixels::width; x += cell)
        panel_.fillRect(x, y, cell, cell,
                        ((x / cell) + (y / cell)) & 1 ? a : b);
  }
  void diagnosticFirmwareChecker(uint16_t a, uint16_t b, int cell) {
    diagnostic_ = true;
    if (!canvas_) return;
    for (int y = 0; y < MiniPixels::height; ++y)
      for (int x = 0; x < MiniPixels::width; ++x)
        canvas_->drawPixel(x, y, ((x / cell) + (y / cell)) & 1 ? a : b);
    blitCanvas();
  }
  void present(bool = true) {
    if (diagnostic_ || !canvas_ || (!dirty_ && !redraw_)) return;
    if (directActive_) { blitCanvas(); dirty_ = redraw_ = false; return; }
    if (dashboardActive_) {
      if (dashboardButton(dashboardFocus_) < 0) dashboardFocus_ = 0;
      canvas_->fillScreen(ST7735_BLACK); canvas_->setTextSize(1);
      canvas_->setTextColor(ST7735_CYAN); canvas_->setCursor(1, 2);
      canvas_->print(layout.title);
      for (int row = 0; row < 9; ++row) {
        canvas_->setCursor(1, 15 + row * 10); canvas_->setTextColor(dashboardColors_[row]);
        canvas_->print(dashboardLines_[row]);
      }
      int count = 0;
      while (dashboardButton(count) >= 0) ++count;
      for (int i = 0; i < count; ++i) {
        const int x = i * MiniPixels::width / count;
        const int w = MiniPixels::width / count;
        const bool focused = i == dashboardFocus_;
        canvas_->fillRect(x, MiniPixels::height - 18, w - 1, 17,
                          focused ? ST7735_CYAN : ST7735_BLUE);
        canvas_->setTextColor(focused ? ST7735_BLACK : ST7735_WHITE);
        const char* label = layout.items[dashboardButton(i)].label;
        canvas_->setCursor(x + (w - int(strlen(label)) * 6) / 2,
                           MiniPixels::height - 13);
        canvas_->print(label);
      }
      blitCanvas(); dirty_ = redraw_ = false; return;
    }
    if (keyboardActive_) {
      drawKeyboard();
      blitCanvas();
      dirty_ = redraw_ = false;
      return;
    }
    if (dirty_) layout.build();
    canvas_->fillScreen(ST7735_BLACK);
    canvas_->setTextSize(1);
    canvas_->setTextColor(ST7735_CYAN);
    canvas_->setCursor(1, 2);
    char heading[MiniLayout::columns + 1] = {};
    strncpy(heading, layout.title, MiniLayout::columns); canvas_->print(heading);
    {
      const uint16_t gpsColor = gpsState_ >= 2 ? 0x07e0
                                : gpsState_ == 1 ? 0xffe0 : 0x7bef;
      canvas_->setTextColor(gpsColor);
      canvas_->setCursor(MiniPixels::width - 27, 2); canvas_->print("GPS");
      canvas_->fillCircle(MiniPixels::width - 4, 5, 2, gpsColor);
    }
#ifndef AWOK_CARDPUTER_V11
    if (detail_[0]) {
      char sub[MiniLayout::columns + 2] = {};
      strncpy(sub, detail_, MiniLayout::columns + 1);
      canvas_->setTextColor(0xc618);
      canvas_->setCursor(1, 13); canvas_->print(sub);
    }
    canvas_->drawFastHLine(0, MiniLayout::bodyTop - 2, MiniPixels::width, ST7735_BLUE);
#else
    canvas_->drawFastHLine(0, 12, MiniPixels::width, ST7735_BLUE);
#endif
    for (int row = 0; row < MiniLayout::visible; ++row) {
      const int index = layout.top + row;
      if (index >= layout.lineCount) break;
      const auto& line = layout.lines[index];
      const auto& item = layout.items[line.item];
      const int y = MiniLayout::bodyTop + row * MiniLayout::pitch;
      const bool focus = index == layout.focus;
      if (focus) canvas_->fillRect(0, y - 1, MiniPixels::width - 2,
                                  MiniLayout::pitch, 0x2104);
      if (item.kind == MiniLayout::graph) {
        drawGraphSlice(y, line.offset);
      } else if (item.kind == MiniLayout::bar && line.offset == 1) {
        canvas_->drawRect(8, y, MiniPixels::width - 14, 7, ST7735_BLUE);
        const int barWidth = MiniPixels::width - 16;
        const int size = std::max(0, std::min(barWidth,
            item.value * barWidth / item.maximum));
        canvas_->fillRect(9, y + 1, size, 5, ST7735_CYAN);
      } else {
        char label[MiniLayout::columns + 1] = {};
        const int offset = item.kind == MiniLayout::text ? line.offset : 0;
        strncpy(label, item.label + offset, line.length);
        canvas_->setTextColor(item.action() ? ST7735_WHITE : item.color);
        canvas_->setCursor(8, y); canvas_->print(label);
      }
      // Persistent left marker so selectable rows are always distinct from
      // description/info text (not only when focused): ">" = selectable,
      // brighter when focused; info lines stay unmarked (":" only on focus).
      if (item.action()) {
        canvas_->setTextColor(focus ? ST7735_CYAN : ST7735_BLUE);
        canvas_->setCursor(1, y); canvas_->print(">");
      } else if (focus) {
        canvas_->setTextColor(ST7735_CYAN);
        canvas_->setCursor(1, y); canvas_->print(":");
      }
    }
    if (layout.lineCount > MiniLayout::visible) {
      const int bodyHeight = (MiniPixels::height - 13) - MiniLayout::bodyTop;
      const int h = std::max(3, bodyHeight * MiniLayout::visible / layout.lineCount);
      const int y = MiniLayout::bodyTop + (bodyHeight - h) * layout.top /
          std::max(1, layout.lineCount - MiniLayout::visible);
      canvas_->fillRect(MiniPixels::width - 1, y, 1, h, ST7735_CYAN);
    }
    canvas_->drawFastHLine(0, MiniPixels::height - 13,
                           MiniPixels::width, ST7735_BLUE);
    canvas_->setTextColor(layout.overflow ? ST7735_RED : ST7735_CYAN);
    canvas_->setCursor(1, MiniPixels::height - 10);
#ifdef AWOK_CARDPUTER_V11
    canvas_->print(layout.overflow ? "Content limit reached"
                                   : ";up ,left .down /right Enter ~:back");
#else
    canvas_->print(layout.overflow ? "Content limit reached" : version_);
#endif
    blitCanvas();
    dirty_ = redraw_ = false;
  }
  MiniLayout layout;
  int gpsState_ = 0;
  const char* version_ = "";
  char detail_[48] = {};
  bool directActive_ = false;
 private:
  void drawKeyboard() {
    canvas_->fillScreen(ST7735_BLACK);
    canvas_->setTextSize(1);
    canvas_->setTextColor(ST7735_CYAN);
    canvas_->setCursor(2, 2);
    canvas_->print(keyboardTitle_);
    canvas_->drawRoundRect(2, 12, MiniPixels::width - 4, 13, 2, ST7735_BLUE);
    canvas_->setCursor(5, 15);
    canvas_->setTextColor(ST7735_WHITE);
    canvas_->print(keyboardPreview_);
    canvas_->print(keyboardPending_ ? '^' : '_');
    canvas_->setTextColor(keyboardLength_ == keyboardLimit_ ? ST7735_YELLOW : ST7735_CYAN);
    canvas_->setCursor(2, 27);
    canvas_->printf("%d/%d", keyboardLength_, keyboardLimit_);
#ifdef AWOK_CARDPUTER_V11
    canvas_->setTextColor(ST7735_WHITE);
    canvas_->setCursor(2, 45); canvas_->print("Type normally; punctuation works.");
    canvas_->setCursor(2, 59); canvas_->print("Fn + ; , . /: move action");
    canvas_->setCursor(2, 73); canvas_->print("Enter: save   Fn+~/Tab: cancel");
    canvas_->setCursor(2, 87); canvas_->print("Backspace: erase");
    const char* actions[] = {"Cancel", "Delete", "Save"};
    for (int i = 0; i < 3; ++i) {
      const int x = i * MiniPixels::width / 3;
      canvas_->fillRect(x + 1, 106, MiniPixels::width / 3 - 2, 18,
                        i == editorAction_ ? ST7735_CYAN : ST7735_BLUE);
      canvas_->setTextColor(i == editorAction_ ? ST7735_BLACK : ST7735_WHITE);
      canvas_->setCursor(x + 12, 111);
      canvas_->print(actions[i]);
    }
    canvas_->setTextColor(ST7735_CYAN);
    canvas_->setCursor(2, 126);
    canvas_->print("Fn+Enter: selected action");
    return;
#else
    canvas_->setCursor(64, 27);
    canvas_->printf("%s %s", AwokKeyboard::modeName(keyboardMode_), keyboardPending_ ? "tap" : "");
    for (int i = 0; i < AwokKeyboard::kSlots; ++i) {
      const auto key = AwokKeyboard::key(i, keyboardMode_, true);
      if (!key.valid()) continue;
      const bool focused = i == keyboardFocus_;
      const uint16_t border = key.action == AwokKeyboard::Done ? ST7735_GREEN :
          key.action == AwokKeyboard::Cancel || key.action == AwokKeyboard::Delete ? ST7735_RED : ST7735_BLUE;
      if (focused) canvas_->fillRect(key.x, key.y, key.w, key.h, ST7735_CYAN);
      else canvas_->drawRect(key.x, key.y, key.w, key.h, border);
      canvas_->setTextColor(focused ? ST7735_BLACK : ST7735_WHITE);
      const bool group = key.sublabel[0];
      canvas_->setCursor(key.x + (key.w - int(strlen(key.label)) * 6) / 2,
                         key.y + (group ? 1 : (key.h - 8) / 2));
      canvas_->print(key.label);
      if (group) {
        canvas_->setCursor(key.x + (key.w - int(strlen(key.sublabel)) * 6) / 2, key.y + 9);
        canvas_->print(key.sublabel);
      }
    }
#endif
  }
  bool dashboardActive_ = false;
  int dashboardFocus_ = 0;
  char dashboardLines_[9][MiniLayout::columns + 1] = {};
  uint16_t dashboardColors_[9] = {};
  bool keyboardActive_ = false, keyboardPending_ = false;
#ifdef AWOK_CARDPUTER_V11
 public:
  int editorAction() const { return editorAction_; }
 private:
  int editorAction_ = 2;
#endif
  AwokKeyboard::Mode keyboardMode_ = AwokKeyboard::Lower;
  int keyboardFocus_ = 1, keyboardLength_ = 0, keyboardLimit_ = 32;
  char keyboardTitle_[22] = {}, keyboardPreview_[MiniLayout::columns + 1] = {};
  void blitCanvas() {
    uint16_t pixels[MiniPixels::width];
    panel_.startWrite();
    panel_.setAddrWindow(0, 0, MiniPixels::width, MiniPixels::height);
    for (int y = 0; y < MiniPixels::height; ++y) {
      for (int x = 0; x < MiniPixels::width; ++x)
        pixels[x] = canvas_->pixels.get(x, y);
      panel_.writePixels(pixels, MiniPixels::width, true);
    }
    panel_.endWrite();
  }
  void drawGraphSlice(int y, int part) {
    // Draw only this row slice so scrolling cannot overwrite the header.
    int previousX = -1, previousY = -1;
    const int origin = y - part * MiniLayout::pitch;
    for (int i = 0; i < layout.sampleCount; ++i) {
      const int value = layout.samples[i];
      if (value <= -127) { previousX = -1; continue; }
      const int px = 9 + i * (MiniPixels::width - 16) / 29;
      const int py = origin + 40 - (std::max(-100, std::min(-30, value)) + 100) * 38 / 70;
      if (previousX >= 0) {
        for (int x = previousX; x <= px; ++x) {
          const int yy = previousY + (py - previousY) * (x - previousX) /
              std::max(1, px - previousX);
          if (yy >= y && yy < y + MiniLayout::pitch)
            canvas_->drawPixel(x, yy, ST7735_GREEN);
        }
      }
      if (py >= y && py < y + MiniLayout::pitch)
        canvas_->drawPixel(px, py, ST7735_GREEN);
      previousX = px; previousY = py;
    }
    canvas_->drawFastVLine(7, y, MiniLayout::pitch, ST7735_BLUE);
  }
#ifdef AWOK_CARDPUTER_V11
  decltype(M5Cardputer.Display)& panel_ = M5Cardputer.Display;
#else
  Adafruit_ST7735 panel_;
#endif
  MiniCanvas* canvas_ = nullptr;
  bool dirty_ = true, redraw_ = false;
  bool diagnostic_ = false;
};
