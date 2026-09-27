#include "fleet_location.h"
#include "link_protocol.h"

#include <cassert>
#include <cmath>
#include <limits>

int main() {
  static_assert(sizeof(LinkPacket) == 36, "Fleet heartbeat must keep its wire size");
  static_assert(sizeof(FleetWardriveRow) == 76, "Fleet rows must keep their wire size");
  const FleetLocation none;
  const FleetLocation coordinator = fleetLocation(42.1f, -83.2f, 201);
  const FleetLocation observed = fleetLocation(42.3f, -83.4f, 203);

  assert(!fleetChooseLocation(none, none).valid);
  assert(fleetChooseLocation(none, coordinator).lat == coordinator.lat);
  assert(fleetChooseLocation(observed, coordinator).lat == observed.lat);
  assert(fleetLocation(0.0f, 0.0f, 0).valid);  // 0,0 is real when explicitly fixed.
  assert(!fleetLocation(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0).valid);
  assert(!fleetLocation(0.0f, std::numeric_limits<float>::quiet_NaN(), 0).valid);
  assert(!fleetLocation(91.0f, 0.0f, 0).valid);
  assert(!fleetLocation(0.0f, -181.0f, 0).valid);
}
