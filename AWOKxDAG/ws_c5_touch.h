#pragma once
// Waveshare ESP32-C5-Touch-LCD-3.5 capacitive touch: FT6336 at 0x38 on the
// shared I2C bus (SDA 27 / SCL 26). Drop-in for the Pancake AwokCapTouch -- same
// begin()/read()/setRotation() interface, so input.ino readTouch() and
// screentest.ino use it unchanged under PANCAKE_CAP_TOUCH.
//
// What differs from pancake_touch.h, and why (all HW-verified on this board; see
// the BSP HARDWARE-FINDINGS "touch-break story"):
//   * No reset/Wire bring-up here -- panel power and the FT6336 reset are on the
//     CH32V006 expander, brought up once in initializeDisplayAndTouch() before
//     this begin() runs.
//   * INT gate on GPIO3. A blind poll with no finger down can return a stale /
//     phantom point (a menu row self-selecting at boot). Setting G_MODE=0 makes
//     INT a stable LEVEL (low = finger), and read() skips the I2C read entirely
//     while INT is high. The win is skipping the read, not interrupt latency.
//   * Per-read bus re-open (the C5 read quirk) + one retry, so touch coexists
//     with the GPS / PMU on the same bus.
#ifdef PANCAKE_CAP_TOUCH

#include <Wire.h>

class AwokCapTouch {
 public:
  bool begin() {
    uint8_t chipId = 0;
    Wire.beginTransmission(kAddr);
    Wire.write(kRegChipId);
    if (Wire.endTransmission(false) == 0) {
      Wire.requestFrom(static_cast<int>(kAddr), 1);
      if (Wire.available()) chipId = Wire.read();
    }
    present_ = (chipId == 0x64);  // FT6336U reports 0x64 at CHIPER (0xA3)
    Serial.printf("[ws-c5] FT6336 id 0x%02X %s\n", chipId,
                  present_ ? "(OK)" : "(not found)");

    // Low threshold for a responsive bare-glass feel; parsePoint's validation
    // keeps noise from registering. Raise toward 30-40 if a film/case is added.
    Wire.beginTransmission(kAddr);
    Wire.write(kRegThreshold);
    Wire.write(12);
    Wire.endTransmission();

    // G_MODE = 0 (interrupt-polling): INT becomes a stable LEVEL so a plain
    // digitalRead gates reads. Required -- without it the level is unreliable.
    if (AwokPins::kTouchInt >= 0) {
      pinMode(AwokPins::kTouchInt, INPUT);  // external 10K pull-up on TP_INT
      Wire.beginTransmission(kAddr);
      Wire.write(kRegGMode);
      Wire.write(static_cast<uint8_t>(0x00));
      Wire.endTransmission();
      Serial.printf("[ws-c5] FT6336 INT gate on GPIO%d\n", AwokPins::kTouchInt);
    }
    return present_;
  }

  // XPT2046-compatible stub so shared setup code can call touch.setRotation().
  void setRotation(uint8_t) {}

  // Fills raw ST7796 panel coordinates (0..319 / 0..479) when a finger is down.
  bool read(uint16_t& panelX, uint16_t& panelY) {
    // INT gate: if no finger (INT high), skip the I2C read -- kills phantoms.
    if (AwokPins::kTouchInt >= 0 && digitalRead(AwokPins::kTouchInt) != LOW)
      return false;

    // C5 read quirk: re-open the bus ONCE at the top of the read (never per
    // retry -- that is the Phase-1 stiffness the fleet traced).
    WsC5::busReopen();

    if (parsePoint(panelX, panelY)) return true;
    // The FT6336 intermittently drops a single read even with a finger held; one
    // cheap retry (no second re-open) rides out that transient.
    return parsePoint(panelX, panelY);
  }

 private:
  static constexpr uint8_t kAddr         = 0x38;
  static constexpr uint8_t kRegTdStatus  = 0x02;  // then 6 bytes of point data
  static constexpr uint8_t kRegChipId    = 0xA3;  // CHIPER
  static constexpr uint8_t kRegThreshold = 0x80;  // IDTHRESHOLD (lower = sensitive)
  static constexpr uint8_t kRegGMode     = 0xA4;  // G_MODE: 0 = interrupt-polling

  bool present_ = false;

  bool parsePoint(uint16_t& x, uint16_t& y) {
    uint8_t data[7];
    if (!readRegisters(kRegTdStatus, data, sizeof(data))) return false;
    const uint8_t points = data[0] & 0x0F;
    if (points < 1 || points > 2) return false;  // reject phantom / noise
    const uint16_t rx = (static_cast<uint16_t>(data[1] & 0x0F) << 8) | data[2];
    const uint16_t ry = (static_cast<uint16_t>(data[3] & 0x0F) << 8) | data[4];
    if (rx >= AwokST7796::kPanelW || ry >= AwokST7796::kPanelH) return false;
    x = rx; y = ry;
    return true;
  }

  bool readRegisters(uint8_t reg, uint8_t* buf, uint8_t len) {
    Wire.beginTransmission(kAddr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom(static_cast<int>(kAddr), static_cast<int>(len));
    for (uint8_t i = 0; i < len; ++i) buf[i] = Wire.available() ? Wire.read() : 0;
    return true;
  }
};

#endif  // PANCAKE_CAP_TOUCH
