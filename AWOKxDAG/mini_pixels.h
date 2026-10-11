#pragma once
#include <stdint.h>
#include <string.h>

// The native UI uses this fixed palette. Two pixels share one byte, saving
// 24 KB versus an RGB565 canvas while preserving all current UI colors.
struct MiniPixels {
#ifdef AWOK_CARDPUTER_V11
  static constexpr int width = 240, height = 135;
#else
  static constexpr int width = 128, height = 128;
#endif
  static constexpr uint16_t palette[16] = {
      0x0000, 0xffff, 0x07ff, 0x001f, 0x07e0, 0xf800, 0xffe0, 0x2104,
      0x7bef, 0x1082, 0xf81f, 0xfd20, 0x4208, 0x8410, 0xc618, 0x03ef};
  uint8_t pixels[width * height / 2] = {};

  static uint8_t colorIndex(uint16_t color) {
    uint8_t best = 0;
    int bestDistance = 100000;
    for (uint8_t i = 0; i < 16; ++i) {
      if (color == palette[i]) return i;
      const int r = int(color >> 11) - int(palette[i] >> 11);
      const int g = int((color >> 5) & 63) - int((palette[i] >> 5) & 63);
      const int b = int(color & 31) - int(palette[i] & 31);
      const int distance = r*r*4 + g*g + b*b*4;
      if (distance < bestDistance) { best = i; bestDistance = distance; }
    }
    return best;
  }
  void fill(uint16_t color) {
    const uint8_t i = colorIndex(color);
    memset(pixels, i | (i << 4), sizeof(pixels));
  }
  void setIndex(int x, int y, uint8_t color) {
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    const int index = y * width + x;
    uint8_t& byte = pixels[index / 2];
    if (index & 1) byte = (byte & 0x0f) | ((color & 15) << 4);
    else byte = (byte & 0xf0) | (color & 15);
  }
  uint16_t get(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return palette[0];
    const int index = y * width + x;
    return palette[(pixels[index / 2] >> ((index & 1) * 4)) & 15];
  }
};
