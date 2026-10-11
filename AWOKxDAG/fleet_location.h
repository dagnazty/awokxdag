#pragma once

#include <cmath>
#include <stdint.h>

// A fleet observation can carry its own position or use the coordinator's
// fresh fix. Never turn a missing fix into 0,0.
struct FleetLocation {
  float lat = 0.0f;
  float lon = 0.0f;
  int16_t alt = 0;
  bool valid = false;
};

inline FleetLocation fleetLocation(float lat, float lon, int16_t alt) {
  FleetLocation fix;
  if (std::isfinite(lat) && std::isfinite(lon) &&
      lat >= -90.0f && lat <= 90.0f && lon >= -180.0f && lon <= 180.0f) {
    fix.lat = lat;
    fix.lon = lon;
    fix.alt = alt;
    fix.valid = true;
  }
  return fix;
}

inline FleetLocation fleetChooseLocation(const FleetLocation& observed,
                                         const FleetLocation& coordinator) {
  if (observed.valid) return observed;
  return coordinator;
}
