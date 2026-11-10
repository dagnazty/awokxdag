#pragma once
// Waveshare ESP32-C5-Touch-LCD-3.5 board support: shared-I2C discipline, the
// CH32V006 I/O expander (panel power / resets / backlight PWM), the LC76G GPS
// over I2C, and the AXP2101 fuel gauge over raw I2C. Every sequence here mirrors
// the hardware-verified bring-up in the BSP (slickrick773/ESP32-C5-BSP,
// src/board/ws_c5_35 + src/chip/esp32c5_i2c_bus) and GhostESP #433's
// ws_c5_exio.c. See docs/waveshare-c5-35.md.
//
// AxD's main loop is single-threaded, so every I2C owner (touch, GPS, battery)
// runs from loop() and is serialised for free -- there is no bus mutex here (the
// BSP adds one only for multi-task apps). What this board still needs from the
// C5's shared bus are two things the findings proved on hardware:
//   1. A per-transaction bus re-open. The C5's I2C controller, once a sketch has
//      called Wire.begin() early, leaves subsequent READS unable to complete
//      (writes ACK, reads return 0) until Wire.end()/Wire.begin(). Do it ONCE at
//      the top of a read transaction -- never per retry (that was the fleet's
//      Phase-1 touch "stiffness": three re-opens per read).
//   2. A saved/restored Wire timeout. The GPS needs a long (~300 ms) timeout for
//      the module's clock-stretch; leaking it onto the short touch reads that
//      follow makes them sluggish, so it is restored at the end of the cycle.
#ifdef AWOK_WAVESHARE_C5_35
#include <Arduino.h>
#include <Wire.h>

namespace WsC5 {

// ---- Shared I2C bus --------------------------------------------------------
static void busBegin() {
  Wire.begin(AwokPins::kI2cSda, AwokPins::kI2cScl, AwokPins::kI2cHz);
}

// C5 read quirk (sharp edge 1 above). Call once at the top of a read cycle.
static void busReopen() {
  Wire.end();
  Wire.begin(AwokPins::kI2cSda, AwokPins::kI2cScl, AwokPins::kI2cHz);
}

// RAII Wire-timeout override (sharp edge 2 above): set a transaction timeout and
// restore the previous one on scope exit.
class TimeoutGuard {
 public:
  explicit TimeoutGuard(uint16_t ms) : saved_(Wire.getTimeOut()) { Wire.setTimeOut(ms); }
  ~TimeoutGuard() { Wire.setTimeOut(saved_); }
  TimeoutGuard(const TimeoutGuard&) = delete;
  TimeoutGuard& operator=(const TimeoutGuard&) = delete;
 private:
  uint16_t saved_;
};

// ---- CH32V006 I/O expander (0x24) ------------------------------------------
// Owns the FT6336 reset (EXIO0), the ST7796 reset (EXIO1) and panel power /
// backlight (EXIO5 + a PWM register). The byte sequence is the HW-verified one;
// the EXIO bit assignments are left exactly as the working driver has them. (The
// Waveshare schematic labels EXIO0/EXIO1 the other way round, but the driver is
// verified as written, so the names are functional, not authoritative -- see the
// BSP HARDWARE-FINDINGS.)
namespace Expander {
static constexpr uint8_t kAddr    = 0x24;
static constexpr uint8_t kModeReg = 0x02;  // direction / mode
static constexpr uint8_t kOutReg  = 0x03;  // output latch
static constexpr uint8_t kPwmReg  = 0x05;  // backlight PWM
static constexpr uint8_t kTpRst   = 0;     // EXIO0 -> FT6336 reset
static constexpr uint8_t kLcdRst  = 1;     // EXIO1 -> ST7796 reset
static constexpr uint8_t kPower   = 5;     // EXIO5 -> panel power / backlight

static bool    s_ready = false;
static uint8_t s_out   = 0;

static bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kAddr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}
static bool setBit(uint8_t bit, bool level) {
  if (level) s_out |= (uint8_t)(1u << bit);
  else       s_out &= (uint8_t)~(1u << bit);
  return writeReg(kOutReg, s_out);
}

// Power the panel and pulse both resets with the rail up. Runs once: a second
// LCD reset after the ST7796 is initialised would blank the screen.
static bool begin() {
  if (s_ready) return true;
  const uint8_t used = (uint8_t)((1u << kTpRst) | (1u << kLcdRst) | (1u << kPower));
  bool ok = true;
  // Keep the microSD deselected while the panel comes up on the shared SPI bus.
  pinMode(AwokPins::kSdCs, OUTPUT);
  digitalWrite(AwokPins::kSdCs, HIGH);
  ok &= writeReg(kModeReg, 0xFF);
  ok &= writeReg(kOutReg, 0x00);
  ok &= writeReg(kPwmReg, 0x00);
  ok &= writeReg(kModeReg, used);
  // Bring the rail up together with both resets released; pulsing the touch
  // reset while its rail is off leaves the FT6336 unresponsive.
  s_out = (uint8_t)((1u << kTpRst) | (1u << kLcdRst)); ok &= writeReg(kOutReg, s_out); delay(50);
  s_out = 0x00;                                        ok &= writeReg(kOutReg, s_out); delay(50);
  s_out = used;                                        ok &= writeReg(kOutReg, s_out); delay(50);
  // Power is on; pulse each reset.
  ok &= setBit(kLcdRst, false); delay(20);
  ok &= setBit(kLcdRst, true);  delay(120);
  ok &= setBit(kTpRst, false);  delay(20);
  ok &= setBit(kTpRst, true);   delay(300);  // FT6336 boot
  // The backlight stays off here (PWM was zeroed above). Lighting it before the
  // first frame shows the panel's own memory: static after power-on, the
  // previous picture after reset. setup() calls allowBacklight() once the first
  // frame is on the glass.
  s_ready = ok;
  Serial.printf("[ws-c5] expander 0x24 %s\n", ok ? "ready" : "FAILED");
  return ok;
}

static bool s_lightAllowed = false;

// Call once, after the first full frame has been pushed to the panel.
static void allowBacklight() { s_lightAllowed = true; }
static bool backlightAllowed() { return s_lightAllowed; }

// Backlight duty, 0 (off) .. 255 (full). No-op until begin() has run and the
// first frame has been drawn (allowBacklight()).
static void backlight(uint8_t duty) {
  if (!s_ready || !s_lightAllowed) return;
  writeReg(kPwmReg, duty);
}
}  // namespace Expander

// ---- LC76G GPS over I2C (0x50 / 0x54 / 0x58) -------------------------------
// Waveshare's LC76G_RPI_I2C protocol, ported onto the shared bus. The whole
// multi-frame cycle runs synchronously from loop(), which gives the "no other
// slave addressed mid-transaction" guarantee Quectel requires for free. Each
// NMEA byte is handed to the caller's sink (gps.ino feeds TinyGPSPlus).
namespace Gps {
static constexpr uint8_t  kCmdAddr  = 0x50;  // command / register write
static constexpr uint8_t  kRdAddr   = 0x54;  // data read
static constexpr uint8_t  kWrAddr   = 0x58;  // data write (wake)
static constexpr uint32_t kChunk    = 1024;  // bytes per cycle; bounds the hitch
static constexpr uint32_t kPeriodMs = 500;   // min gap between cycles (~2 Hz)
static constexpr uint32_t kStepMs   = 3;     // inter-step settle (demo uses 10)

static void put32le(uint8_t* b, uint32_t v) {
  b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24);
}
static uint8_t writeCr(uint32_t reg, uint32_t len) {  // 0 = ok
  uint8_t b[8]; put32le(b, reg); put32le(b + 4, len);
  Wire.beginTransmission(kCmdAddr); Wire.write(b, 8); return Wire.endTransmission();
}
static size_t readData(uint8_t* buf, size_t n) {
  size_t got = Wire.requestFrom((uint16_t)kRdAddr, (size_t)n);
  for (size_t i = 0; i < got; i++) buf[i] = Wire.read();
  return got;
}
static uint8_t dummy(uint8_t addr) {  // 1-byte write; 0 = ACKed
  Wire.beginTransmission(addr); Wire.write((uint8_t)0); return Wire.endTransmission();
}

// One atomic read cycle. Caller is the single-threaded loop. Returns bytes read,
// 0 if nothing was pending, or -1 on a bus fail.
static int poll(void (*sink)(char)) {
  static uint8_t buf[kChunk];
  int out = -1;
  TimeoutGuard to(300);               // long timeout for the clock-stretch
  busReopen();                        // C5 read quirk: re-open ONCE, not per step
  Wire.setBufferSize(kChunk + 16);    // GPS frames exceed the default 128-byte buffer
  do {
    delay(kStepMs);
    if (dummy(kCmdAddr) != 0) {        // demo Recovery_I2C: nudge 0x54 then 0x58
      dummy(kRdAddr); dummy(kWrAddr);
      break;
    }
    delay(kStepMs);
    if (writeCr(0xAA510008UL, 4) != 0) break;
    delay(kStepMs);
    uint8_t lb[4];
    if (readData(lb, 4) != 4) break;
    uint32_t len = lb[0] | (lb[1] << 8) | ((uint32_t)lb[2] << 16) | ((uint32_t)lb[3] << 24);
    if (len == 0) { out = 0; break; }
    if (len >= 35u * 1024u) break;     // demo treats this as illegal
    if (len > kChunk) len = kChunk;    // drain the rest on later cycles
    delay(kStepMs);
    if (writeCr(0xAA512000UL, len) != 0) break;
    delay(kStepMs);
    size_t got = readData(buf, len);
    if (got != len) break;
    for (size_t i = 0; i < got; i++) sink((char)buf[i]);
    out = (int)got;
  } while (0);
  return out;
}

// Nudge the module's I2C transmitter after it idles (demo Ql_Wake_I2C).
static bool wake() {
  bool ok = false;
  TimeoutGuard to(300);
  busReopen();
  do {
    delay(kStepMs); if (dummy(kCmdAddr) != 0) break;
    delay(kStepMs); if (writeCr(0xAA510004UL, 4) != 0) break;
    delay(kStepMs);
    uint8_t fb[4];
    if (readData(fb, 4) != 4) break;
    uint32_t freeLen = fb[0] | (fb[1] << 8) | ((uint32_t)fb[2] << 16) | ((uint32_t)fb[3] << 24);
    if (freeLen < 1) break;
    delay(kStepMs); if (writeCr(0xAA531000UL, 1) != 0) break;
    delay(kStepMs); ok = (dummy(kWrAddr) == 0);
  } while (0);
  return ok;
}

static bool     s_present   = false;  // 0x50 answered a probe
static uint32_t s_lastProbe = 0;
static const uint32_t kReprobeMs = 5000;  // silent re-probe cadence when absent

// Silent presence check for the GPS at 0x50. A zero-byte transaction takes the
// i2c *probe* path (logs at verbose, not error, on a NACK), so a board with no
// GPS wired makes no noise. Gates the heavy read cycle below: that cycle writes
// to 0x50/0x54/0x58, and with no module every write errors and thrashes the
// shared I2C bus that touch rides -- which shows up as laggy/stuck UI input.
static bool probe() {
  TimeoutGuard to(100);
  busReopen();
  Wire.beginTransmission(kCmdAddr);
  return Wire.endTransmission() == 0;
}

// One service step. Throttled to kPeriodMs; skipped while the UI is being used
// (busyUntilMs in the future) so touch stays smooth. The multi-write cycle runs
// only once 0x50 has answered a probe; when absent we re-probe at most every
// kReprobeMs so a later-wired module is still picked up without flooding the bus.
static int tick(void (*sink)(char), uint32_t now, uint32_t busyUntilMs) {
  static uint32_t s_lastAt = 0;
  static uint8_t  s_empty  = 0;
  if (s_lastAt && now - s_lastAt < kPeriodMs) return 0;
  if (busyUntilMs && (int32_t)(busyUntilMs - now) > 0) return 0;  // defer while touched
  s_lastAt = now ? now : 1;
  if (!s_present) {
    if (s_lastProbe && (uint32_t)(now - s_lastProbe) < kReprobeMs) return 0;
    s_lastProbe = now ? now : 1;
    s_present = probe();
    if (!s_present) return 0;  // no module: leave the bus (and touch) alone
  }
  int r = poll(sink);
  if (r < 0) { s_present = false; s_lastProbe = now ? now : 1; return r; }  // lost it: re-probe
  if (r == 0) { if (++s_empty >= 8) { s_empty = 0; wake(); } }
  else { s_empty = 0; }
  return r;
}
}  // namespace Gps

// ---- AXP2101 PMU (0x34), raw I2C -------------------------------------------
// No XPowersLib: read the fuel gauge with raw register reads (keeps AxD's CI
// libraries unchanged). Registers HW-verified in HARDWARE-FINDINGS: 0x03 id =
// 0x4A, 0x18 bit 3 = gauge enable, 0x00 (STATUS1) bit 3 = battery present, 0xA4
// = percent. Read 0xA4 only when a cell is fitted, else the value is bogus.
namespace Battery {
static constexpr uint8_t kAddr = 0x34;

static bool s_present = false;
static bool s_cell    = false;
static int  s_pct     = -1;

static bool readReg(uint8_t reg, uint8_t& out) {
  Wire.beginTransmission(kAddr);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) return false;  // STOP: C5 bus discipline
  if (Wire.requestFrom((uint16_t)kAddr, (size_t)1) != 1) return false;
  out = Wire.read();
  return true;
}
static bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kAddr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

static bool begin() {
  busReopen();   // C5 read quirk can fail the chip-id read right after Wire.begin
  uint8_t id = 0;
  s_present = readReg(0x03, id) && id == 0x4A;
  if (s_present) {
    uint8_t en = 0;
    if (readReg(0x18, en) && !(en & 0x08)) writeReg(0x18, (uint8_t)(en | 0x08));
  }
  Serial.printf("[ws-c5] AXP2101 0x34 %s (id 0x%02X)\n", s_present ? "ready" : "NOT FOUND", id);
  return s_present;
}

static void refresh() {
  if (!s_present) return;
  busReopen();
  uint8_t st = 0, pct = 0;
  s_cell = readReg(0x00, st) && (st & 0x08);
  s_pct  = (s_cell && readReg(0xA4, pct)) ? (int)pct : -1;
}

static bool present()  { return s_present; }
static bool hasCell()  { return s_cell; }
static int  percent()  { return s_pct; }
}  // namespace Battery

}  // namespace WsC5
#endif  // AWOK_WAVESHARE_C5_35
