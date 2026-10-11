#pragma once

#include <stdint.h>
#include <string.h>

// Camera-scan clues, not device identification. A vendor assignment describes
// the radio maker; a name or service can be spoofed. Keep single clues weak.
namespace CameraSignals {

constexpr uint16_t kVendorOui = 1u << 0;
constexpr uint16_t kGenericName = 1u << 1;
constexpr uint16_t kFlockOui = 1u << 2;
constexpr uint16_t kAxonOui = 1u << 3;
constexpr uint16_t kFlockName = 1u << 4;
constexpr uint16_t kAxonName = 1u << 5;
constexpr uint16_t kXuntong = 1u << 6;
constexpr uint16_t kRavenService = 1u << 7;
constexpr uint16_t kRavenServices = 1u << 8;  // two or more distinct UUIDs
constexpr uint16_t kFlockSsid = 1u << 9;

enum Confidence : uint8_t { kNone, kLow, kMedium, kHigh };

inline bool oui(const uint8_t* mac, uint8_t a, uint8_t b, uint8_t c) {
  // A locally administered address is not evidence of the registered owner.
  return mac && !(mac[0] & 0x02) && mac[0] == a && mac[1] == b &&
         mac[2] == c;
}

inline bool flockOui(const uint8_t* mac) {
  return oui(mac, 0xB4, 0x1E, 0x52);  // IEEE MA-L: Flock Safety
}

inline bool axonOui(const uint8_t* mac) {
  return oui(mac, 0x00, 0x25, 0xDF);  // IEEE MA-L: Axon Enterprise
}

inline char lower(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

inline bool starts(const char* text, const char* prefix) {
  if (!text) return false;
  while (*prefix) {
    if (!*text || lower(*text++) != lower(*prefix++)) return false;
  }
  return true;
}

inline bool equals(const char* text, const char* value) {
  if (!text) return false;
  while (*value) {
    if (!*text || lower(*text++) != lower(*value++)) return false;
  }
  return *text == '\0';
}

inline bool hexSuffix(const char* text, unsigned minDigits, unsigned maxDigits) {
  unsigned n = 0;
  while (text && *text && n <= maxDigits) {
    const char c = lower(*text++);
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    ++n;
  }
  return text && !*text && n >= minDigits && n <= maxDigits;
}

inline bool flockSsid(const char* name) {
  return starts(name, "flock-") && hexSuffix(name + 6, 4, 8);
}

inline bool decimalSuffix(const char* text, unsigned minDigits, unsigned maxDigits) {
  unsigned n = 0;
  while (text && *text && n <= maxDigits) {
    if (*text < '0' || *text > '9') return false;
    ++text;
    ++n;
  }
  return text && !*text && n >= minDigits && n <= maxDigits;
}

inline bool flockName(const char* name) {
  return equals(name, "flock") || equals(name, "fs ext battery") ||
         equals(name, "penguin") ||
         (starts(name, "penguin-") && decimalSuffix(name + 8, 1, 12)) ||
         equals(name, "pigvision") || equals(name, "flockcam") ||
         (starts(name, "fs-") && hexSuffix(name + 3, 4, 8));
}

inline bool axonName(const char* name) {
  return starts(name, "axon body") || starts(name, "axon flex");
}

inline uint8_t ravenServiceBit(uint16_t shortUuid) {
  // Exact custom services seen in Raven research. Do not match the whole
  // 0x3100-0x3500 range or generic standard services (0x180A/09/19).
  switch (shortUuid) {
    case 0x3100: return 1u << 0;
    case 0x3200: return 1u << 1;
    case 0x3300: return 1u << 2;
    case 0x3400: return 1u << 3;
    case 0x3500: return 1u << 4;
    default: return 0;
  }
}

inline unsigned bitCount(uint8_t bits) {
  unsigned count = 0;
  while (bits) {
    count += bits & 1u;
    bits >>= 1;
  }
  return count;
}

inline bool sightingExpired(uint32_t nowMs, uint32_t seenMs,
                            uint32_t retentionMs) {
  // Unsigned subtraction also works when millis() wraps after ~49 days.
  return static_cast<uint32_t>(nowMs - seenMs) >= retentionMs;
}

inline Confidence confidence(uint16_t bits) {
  if ((bits & kFlockSsid && bits & kFlockOui) ||
      (bits & kFlockName && bits & (kFlockOui | kXuntong | kRavenService)) ||
      (bits & kAxonName && bits & kAxonOui)) return kHigh;
  if (bits & (kFlockSsid | kFlockName | kAxonName | kRavenServices))
    return kMedium;
  if ((bits & kVendorOui) && (bits & kGenericName)) return kMedium;
  return bits ? kLow : kNone;
}

inline const char* confidenceLabel(Confidence level) {
  switch (level) {
    case kHigh: return "HIGH";
    case kMedium: return "MED";
    case kLow: return "LOW";
    default: return "NONE";
  }
}

inline const char* evidenceLabel(uint16_t bits) {
  if ((bits & kFlockSsid) && (bits & kFlockOui)) return "OUI + SSID";
  if ((bits & kFlockName) && (bits & kXuntong)) return "name + mfg";
  if ((bits & kFlockName) && (bits & kRavenService)) return "name + UUID";
  if ((bits & kFlockName) && (bits & kFlockOui)) return "OUI + name";
  if ((bits & kAxonName) && (bits & kAxonOui)) return "OUI + name";
  if ((bits & kVendorOui) && (bits & kGenericName)) return "OUI + name";
  if (bits & kFlockSsid) return "Flock SSID";
  if (bits & kFlockName) return "BLE name";
  if (bits & kAxonName) return "device name";
  if (bits & kRavenServices) return "2+ Raven UUIDs";
  if (bits & kRavenService) return "Raven UUID";
  if (bits & kXuntong) return "mfg 0x09C8";
  if (bits & (kFlockOui | kAxonOui | kVendorOui)) return "vendor OUI";
  if (bits & kGenericName) return "device name";
  return "";
}

}  // namespace CameraSignals
