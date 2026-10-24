#pragma once
// Pancake C5 capacitive touch: FT6336 on the shared I2C bus (SDA 9 / SCL 10,
// reset on GPIO 8). Reports raw panel coordinates in the ST7796's native
// 320x480 space; input.ino readTouch() maps those back into the 240x320 design
// grid the UI is authored in (draws scale that grid up via scaleX/scaleY).
//
// Register logic follows ESP32_FlipSocial/ft6336.h, the proven Pancake driver.
// Exposes just begin()/read(), a lighter interface than XPT2046_Touchscreen --
// the two never coexist in one build, so readTouch() branches on PANCAKE_CAP_TOUCH.
#ifdef PANCAKE_CAP_TOUCH

#include <Wire.h>

class AwokCapTouch {
 public:
  bool begin() {
    pinMode(AwokPins::kTouchReset, OUTPUT);
    digitalWrite(AwokPins::kTouchReset, LOW);
    delay(10);
    digitalWrite(AwokPins::kTouchReset, HIGH);
    delay(300);
    Wire.begin(AwokPins::kI2cSda, AwokPins::kI2cScl, 400000U);

    uint8_t chipId = 0;
    Wire.beginTransmission(kAddr);
    Wire.write(0xA3);  // chip id register
    if (Wire.endTransmission(false) == 0) {
      Wire.requestFrom(static_cast<int>(kAddr), 1);
      if (Wire.available()) chipId = Wire.read();
    }
    Serial.printf("[touch] FT6336 ID: 0x%02X%s\n", chipId,
                  chipId == 0x64 ? " (OK)" : " (unexpected)");
    // Raise the touch threshold a little to suppress phantom presses in-case.
    Wire.beginTransmission(kAddr);
    Wire.write(0x80);  // IDTHRESHOLD (default 22; higher = less sensitive)
    Wire.write(40);
    Wire.endTransmission();
    return chipId == 0x64;
  }

  // XPT2046-compatible stub so shared setup code can call touch.setRotation().
  void setRotation(uint8_t) {}

  // Returns true and fills raw panel coordinates when a finger is down.
  bool read(uint16_t& panelX, uint16_t& panelY) {
    uint8_t data[7];
    if (!readRegisters(0x02 /* TD_STATUS */, data, sizeof(data))) return false;
    if ((data[0] & 0x0F) == 0) return false;  // no active touch points
    panelX = (static_cast<uint16_t>(data[1] & 0x0F) << 8) | data[2];
    panelY = (static_cast<uint16_t>(data[3] & 0x0F) << 8) | data[4];
    return true;
  }

 private:
  static constexpr uint8_t kAddr = 0x38;

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
