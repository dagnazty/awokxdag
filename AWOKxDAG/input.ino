// AWOKxDAG — touch + serial input dispatch (compiled as part of the sketch; see awok_common.h)

bool readTouch(int& screenX, int& screenY) {
  if (!touch.touched()) return false;
  TS_Point point = touch.getPoint();
  if (point.z < AwokTouchCalibration::kPressureMin) return false;
  screenX = constrain(map(point.x, AwokTouchCalibration::kXMin,
                          AwokTouchCalibration::kXMax, 0, kScreenWidth - 1),
                      0, kScreenWidth - 1);
  screenY = constrain(map(point.y, AwokTouchCalibration::kYMin,
                          AwokTouchCalibration::kYMax, 0, kScreenHeight - 1),
                      0, kScreenHeight - 1);
  return true;
}

void handleTouch() {
  if (scanInProgress || millis() - lastTouchMs < 250) return;
  int x = 0;
  int y = 0;
  if (!readTouch(x, y)) return;
  lastTouchMs = millis();
  Serial.printf("[touch] x=%d y=%d\n", x, y);
  if (currentView == View::kHome) {
    if (homePage == 1) {
      homePage = 0;  // any tap on the About page returns to the tiles
      drawHome();
      return;
    }
    if (y >= 44 && y < 84) {
      drawReconMenu();
    } else if (y >= 88 && y < 128) {
      drawAttacksMenu();
    } else if (y >= 132 && y < 172) {
      drawMonitorMenu();
    } else if (y >= 176 && y < 216) {
      drawGps();
    } else if (y >= 220 && y < 260) {
      drawStatus();
    } else if (y >= kFooterTop) {
      homePage = 1;  // footer opens the About page (page 2)
      drawHome();
    }
    return;
  }
  if (currentView == View::kStatus) {
    handleStatusTouch(x, y);
    return;
  }
  if (currentView == View::kFiles) {
    handleFilesTouch(x, y);
    return;
  }
  if (currentView == View::kRecon) {
    if (y < kFooterTop) {
      const int start = reconPage * kMenuPerPage;
      for (int row = 0; row < kMenuPerPage; ++row) {
        const int index = start + row;
        if (index >= kReconItemCount) break;
        const int by = kMenuFirstY + row * kMenuRowPitch;
        if (y >= by && y < by + kMenuRowHeight) {
          launchReconItem(index);
          return;
        }
      }
      return;
    }
    const int pages = reconPageCount();
    if (pages <= 1) {
      drawHome();
    } else if (x < 80) {
      drawHome();
    } else if (x < 160) {
      reconPage = (reconPage - 1 + pages) % pages;
      drawReconMenu();
    } else {
      reconPage = (reconPage + 1) % pages;
      drawReconMenu();
    }
    return;
  }
  if (currentView == View::kMonitor) {
    if (y < kFooterTop) {
      const int start = monitorPage * kMenuPerPage;
      for (int row = 0; row < kMenuPerPage; ++row) {
        const int index = start + row;
        if (index >= kMonitorItemCount) break;
        const int by = kMenuFirstY + row * kMenuRowPitch;
        if (y >= by && y < by + kMenuRowHeight) {
          launchMonitorItem(index);
          return;
        }
      }
      return;
    }
    const int pages = monitorPageCount();
    if (pages <= 1) {
      drawHome();
    } else if (x < 80) {
      drawHome();
    } else if (x < 160) {
      monitorPage = (monitorPage - 1 + pages) % pages;
      drawMonitorMenu();
    } else {
      monitorPage = (monitorPage + 1) % pages;
      drawMonitorMenu();
    }
    return;
  }
  if (currentView == View::kWifi && y >= 44 && y < 264) {
    const int index = (y - 44) / 22;
    if (index < min(wifiCount, kVisibleRows)) {
      openWifiAudit(wifiEntries[index], View::kWifi);
    }
    return;
  }
  if (currentView == View::kSaved && y >= 44 && y < 264) {
    const int index = (y - 44) / 22;
    if (index < savedCount) openWifiAudit(savedEntries[index], View::kSaved);
    return;
  }
  if (currentView == View::kBle && y >= 44 && y < 264) {
    const int index = (y - 44) / 22;
    if (index < min(bleCount, kVisibleRows)) {
      openBleDetail(bleEntries[index]);
    }
    return;
  }
  if (currentView == View::kDeauthSelect && y >= 44 && y < 264) {
    const int index = (y - 44) / 22;
    if (index < min(wifiCount, kVisibleRows)) {
      toggleDeauthTarget(wifiEntries[index]);
      drawDeauthSelect();
    }
    return;
  }
  if (currentView == View::kAttacks && y < kFooterTop) {
    if (y >= 44 && y < 82) {
      startBeaconFlood();
    } else if (y >= 86 && y < 124) {
      startEvilPortalPlain();
    } else if (y >= 128 && y < 166) {
      startEvilTwin();
    } else if (y >= 170 && y < 208) {
      startProbeLure();
    }
    return;
  }
  if (y < kFooterTop) return;
  if (currentView == View::kPacketMon) {
    stopPacketMon();
    drawReconMenu();
    return;
  }
  if (currentView == View::kCameraScan) {
    if (x < kScreenWidth / 2) {
      stopCameraScan();
      drawReconMenu();
    } else {
      cameraCount = 0;
      drawCameraScan();
    }
    return;
  }
  if (currentView == View::kLocator) {
    if (x < 80) {
      stopLocator();
      drawWifiAudit();
    } else if (x < 160) {
      startLocator();  // reset the hunt
    } else {
      stopLocator();
      beginWifiSignalMonitor();
    }
    return;
  }
  if (currentView == View::kWpsScan) {
    stopWpsScan();
    drawReconMenu();
    return;
  }
  if (currentView == View::kRogueWatch) {
    stopRogueWatch();
    drawMonitorMenu();
    return;
  }
  if (currentView == View::kHiddenReveal) {
    stopHiddenReveal();
    drawReconMenu();
    return;
  }
  if (currentView == View::kSecurityAudit) {
    if (x < kScreenWidth / 2) {
      stopSecurityAudit();
      drawReconMenu();
    } else {
      lastAuditCsvOk = exportSecurityAuditToSd();
      drawSecurityAudit();
    }
    return;
  }
  if (currentView == View::kTrackerScan) {
    if (x < kScreenWidth / 2) {
      stopTrackerScan();
      drawReconMenu();
    } else {
      lastTrackerCsvOk = exportTrackersToSd();
      drawTrackerScan();
    }
    return;
  }
  if (currentView == View::kHarvester) {
    if (x < kScreenWidth / 2) {
      stopHarvester();
      drawReconMenu();
    } else {
      resetHarvester();
      drawHarvester();
    }
    return;
  }
  if (currentView == View::kProbeIntel) {
    if (x < kScreenWidth / 2) {
      stopProbeIntel();
      drawReconMenu();
    } else {
      lastProbeIntelCsvOk = exportProbeIntelToSd();
      drawProbeIntel();
    }
    return;
  }
  if (currentView == View::kKarmaWatch) {
    if (x < kScreenWidth / 2) {
      stopKarmaWatch();
      drawMonitorMenu();
    } else {
      resetKarmaWatch();
      drawKarmaWatch();
    }
    return;
  }
  if (currentView == View::kBeaconWatch) {
    if (x < kScreenWidth / 2) {
      stopBeaconWatch();
      drawMonitorMenu();
    } else {
      resetBeaconWatch();
      drawBeaconWatch();
    }
    return;
  }
  if (currentView == View::kAuthFlood) {
    if (x < kScreenWidth / 2) {
      stopAuthFlood();
      drawMonitorMenu();
    } else {
      resetAuthFlood();
      drawAuthFlood();
    }
    return;
  }
  if (currentView == View::kDeauthMonitor) {
    if (x < kScreenWidth / 2) {
      stopDeauthMonitor();
      drawHome();
    } else {
      deauthFrameCount = 0;
      disassocFrameCount = 0;
      deauthEventsSinceDraw = 0;
      haveDeauthHit = false;
      drawDeauthMonitor();
    }
    return;
  }
  if (currentView == View::kDeauthAttack) {
    if (x < kScreenWidth / 2) {
      stopDeauthAttack();
      if (deauthAttackReturnView == View::kDeauthSelect) {
        drawDeauthSelect();
      } else {
        drawWifiAudit();
      }
    } else {
      if (deauthAttackActive) {
        stopDeauthAttack();
        drawDeauthAttack();
      } else if (deauthTargetCount > 0) {
        startDeauthAttack();
      }
    }
    return;
  }
  if (currentView == View::kHandshake) {
    if (x < kScreenWidth / 2) {
      stopHandshakeCapture();
      drawWifiAudit();
    } else {
      handshakePulseEnabled = !handshakePulseEnabled;
      drawHandshake();
    }
    return;
  }
  if (currentView == View::kClientSniffer) {
    if (x < kScreenWidth / 2) {
      stopClientSniffer();
      drawHome();
    } else {
      lastClientCsvOk = exportClientsToSd();
      drawClientSniffer();
    }
    return;
  }
  if (currentView == View::kAttacks) {
    drawHome();
    return;
  }
  if (currentView == View::kGps) {
    if (x < 80) {
      drawHome();
    } else if (x < 160) {
      cycleGpsBaud();
      drawGps();
    } else {
      startWardrive();
    }
    return;
  }
  if (currentView == View::kWardrive) {
    if (x < kScreenWidth / 2) {
      stopWardrive();
      drawGps();
    } else {
      stopWardrive();
      drawHome();
    }
    return;
  }
  if (currentView == View::kBeaconFlood) {
    if (x < kScreenWidth / 2) {
      stopBeaconFlood();
      drawAttacksMenu();
    } else if (beaconFloodActive) {
      stopBeaconFlood();
      drawBeaconFlood();
    } else {
      startBeaconFlood();
    }
    return;
  }
  if (currentView == View::kBleSpamWatch) {
    if (x < kScreenWidth / 2) {
      stopBleDetect();
      drawHome();
    } else {
      bleDetectTotal = 0;
      bleDetectSpam = 0;
      bleDetectPeakRate = 0;
      bleDetectRate = 0;
      bleDetectAlert = false;
      bleDetectLastVendor = 0;
      drawBleDetect();
    }
    return;
  }
  if (currentView == View::kProbeLure) {
    if (x < kScreenWidth / 2) {
      stopProbeLure();
      drawAttacksMenu();
    } else if (probeLureActive) {
      stopProbeLure();
      drawProbeLure();
    } else {
      startProbeLure();
    }
    return;
  }
  if (currentView == View::kEvilPortal) {
    if (x < kScreenWidth / 2) {
      stopEvilPortal();
      drawAttacksMenu();
    } else {
      portalCredsCount = 0;
      lastPortalCred = "";
      SD.remove(kPortalCredsPath);
      portalLogReady = openPortalLog();
      drawEvilPortal();
    }
    return;
  }
  if (currentView == View::kDeauthSelect) {
    if (x < 80) {
      drawWifiResults();
    } else if (x < 160) {
      deauthTargetCount = 0;
      drawDeauthSelect();
    } else if (deauthTargetCount > 0) {
      deauthAttackReturnView = View::kDeauthSelect;
      openDeauthAttackView();
    }
    return;
  }
  if (currentView == View::kWifi) {
    if (x < 80) {
      drawHome();
    } else if (x < 160) {
      drawDeauthSelect();
    } else {
      scanWifi();
    }
    return;
  }
  if (currentView == View::kWifiMonitor) {
    if (x < 80) {
      signalMonitorActive = false;
      drawWifiAudit();
    } else if (x < 160) {
      beginWifiSignalMonitor();
    } else {
      signalMonitorActive = false;
      startLocator();
    }
    return;
  }
  if (currentView == View::kBleDetail) {
    if (x < kScreenWidth / 2) {
      drawBleResults();
    } else {
      scanBle();
    }
    return;
  }
  if (currentView == View::kWifiAudit) {
    if (x < 48) {
      if (auditReturnView == View::kSaved) {
        drawSavedNetworks();
      } else {
        drawWifiResults();
      }
    } else if (x < 96) {
      beginWifiSignalMonitor();
    } else if (x >= 192) {
      grabHandshake();
    } else if (x >= 144) {
      openDeauthAttackSingle();
    } else {
      const bool wasSaved = isSaved(selectedWifi);
      if (toggleSavedNetwork(selectedWifi)) {
        if (lastSavedSdWriteOk) {
          auditStatus = wasSaved ? "Removed; SD mirror updated."
                                 : "Saved to device + SD card.";
        } else if (sdReady) {
          auditStatus = wasSaved ? "Removed; SD export failed."
                                 : "Saved; SD export failed.";
        } else {
          auditStatus = wasSaved ? "Removed; SD card unavailable."
                                 : "Saved; insert SD to export.";
        }
      } else {
        auditStatus = savedCount >= kMaxSaved ? "Saved list is full."
                                              : "Storage write failed.";
      }
      drawWifiAudit();
    }
    return;
  }
  if (x < kScreenWidth / 2) {
    drawHome();
  } else if (currentView == View::kBle) {
    scanBle();
  } else if (currentView == View::kChannels) {
    scanWifiForChannelMap();
  } else if (currentView == View::kSaved) {
    scanWifi();
  }
}

void handleSerial() {
  if (!Serial.available() || scanInProgress) return;
  const char command = static_cast<char>(tolower(Serial.read()));
  if (deauthAttackActive || deauthMonitorActive || handshakeCaptureActive ||
      clientSnifferActive || beaconFloodActive || evilPortalActive ||
      wardriveActive || pktmonActive || wpsScanActive || rogueWatchActive ||
      hiddenRevealActive || cameraActive || bleDetectActive ||
      probeLureActive || securityAuditActive || trackerScanActive ||
      harvesterActive || probeIntelActive || karmaWatchActive ||
      beaconWatchActive || authFloodActive) {
    if (command == 'h') {
      if (deauthAttackActive) stopDeauthAttack();
      if (deauthMonitorActive) stopDeauthMonitor();
      if (handshakeCaptureActive) stopHandshakeCapture();
      if (clientSnifferActive) stopClientSniffer();
      if (beaconFloodActive) stopBeaconFlood();
      if (evilPortalActive) stopEvilPortal();
      if (wardriveActive) stopWardrive();
      if (pktmonActive) stopPacketMon();
      if (wpsScanActive) stopWpsScan();
      if (rogueWatchActive) stopRogueWatch();
      if (hiddenRevealActive) stopHiddenReveal();
      if (cameraActive) stopCameraScan();
      if (bleDetectActive) stopBleDetect();
      if (probeLureActive) stopProbeLure();
      if (securityAuditActive) stopSecurityAudit();
      if (trackerScanActive) stopTrackerScan();
      if (harvesterActive) stopHarvester();
      if (probeIntelActive) stopProbeIntel();
      if (karmaWatchActive) stopKarmaWatch();
      if (beaconWatchActive) stopBeaconWatch();
      if (authFloodActive) stopAuthFlood();
      drawHome();
    }
    return;
  }
  if (command == 'm') startDeauthMonitor();
  if (command == 'p') startClientSniffer();
  if (command == 'k') startPacketMon();
  if (command == 'g') drawGps();
  if (command == 'u') {
    cycleGpsBaud();
    if (currentView == View::kGps) drawGps();
  }
  if (command == 'r') {
    gpsRawEcho = !gpsRawEcho;
    Serial.printf("\n[gps] raw echo %s\n", gpsRawEcho ? "ON" : "OFF");
  }
  if (command == 'w') scanWifi();
  if (command == 'c') {
    if (wifiCount) {
      drawChannelMap();
    } else {
      scanWifiForChannelMap();
    }
  }
  if (command == 'b') scanBle();
  if (command == 's') drawSavedNetworks();
  if (command == 'd') {
    initializeSdCard();
    if (sdReady) {
      lastSavedSdWriteOk = exportSavedNetworksToSd();
      if (wifiCount) lastScanSdWriteOk = exportWifiScanToSd();
      if (bleCount) lastBleScanSdWriteOk = exportBleScanToSd();
    }
    drawHome();
  }
  if (command == 'h') drawHome();
}

