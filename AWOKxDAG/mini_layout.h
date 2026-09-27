#pragma once
#include <algorithm>
#include <stdint.h>
#include <string.h>

// Native 128px layout. Legacy coordinates identify existing actions only;
// no portrait framebuffer or pixel scaling is used.
struct MiniLayout {
#ifdef AWOK_CARDPUTER_V11
  static constexpr int width = 240, height = 135;
  static constexpr int columns = 37, visible = 10, pitch = 10, bodyTop = 15;
#else
  static constexpr int width = 128, height = 128;
  static constexpr int columns = 19, visible = 9, pitch = 11, bodyTop = 15;
#endif
  static constexpr int maxItems = 96, maxLines = 512, textBytes = 96;
  enum Kind : uint8_t { text, bar, graph };
  struct Item {
    char label[textBytes] = {};
    int16_t y = 0, x = 0, endX = 0, targetX = -1, targetY = -1;
    int value = 0, maximum = 1;
    uint16_t color = 0xffff;
    Kind kind = text;
    bool action() const { return targetX >= 0; }
  };
  struct Line { uint8_t item = 0, offset = 0, length = 0; };
  static_assert(maxItems <= 256 && textBytes <= 256, "Layout indices must fit");
  Item items[maxItems];
  Line lines[maxLines];
  int count = 0, lineCount = 0, focus = 0, top = 0;
  int restoreX = -1, restoreY = -1;
  bool newScreen = true, overflow = false;
  char title[48] = {};
  int32_t samples[30] = {};
  int sampleCount = 0;
  int rowCount = 0;

  void clear() {
    restoreX = restoreY = -1;
    if (lineCount && focus < lineCount) {
      const Item& item = items[lines[focus].item];
      restoreX = item.targetX; restoreY = item.targetY;
    }
    count = lineCount = rowCount = sampleCount = 0;
    overflow = false;
  }
  void header(const char* label) {
    newScreen = strcmp(title, label) != 0;
    strncpy(title, label, sizeof(title) - 1);
    title[sizeof(title) - 1] = 0;
    if (newScreen) { focus = top = 0; restoreX = restoreY = -1; }
  }
  Item* add(int x, int y) {
    if (count == maxItems) { overflow = true; return nullptr; }
    Item& item = items[count++];
    item = Item{}; item.x = item.endX = x; item.y = y;
    return &item;
  }
  void character(int x, int y, int advance, char c, uint16_t color) {
    Item* item = nullptr;
    for (int i = 0; i < count; ++i)
      if (items[i].y == y && !items[i].action() && items[i].kind == text) {
        item = &items[i]; break;
      }
    if (!item) item = add(x, y);
    if (!item) return;
    int length = strlen(item->label);
    if (length && x > item->endX + 2 && length < textBytes - 1)
      item->label[length++] = ' ';
    if (length < textBytes - 1) {
      item->label[length++] = c; item->label[length] = 0;
    } else overflow = true;
    item->endX = x + advance;
    item->color = color;
  }
  void button(int x, int y, int w, int h, const char* label, uint16_t color) {
    Item* item = add(x, y);
    if (!item) return;
    strncpy(item->label, label, textBytes - 1);
    item->targetX = x + w / 2; item->targetY = y + h / 2;
    item->color = color;
  }
  void build() {
    std::sort(items, items + count, [](const Item& a, const Item& b) {
      return a.y == b.y ? a.x < b.x : a.y < b.y;
    });
    lineCount = 0;
    for (int i = 0; i < count; ++i) {
      Item& item = items[i];
      if (!item.action() && item.y >= 48 && item.y < 48 + rowCount * 22) {
        item.targetX = 120; item.targetY = 55 + ((item.y - 48) / 22) * 22;
      }
      if (item.kind != text) {
        const int n = item.kind == graph ? 4 : 2;
        for (int part = 0; part < n; ++part) {
          if (lineCount == maxLines) { overflow = true; break; }
          lines[lineCount++] = {uint8_t(i), uint8_t(part), uint8_t(columns)};
        }
      } else {
        const int length = strlen(item.label);
        int offset = 0;
        do {
          if (lineCount == maxLines) { overflow = true; break; }
          int take = std::min(columns, length - offset);
          if (offset + take < length && item.label[offset + take] != ' ') {
            int space = take;
            while (space > 0 && item.label[offset + space] != ' ') --space;
            if (space > 0) take = space;
          }
          lines[lineCount++] = {uint8_t(i), uint8_t(offset), uint8_t(take)};
          offset += take;
          while (offset < length && item.label[offset] == ' ') ++offset;
        } while (offset < length);
      }
    }
    if (newScreen) {
      for (int i = 0; i < lineCount; ++i) {
        const Item& item = items[lines[i].item];
        if (item.action() && item.targetY < 278) { focus = i; break; }
      }
      newScreen = false;
    } else if (restoreX >= 0) {
      for (int i = 0; i < lineCount; ++i) {
        const Item& item = items[lines[i].item];
        if (item.targetX == restoreX && item.targetY == restoreY) {
          focus = i; break;
        }
      }
    }
    restoreX = restoreY = -1;
    constrainFocus();
  }
  void constrainFocus() {
    focus = std::max(0, std::min(focus, lineCount - 1));
    if (focus < top) top = focus;
    if (focus >= top + visible) top = focus - visible + 1;
    top = std::max(0, std::min(top, std::max(0, lineCount - visible)));
  }
  void move(int amount) { focus += amount; constrainFocus(); }
  // Right selects footer actions quickly; left returns to the first body row.
  void jump(bool footer) {
    focus = 0;
    if (footer) {
      for (int i = 0; i < lineCount; ++i) {
        const Item& item = items[lines[i].item];
        if (item.action() && item.targetY >= 278) { focus = i; break; }
      }
    }
    top = focus;
    constrainFocus();
  }
  bool selection(int& x, int& y) const {
    if (!lineCount) return false;
    const Item& item = items[lines[focus].item];
    if (!item.action()) return false;
    x = item.targetX; y = item.targetY; return true;
  }
};
