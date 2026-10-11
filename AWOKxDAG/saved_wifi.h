#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace SavedWifi {

constexpr uint32_t kVersion = 1;
constexpr uint32_t kCapacity = 4;

struct Record {
  char ssid[33] = {};
  char password[64] = {};
};

struct Snapshot {
  uint32_t version = kVersion;
  uint32_t count = 0;
  Record entries[kCapacity] = {};
};

inline void wipe(void* data, size_t length) {
  volatile uint8_t* bytes = static_cast<volatile uint8_t*>(data);
  while (length--) *bytes++ = 0;
}

inline bool valid(const Snapshot& snapshot) {
  if (snapshot.version != kVersion || snapshot.count > kCapacity) return false;
  for (uint32_t i = 0; i < snapshot.count; ++i) {
    const char* ssidEnd = static_cast<const char*>(
        memchr(snapshot.entries[i].ssid, 0, sizeof(Record::ssid)));
    const char* passwordEnd = static_cast<const char*>(
        memchr(snapshot.entries[i].password, 0, sizeof(Record::password)));
    if (!ssidEnd || !passwordEnd || ssidEnd == snapshot.entries[i].ssid)
      return false;
    const size_t passwordLength = passwordEnd - snapshot.entries[i].password;
    if (passwordLength && passwordLength < 8) return false;
    for (uint32_t j = 0; j < i; ++j)
      if (strcmp(snapshot.entries[i].ssid, snapshot.entries[j].ssid) == 0)
        return false;
  }
  return true;
}

inline int find(const Snapshot& snapshot, const char* ssid) {
  for (uint32_t i = 0; i < snapshot.count; ++i)
    if (strcmp(snapshot.entries[i].ssid, ssid) == 0) return static_cast<int>(i);
  return -1;
}

inline bool put(Snapshot& snapshot, const char* ssid, const char* password) {
  if (!valid(snapshot) || !ssid || !password) return false;
  const size_t ssidLength = strnlen(ssid, sizeof(Record::ssid));
  const size_t passwordLength = strnlen(password, sizeof(Record::password));
  if (!ssidLength || ssidLength > 32 || passwordLength > 63 ||
      (passwordLength && passwordLength < 8)) return false;
  int index = find(snapshot, ssid);
  if (index < 0) {
    if (snapshot.count == kCapacity) return false;
    index = static_cast<int>(snapshot.count++);
  }
  Record& entry = snapshot.entries[index];
  wipe(&entry, sizeof(entry));
  memcpy(entry.ssid, ssid, ssidLength);
  memcpy(entry.password, password, passwordLength);
  return true;
}

inline bool remove(Snapshot& snapshot, uint32_t index) {
  if (!valid(snapshot) || index >= snapshot.count) return false;
  for (uint32_t i = index + 1; i < snapshot.count; ++i)
    snapshot.entries[i - 1] = snapshot.entries[i];
  wipe(&snapshot.entries[--snapshot.count], sizeof(Record));
  return true;
}

}  // namespace SavedWifi
