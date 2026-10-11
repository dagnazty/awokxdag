#pragma once
#include <stddef.h>
#include <stdint.h>

// IEEE CRC-32, same checksum in the browser and on the SD snapshot. Detects
// accidental corruption; this is an integrity check, not authentication.
inline uint32_t fileCrcUpdate(uint32_t state, const uint8_t* bytes, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    state ^= bytes[i];
    for (int bit = 0; bit < 8; ++bit)
      state = (state >> 1) ^ ((state & 1) ? 0xedb88320U : 0U);
  }
  return state;
}
