#pragma once
#include <stdint.h>
#include <string.h>

// One geometry for drawing, touch hit testing and Mini joystick selection.
namespace AwokKeyboard {
enum Action : uint8_t { Character, ChangeMode, Next, Delete, Cancel, Done };
enum Mode : uint8_t { Lower, Upper, Digits, Symbols };
constexpr int kSlots = 15;
constexpr uint32_t kTapTimeoutMs = 1000;
// A number follows its letters, just like a traditional phone keypad.
constexpr const char* kGroups[] = {".,?!1", "abc2", "def3", "ghi4", "jkl5", "mno6",
                                  "pqrs7", "tuv8", "wxyz9", "", " 0", ""};
constexpr const char* kSymbols[] = {".,?!", "@#$", "%&*^", "-_=", "+/\\", "()[]",
                                   "{}<>", ":;\"'", "`~|", "", " !", ""};
inline const char* modeName(Mode mode) {
  return mode == Lower ? "abc" : mode == Upper ? "ABC" : mode == Digits ? "123" : "SYM";
}
struct Key {
  int x = 0, y = 0, w = 0, h = 0;
  Action action = Character;
  char label[8] = {}, sublabel[8] = {}, choices[8] = {};
  bool valid() const { return w > 0; }
  bool contains(int px, int py) const {
    return valid() && px >= x && px < x + w && py >= y && py < y + h;
  }
};
inline Key key(int index, Mode mode, bool mini = false) {
  Key k;
  if (index < 0 || index >= kSlots) return k;
  k.x = (mini ? 2 : 6) + (index % 3) * (mini ? 42 : 78);
  k.y = (mini ? 37 : 92) + (index / 3) * (mini ? 18 : 45);
  k.w = mini ? 40 : 72;
  k.h = mini ? 17 : 41;
  if (index >= 12) {
    k.action = index == 12 ? Cancel : index == 13 ? Delete : Done;
    strcpy(k.label, index == 12 ? "Cancel" : index == 13 ? "Del" : "Done");
  } else if (index == 9 || index == 11) {
    k.action = index == 9 ? ChangeMode : Next;
    strcpy(k.label, index == 9 ? "*" : "#");
    strcpy(k.sublabel, index == 9 ? "Mode" : "Next");
  } else {
    k.label[0] = index == 10 ? '0' : '1' + index;
    if (mode == Digits) strcpy(k.choices, k.label);
    else {
      strcpy(k.choices, mode == Symbols ? kSymbols[index] : kGroups[index]);
      if (mode == Upper)
        for (char* c = k.choices; *c; ++c) if (*c >= 'a' && *c <= 'z') *c -= 'a' - 'A';
      strcpy(k.sublabel, k.choices);
      if (mode != Symbols) k.sublabel[strlen(k.sublabel) - 1] = 0;
      if (index == 10) strcpy(k.sublabel, mode == Symbols ? "sp !" : "Space");
    }
  }
  return k;
}
inline int hit(int x, int y, Mode mode) {
  for (int i = 0; i < kSlots; ++i) if (key(i, mode).contains(x, y)) return i;
  return -1;
}

// Small state machine; the editor owns text and applies append/replace results.
struct Tap {
  int index = -1;
  uint8_t choice = 0;
  uint32_t lastMs = 0;
  void commit() { index = -1; choice = 0; }
  bool expire(uint32_t now) {
    if (index < 0 || uint32_t(now - lastMs) < kTapTimeoutMs) return false;
    commit();
    return true;
  }
  char press(int pressed, Mode mode, uint32_t now, bool room, bool& replace) {
    expire(now);
    const Key k = key(pressed, mode);
    replace = index == pressed && mode != Digits;
    if (!replace && !room) { commit(); return 0; }
    choice = replace ? (choice + 1) % strlen(k.choices) : 0;
    index = mode == Digits ? -1 : pressed;
    lastMs = now;
    return k.choices[choice];
  }
};

// Horizontal movement stays on the row; vertical movement picks the nearest
// key in the next row. No wrapping into Cancel/Done from an outer edge.
inline int move(int focus, int direction, bool horizontal, Mode mode) {
  const Key from = key(focus, mode, true);
  if (!from.valid()) return 0;
  int best = focus, bestScore = 100000;
  for (int i = 0; i < kSlots; ++i) {
    const Key to = key(i, mode, true);
    if (!to.valid() || i == focus) continue;
    const int dx = (to.x * 2 + to.w) - (from.x * 2 + from.w);
    const int dy = to.y - from.y;
    const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (horizontal ? (dy != 0 || dx * direction <= 0) : (dy * direction <= 0)) continue;
    const int score = horizontal ? ax : ay * 256 + ax;
    if (score < bestScore) { bestScore = score; best = i; }
  }
  return best;
}
}  // namespace AwokKeyboard
