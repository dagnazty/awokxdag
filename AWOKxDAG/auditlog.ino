// AWOKxDAG — persistent firmware operational audit trail.
//
// This is deliberately separate from security_audit.csv, which contains the
// passive Wi-Fi posture report. This file records what the firmware/operator
// did. It never records portal form values, packet payloads, or credentials.

bool firmwareAuditReady = false;
uint32_t firmwareAuditSessionId = 0;

bool createFirmwareAuditFileIfNeeded() {
  if (SD.exists(kFirmwareAuditCsvPath)) return true;
  File file = SD.open(kFirmwareAuditCsvPath, FILE_WRITE);
  if (!file) return false;
  file.println(
      "session_id,firmware_version,event_time,uptime_ms,category,action,"
      "outcome,details,latitude,longitude,altitude_m");
  file.flush();
  const bool ok = file.getWriteError() == 0;
  file.close();
  return ok;
}

bool initializeFirmwareAudit() {
  firmwareAuditReady = false;
  if (!sdReady) return false;
  if (firmwareAuditSessionId == 0) firmwareAuditSessionId = esp_random();

  if (SD.exists(kFirmwareAuditCsvPath)) {
    File current = SD.open(kFirmwareAuditCsvPath, FILE_READ);
    const uint32_t size = current ? current.size() : 0;
    if (current) current.close();
    if (size >= kFirmwareAuditMaxBytes) {
      SD.remove(kFirmwareAuditPreviousCsvPath);
      if (!SD.rename(kFirmwareAuditCsvPath,
                     kFirmwareAuditPreviousCsvPath)) {
        Serial.println("[fw-audit] could not rotate audit log");
        return false;
      }
    }
  }

  firmwareAuditReady = createFirmwareAuditFileIfNeeded();
  if (!firmwareAuditReady) {
    Serial.println("[fw-audit] audit log unavailable");
  }
  return firmwareAuditReady;
}

bool recordFirmwareAudit(const char* category, const char* action,
                         const char* outcome, const String& details) {
  if (!sdReady) return false;
  // Recover after a user deletes the live log or after an earlier open error.
  if (!firmwareAuditReady || !SD.exists(kFirmwareAuditCsvPath)) {
    if (!initializeFirmwareAudit()) return false;
  }

  File file = SD.open(kFirmwareAuditCsvPath, FILE_APPEND);
  if (!file) {
    firmwareAuditReady = false;
    Serial.println("[fw-audit] could not append audit event");
    return false;
  }
  // Rotate here as well as at boot so a long-running session remains bounded.
  if (file.size() >= kFirmwareAuditMaxBytes) {
    file.close();
    firmwareAuditReady = false;
    if (!initializeFirmwareAudit()) return false;
    file = SD.open(kFirmwareAuditCsvPath, FILE_APPEND);
    if (!file) {
      firmwareAuditReady = false;
      return false;
    }
  }
  file.printf("%08lX,", static_cast<unsigned long>(firmwareAuditSessionId));
  file.print(csvField(kVersion));
  file.print(',');
  file.print(csvField(gpsTimestamp()));
  file.print(',');
  file.print(millis());
  file.print(',');
  file.print(csvField(category));
  file.print(',');
  file.print(csvField(action));
  file.print(',');
  file.print(csvField(outcome));
  file.print(',');
  file.print(csvField(details));
  file.print(gpsCsvFields());
  file.println();
  file.flush();
  const bool ok = file.getWriteError() == 0;
  file.close();
  if (!ok) {
    firmwareAuditReady = false;
    Serial.println("[fw-audit] write failed");
    return false;
  }
  Serial.printf("[fw-audit] %s/%s: %s\n", category, action, outcome);
  return true;
}
