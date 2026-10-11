#pragma once
#include <stddef.h>
#include <string.h>
#include <time.h>
#include "timezone_data.h"

namespace AwokTime {
constexpr int kZoneCount = sizeof(kZones) / sizeof(kZones[0]);
inline int zoneByName(const char* name) {
  for (int i = 0; i < kZoneCount; ++i) if (!strcmp(name, kZones[i].name)) return i;
  return -1;
}
// Port of the CC0 tz-lookup quadtree decoder. All data stays in mapped flash;
// no heap, filesystem, network, large scratch buffer or external library.
inline int zoneAt(double lat, double lon) {
  if (!(lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180)) return -1;
  if (lat >= 90) return zoneByName("Etc/GMT");
  int node = -1;
  double x = (180 + lon) * 48 / 360.00000000000006;
  double y = (90 - lat) * 24 / 180.00000000000003;
  int u = int(x), v = int(y), pos = v * 96 + u * 2;
  for (int depth = 0; depth < 32; ++depth) {
    if (pos < 0 || size_t(pos + 1) >= sizeof(kTree) - 1) return -1;
    const int value = int(kTree[pos]) * 56 + int(kTree[pos + 1]) - 1995;
    if (value + kZoneCount >= 3136) {
      const int zone = value + kZoneCount - 3136;
      return zone < kZoneCount ? zone : -1;
    }
    node += value + 1;
    x = (x - u) * 2;
    y = (y - v) * 2;
    u = int(x); v = int(y);
    pos = node * 8 + v * 4 + u * 2 + 2304;
  }
  return -1;
}
inline bool offsetAt(int zone, int64_t epoch, int& minutes, bool& dst) {
  if (zone < 0 || zone >= kZoneCount || epoch < kEpoch || epoch >= kEnd) return false;
  const Zone& z = kZones[zone];
  const uint32_t relative = uint32_t(epoch - kEpoch);
  int low = z.first, high = z.first + z.count;
  while (low + 1 < high) {
    const int middle = (low + high) / 2;
    if (kPeriods[middle].since <= relative) low = middle;
    else high = middle;
  }
  const int code = kPeriods[low].offsetAndDst;
  dst = (code & 1) != 0;
  minutes = (code - int(dst)) / 2;
  return true;
}
inline bool localTime(int zone, time_t epoch, struct tm& local, int& minutes, bool& dst) {
  if (!offsetAt(zone, int64_t(epoch), minutes, dst)) return false;
  const time_t shifted = epoch + minutes * 60;
  if (!gmtime_r(&shifted, &local)) return false;
  local.tm_isdst = dst ? 1 : 0;
  return true;
}
}  // namespace AwokTime
