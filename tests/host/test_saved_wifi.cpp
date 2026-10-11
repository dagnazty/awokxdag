#include <cassert>
#include <cstring>

#include "saved_wifi.h"

int main() {
  SavedWifi::Snapshot snapshot;
  assert(SavedWifi::valid(snapshot));
  assert(SavedWifi::put(snapshot, "Home", "correct-horse"));
  assert(SavedWifi::put(snapshot, "Guest", ""));
  assert(snapshot.count == 2);
  assert(SavedWifi::find(snapshot, "Home") == 0);
  assert(SavedWifi::put(snapshot, "Home", "new-password"));
  assert(snapshot.count == 2);
  assert(std::strcmp(snapshot.entries[0].password, "new-password") == 0);
  assert(!SavedWifi::put(snapshot, "", "correct-horse"));
  assert(!SavedWifi::put(snapshot, "Short", "short"));
  assert(SavedWifi::put(snapshot, "Third", "password3"));
  assert(SavedWifi::put(snapshot, "Fourth", "password4"));
  assert(!SavedWifi::put(snapshot, "Fifth", "password5"));
  assert(SavedWifi::remove(snapshot, 0));
  assert(snapshot.count == 3);
  assert(SavedWifi::find(snapshot, "Guest") == 0);
  assert(!SavedWifi::remove(snapshot, 3));
  snapshot.entries[0].ssid[0] = 0;
  assert(!SavedWifi::valid(snapshot));
  SavedWifi::wipe(&snapshot, sizeof(snapshot));
  const auto* bytes = reinterpret_cast<const unsigned char*>(&snapshot);
  for (size_t i = 0; i < sizeof(snapshot); ++i) assert(bytes[i] == 0);
}
