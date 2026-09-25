#include <atomic>

// AWOKxDAG — capture / SD file manager (compiled as part of the sketch; see
// awok_common.h)
//
// Bounded capture browser. The remote protocol continues to use fileRows indices;
// filtering changes only the board's view map, never the transfer index.
constexpr int kMaxFileRows = 64;
constexpr int kFilesPerPage = 4;
constexpr int kFileCardTop = 88, kFileCardPitch = 44, kFileCardHeight = 40;

struct FileRow {
  String name;
  uint32_t size;
  time_t modified = 0;
};

FileRow fileRows[kMaxFileRows];
int fileRowCount = 0, fileDirectoryCount = 0;
int fileSelected = -1;
bool fileConfirmDelete = false;
uint64_t fileTotalBytes = 0;
int filePage = 0, fileFilter = 0, fileViewMode = 0; // 0 list, 1 filters, 2 details
int fileViewRows[kMaxFileRows], fileViewCount = 0;
uint32_t fileListRevision = 0, fileDrawRevision = 0;
String fileNotice;
const char* const kFileFilters[] = {"All captures", "Wardrive", "PCAP", "Logs / other"};

String fileBaseName(const String& n) {
  const int slash = n.lastIndexOf('/');
  return slash >= 0 ? n.substring(slash + 1) : n;
}

String fileFullPath(const String& n) {
  if (n.startsWith("/")) return n;
  return String(kSdDirectory) + "/" + n;
}

int fileKind(const String& path) {
  String name = fileBaseName(path);
  name.toLowerCase();
  if (name.startsWith("wardrive") && name.endsWith(".csv")) return 1;
  if (name.endsWith(".pcap") || name.endsWith(".pcapng") || name.endsWith(".cap")) return 2;
  return 3;
}

// Valid SD timestamps sort ahead of unknown timestamps. Filename order is a
// deterministic fallback (generated wardrive names are zero-padded).
bool fileIsNewer(time_t modified, const String& name, int other) {
  if (modified != fileRows[other].modified) return modified > fileRows[other].modified;
  return fileBaseName(name).compareTo(fileBaseName(fileRows[other].name)) > 0;
}

void refreshFileView() {
  fileViewCount = 0;
  for (int i = 0; i < fileRowCount; ++i)
    if (fileFilter == 0 || fileKind(fileRows[i].name) == fileFilter) fileViewRows[fileViewCount++] = i;
  const int pages = max(1, (fileViewCount + kFilesPerPage - 1) / kFilesPerPage);
  filePage = max(0, min(filePage, pages - 1));
}

int filePageCount() { return max(1, (fileViewCount + kFilesPerPage - 1) / kFilesPerPage); }

bool fileButtonHit(int x, int y, int left, int top, int width, int height) {
  return x >= left && x < left + width && y >= top && y < top + height;
}

bool fileSelectionProtected() {
  return fileSelected >= 0 && fileSelected < fileRowCount && g_wardriveFile &&
         fileFullPath(fileRows[fileSelected].name) == g_wardriveCsvPath;
}

String fileSizeLabel(uint32_t bytes) {
  if (bytes >= 1024 * 1024) return String(bytes / 1048576.0, 1) + " MB";
  if (bytes >= 1024) return String(bytes / 1024.0, 1) + " KB";
  return String(bytes) + " B";
}

String fileModifiedLabel(time_t stamp) {
  if (stamp <= 0) return "Unknown";
  struct tm local = {};
  if (!localtime_r(&stamp, &local)) return "Unknown";
  char text[24];
  strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &local);
  return String(text);
}

void drawFilesManager() {
  currentView = View::kFiles;
  refreshFileView();
  fileDrawRevision = fileListRevision;
  if (fileViewMode == 2 && (fileSelected < 0 || fileSelected >= fileRowCount)) fileViewMode = 0;
  display.fillScreen(kBackground);
  if (fileViewMode == 1) {
    drawHeader("FILE FILTERS", "choose a type or refresh the SD");
    for (int i = 0; i < 4; ++i)
      drawSmallButton(8, 46 + i * 44, 224, 40,
          String(fileFilter == i ? "> " : "") + kFileFilters[i], fileFilter == i ? kGood : kAccent);
    drawSmallButton(8, 222, 224, 40, "Refresh SD list", kAccent);
    drawSmallButton(4, 280, 112, 36, "Back", kMuted);
    drawSmallButton(124, 280, 112, 36, "Home", kAccent);
    return;
  }
  if (fileViewMode == 2) {
    drawHeader(fileConfirmDelete ? "DELETE FILE?" : "FILE DETAILS", fileConfirmDelete ? "permanent removal from SD" : "Captures / selected file");
    const String name = fileBaseName(fileRows[fileSelected].name);
    display.setTextSize(1); display.setTextColor(ILI9341_WHITE, kBackground);
    for (int line = 0; line < 4 && line * 37 < int(name.length()); ++line) {
      display.setCursor(scaleX(8), scaleY(50 + line * 12));
      display.print(line == 3 ? clipped(name.substring(line * 37), 37) : name.substring(line * 37, (line + 1) * 37));
    }
    display.setTextColor(kMuted, kBackground);
    display.setCursor(scaleX(8), scaleY(106)); display.print(String("Type: ") + kFileFilters[fileKind(name)]);
    display.setCursor(scaleX(8), scaleY(120)); display.print("Size: " + String(fileRows[fileSelected].size) + " bytes");
    display.setCursor(scaleX(8), scaleY(134)); display.print("Modified: " + fileModifiedLabel(fileRows[fileSelected].modified));
    display.setCursor(scaleX(8), scaleY(160)); display.print("Preview / download on your phone:");
    display.setCursor(scaleX(8), scaleY(174)); display.print("Website > SD Files > select file");
    if (fileNotice.length() || fileSelectionProtected()) {
      display.setTextColor(kWarn, kBackground); display.setCursor(scaleX(8), scaleY(196));
      display.print(fileSelectionProtected() ? "Stop wardriving before deleting." : clipped(fileNotice, 37));
    }
    if (fileConfirmDelete) {
      drawSmallButton(8, 220, 224, 42, "Delete this file", kBad);
      drawSmallButton(4, 280, 232, 36, "Cancel", kMuted);
    } else {
      drawSmallButton(4, 280, 112, 36, "Back", kMuted);
      if (!fileSelectionProtected()) drawSmallButton(124, 280, 112, 36, "Delete...", kBad);
    }
    return;
  }
  String detail = String(fileViewCount) + " matches | " + String(filePage + 1) + "/" + String(filePageCount());
  if (fileDirectoryCount > kMaxFileRows) detail = "Newest 64 of " + String(fileDirectoryCount) + " | " + String(filePage + 1) + "/" + String(filePageCount());
  drawHeader("CAPTURES", detail);
  drawSmallButton(8, 44, 224, 38, String(kFileFilters[fileFilter]) + " / Filter & refresh", kAccent);
  const int start = filePage * kFilesPerPage;
  for (int row = 0; row < kFilesPerPage && start + row < fileViewCount; ++row) {
    const int idx = fileViewRows[start + row], y = kFileCardTop + row * kFileCardPitch;
    const String name = fileBaseName(fileRows[idx].name);
#ifdef AWOK_MINI_DISPLAY
    display.button(8, y, 224, kFileCardHeight,
        (name + " | " + fileSizeLabel(fileRows[idx].size)).c_str(), kAccent);
#else
    display.drawRoundRect(scaleX(8), scaleY(y), scaleX(224), scaleY(kFileCardHeight), 4, kPanel);
    display.setTextSize(1); display.setTextColor(ILI9341_WHITE, kBackground);
    display.setCursor(scaleX(16), scaleY(y + 7)); display.print(clipped(name, 34));
    display.setTextColor(kMuted, kBackground); display.setCursor(scaleX(16), scaleY(y + 24));
    display.print(fileSizeLabel(fileRows[idx].size) + " | " + kFileFilters[fileKind(name)]);
#endif
  }
  if (!fileViewCount) {
    display.setTextSize(1); display.setTextColor(kMuted, kBackground);
    display.setCursor(scaleX(12), scaleY(120)); display.print(!sdReady ? "SD not mounted" : fileRowCount ? "No files match this filter" : "No captures on SD");
    display.setCursor(scaleX(12), scaleY(138)); display.print("Use Filter & refresh above.");
  }
  display.setTextSize(1); display.setTextColor(fileNotice.length() ? kWarn : kMuted, kBackground);
  display.setCursor(scaleX(8), scaleY(268)); display.print(fileNotice.length() ? clipped(fileNotice, 37) : "Newest first | tap a file for details");
  drawSmallButton(4, 280, 72, 36, "Back", kMuted);
  if (filePage > 0) drawSmallButton(84, 280, 72, 36, "Prev", kAccent);
  if (filePage + 1 < filePageCount()) drawSmallButton(164, 280, 72, 36, "Next", kAccent);
}

bool scanSdFiles() {
  ++fileListRevision;
  fileRowCount = fileDirectoryCount = 0;
  fileSelected = -1;
  fileConfirmDelete = false;
  fileTotalBytes = 0;
  if (!ensureSdCard()) return false;
  File dir = SD.open(kSdDirectory);
  if (!dir || !dir.isDirectory()) return false;
  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      ++fileDirectoryCount;
      const String name = String(entry.name());
      const time_t modified = max(time_t(0), entry.getLastWrite());
      int slot = fileRowCount;
      if (slot == kMaxFileRows) {
        slot = 0;
        for (int i = 1; i < fileRowCount; ++i)
          if (fileIsNewer(fileRows[slot].modified, fileRows[slot].name, i)) slot = i;
        if (!fileIsNewer(modified, name, slot)) slot = -1;
      } else ++fileRowCount;
      if (slot >= 0) {
        fileRows[slot].name = name;
        fileRows[slot].size = entry.size();
        fileRows[slot].modified = modified;
      }
    }
    entry.close();
    entry = dir.openNextFile();
    if (fileDirectoryCount % 32 == 0) delay(1);  // yield in large directories
  }
  dir.close();
  for (int i = 1; i < fileRowCount; ++i) {
    int j = i;
    while (j > 0 && fileIsNewer(fileRows[j].modified, fileRows[j].name, j - 1)) {
      const FileRow temp = fileRows[j - 1];
      fileRows[j - 1] = fileRows[j]; fileRows[j] = temp; --j;
    }
  }
  for (int i = 0; i < fileRowCount; ++i) fileTotalBytes += fileRows[i].size;
  return true;
}

void openFilesManager() {
  filePage = fileFilter = fileViewMode = 0;
  fileNotice = "";
  if (!scanSdFiles()) fileNotice = "SD list failed; try Refresh.";
  drawFilesManager();
}

std::atomic<bool> g_fileStreamAborted{false};

void linkAbortFileStream() {
  g_fileStreamAborted = true;
}

static bool encodeBase64Chunk(const uint8_t* in, size_t inLen, char* out, size_t maxOut) {
  static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const size_t outLen = ((inLen + 2) / 3) * 4;
  if (maxOut <= outLen) return false;
  size_t o = 0;
  for (size_t i = 0; i < inLen; i += 3) {
    const uint32_t a = in[i];
    const uint32_t b = (i + 1 < inLen) ? in[i + 1] : 0;
    const uint32_t c = (i + 2 < inLen) ? in[i + 2] : 0;
    const uint32_t triple = (a << 16) | (b << 8) | c;
    out[o++] = alphabet[(triple >> 18) & 63];
    out[o++] = alphabet[(triple >> 12) & 63];
    out[o++] = (i + 1 < inLen) ? alphabet[(triple >> 6) & 63] : '=';
    out[o++] = (i + 2 < inLen) ? alphabet[triple & 63] : '=';
  }
  out[o] = '\0';
  return true;
}

void linkStreamFileList() {
  scanSdFiles();
  const int n = fileRowCount;
#ifdef AWOK_HEADLESS
  for (int i = 0; i < n; ++i) {
    char line[96];
    const String base = fileBaseName(fileRows[i].name);
    snprintf(line, sizeof(line), "$FILELIST,%u,%u,%lu,%s",
             i, n, static_cast<unsigned long>(fileRows[i].size), base.c_str());
    Serial.println(line);
    bridgeNotifyResult(kSourceFiles, reinterpret_cast<const uint8_t*>(line), strlen(line));
    delay(20);
  }
  char endLine[64];
  snprintf(endLine, sizeof(endLine), "$FILELIST_END,%u,%lu",
           n, static_cast<unsigned long>(fileTotalBytes));
  Serial.println(endLine);
  bridgeNotifyResult(kSourceFiles, reinterpret_cast<const uint8_t*>(endLine), strlen(endLine));
#else
  if (!linkEnsureEspNow()) return;
  esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
  for (int i = 0; i < n; ++i) {
    char line[96];
    const String base = fileBaseName(fileRows[i].name);
    snprintf(line, sizeof(line), "$FILELIST,%u,%u,%lu,%s",
             i, n, static_cast<unsigned long>(fileRows[i].size), base.c_str());
    Serial.println(line);
    if (linkEspNowReady) {
      AxdFileEntryMsg msg;
      msg.index = static_cast<uint8_t>(i);
      msg.count = static_cast<uint8_t>(n);
      msg.size = fileRows[i].size;
      strncpy(msg.name, base.c_str(), sizeof(msg.name) - 1);
      esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&msg), sizeof(msg));
    }
    delay(30);
  }
  char endLine[64];
  snprintf(endLine, sizeof(endLine), "$FILELIST_END,%u,%lu",
           n, static_cast<unsigned long>(fileTotalBytes));
  Serial.println(endLine);
  if (linkEspNowReady) {
    AxdFileEntryMsg endMsg;
    endMsg.index = 255;  // end sentinel
    endMsg.count = static_cast<uint8_t>(n);
    endMsg.size = static_cast<uint32_t>(fileTotalBytes);
    esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&endMsg), sizeof(endMsg));
  }
#endif
  Serial.printf("[files] streamed %d file(s) listing\n", n);
}

void linkStreamFileData(uint8_t index) {
  g_fileStreamAborted = false;
  if (!ensureSdCard()) {
    const char* err = "$FILEERR,SD_UNAVAILABLE";
    Serial.println(err);
#ifdef AWOK_HEADLESS
    bridgeNotifyResult(kSourceFiles, reinterpret_cast<const uint8_t*>(err), strlen(err));
#else
    if (linkEnsureEspNow()) {
      esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
      AxdFileDoneMsg done;
      done.status = 1;
      esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&done), sizeof(done));
    }
#endif
    return;
  }

  if (fileRowCount == 0 || index >= fileRowCount) {
    scanSdFiles();
  }
  if (index >= fileRowCount) {
    const char* err = "$FILEERR,FILE_NOT_FOUND";
    Serial.println(err);
#ifdef AWOK_HEADLESS
    bridgeNotifyResult(kSourceFiles, reinterpret_cast<const uint8_t*>(err), strlen(err));
#else
    if (linkEnsureEspNow()) {
      esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
      AxdFileDoneMsg done;
      done.status = 1;
      esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&done), sizeof(done));
    }
#endif
    return;
  }

  const String path = fileFullPath(fileRows[index].name);
  const String base = fileBaseName(fileRows[index].name);
  // A live wardrive keeps its writer open; publish buffered bytes before reading.
  if (path == g_wardriveCsvPath) flushWardriveCsv();
  File file = SD.open(path.c_str(), FILE_READ);
  if (!file) {
    char err[64];
    snprintf(err, sizeof(err), "$FILEERR,CANNOT_OPEN,%s", base.c_str());
    Serial.println(err);
#ifdef AWOK_HEADLESS
    bridgeNotifyResult(kSourceFiles, reinterpret_cast<const uint8_t*>(err), strlen(err));
#else
    if (linkEnsureEspNow()) {
      esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
      AxdFileDoneMsg done;
      done.status = 1;
      esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&done), sizeof(done));
    }
#endif
    return;
  }

  const uint32_t fileSize = file.size();
  constexpr size_t kRawChunkSize = 96;
  const uint16_t totalChunks = (fileSize == 0) ? 1 : static_cast<uint16_t>((fileSize + kRawChunkSize - 1) / kRawChunkSize);

  Serial.printf("[files] streaming %s (%lu bytes, %u chunks)\n",
                base.c_str(), static_cast<unsigned long>(fileSize), totalChunks);

#ifndef AWOK_HEADLESS
  if (!linkEnsureEspNow()) {
    file.close();
    return;
  }
  esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
#endif

  uint8_t rawBuf[kRawChunkSize];
  char b64Buf[136];
  uint16_t seq = 0;

  if (fileSize == 0) {
    char line[64];
    snprintf(line, sizeof(line), "$FILEDATA,1,1,");
    Serial.println(line);
#ifdef AWOK_HEADLESS
    bridgeNotifyResult(kSourceFiles, reinterpret_cast<const uint8_t*>(line), strlen(line));
#else
    if (linkEspNowReady) {
      AxdFileDataMsg dmsg;
      dmsg.chunkSeq = 1;
      dmsg.totalChunks = 1;
      dmsg.dataLen = 0;
      esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&dmsg), sizeof(dmsg));
    }
#endif
  } else {
    while (file.available() && !g_fileStreamAborted) {
      seq++;
      const size_t bytesRead = file.read(rawBuf, sizeof(rawBuf));
      if (bytesRead == 0) break;
      encodeBase64Chunk(rawBuf, bytesRead, b64Buf, sizeof(b64Buf));

      char line[180];
      snprintf(line, sizeof(line), "$FILEDATA,%u,%u,%s", seq, totalChunks, b64Buf);
      Serial.println(line);

#ifdef AWOK_HEADLESS
      bridgeNotifyResult(kSourceFiles, reinterpret_cast<const uint8_t*>(line), strlen(line));
#else
      if (linkEspNowReady) {
        AxdFileDataMsg dmsg;
        dmsg.chunkSeq = seq;
        dmsg.totalChunks = totalChunks;
        dmsg.dataLen = static_cast<uint8_t>(strlen(b64Buf));
        strncpy(dmsg.data, b64Buf, sizeof(dmsg.data) - 1);
        esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&dmsg), sizeof(dmsg));
      }
#endif
      delay(30);
    }
  }

  file.close();
  delay(30);

  char doneLine[80];
  if (g_fileStreamAborted) {
    snprintf(doneLine, sizeof(doneLine), "$FILEABORT,%s", base.c_str());
  } else {
    snprintf(doneLine, sizeof(doneLine), "$FILEDONE,%s,%lu",
             base.c_str(), static_cast<unsigned long>(fileSize));
  }
  Serial.println(doneLine);

#ifdef AWOK_HEADLESS
  bridgeNotifyResult(kSourceFiles, reinterpret_cast<const uint8_t*>(doneLine), strlen(doneLine));
#else
  if (linkEspNowReady) {
    AxdFileDoneMsg done;
    done.status = g_fileStreamAborted ? 2 : 0;
    done.totalBytes = fileSize;
    strncpy(done.name, base.c_str(), sizeof(done.name) - 1);
    esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&done), sizeof(done));
  }
#endif
}

// Only an ACK from the browser advances the SD reader. Receiving at the bridge
// or successfully queueing a BLE notification alone does not count as delivery.
static std::atomic<uint32_t> fileReliableToken{0};
static std::atomic<uint32_t> fileReliableWaitingSeq{0};

bool linkReliableFileActive() { return fileReliableToken.load() != 0; }
bool linkReliableFileTokenMatches(uint32_t token) { return token != 0 && token == fileReliableToken.load(); }

void linkReceiveFileAck(uint32_t token, uint32_t seq) {
  if (token == 0 || token != fileReliableToken.load()) return;
  uint32_t expected = seq;
  fileReliableWaitingSeq.compare_exchange_strong(expected, 0);
}

static bool fileSendReliable(AxdFileChunkMsg& chunk) {
  fileReliableWaitingSeq.store(chunk.seq);
  for (int attempt = 0; attempt < kFileChunkAttempts && !g_fileStreamAborted; ++attempt) {
    esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
    const esp_err_t sent = esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&chunk), sizeof(chunk));
    if (sent != ESP_OK) {
      Serial.printf("[files] ESP-NOW enqueue failed: seq=%lu error=%d\n",
                    static_cast<unsigned long>(chunk.seq), static_cast<int>(sent));
    }
    const uint32_t started = millis();
    while (millis() - started < kFileChunkRetryMs && !g_fileStreamAborted) {
      if (fileReliableWaitingSeq.load() == 0) return true;
      delay(1);  // ACKs arrive in onLinkRecv, independent of the blocked main loop
    }
    Serial.printf("[files] retry seq=%lu attempt=%d\n",
                  static_cast<unsigned long>(chunk.seq), attempt + 1);
  }
  return false;
}

void linkStreamFileReliable(uint8_t index, uint32_t token) {
  g_fileStreamAborted = false;
  fileReliableToken.store(token);
  AxdFileChunkMsg chunk;
  chunk.token = token;
  chunk.seq = 1;
  File file;
  String base;
  const char* error = nullptr;
  char errorDetail[96] = {};
  if (!ensureSdCard()) error = "SD unavailable";
  if (!error && (fileRowCount == 0 || index >= fileRowCount)) scanSdFiles();
  if (!error && index >= fileRowCount) error = "File not found";
  if (!error) {
    const String path = fileFullPath(fileRows[index].name);
    base = fileBaseName(fileRows[index].name);
    if (path == g_wardriveCsvPath) flushWardriveCsv();
    file = SD.open(path.c_str(), FILE_READ);
    if (!file) error = "Cannot open file";
  }
  if (!error) {
    chunk.totalBytes = file.size();
    Serial.printf("[files] reliable download: %s, %lu bytes, token=%lu\n",
                  base.c_str(), static_cast<unsigned long>(chunk.totalBytes),
                  static_cast<unsigned long>(token));
    uint32_t remaining = chunk.totalBytes;
    uint8_t raw[kFileChunkBytes];
    do {
      const size_t wanted = remaining < sizeof(raw) ? remaining : sizeof(raw);
      const size_t got = wanted ? file.read(raw, wanted) : 0;
      if (got != wanted) { error = "SD read failed"; break; }
      encodeBase64Chunk(raw, got, chunk.data, sizeof(chunk.data));
      if (!fileSendReliable(chunk)) {
        snprintf(errorDetail, sizeof(errorDetail), "No browser ACK for chunk %lu after %d attempts",
                 static_cast<unsigned long>(chunk.seq), kFileChunkAttempts);
        error = errorDetail;
        break;
      }
      remaining -= got;
      ++chunk.seq;
    } while (remaining && !g_fileStreamAborted);
    file.close();
  }
  if (!g_fileStreamAborted) {
    chunk.kind = error ? 2 : 1;
    strncpy(chunk.data, error ? error : base.c_str(), sizeof(chunk.data) - 1);
    // Use a distinct sequence for an error after a timed-out data chunk, so a
    // delayed data ACK cannot acknowledge the error/completion message.
    if (error) ++chunk.seq;
    fileSendReliable(chunk);
  }
  fileReliableToken.store(0);
  fileReliableWaitingSeq.store(0);
}

// Snapshot-based transfer: checksum the selected byte range before accepting a
// resume. A growing CSV may resume its original prefix; a changed/truncated file
// must restart. The browser also validates name, size and CRC before ACKing the
// manifest, so a reordered directory can never silently substitute another file.
void linkStreamFileVerified(uint8_t index, uint32_t token, uint32_t startSeq,
                            uint32_t snapshotBytes, uint32_t expectedCrc) {
  g_fileStreamAborted = false;
  fileReliableToken.store(token);
  AxdFileChunkMsg chunk;
  chunk.token = token;
  chunk.seq = 1;
  const char* error = nullptr;
  File file;
  String base;
  if (!ensureSdCard()) error = "SD_UNAVAILABLE";
  if (!error && (fileRowCount == 0 || index >= fileRowCount)) scanSdFiles();
  if (!error && index >= fileRowCount) error = "FILE_NOT_FOUND";
  if (!error) {
    const String path = fileFullPath(fileRows[index].name);
    base = fileBaseName(fileRows[index].name);
    if (path == g_wardriveCsvPath) flushWardriveCsv();
    file = SD.open(path.c_str(), FILE_READ);
    if (!file) error = "CANNOT_OPEN";
  }
  uint32_t checksum = 0, prefixState = 0xffffffffU;
  uint32_t offset = 0, total = 1;
  if (!error) {
    chunk.totalBytes = startSeq ? snapshotBytes : file.size();
    total = chunk.totalBytes / kFileChunkBytes + (chunk.totalBytes % kFileChunkBytes != 0);
    if (!total) total = 1;
    if (file.size() < chunk.totalBytes) error = "FILE_CHANGED: restart download";
    else if (startSeq > total + 1) error = "INVALID_RESUME";
    else offset = startSeq > total ? chunk.totalBytes : startSeq ? (startSeq - 1) * kFileChunkBytes : 0;
  }
  uint8_t raw[kFileChunkBytes];
  if (!error) {
    uint32_t state = 0xffffffffU, readBytes = 0, lastProgress = millis();
    // ACKed progress also keeps the bridge parked during a long SD hash pass.
    chunk.kind = 5; chunk.seq = 0xfffffffeU;
    strcpy(chunk.data, "Checking SD snapshot");
    if (!fileSendReliable(chunk)) error = "NO_BROWSER_ACK";
    while (!error && readBytes < chunk.totalBytes && !g_fileStreamAborted) {
      const size_t wanted = min(kFileChunkBytes, chunk.totalBytes - readBytes);
      const size_t got = file.read(raw, wanted);
      if (got != wanted) { error = "SD_READ_FAILED"; break; }
      state = fileCrcUpdate(state, raw, got);
      readBytes += got;
      if (readBytes == offset) prefixState = state;
      if ((readBytes % 4096) < kFileChunkBytes) delay(1);
      if (millis() - lastProgress >= 1000) {
        if (!fileSendReliable(chunk)) { error = "NO_BROWSER_ACK"; break; }
        lastProgress = millis();
      }
    }
    checksum = state ^ 0xffffffffU;
    if (!error && startSeq && checksum != expectedCrc) error = "FILE_CHANGED: restart download";
    if (!error && !g_fileStreamAborted) {
      chunk.kind = 3; chunk.seq = 0xffffffffU;
      snprintf(chunk.data, sizeof(chunk.data), "%08lx,%s", static_cast<unsigned long>(checksum), base.c_str());
      if (!fileSendReliable(chunk)) error = "MANIFEST_NOT_ACCEPTED";
    }
    if (!error && !g_fileStreamAborted && !file.seek(offset)) error = "SD_SEEK_FAILED";
    uint32_t remaining = chunk.totalBytes - offset;
    chunk.kind = 0; chunk.seq = startSeq ? startSeq : 1;
    // Empty files still have one empty chunk; total+1 resumes completion only.
    while (!error && chunk.seq <= total && !g_fileStreamAborted) {
      const size_t wanted = min(kFileChunkBytes, remaining);
      const size_t got = wanted ? file.read(raw, wanted) : 0;
      if (got != wanted) { error = "SD_READ_FAILED"; break; }
      prefixState = fileCrcUpdate(prefixState, raw, got);
      encodeBase64Chunk(raw, got, chunk.data, sizeof(chunk.data));
      if (!fileSendReliable(chunk)) { error = "NO_BROWSER_ACK: resume download"; break; }
      remaining -= got;
      ++chunk.seq;
    }
    if (!error && !g_fileStreamAborted && (prefixState ^ 0xffffffffU) != checksum)
      error = "FILE_CHANGED: restart download";
  }
  if (file) file.close();
  if (!g_fileStreamAborted) {
    chunk.kind = error ? 2 : 4;
    chunk.seq = total + (error ? 2 : 1);
    if (error) snprintf(chunk.data, sizeof(chunk.data), "%s", error);
    else snprintf(chunk.data, sizeof(chunk.data), "%08lx,%s", static_cast<unsigned long>(checksum), base.c_str());
    fileSendReliable(chunk);
  }
  fileReliableToken.store(0);
  fileReliableWaitingSeq.store(0);
}

void linkDeleteFile(uint8_t index) {
  if (!ensureSdCard() || index >= fileRowCount) {
    const char* err = "$FILEDELETE,failed";
    Serial.println(err);
#ifdef AWOK_HEADLESS
    bridgeNotifyResult(kSourceFiles, reinterpret_cast<const uint8_t*>(err), strlen(err));
#else
    if (linkEnsureEspNow()) {
      esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
      AxdFileDoneMsg done;
      done.status = 1;
      esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&done), sizeof(done));
    }
#endif
    return;
  }
  const String path = fileFullPath(fileRows[index].name);
  const String base = fileBaseName(fileRows[index].name);
  const bool removed = SD.remove(path.c_str());
  Serial.printf("[files] remote delete %s: %s\n", base.c_str(), removed ? "success" : "failed");
  recordFirmwareAudit("storage", "remote_file_delete",
                      removed ? "success" : "failed", "path=" + path);
  char line[80];
  snprintf(line, sizeof(line), "$FILEDELETE,%s,%s", removed ? "ok" : "failed", base.c_str());
  Serial.println(line);
#ifdef AWOK_HEADLESS
  bridgeNotifyResult(kSourceFiles, reinterpret_cast<const uint8_t*>(line), strlen(line));
#else
  if (linkEnsureEspNow()) {
    esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
    AxdFileDoneMsg done;
    done.status = removed ? 0 : 1;
    strncpy(done.name, base.c_str(), sizeof(done.name) - 1);
    esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&done), sizeof(done));
  }
#endif
  linkStreamFileList();
}

void handleFilesTouch(int x, int y) {
  // A remote list/refresh may replace indices while the old screen is visible.
  // Redraw before accepting another action, especially a deletion confirmation.
  if (fileDrawRevision != fileListRevision) {
    fileViewMode = 0; fileConfirmDelete = false;
    fileNotice = "List updated; select the file again.";
    drawFilesManager(); return;
  }
  if (fileViewMode == 1) {
    if (fileButtonHit(x, y, 8, 46, 224, 172)) {
      const int row = (y - 46) / 44;
      if ((y - 46) % 44 >= 40) return;
      fileFilter = row; filePage = 0; fileViewMode = 0; fileNotice = "";
    } else if (fileButtonHit(x, y, 8, 222, 224, 40)) {
      fileNotice = scanSdFiles() ? "SD list refreshed." : "SD list failed; try Refresh.";
      fileViewMode = 0;
    } else if (fileButtonHit(x, y, 4, 280, 112, 36)) fileViewMode = 0;
    else if (fileButtonHit(x, y, 124, 280, 112, 36)) { drawHome(); return; }
    else return;
    drawFilesManager(); return;
  }
  if (fileViewMode == 2) {
    if (fileSelected < 0 || fileSelected >= fileRowCount) { fileViewMode = 0; drawFilesManager(); return; }
    if (fileConfirmDelete) {
      if (fileButtonHit(x, y, 4, 280, 232, 36)) { fileConfirmDelete = false; drawFilesManager(); return; }
      if (!fileButtonHit(x, y, 8, 220, 224, 42)) return;
      if (fileSelectionProtected()) { fileConfirmDelete = false; drawFilesManager(); return; }
      const String path = fileFullPath(fileRows[fileSelected].name);
      const bool removed = SD.remove(path.c_str());
      recordFirmwareAudit("storage", "file_delete", removed ? "success" : "failed", "path=" + path);
      fileConfirmDelete = false;
      if (removed) {
        fileViewMode = 0;
        fileNotice = scanSdFiles() ? "File deleted." : "Deleted; SD refresh failed.";
      } else fileNotice = "Delete failed; file was not removed.";
    } else if (fileButtonHit(x, y, 4, 280, 112, 36)) { fileViewMode = 0; fileNotice = ""; }
    else if (fileButtonHit(x, y, 124, 280, 112, 36) && !fileSelectionProtected()) { fileConfirmDelete = true; fileNotice = ""; }
    else return;
    drawFilesManager(); return;
  }
  if (fileButtonHit(x, y, 8, 44, 224, 38)) { fileViewMode = 1; drawFilesManager(); return; }
  if (fileButtonHit(x, y, 8, kFileCardTop, 224, kFilesPerPage * kFileCardPitch)) {
    const int row = (y - kFileCardTop) / kFileCardPitch;
    if ((y - kFileCardTop) % kFileCardPitch >= kFileCardHeight) return;
    const int position = filePage * kFilesPerPage + row;
    if (position >= fileViewCount) return;
    fileSelected = fileViewRows[position]; fileViewMode = 2; fileConfirmDelete = false; fileNotice = "";
  } else if (fileButtonHit(x, y, 4, 280, 72, 36)) { drawHome(); return; }
  else if (fileButtonHit(x, y, 84, 280, 72, 36) && filePage > 0) --filePage;
  else if (fileButtonHit(x, y, 164, 280, 72, 36) && filePage + 1 < filePageCount()) ++filePage;
  else return;
  drawFilesManager();
}
