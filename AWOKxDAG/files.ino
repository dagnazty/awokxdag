// AWOKxDAG — capture / SD file manager (compiled as part of the sketch; see
// awok_common.h)
//
// Lists the files under /awokxdag with sizes, paginated so every file is
// reachable, and lets you delete one behind a two-tap confirm. Reached from the
// Status screen.

constexpr int kMaxFileRows = 64;

struct FileRow {
  String name;
  uint32_t size;
};

FileRow fileRows[kMaxFileRows];
int fileRowCount = 0;
int fileSelected = -1;  // absolute index, -1 = none
bool fileConfirmDelete = false;
uint64_t fileTotalBytes = 0;
int filePage = 0;

String fileBaseName(const String& n) {
  const int slash = n.lastIndexOf('/');
  return slash >= 0 ? n.substring(slash + 1) : n;
}

String fileFullPath(const String& n) {
  if (n.startsWith("/")) return n;
  return String(kSdDirectory) + "/" + n;
}

int filePageCount() {
  if (fileRowCount == 0) return 1;
  return (fileRowCount + kVisibleRows - 1) / kVisibleRows;
}

void drawFilesManager() {
  currentView = View::kFiles;
  const int pages = filePageCount();
  if (filePage >= pages) filePage = 0;
  display.fillScreen(kBackground);
  drawHeader("CAPTURES",
             String(fileRowCount) + " files | " +
                 String(static_cast<uint32_t>(fileTotalBytes / 1024)) +
                 " KB | pg " + String(filePage + 1) + "/" + String(pages));
  display.setTextSize(1);
  const int start = filePage * kVisibleRows;
  for (int row = 0; row < kVisibleRows; ++row) {
    const int idx = start + row;
    if (idx >= fileRowCount) break;
    const int y = 48 + row * 22;
    display.setTextColor(idx == fileSelected ? kAccent : ILI9341_WHITE,
                         kBackground);
    display.setCursor(5, y);
    display.print(clipped(fileBaseName(fileRows[idx].name), 26));
    display.setTextColor(kMuted, kBackground);
    display.setCursor(5, y + 11);
    if (fileRows[idx].size >= 1024) {
      display.printf("%lu KB",
                     static_cast<unsigned long>(fileRows[idx].size / 1024));
    } else {
      display.printf("%lu B", static_cast<unsigned long>(fileRows[idx].size));
    }
  }
  if (fileRowCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(40, 140);
    display.print(sdReady ? "No files in /awokxdag" : "SD not mounted");
  }
  const char* action =
      fileSelected >= 0 ? (fileConfirmDelete ? "OK!" : "Del") : "Resc";
  drawFourButtonFooter("Back", "< Prev", "Next >", action);
}

void openFilesManager() {
  fileRowCount = 0;
  fileSelected = -1;
  fileConfirmDelete = false;
  fileTotalBytes = 0;
  filePage = 0;
  if (ensureSdCard()) {
    File dir = SD.open(kSdDirectory);
    if (dir && dir.isDirectory()) {
      File entry = dir.openNextFile();
      while (entry && fileRowCount < kMaxFileRows) {
        if (!entry.isDirectory()) {
          fileRows[fileRowCount].name = String(entry.name());
          fileRows[fileRowCount].size = entry.size();
          fileTotalBytes += entry.size();
          ++fileRowCount;
        }
        entry = dir.openNextFile();
      }
      dir.close();
    }
  }
  drawFilesManager();
}

void handleFilesTouch(int x, int y) {
  if (y < kFooterTop) {
    if (y >= 48) {
      const int row = (y - 48) / 22;
      const int idx = filePage * kVisibleRows + row;
      if (row >= 0 && row < kVisibleRows && idx < fileRowCount) {
        fileSelected = idx;
        fileConfirmDelete = false;
        drawFilesManager();
      }
    }
    return;
  }
  const int pages = filePageCount();
  if (x < 60) {
    drawStatus();
  } else if (x < 120) {
    filePage = (filePage - 1 + pages) % pages;
    fileSelected = -1;
    fileConfirmDelete = false;
    drawFilesManager();
  } else if (x < 180) {
    filePage = (filePage + 1) % pages;
    fileSelected = -1;
    fileConfirmDelete = false;
    drawFilesManager();
  } else if (fileSelected >= 0) {
    if (!fileConfirmDelete) {
      fileConfirmDelete = true;
      drawFilesManager();
    } else {
      SD.remove(fileFullPath(fileRows[fileSelected].name).c_str());
      Serial.printf("[files] deleted %s\n", fileRows[fileSelected].name.c_str());
      openFilesManager();
    }
  } else {
    openFilesManager();  // rescan when nothing is selected
  }
}
