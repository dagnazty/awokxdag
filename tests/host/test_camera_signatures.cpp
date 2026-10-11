#include "camera_signatures.h"
#include <cassert>

using namespace CameraSignals;

int main() {
  const uint8_t flock[6] = {0xB4, 0x1E, 0x52, 0x01, 0x02, 0x03};
  const uint8_t axon[6] = {0x00, 0x25, 0xDF, 0x01, 0x02, 0x03};
  const uint8_t local[6] = {0xB6, 0x1E, 0x52, 0x01, 0x02, 0x03};
  assert(flockOui(flock) && !axonOui(flock));
  assert(axonOui(axon) && !flockOui(axon));
  assert(!flockOui(local));  // randomized address is not a vendor OUI

  assert(flockSsid("Flock-3A88") && flockSsid("flock-D6ED5E"));
  assert(!flockSsid("Flock-fan") && !flockSsid("Flock-3A88-junk"));
  assert(flockName("FS Ext Battery") && flockName("Penguin-1234"));
  assert(!flockName("Penguin-router") && !flockName("FS-speaker"));
  assert(axonName("Axon Body 3") && !axonName("Axon Networks"));

  assert(ravenServiceBit(0x3100) == 1);
  assert(ravenServiceBit(0x3500) == 16);
  assert(ravenServiceBit(0x3101) == 0);  // do not match a broad range
  assert(ravenServiceBit(0x180A) == 0);  // generic Device Information
  assert(bitCount(ravenServiceBit(0x3100) | ravenServiceBit(0x3200)) == 2);

  assert(!sightingExpired(90500, 1000, 90000));
  assert(sightingExpired(91000, 1000, 90000));
  assert(!sightingExpired(500, UINT32_MAX - 1000, 90000));
  assert(sightingExpired(500, UINT32_MAX - 90000, 90000));

  assert(confidence(kFlockOui) == kLow);
  assert(confidence(kAxonOui) == kLow);
  assert(confidence(kXuntong) == kLow);
  assert(confidence(kRavenService) == kLow);
  assert(confidence(kFlockName) == kMedium);
  assert(confidence(kFlockOui | kFlockSsid) == kHigh);
  assert(confidence(kFlockName | kXuntong) == kHigh);
  assert(confidence(kAxonName | kAxonOui) == kHigh);
  assert(confidence(kRavenService | kRavenServices) == kMedium);
  assert(confidence(kVendorOui | kGenericName) == kMedium);
}
