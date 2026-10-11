// Selected-device GATT inventory for C5 screen boards. A probe issues one raw
// ATT Read Request and records only its status and byte count. NimBLE-Arduino's
// readValue() retries authentication by pairing, so it must not be used here.
#ifdef AWOK_GATT_AUDIT
#include <NimBLEClient.h>
#include <NimBLEConnInfo.h>
#include <host/ble_gatt.h>
#include <host/ble_uuid.h>

constexpr char kGattAuditCsvPath[] = "/awokxdag/ble_gatt_audit.csv";
constexpr int kGattMaxCharacteristics = 48;
constexpr int kGattMaxServices = 16;
constexpr uint32_t kGattReadTimeoutMs = 4000;
constexpr uint32_t kGattDiscoveryTimeoutMs = 5000;

struct GattServiceRange {
  char uuid[37] = {0};
  uint16_t first = 0;
  uint16_t last = 0;
};

ToolBuffer<GattAuditRow, kGattMaxCharacteristics> gattRows;
NimBLEClient* gattClient = nullptr;
int gattCount = 0;
int gattPage = 0;
bool gattTruncated = false;
bool gattSdOk = false;
String gattMessage;
std::atomic<bool> gattReadDone{false};
std::atomic<int> gattReadResult{-1};
std::atomic<unsigned> gattReadLength{0};
std::atomic<uint32_t> gattReadGeneration{0};
GattServiceRange gattServices[kGattMaxServices];
int gattServiceCount = 0;
char gattCurrentService[37] = {0};
std::atomic<bool> gattDiscoveryDone{false};
std::atomic<int> gattDiscoveryStatus{-1};
std::atomic<uint32_t> gattDiscoveryGeneration{0};
std::atomic<bool> gattDisconnected{true};
std::atomic<int> gattDisconnectReason{0};

class GattAuditClientCallbacks : public NimBLEClientCallbacks {
 public:
  void onDisconnect(NimBLEClient*, int reason) override {
    gattDisconnectReason.store(reason);
    gattDisconnected.store(true);
  }
};
GattAuditClientCallbacks gattClientCallbacks;

static int gattServiceCallback(uint16_t, const ble_gatt_error* error,
                               const ble_gatt_svc* service, void* arg) {
  if (reinterpret_cast<uintptr_t>(arg) != gattDiscoveryGeneration.load()) return 0;
  if (error && error->status == 0 && service) {
    if (gattServiceCount < kGattMaxServices) {
      GattServiceRange& row = gattServices[gattServiceCount++];
      ble_uuid_to_str(&service->uuid.u, row.uuid);
      row.first = service->start_handle;
      row.last = service->end_handle;
    } else {
      gattTruncated = true;
      gattDiscoveryStatus.store(BLE_HS_EDONE);
      gattDiscoveryDone.store(true);
      return BLE_HS_EDONE;  // stop issuing more service requests
    }
    return 0;
  }
  gattDiscoveryStatus.store(error ? error->status : BLE_HS_EUNKNOWN);
  gattDiscoveryDone.store(true);
  return 0;
}

static int gattCharacteristicCallback(uint16_t, const ble_gatt_error* error,
                                      const ble_gatt_chr* characteristic, void* arg) {
  if (reinterpret_cast<uintptr_t>(arg) != gattDiscoveryGeneration.load()) return 0;
  if (error && error->status == 0 && characteristic) {
    if (gattCount < kGattMaxCharacteristics) {
      GattAuditRow& row = gattRows[gattCount++];
      snprintf(row.service, sizeof(row.service), "%s", gattCurrentService);
      ble_uuid_to_str(&characteristic->uuid.u, row.characteristic);
      row.handle = characteristic->val_handle;
      const uint8_t props = characteristic->properties;
      row.properties = ((props & BLE_GATT_CHR_PROP_READ) ? 1 : 0) |
                       ((props & BLE_GATT_CHR_PROP_WRITE) ? 2 : 0) |
                       ((props & BLE_GATT_CHR_PROP_WRITE_NO_RSP) ? 4 : 0) |
                       ((props & BLE_GATT_CHR_PROP_NOTIFY) ? 8 : 0) |
                       ((props & BLE_GATT_CHR_PROP_INDICATE) ? 16 : 0);
    } else {
      gattTruncated = true;
      gattDiscoveryStatus.store(BLE_HS_EDONE);
      gattDiscoveryDone.store(true);
      return BLE_HS_EDONE;  // stop issuing more characteristic requests
    }
    return 0;
  }
  gattDiscoveryStatus.store(error ? error->status : BLE_HS_EUNKNOWN);
  gattDiscoveryDone.store(true);
  return 0;
}

bool awaitGattDiscovery(uint32_t generation) {
  const uint32_t started = millis();
  while (!gattDiscoveryDone.load() && !gattDisconnected.load() &&
         millis() - started < kGattDiscoveryTimeoutMs)
    delay(10);
  if (!gattDiscoveryDone.load()) {
    gattDiscoveryGeneration.fetch_add(1);  // ignore any late callback
    gattMessage = gattDisconnected.load() ? "Disconnected during discovery"
                                       : "Discovery timeout";
    if (gattClient && gattClient->isConnected()) gattClient->disconnect();
    return false;
  }
  if (gattDiscoveryGeneration.load() != generation ||
      gattDiscoveryStatus.load() != BLE_HS_EDONE) {
    gattMessage = "Discovery error " + String(gattDiscoveryStatus.load());
    return false;
  }
  // The callback sets the completion flag before NimBLE removes its procedure.
  // Let its host task finish before starting the next discovery request.
  delay(10);
  return true;
}

void drawGattProgress(const char* stage) {
  display.fillScreen(kBackground);
  drawHeader("GATT AUDIT", "active connection to selected device");
  display.setTextColor(kAccent, kBackground);
  display.setTextSize(2);
  display.setCursor(18, 135);
  display.print(stage);
  display.present(false);
}

static int gattRawReadCallback(uint16_t, const ble_gatt_error* error,
                               ble_gatt_attr* attr, void* arg) {
  if (reinterpret_cast<uintptr_t>(arg) != gattReadGeneration.load()) return 0;
  const int status = error ? error->status : BLE_HS_EUNKNOWN;
  gattReadLength.store(status == 0 && attr && attr->om
                           ? OS_MBUF_PKTLEN(attr->om) : 0);
  gattReadResult.store(status);
  gattReadDone.store(true);
  return 0;
}

String gattReadLabel(const GattAuditRow& row) {
  if (!(row.properties & 1)) return "no read property";
  if (row.readStatus == -1) return "tap to test read";
  if (row.readStatus == -2) return "read timeout";
  if (row.readStatus == -3) return "encrypted during read";
  if (row.readStatus == 0) return "read OK: " + String(row.readBytes) + " bytes";
  if (row.readStatus == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHEN) ||
      row.readStatus == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHOR) ||
      row.readStatus == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_ENC))
    return "authentication needed";
  return "read error " + String(row.readStatus);
}

String gattProperties(const GattAuditRow& row) {
  String out;
  if (row.properties & 1) out += 'R';
  if (row.properties & 2) out += 'W';
  if (row.properties & 4) out += 'w';
  if (row.properties & 8) out += 'N';
  if (row.properties & 16) out += 'I';
  return out.length() ? out : "none";
}

void drawGattAudit() {
  currentView = View::kBleGattAudit;
  display.fillScreen(kBackground);
  const int pages = max(1, (gattCount + kMenuPerPage - 1) / kMenuPerPage);
  if (gattPage >= pages) gattPage = 0;
  drawHeader("GATT AUDIT", selectedBle.address);
  display.setTextSize(1);
  display.setTextColor(kMuted, kBackground);
  display.setCursor(7, 44);
  display.printf("%d chars  %d/%d%s  SD %s", gattCount, gattPage + 1, pages,
                 gattTruncated ? " +more" : "", gattSdOk ? "saved" : "--");
  if (gattMessage.length()) {
    display.setCursor(7, 267);
    display.print(clipped(gattMessage, 39));
  }
  for (int row = 0; row < kMenuPerPage; ++row) {
    const int i = gattPage * kMenuPerPage + row;
    if (i >= gattCount) break;
    const GattAuditRow& item = gattRows[i];
    String title = String(item.characteristic);
    if (title.length() > 29) title = clipped(title, 29);
    String detail = "S:" + clipped(String(item.service), 9) + " " +
                    gattProperties(item) + " " + gattReadLabel(item);
    drawMenuCard(kMenuFirstY + row * kMenuRowPitch, title, detail,
                 item.readStatus == 0 ? kGood : kAccent);
  }
  if (!gattCount) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(8, 108);
    display.print(gattMessage.length() ? clipped(gattMessage, 36)
                                       : "No characteristics discovered");
  }
  if (pages > 1) drawFourButtonFooter("Back", "Prev", "Next", "Save");
  else drawFooter("Back", "Save");
}

bool exportGattAuditToSd() {
  if (!sdReady || !gattRows || !gattCount) return false;
  const String temporaryPath = String(kGattAuditCsvPath) + ".tmp";
  SD.remove(temporaryPath.c_str());
  File file = SD.open(temporaryPath.c_str(), FILE_WRITE);
  if (!file) return false;
  file.println("export_uptime_ms,export_time,address,address_type,rssi,service_uuid,characteristic_uuid,properties,read_status,read_status_code,read_bytes,read_at_ms");
  const String stamp = gpsTimestamp();
  for (int i = 0; i < gattCount; ++i) {
    const GattAuditRow& row = gattRows[i];
    file.print(millis()); file.print(',');
    file.print(csvField(stamp)); file.print(',');
    file.print(csvField(selectedBle.address)); file.print(',');
    file.print(csvField(bleAddressTypeLabel(selectedBle.addressType))); file.print(',');
    file.print(selectedBle.rssi); file.print(',');
    file.print(csvField(row.service)); file.print(',');
    file.print(csvField(row.characteristic)); file.print(',');
    file.print(csvField(gattProperties(row))); file.print(',');
    file.print(csvField(gattReadLabel(row))); file.print(',');
    file.print(row.readStatus); file.print(',');
    file.print(row.readStatus == 0 ? row.readBytes : 0); file.print(',');
    file.println(row.readAtMs);
  }
  file.flush();
  const bool ok = file.getWriteError() == 0;
  file.close();
  if (!ok) { SD.remove(temporaryPath.c_str()); return false; }
  SD.remove(kGattAuditCsvPath);
  if (!SD.rename(temporaryPath.c_str(), kGattAuditCsvPath)) {
    SD.remove(temporaryPath.c_str());
    return false;
  }
  return true;
}

bool gattAuditActive() {
  return gattClient != nullptr || static_cast<bool>(gattRows);
}

// Must run while NimBLE is still initialized. NimBLEClient's destructor stops
// a host callout, so deinit(true) cannot be the first place that deletes it.
bool stopGattAuditForRadio() {
  gattSdOk = exportGattAuditToSd();
  gattDiscoveryGeneration.fetch_add(1);
  gattReadGeneration.fetch_add(1);
  if (gattClient) {
    if (gattClient->isConnected()) {
      gattClient->disconnect();
    }
    const uint32_t started = millis();
    while (!gattDisconnected.load() && millis() - started < 1500) delay(10);
    if (!gattDisconnected.load()) {
      Serial.println("[gatt] disconnect timed out; keeping BLE initialized");
      gattMessage = "Disconnect pending; try Back again";
      return false;
    }
    // onDisconnect runs before NimBLEClient marks its state DISCONNECTED.
    // Yield to the host task so deleteClient destroys it synchronously.
    delay(20);
    if (!NimBLEDevice::deleteClient(gattClient)) {
      Serial.println("[gatt] client deletion failed; keeping BLE initialized");
      gattMessage = "BLE cleanup failed; try Back again";
      return false;
    }
    gattClient = nullptr;
  }
  gattRows.release();
  gattCount = 0;
  return true;
}

bool closeGattAudit() {
  const int count = gattCount;
  if (!stopGattAuditForRadio()) return false;
  releaseBleMemory();
  recordFirmwareAudit("ble", "gatt_audit", "stopped",
                      "address=" + selectedBle.address + "; characteristics=" + String(count));
  return true;
}

void startGattAudit() {
  if (!selectedBle.connectable) return;
  gattCount = 0;
  gattPage = 0;
  gattTruncated = false;
  gattSdOk = false;
  gattMessage = "";
  gattServiceCount = 0;
  if (!gattRows.allocate()) { showToolMemoryError("GATT audit"); return; }
  if (!ensureBleReady(false)) { gattRows.release(); return; }
  const NimBLEAddress address(selectedBle.address.c_str(), selectedBle.addressType);
  // A previous bond would make a connection encrypted and invalidate an
  // unauthenticated-read result. Leave the bond untouched and refuse this run.
  if (NimBLEDevice::isBonded(address)) {
    gattMessage = "Already bonded; unpaired test unavailable";
    recordFirmwareAudit("ble", "gatt_audit", "refused",
                        "address=" + selectedBle.address + "; already_bonded");
    drawGattAudit();
    return;
  }
  scanInProgress = true;
  drawGattProgress("Connecting...");
  Serial.printf("[gatt] connecting to %s\n", selectedBle.address.c_str());
  gattClient = NimBLEDevice::createClient();
  if (!gattClient) {
    gattMessage = "Could not allocate BLE client";
  } else {
    gattDisconnected.store(false);
    gattDisconnectReason.store(0);
    gattClient->setClientCallbacks(&gattClientCallbacks, false);
    gattClient->setConnectTimeout(5000);
    if (!gattClient->connect(address, true, false, false)) {
      gattDisconnected.store(true);
      gattMessage = "Connection failed or timed out";
    } else {
      Serial.println("[gatt] discovering services");
      logMemory("GATT connected");
      drawGattProgress("Discovering...");
      gattDiscoveryDone.store(false);
      gattDiscoveryStatus.store(-1);
      uint32_t generation = gattDiscoveryGeneration.fetch_add(1) + 1;
      int rc = ble_gattc_disc_all_svcs(
          gattClient->getConnHandle(), gattServiceCallback,
          reinterpret_cast<void*>(static_cast<uintptr_t>(generation)));
      bool discovered = false;
      if (rc != 0) {
        gattMessage = "Service discovery error " + String(rc);
      } else if (awaitGattDiscovery(generation)) {
        discovered = true;
      }
      for (int serviceIndex = 0; discovered && serviceIndex < gattServiceCount;
           ++serviceIndex) {
        if (gattCount >= kGattMaxCharacteristics) {
          gattTruncated = true;
          break;
        }
        const GattServiceRange& service = gattServices[serviceIndex];
        memcpy(gattCurrentService, service.uuid, sizeof(gattCurrentService));
        Serial.printf("[gatt] characteristics %d/%d %s\n",
                      serviceIndex + 1, gattServiceCount, service.uuid);
        gattDiscoveryDone.store(false);
        gattDiscoveryStatus.store(-1);
        generation = gattDiscoveryGeneration.fetch_add(1) + 1;
        rc = ble_gattc_disc_all_chrs(
            gattClient->getConnHandle(), service.first, service.last,
            gattCharacteristicCallback,
            reinterpret_cast<void*>(static_cast<uintptr_t>(generation)));
        if (rc != 0) {
          gattMessage = "Characteristic discovery error " + String(rc);
          discovered = false;
        } else {
          discovered = awaitGattDiscovery(generation);
        }
      }
      if (discovered) {
        gattMessage = gattCount ? "Tap a readable row to test access"
                                : "No characteristics discovered";
      }
      Serial.printf("[gatt] discovery %s; services=%d characteristics=%d\n",
                    discovered ? "complete" : "stopped", gattServiceCount,
                    gattCount);
      if (gattDisconnected.load())
        Serial.printf("[gatt] disconnected reason=%d\n", gattDisconnectReason.load());
      logMemory("GATT inventory finished");
    }
  }
  scanInProgress = false;
  gattSdOk = exportGattAuditToSd();
  recordFirmwareAudit("ble", "gatt_audit", gattCount ? "success" : "partial",
                      "address=" + selectedBle.address + "; characteristics=" + String(gattCount));
  drawGattAudit();
}

void probeGattRead(int index) {
  if (index < 0 || index >= gattCount) return;
  if (!gattClient || !gattClient->isConnected()) {
    gattMessage = "Disconnected; reopen GATT Audit";
    drawGattAudit();
    return;
  }
  GattAuditRow& row = gattRows[index];
  if (!(row.properties & 1)) return;
  if (gattClient->getConnInfo().isEncrypted()) {
    gattMessage = "Encrypted link; unpaired read skipped";
    drawGattAudit();
    return;
  }
  const uint32_t generation = gattReadGeneration.fetch_add(1) + 1;
  gattReadDone.store(false);
  gattReadResult.store(-1);
  gattReadLength.store(0);
  scanInProgress = true;
  drawGattProgress("Reading...");
  const int rc = ble_gattc_read(gattClient->getConnHandle(), row.handle,
                                gattRawReadCallback,
                                reinterpret_cast<void*>(static_cast<uintptr_t>(generation)));
  if (rc != 0) {
    row.readStatus = rc;
  } else {
    const uint32_t started = millis();
    while (!gattReadDone.load() && millis() - started < kGattReadTimeoutMs) delay(10);
    if (gattReadDone.load()) {
      row.readStatus = gattReadResult.load();
      row.readBytes = gattReadLength.load();
    } else {
      row.readStatus = -2;
      gattReadGeneration.fetch_add(1);  // discard any late callback
      gattClient->disconnect();  // cancel pending request before another probe
      gattMessage = "Read timed out; reconnect to continue";
    }
  }
  if (gattClient->isConnected() && gattClient->getConnInfo().isEncrypted()) {
    row.readStatus = -3;
    row.readBytes = 0;
    gattMessage = "Link encrypted during probe";
  }
  row.readAtMs = millis();
  scanInProgress = false;
  gattSdOk = exportGattAuditToSd();
  recordFirmwareAudit("ble", "gatt_read_probe", "complete",
                      "address=" + selectedBle.address + "; uuid=" + row.characteristic +
                          "; status=" + String(row.readStatus));
  drawGattAudit();
}

void handleGattTouch(int x, int y) {
  if (y >= kMenuFirstY && y < kFooterTop) {
    const int row = (y - kMenuFirstY) / kMenuRowPitch;
    const int within = (y - kMenuFirstY) - row * kMenuRowPitch;
    if (row < kMenuPerPage && within < kMenuCardHeight)
      probeGattRead(gattPage * kMenuPerPage + row);
    return;
  }
  if (y < kFooterTop) return;
  const int pages = max(1, (gattCount + kMenuPerPage - 1) / kMenuPerPage);
  if (pages == 1) {
    if (x < kScreenWidth / 2) {
      if (closeGattAudit()) drawBleDetail();
      else drawGattAudit();
    }
    else { gattSdOk = exportGattAuditToSd(); drawGattAudit(); }
    return;
  }
  if (x < 60) {
    if (closeGattAudit()) drawBleDetail();
    else drawGattAudit();
  }
  else if (x < 120) { gattPage = (gattPage + pages - 1) % pages; drawGattAudit(); }
  else if (x < 180) { gattPage = (gattPage + 1) % pages; drawGattAudit(); }
  else { gattSdOk = exportGattAuditToSd(); drawGattAudit(); }
}
#endif
