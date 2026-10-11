#pragma once
#include <stdint.h>

// Fixed-size session counters; no route history or per-observation allocation.
struct WardriveSessionStats {
  uint8_t mode = 0;
  uint32_t id = 0, elapsedMs = 0, sampledMs = 0, fixMs = 0;
  uint32_t rows = 0, bytes = 0, flushedRows = 0, lastFlushMs = 0;
  uint32_t rateBuckets[6] = {}, rateTick = 0, lastCount = 0;
  float distanceM = 0;
  double lastLat = 0, lastLon = 0;
  bool lastPosition = false, writeError = false, didFlush = false;
  void count(uint32_t elapsed, uint32_t total) {
    const uint32_t tick = elapsed / 10000;
    if (tick - rateTick >= 6) for (auto& bucket : rateBuckets) bucket = 0;
    else for (uint32_t step = rateTick + 1; step <= tick; ++step) rateBuckets[step % 6] = 0;
    rateTick = tick;
    rateBuckets[tick % 6] += total >= lastCount ? total - lastCount : 0;
    lastCount = total;
  }
  uint32_t perMinute() const {
    uint32_t count = 0;
    for (const auto value : rateBuckets) count += value;
    const uint32_t window = elapsedMs < 60000 ? elapsedMs : 60000;
    return window ? uint32_t(uint64_t(count) * 60000 / window) : 0;
  }
  unsigned fixPercent() const { return elapsedMs ? unsigned(uint64_t(fixMs) * 100 / elapsedMs) : 0; }
};
