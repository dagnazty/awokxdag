#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <unordered_set>

constexpr uint32_t MALLOC_CAP_SPIRAM = 1;
constexpr uint32_t MALLOC_CAP_INTERNAL = 2;
constexpr uint32_t MALLOC_CAP_8BIT = 4;

namespace HeapMock {
inline bool failExternal = false;
inline bool failInternal = false;
inline unsigned calls = 0;
inline uint32_t lastCaps = 0;
inline std::unordered_set<void*> live;
}

inline void* heap_caps_malloc(size_t bytes, uint32_t caps) {
  ++HeapMock::calls;
  HeapMock::lastCaps = caps;
  if ((caps & MALLOC_CAP_SPIRAM) && HeapMock::failExternal) return nullptr;
  if ((caps & MALLOC_CAP_INTERNAL) && HeapMock::failInternal) return nullptr;
  void* p = std::malloc(bytes);
  if (p) HeapMock::live.insert(p);
  return p;
}

inline void heap_caps_free(void* p) {
  assert(HeapMock::live.erase(p) == 1);
  std::free(p);
}
