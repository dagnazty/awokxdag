bool topoGraphMode = false;

constexpr uint32_t kTopoFreshMs = 3000;
constexpr uint32_t kTopoHideMs = 15000;
constexpr uint32_t kTopoAnchorSwitchMs = 5000;
constexpr int kTopoAnchorMarginDb = 7;
constexpr int kTopoMaxNodes = 12;
constexpr int kTopoTrackSlots = 32;
constexpr int kTopoGroupSlots = 12;

static const uint16_t kTopoPal[101] = {
  0x0049, 0x088B, 0x08AC, 0x08CD, 0x08EE, 0x090F, 0x0930, 0x0951, 0x0992, 0x09B2,
  0x09F3, 0x0A13, 0x1234, 0x1274, 0x1295, 0x12B5, 0x12F5, 0x1316, 0x1336, 0x1356,
  0x1396, 0x13B5, 0x13D5, 0x13F5, 0x1415, 0x1435, 0x1454, 0x1494, 0x14B4, 0x14D4,
  0x14F4, 0x1513, 0x1513, 0x1532, 0x1D32, 0x1D51, 0x1D50, 0x1D70, 0x1D6F, 0x1D8F,
  0x258E, 0x25AE, 0x25AD, 0x25CC, 0x25CC, 0x25EB, 0x2DEB, 0x2DEA, 0x35EA, 0x3E0A,
  0x4609, 0x4E09, 0x5608, 0x5608, 0x5E08, 0x6627, 0x6E27, 0x7627, 0x7E26, 0x7E26,
  0x8626, 0x8E25, 0x9645, 0x9E25, 0x9E24, 0xA624, 0xA604, 0xAE04, 0xADE4, 0xB5E4,
  0xB5E4, 0xBDC4, 0xC5C4, 0xC5A4, 0xCDA4, 0xCD84, 0xD584, 0xD584, 0xDD63, 0xDD63,
  0xE543, 0xE543, 0xED03, 0xECE3, 0xECA4, 0xEC84, 0xEC44, 0xEC24, 0xF404, 0xF3C4,
  0xF3A4, 0xF364, 0xF344, 0xF304, 0xF2E4, 0xFAC4, 0xFA84, 0xFA64, 0xFA24, 0xFA04,
  0xF9E5
};

static const int16_t kTopoSin[24] = {
  0, 66, 128, 181, 222, 247, 256, 247, 222, 181, 128, 66,
  0, -66, -128, -181, -222, -247, -256, -247, -222, -181, -128, -66
};

static int topoNodeX[kTopoMaxNodes];
static int topoNodeY[kTopoMaxNodes];
static uint8_t topoNodeBssid[kTopoMaxNodes][6];
static int topoNodeCount = 0;
static uint8_t topoSelBssid[6];
static bool topoHasSel = false;
static uint8_t topoTrackBssid[kTopoTrackSlots][6];
static int16_t topoTrackRssi[kTopoTrackSlots];
static uint32_t topoTrackSeen[kTopoTrackSlots];
static bool topoTrackValid[kTopoTrackSlots];
static char topoGroupName[kTopoGroupSlots][33];
static uint8_t topoGroupAnchor[kTopoGroupSlots][6];
static uint8_t topoGroupChallenger[kTopoGroupSlots][6];
static uint32_t topoGroupChallengerSince[kTopoGroupSlots];
static uint32_t topoGroupSeen[kTopoGroupSlots];
static bool topoGroupValid[kTopoGroupSlots];
static bool topoGroupChallengerValid[kTopoGroupSlots];

int topoSinT(int i) { return kTopoSin[((i % 24) + 24) % 24]; }
int topoCosT(int i) { return kTopoSin[(((i + 6) % 24) + 24) % 24]; }

int topoChanIdx(int ch) {
  if (ch <= 14) {
    int i = 13 + ((ch - 1) * 10) / 13;
    if (i > 23) i = 23;
    if (i < 13) i = 13;
    return i;
  }
  int i = 1 + (ch - 36) / 12;
  if (i > 11) i = 11;
  if (i < 1) i = 1;
  return i;
}

uint32_t topoGroupHash(const char* ssid, const uint8_t* bssid) {
  uint32_t h = 2166136261u;
  if (ssid && ssid[0]) {
    for (int i = 0; ssid[i] && i < 32; ++i) {
      h ^= (uint8_t)ssid[i];
      h *= 16777619u;
    }
  } else {
    for (int i = 0; i < 6; ++i) {
      h ^= bssid[i];
      h *= 16777619u;
    }
  }
  return h;
}

int topoGroupLane(const char* ssid, const uint8_t* bssid) {
  static const int lanes[7] = {0, 9, -9, 18, -18, 27, -27};
  return lanes[topoGroupHash(ssid, bssid) % 7];
}

uint16_t topoColor(int rssi, int minR, int span) {
  int lvl = (rssi - minR) * 100 / span;
  if (lvl < 0) lvl = 0;
  if (lvl > 100) lvl = 100;
  return kTopoPal[lvl];
}

uint16_t topoDim(uint16_t c, int pct) {
  int r = (c >> 11) & 0x1F;
  int g = (c >> 5) & 0x3F;
  int b = c & 0x1F;
  r = r * pct / 100;
  g = g * pct / 100;
  b = b * pct / 100;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

int topoFreshPct(uint32_t now, uint32_t lastSeen) {
  uint32_t age = now - lastSeen;
  if (age >= kTopoHideMs) return 0;
  if (age <= kTopoFreshMs) return 100;
  return 100 - (int)((age - kTopoFreshMs) * 70 / (kTopoHideMs - kTopoFreshMs));
}

int topoSmoothRssi(const uint8_t* bssid, int rssi, uint32_t now) {
  int freeSlot = -1;
  int oldestSlot = 0;
  uint32_t oldestSeen = 0xFFFFFFFFu;
  for (int i = 0; i < kTopoTrackSlots; ++i) {
    if (topoTrackValid[i]) {
      if (memcmp(topoTrackBssid[i], bssid, 6) == 0) {
        if (now - topoTrackSeen[i] > kTopoHideMs) topoTrackRssi[i] = rssi;
        else topoTrackRssi[i] = (int16_t)((topoTrackRssi[i] * 3 + rssi) / 4);
        topoTrackSeen[i] = now;
        return topoTrackRssi[i];
      }
      if (topoTrackSeen[i] < oldestSeen) {
        oldestSeen = topoTrackSeen[i];
        oldestSlot = i;
      }
    } else if (freeSlot < 0) {
      freeSlot = i;
    }
  }
  int slot = freeSlot >= 0 ? freeSlot : oldestSlot;
  topoTrackValid[slot] = true;
  memcpy(topoTrackBssid[slot], bssid, 6);
  topoTrackRssi[slot] = rssi;
  topoTrackSeen[slot] = now;
  return rssi;
}

int topoFindGroupState(const char* ssid, uint32_t now) {
  int freeSlot = -1;
  int oldestSlot = 0;
  uint32_t oldestSeen = 0xFFFFFFFFu;
  for (int i = 0; i < kTopoGroupSlots; ++i) {
    if (topoGroupValid[i]) {
      if (strncmp(topoGroupName[i], ssid, 32) == 0) {
        topoGroupSeen[i] = now;
        return i;
      }
      if (topoGroupSeen[i] < oldestSeen) {
        oldestSeen = topoGroupSeen[i];
        oldestSlot = i;
      }
    } else if (freeSlot < 0) {
      freeSlot = i;
    }
  }
  int slot = freeSlot >= 0 ? freeSlot : oldestSlot;
  topoGroupValid[slot] = true;
  strncpy(topoGroupName[slot], ssid, 32);
  topoGroupName[slot][32] = '\0';
  memset(topoGroupAnchor[slot], 0, 6);
  memset(topoGroupChallenger[slot], 0, 6);
  topoGroupChallengerSince[slot] = 0;
  topoGroupSeen[slot] = now;
  topoGroupChallengerValid[slot] = false;
  return slot;
}

int topoRadiusForRssi(int rssi) {
  int loss = -rssi;
  int rr = 30 + (loss - 35) * 66 / 60;
  if (rr < 30) rr = 30;
  if (rr > 96) rr = 96;
  return rr;
}

void topoGraphReset() {
  topoHasSel = false;
  topoNodeCount = 0;
  for (int i = 0; i < kTopoTrackSlots; ++i) topoTrackValid[i] = false;
  for (int i = 0; i < kTopoGroupSlots; ++i) {
    topoGroupValid[i] = false;
    topoGroupChallengerValid[i] = false;
  }
}

const char* topoSecurityLabel(uint8_t security) {
  switch (security) {
    case kTopoSecOpen: return "Open";
    case kTopoSecWep: return "WEP";
    case kTopoSecWpa: return "WPA";
    case kTopoSecWpa2: return "WPA2";
    case kTopoSecWpa23: return "WPA2/3";
    case kTopoSecWpa3: return "WPA3";
    default: return "Unknown";
  }
}

bool topoSecurityWarn(uint8_t security) {
  return security == kTopoSecOpen || security == kTopoSecWep || security == kTopoSecWpa;
}

bool topoGraphTap(int x, int y) {
  int best = -1;
  int bestD2 = 23 * 23 + 1;
  for (int i = 0; i < topoNodeCount; ++i) {
    int dx = x - topoNodeX[i];
    int dy = y - topoNodeY[i];
    int d2 = dx * dx + dy * dy;
    if (d2 <= 23 * 23 && d2 < bestD2) {
      best = i;
      bestD2 = d2;
    }
  }
  if (best >= 0) {
    memcpy(topoSelBssid, topoNodeBssid[best], 6);
    topoHasSel = true;
    return true;
  }
  topoHasSel = false;
  return false;
}

void drawTopoPopup(int idx) {
  const TopoAp& ap = topoAps[idx];
  // Design-grid overlay; scaled so it sits below the header and fills the width.
  const int bx = 6;
  const int by = 44;
  const int bw = 228;
  const int bh = 58;
  display.fillRect(scaleX(bx), scaleY(by), scaleX(bw), scaleY(bh), kPanel);
  display.drawRect(scaleX(bx), scaleY(by), scaleX(bw), scaleY(bh), kAccent);
  display.setTextColor(kAccent, kPanel);
  display.setCursor(scaleX(bx + 4), scaleY(by + 3));
  display.print(ap.ssid[0] ? clipped(ap.ssid, 36) : String("<hidden>"));
  display.setTextColor(kForeground, kPanel);
  display.setCursor(scaleX(bx + 4), scaleY(by + 15));
  display.print(macToString(ap.bssid));
  display.setCursor(scaleX(bx + 4), scaleY(by + 27));
  display.print("ch ");
  display.print(ap.channel);
  display.print("  ");
  display.print(ap.channel <= 14 ? "2.4GHz" : "5GHz");
  display.print("  ");
  display.print(ap.rssi);
  display.print("dBm");
  display.setCursor(scaleX(bx + 4), scaleY(by + 39));
  display.setTextColor(topoSecurityWarn(ap.security) ? kWarn : kMuted, kPanel);
  display.print(topoSecurityLabel(ap.security));
  display.setTextColor(kMuted, kPanel);
  display.print("   ");
  display.print(ap.clientCount);
  display.print(" cli");
  display.setCursor(scaleX(bx + 4), scaleY(by + 50));
  display.print("tap away to clear");
}

void drawTopologyGraphBody() {
  const int cx = kScreenWidth / 2;
  const int cyTop = kHeaderHeight;
  const int cyBot = kFooterTop;
  const int cy = (cyTop + cyBot) / 2;
  const uint32_t now = millis();

  display.fillRect(0, cyTop, kScreenWidth, cyBot - cyTop, kBackground);
  display.drawCircle(cx, cy, 48, topoDim(kMuted, 22));
  display.drawCircle(cx, cy, 95, topoDim(kMuted, 22));

  int smoothByAp[kMaxTopoAps];
  for (int i = 0; i < topoApCount; ++i) {
    if (topoFreshPct(now, topoAps[i].lastSeenMs) > 0) smoothByAp[i] = topoSmoothRssi(topoAps[i].bssid, topoAps[i].rssi, now);
    else smoothByAp[i] = topoAps[i].rssi;
  }

  int order[kMaxTopoAps];
  int n = topoApCount;
  for (int i = 0; i < n; ++i) order[i] = i;
  for (int i = 0; i < n - 1; ++i) {
    for (int j = i + 1; j < n; ++j) {
      if (smoothByAp[order[j]] > smoothByAp[order[i]]) {
        int t = order[i];
        order[i] = order[j];
        order[j] = t;
      }
    }
  }

  int shownIdx[kTopoMaxNodes];
  int shownRssi[kTopoMaxNodes];
  int shownN = 0;
  for (int k = 0; k < n && shownN < kTopoMaxNodes; ++k) {
    if (topoFreshPct(now, topoAps[order[k]].lastSeenMs) <= 0) continue;
    shownIdx[shownN] = order[k];
    shownRssi[shownN] = smoothByAp[order[k]];
    ++shownN;
  }

  int minR = -95;
  int maxR = -35;
  if (shownN > 0) {
    minR = shownRssi[0];
    maxR = shownRssi[0];
    for (int s = 1; s < shownN; ++s) {
      if (shownRssi[s] < minR) minR = shownRssi[s];
      if (shownRssi[s] > maxR) maxR = shownRssi[s];
    }
  }
  int span = maxR - minR;
  if (span < 20) span = 20;

  int groupOf[kTopoMaxNodes];
  int groupFirst[kTopoMaxNodes];
  int groupAnchorPos[kTopoMaxNodes];
  int groupCount = 0;
  for (int s = 0; s < shownN; ++s) {
    groupOf[s] = -1;
    const char* ssid = topoAps[shownIdx[s]].ssid;
    if (ssid[0]) {
      for (int g = 0; g < groupCount; ++g) {
        const char* other = topoAps[shownIdx[groupFirst[g]]].ssid;
        if (other[0] && strncmp(ssid, other, 32) == 0) {
          groupOf[s] = g;
          break;
        }
      }
    }
    if (groupOf[s] < 0) {
      groupOf[s] = groupCount;
      groupFirst[groupCount] = s;
      groupAnchorPos[groupCount] = s;
      ++groupCount;
    }
  }

  for (int g = 0; g < groupCount; ++g) {
    int strongest = -1;
    for (int s = 0; s < shownN; ++s) {
      if (groupOf[s] != g) continue;
      if (strongest < 0 || shownRssi[s] > shownRssi[strongest]) strongest = s;
    }
    if (strongest < 0) continue;
    const char* ssid = topoAps[shownIdx[groupFirst[g]]].ssid;
    if (!ssid[0]) {
      groupAnchorPos[g] = strongest;
      continue;
    }
    int state = topoFindGroupState(ssid, now);
    int current = -1;
    for (int s = 0; s < shownN; ++s) {
      if (groupOf[s] == g && memcmp(topoAps[shownIdx[s]].bssid, topoGroupAnchor[state], 6) == 0) {
        current = s;
        break;
      }
    }
    if (current < 0) {
      memcpy(topoGroupAnchor[state], topoAps[shownIdx[strongest]].bssid, 6);
      topoGroupChallengerValid[state] = false;
      groupAnchorPos[g] = strongest;
      continue;
    }
    groupAnchorPos[g] = current;
    if (strongest != current && shownRssi[strongest] >= shownRssi[current] + kTopoAnchorMarginDb) {
      if (!topoGroupChallengerValid[state] || memcmp(topoGroupChallenger[state], topoAps[shownIdx[strongest]].bssid, 6) != 0) {
        memcpy(topoGroupChallenger[state], topoAps[shownIdx[strongest]].bssid, 6);
        topoGroupChallengerSince[state] = now;
        topoGroupChallengerValid[state] = true;
      } else if (now - topoGroupChallengerSince[state] >= kTopoAnchorSwitchMs) {
        memcpy(topoGroupAnchor[state], topoGroupChallenger[state], 6);
        topoGroupChallengerValid[state] = false;
        groupAnchorPos[g] = strongest;
      }
    } else {
      topoGroupChallengerValid[state] = false;
    }
  }

  int posX[kTopoMaxNodes];
  int posY[kTopoMaxNodes];
  int posAngle[kTopoMaxNodes];
  int nodeRadius[kTopoMaxNodes];
  uint16_t nodeHeat[kTopoMaxNodes];

  for (int s = 0; s < shownN; ++s) {
    const TopoAp& ap = topoAps[shownIdx[s]];
    int pct = topoFreshPct(now, ap.lastSeenMs);
    int g = groupOf[s];
    int anchorS = groupAnchorPos[g];
    const TopoAp& anchor = topoAps[shownIdx[anchorS]];
    int baseIdx = topoChanIdx(anchor.channel);
    int anchorR = topoRadiusForRssi(shownRssi[anchorS]);
    int rr = anchorR;
    int idx = baseIdx;

    if (s != anchorS) {
      int rank = 1;
      for (int s2 = 0; s2 < shownN; ++s2) {
        if (s2 == anchorS || groupOf[s2] != g || s2 == s) continue;
        const TopoAp& other = topoAps[shownIdx[s2]];
        if (other.channel < ap.channel ||
            (other.channel == ap.channel && memcmp(other.bssid, ap.bssid, 6) < 0)) {
          ++rank;
        }
      }
      int arcStep = (rank + 1) / 2;
      int arcDir = (rank & 1) ? -1 : 1;
      idx = baseIdx + arcDir * arcStep;
      int ownR = topoRadiusForRssi(shownRssi[s]);
      rr = (anchorR * 3 + ownR) / 4;
    }

    int groupOff = topoGroupLane(anchor.ssid, anchor.bssid);
    int bx = cx + (topoCosT(idx) * rr) / 256;
    int by = cy + (topoSinT(idx) * rr) / 256;
    int ax = bx + (-topoSinT(baseIdx) * groupOff) / 256;
    int ay = by + (topoCosT(baseIdx) * groupOff) / 256;
    if (ax < 9) ax = 9;
    if (ax > kScreenWidth - 9) ax = kScreenWidth - 9;
    if (ay < cyTop + 11) ay = cyTop + 11;
    if (ay > cyBot - 13) ay = cyBot - 13;

    posX[s] = ax;
    posY[s] = ay;
    posAngle[s] = idx;
    nodeRadius[s] = 5 + (ap.clientCount > 3 ? 3 : ap.clientCount);
    nodeHeat[s] = topoDim(topoColor(shownRssi[s], minR, span), pct);
  }

  for (int g = 0; g < groupCount; ++g) {
    int anchorS = groupAnchorPos[g];
    if (anchorS < 0 || anchorS >= shownN) continue;
    display.drawLine(cx, cy, posX[anchorS], posY[anchorS], topoDim(nodeHeat[anchorS], 55));
    for (int s = 0; s < shownN; ++s) {
      if (groupOf[s] != g || s == anchorS) continue;
      display.drawLine(posX[anchorS], posY[anchorS], posX[s], posY[s], topoDim(nodeHeat[s], 62));
    }
  }

  topoNodeCount = 0;
  for (int s = 0; s < shownN; ++s) {
    const TopoAp& ap = topoAps[shownIdx[s]];
    int pct = topoFreshPct(now, ap.lastSeenMs);
    int g = groupOf[s];
    int anchorS = groupAnchorPos[g];
    int ax = posX[s];
    int ay = posY[s];
    int idx = posAngle[s];
    uint16_t heat = nodeHeat[s];

    int drawn = 0;
    static const int coff[4] = {-2, -1, 1, 2};
    for (int c = 0; c < topoClientCount && drawn < 4; ++c) {
      if (!topoClients[c].hasBssid) continue;
      if (memcmp(topoClients[c].bssid, ap.bssid, 6) != 0) continue;
      int cpct = topoFreshPct(now, topoClients[c].lastSeenMs);
      if (cpct <= 0) continue;
      int cidx = idx + coff[drawn];
      int cxx = ax + (topoCosT(cidx) * 13) / 256;
      int cyy = ay + (topoSinT(cidx) * 13) / 256;
      if (cxx < 3) cxx = 3;
      if (cxx > kScreenWidth - 3) cxx = kScreenWidth - 3;
      if (cyy < cyTop + 3) cyy = cyTop + 3;
      if (cyy > cyBot - 3) cyy = cyBot - 3;
      uint16_t chc = topoDim(topoColor(topoClients[c].rssi, minR, span), cpct);
      display.drawLine(ax, ay, cxx, cyy, topoDim(chc, 65));
      display.fillCircle(cxx, cyy, 2, chc);
      ++drawn;
    }

    int nodeR = nodeRadius[s];
    display.fillCircle(ax, ay, nodeR, heat);
    if (ap.isOpen) display.drawCircle(ax, ay, nodeR + 2, topoDim(kWarn, pct));
    if (topoHasSel && memcmp(topoSelBssid, ap.bssid, 6) == 0) display.drawCircle(ax, ay, nodeR + 4, kForeground);

    if (s == anchorS) {
      String label = ap.ssid[0] ? clipped(ap.ssid, 10) : String("<hdn>");
      int lw = label.length() * 6;
      int dx = ax - cx;
      int dy = ay - cy;
      int lx = ax - lw / 2;
      int ly = ay - 3;
      if (abs(dx) >= abs(dy)) {
        lx = dx < 0 ? ax - nodeR - lw - 4 : ax + nodeR + 4;
        ly = ay - 3;
      } else {
        lx = ax - lw / 2;
        ly = dy < 0 ? ay - nodeR - 11 : ay + nodeR + 3;
      }
      if (lx < 1) lx = 1;
      if (lx + lw > kScreenWidth - 1) lx = kScreenWidth - 1 - lw;
      if (ly < cyTop + 1) ly = cyTop + 1;
      if (ly > cyBot - 9) ly = cyBot - 9;
      display.fillRect(lx - 1, ly - 1, lw + 2, 10, kBackground);
      display.setTextColor(topoDim(ap.isOpen ? kWarn : kForeground, pct), kBackground);
      display.setCursor(lx, ly);
      display.print(label);
    }

    topoNodeX[topoNodeCount] = ax;
    topoNodeY[topoNodeCount] = ay;
    memcpy(topoNodeBssid[topoNodeCount], ap.bssid, 6);
    ++topoNodeCount;
  }

  if (topoNodeCount == 0) {
    display.setTextColor(kMuted, kBackground);
    display.setCursor(28, cy - 4);
    display.print("Sniffing links...");
  }

  display.fillCircle(cx, cy, 5, kAccent);
  display.drawCircle(cx, cy, 8, kAccent);
  display.setTextColor(kAccent, kBackground);
  display.setCursor(cx - 9, cy + 10);
  display.print("YOU");

  if (topoHasSel) {
    int si = -1;
    for (int i = 0; i < topoApCount; ++i) {
      if (memcmp(topoAps[i].bssid, topoSelBssid, 6) == 0) {
        si = i;
        break;
      }
    }
    if (si < 0 || topoFreshPct(now, topoAps[si].lastSeenMs) <= 0) topoHasSel = false;
    else drawTopoPopup(si);
  }
}
