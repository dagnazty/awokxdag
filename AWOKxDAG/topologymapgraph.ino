bool topoGraphMode = false;

constexpr uint32_t kTopoFreshMs = 3000;
constexpr uint32_t kTopoHideMs = 15000;
constexpr uint32_t kTopoAnchorSwitchMs = 5000;
constexpr int kTopoAnchorMarginDb = 7;
constexpr int kTopoMaxNodes = 12;
constexpr int kTopoTrackSlots = 32;
constexpr int kTopoGroupSlots = 12;
constexpr int kTopoPopupX = 6;
constexpr int kTopoPopupY = 44;
constexpr int kTopoPopupW = 228;
constexpr int kTopoPopupH = 58;

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
  display.fillRect(kTopoPopupX, kTopoPopupY, kTopoPopupW, kTopoPopupH, kPanel);
  display.drawRect(kTopoPopupX, kTopoPopupY, kTopoPopupW, kTopoPopupH, kAccent);
  display.setTextColor(kAccent, kPanel);
  display.setCursor(kTopoPopupX + 4, kTopoPopupY + 3);
  display.print(ap.ssid[0] ? clipped(ap.ssid, 36) : String("<hidden>"));
  display.setTextColor(kForeground, kPanel);
  display.setCursor(kTopoPopupX + 4, kTopoPopupY + 15);
  display.print(macToString(ap.bssid));
  display.setCursor(kTopoPopupX + 4, kTopoPopupY + 27);
  display.print("ch ");
  display.print(ap.channel);
  display.print("  ");
  display.print(ap.channel <= 14 ? "2.4GHz" : "5GHz");
  display.print("  ");
  display.print(ap.rssi);
  display.print("dBm");
  display.setCursor(kTopoPopupX + 4, kTopoPopupY + 39);
  display.setTextColor(topoSecurityWarn(ap.security) ? kWarn : kMuted, kPanel);
  display.print(topoSecurityLabel(ap.security));
  display.setTextColor(kMuted, kPanel);
  display.print("   ");
  display.print(ap.clientCount);
  display.print(" cli");
  display.setCursor(kTopoPopupX + 4, kTopoPopupY + 50);
  display.print("tap away to clear");
}

int topoRectOverlapArea(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
  int x0 = ax > bx ? ax : bx;
  int y0 = ay > by ? ay : by;
  int x1 = ax + aw < bx + bw ? ax + aw : bx + bw;
  int y1 = ay + ah < by + bh ? ay + ah : by + bh;
  if (x1 <= x0 || y1 <= y0) return 0;
  return (x1 - x0) * (y1 - y0);
}

void topoRelaxPositions(int* posX, int* posY, const int* idealX, const int* idealY, const int* nodeRadius, const bool* nodeAnchor, int shownN, int cx, int cy, int cyTop, int cyBot) {
  for (int pass = 0; pass < 8; ++pass) {
    for (int i = 0; i < shownN; ++i) {
      for (int j = i + 1; j < shownN; ++j) {
        int dx = posX[i] - posX[j];
        int dy = posY[i] - posY[j];
        int adx = abs(dx);
        int ady = abs(dy);
        int dist = (adx > ady ? adx : ady) + ((adx < ady ? adx : ady) >> 1);
        int need = nodeRadius[i] + nodeRadius[j] + 9;
        if (dist >= need) continue;
        if (dist == 0) {
          dx = ((i + j) & 1) ? 1 : -1;
          dy = ((i * 3 + j) & 1) ? 1 : -1;
          dist = 1;
        }
        int overlap = need - dist + 1;
        int mi = nodeAnchor[i] ? 1 : 3;
        int mj = nodeAnchor[j] ? 1 : 3;
        int total = mi + mj;
        int pi = overlap * mi / total;
        int pj = overlap - pi;
        if (pi < 1) pi = 1;
        if (pj < 1) pj = 1;
        int ux = dx * 256 / dist;
        int uy = dy * 256 / dist;
        posX[i] += ux * pi / 256;
        posY[i] += uy * pi / 256;
        posX[j] -= ux * pj / 256;
        posY[j] -= uy * pj / 256;
      }
    }

    for (int i = 0; i < shownN; ++i) {
      int dx = posX[i] - cx;
      int dy = posY[i] - cy;
      int adx = abs(dx);
      int ady = abs(dy);
      int dist = (adx > ady ? adx : ady) + ((adx < ady ? adx : ady) >> 1);
      int need = nodeRadius[i] + 15;
      if (dist < need) {
        if (dist == 0) {
          dx = (i & 1) ? 1 : -1;
          dy = (i & 2) ? 1 : -1;
          dist = 1;
        }
        int push = need - dist + 1;
        posX[i] += dx * push / dist;
        posY[i] += dy * push / dist;
      }

      if (pass < 6) {
        int spring = nodeAnchor[i] ? 5 : 9;
        posX[i] += (idealX[i] - posX[i]) / spring;
        posY[i] += (idealY[i] - posY[i]) / spring;
      }

      if (topoHasSel) {
        int m = nodeRadius[i] + 5;
        if (posX[i] + m > kTopoPopupX && posX[i] - m < kTopoPopupX + kTopoPopupW &&
            posY[i] + m > kTopoPopupY && posY[i] - m < kTopoPopupY + kTopoPopupH) {
          posY[i] = kTopoPopupY + kTopoPopupH + m;
        }
      }

      int edge = nodeRadius[i] + 3;
      if (posX[i] < edge) posX[i] = edge;
      if (posX[i] > kScreenWidth - edge) posX[i] = kScreenWidth - edge;
      if (posY[i] < cyTop + edge) posY[i] = cyTop + edge;
      if (posY[i] > cyBot - edge) posY[i] = cyBot - edge;
    }
  }
}

bool topoPositionClear(int x, int y, int self, const int* posX, const int* posY, const int* nodeRadius, int shownN, int cx, int cy, int cyTop, int cyBot) {
  int edge = nodeRadius[self] + 3;
  if (x < edge || x > kScreenWidth - edge || y < cyTop + edge || y > cyBot - edge) return false;
  int dx = x - cx;
  int dy = y - cy;
  int adx = abs(dx);
  int ady = abs(dy);
  int dist = (adx > ady ? adx : ady) + ((adx < ady ? adx : ady) >> 1);
  if (dist < nodeRadius[self] + 15) return false;
  if (topoHasSel) {
    int m = nodeRadius[self] + 5;
    if (x + m > kTopoPopupX && x - m < kTopoPopupX + kTopoPopupW &&
        y + m > kTopoPopupY && y - m < kTopoPopupY + kTopoPopupH) return false;
  }
  for (int j = 0; j < shownN; ++j) {
    if (j == self) continue;
    int px = x - posX[j];
    int py = y - posY[j];
    int apx = abs(px);
    int apy = abs(py);
    int pd = (apx > apy ? apx : apy) + ((apx < apy ? apx : apy) >> 1);
    if (pd < nodeRadius[self] + nodeRadius[j] + 9) return false;
  }
  return true;
}

void topoHardSeparate(int* posX, int* posY, const int* nodeRadius, const bool* nodeAnchor, int shownN, int cx, int cy, int cyTop, int cyBot) {
  for (int phase = 0; phase < 2; ++phase) {
    for (int i = 0; i < shownN; ++i) {
      if ((phase == 0 && nodeAnchor[i]) || (phase == 1 && !nodeAnchor[i])) continue;
      if (topoPositionClear(posX[i], posY[i], i, posX, posY, nodeRadius, shownN, cx, cy, cyTop, cyBot)) continue;
      int ox = posX[i];
      int oy = posY[i];
      bool found = false;
      for (int ring = 2; ring <= 48 && !found; ring += 2) {
        for (int d = 0; d < 24; ++d) {
          int x = ox + topoCosT(d) * ring / 256;
          int y = oy + topoSinT(d) * ring / 256;
          if (!topoPositionClear(x, y, i, posX, posY, nodeRadius, shownN, cx, cy, cyTop, cyBot)) continue;
          posX[i] = x;
          posY[i] = y;
          found = true;
          break;
        }
      }
    }
  }
}

int topoLabelPenalty(int lx, int ly, int lw, int lh, int self, const int* posX, const int* posY, const int* nodeRadius, int shownN, const int* labelX, const int* labelY, const int* labelW, const bool* labelPlaced, int cy) {
  int penalty = 0;
  for (int i = 0; i < shownN; ++i) {
    int pad = nodeRadius[i] + 2;
    int area = topoRectOverlapArea(lx, ly, lw, lh, posX[i] - pad, posY[i] - pad, pad * 2 + 1, pad * 2 + 1);
    if (area > 0) penalty += area * 8 + (i == self ? 400 : 1200);
  }
  for (int i = 0; i < shownN; ++i) {
    if (!labelPlaced[i]) continue;
    int area = topoRectOverlapArea(lx, ly, lw, lh, labelX[i] - 1, labelY[i] - 1, labelW[i] + 2, 12);
    if (area > 0) penalty += area * 20 + 5000;
  }
  int youArea = topoRectOverlapArea(lx, ly, lw, lh, kScreenWidth / 2 - 15, cy - 10, 30, 31);
  if (youArea > 0) penalty += youArea * 20 + 5000;
  if (topoHasSel) {
    int area = topoRectOverlapArea(lx, ly, lw, lh, kTopoPopupX - 1, kTopoPopupY - 1, kTopoPopupW + 2, kTopoPopupH + 2);
    if (area > 0) penalty += area * 30 + 12000;
  }
  return penalty;
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
  int idealX[kTopoMaxNodes];
  int idealY[kTopoMaxNodes];
  int posAngle[kTopoMaxNodes];
  int nodeRadius[kTopoMaxNodes];
  uint16_t nodeHeat[kTopoMaxNodes];
  bool nodeAnchor[kTopoMaxNodes];

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
    idealX[s] = ax;
    idealY[s] = ay;
    posAngle[s] = idx;
    nodeRadius[s] = 5 + (ap.clientCount > 3 ? 3 : ap.clientCount);
    nodeHeat[s] = topoDim(topoColor(shownRssi[s], minR, span), pct);
    nodeAnchor[s] = s == anchorS;
  }

  topoRelaxPositions(posX, posY, idealX, idealY, nodeRadius, nodeAnchor, shownN, cx, cy, cyTop, cyBot);
  topoHardSeparate(posX, posY, nodeRadius, nodeAnchor, shownN, cx, cy, cyTop, cyBot);

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

    topoNodeX[topoNodeCount] = ax;
    topoNodeY[topoNodeCount] = ay;
    memcpy(topoNodeBssid[topoNodeCount], ap.bssid, 6);
    ++topoNodeCount;
  }

  int labelX[kTopoMaxNodes];
  int labelY[kTopoMaxNodes];
  int labelW[kTopoMaxNodes];
  bool labelPlaced[kTopoMaxNodes];
  for (int s = 0; s < shownN; ++s) labelPlaced[s] = false;

  for (int s = 0; s < shownN; ++s) {
    int g = groupOf[s];
    if (s != groupAnchorPos[g]) continue;
    const TopoAp& ap = topoAps[shownIdx[s]];
    int pct = topoFreshPct(now, ap.lastSeenMs);
    String label = ap.ssid[0] ? clipped(ap.ssid, 10) : String("<hdn>");
    int lw = label.length() * 6;
    int lh = 10;
    int ax = posX[s];
    int ay = posY[s];
    int r = nodeRadius[s];
    int candX[8] = {
      ax + r + 4,
      ax - r - 4 - lw,
      ax - lw / 2,
      ax - lw / 2,
      ax + r + 3,
      ax - r - 3 - lw,
      ax + r + 3,
      ax - r - 3 - lw
    };
    int candY[8] = {
      ay - 4,
      ay - 4,
      ay - r - 11,
      ay + r + 3,
      ay - r - 10,
      ay - r - 10,
      ay + r + 2,
      ay + r + 2
    };
    int best = 0;
    int bestPenalty = 0x7FFFFFFF;
    int bestDot = -0x7FFFFFFF;
    int dx = ax - cx;
    int dy = ay - cy;
    for (int c = 0; c < 8; ++c) {
      int lx = candX[c];
      int ly = candY[c];
      int clampPenalty = 0;
      if (lx < 1) {
        clampPenalty += 1 - lx;
        lx = 1;
      }
      if (lx + lw > kScreenWidth - 1) {
        clampPenalty += lx + lw - (kScreenWidth - 1);
        lx = kScreenWidth - 1 - lw;
      }
      if (ly < cyTop + 1) {
        clampPenalty += cyTop + 1 - ly;
        ly = cyTop + 1;
      }
      if (ly > cyBot - lh) {
        clampPenalty += ly - (cyBot - lh);
        ly = cyBot - lh;
      }
      int penalty = topoLabelPenalty(lx, ly, lw, lh, s, posX, posY, nodeRadius, shownN, labelX, labelY, labelW, labelPlaced, cy) + clampPenalty * 200;
      int ccx = lx + lw / 2;
      int ccy = ly + lh / 2;
      int dot = (ccx - ax) * dx + (ccy - ay) * dy;
      if (penalty < bestPenalty || (penalty == bestPenalty && dot > bestDot)) {
        best = c;
        bestPenalty = penalty;
        bestDot = dot;
      }
    }
    int lx = candX[best];
    int ly = candY[best];
    if (lx < 1) lx = 1;
    if (lx + lw > kScreenWidth - 1) lx = kScreenWidth - 1 - lw;
    if (ly < cyTop + 1) ly = cyTop + 1;
    if (ly > cyBot - lh) ly = cyBot - lh;
    labelX[s] = lx;
    labelY[s] = ly;
    labelW[s] = lw;
    labelPlaced[s] = true;
    display.fillRect(lx - 1, ly - 1, lw + 2, lh, kBackground);
    display.setTextColor(topoDim(ap.isOpen ? kWarn : kForeground, pct), kBackground);
    display.setCursor(lx, ly);
    display.print(label);
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
