#pragma once

// XPT2046 wiring differs between CYD sizes. The 2.8-inch touch controller
// occupies a third pin set, so it is clocked in software while TFT and SD use
// the ESP32's two hardware SPI controllers. E32R35T touch shares TFT HSPI.
#include <Arduino.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>  // TS_Point used by the existing input path

class AwokCydTouch {
 public:
  AwokCydTouch(int cs, int sck, int miso, int mosi, SPIClass* sharedBus)
      : cs_(cs), sck_(sck), miso_(miso), mosi_(mosi), bus_(sharedBus) {}

  bool begin() {
    pinMode(cs_, OUTPUT);
    digitalWrite(cs_, HIGH);
    if (!bus_) {
      pinMode(sck_, OUTPUT);
      pinMode(mosi_, OUTPUT);
      pinMode(miso_, INPUT);
      digitalWrite(sck_, LOW);
    }
    return true;
  }

  void setRotation(uint8_t rotation) { rotation_ = rotation & 3; }

  bool touched() {
    update();
    return point_.z >= 400;
  }

  TS_Point getPoint() {
    update();
    return point_;
  }

 private:
  uint8_t transfer(uint8_t value) {
    if (bus_) return bus_->transfer(value);
    uint8_t received = 0;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      digitalWrite(mosi_, (value & 0x80) ? HIGH : LOW);
      digitalWrite(sck_, HIGH);
      received = (received << 1) | (digitalRead(miso_) ? 1 : 0);
      digitalWrite(sck_, LOW);
      value <<= 1;
    }
    return received;
  }

  uint16_t read12(uint8_t command) {
    transfer(command);
    const uint16_t word = (uint16_t(transfer(0)) << 8) | transfer(0);
    return (word >> 3) & 0x0fff;
  }

  static uint16_t median(uint16_t a, uint16_t b, uint16_t c) {
    if (a > b) { const uint16_t t = a; a = b; b = t; }
    if (b > c) { const uint16_t t = b; b = c; c = t; }
    if (a > b) b = a;
    return b;
  }

  void update() {
    const uint32_t now = millis();
    if (now - sampledAt_ < 3) return;
    sampledAt_ = now;
    if (bus_) bus_->beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(cs_, LOW);
    const uint16_t z1 = read12(0xB1);
    const uint16_t z2 = read12(0xC1);
    const int pressure = int(z1) + 4095 - int(z2);
    uint16_t rawX = 0, rawY = 0;
    if (pressure >= 400) {
      read12(0x91);  // discard first conversion after pressure sampling
      rawX = median(read12(0x91), read12(0x91), read12(0x91));
      rawY = median(read12(0xD1), read12(0xD1), read12(0xD1));
    }
    read12(0xD0);  // power down
    digitalWrite(cs_, HIGH);
    if (bus_) bus_->endTransaction();
    if (pressure < 400) {
      point_ = TS_Point(0, 0, 0);
      return;
    }
    // Match XPT2046_Touchscreen::setRotation(0), which AxD input expects.
    int16_t x = 4095 - rawY;
    int16_t y = rawX;
    if (rotation_ == 1) { x = rawX; y = rawY; }
    else if (rotation_ == 2) { x = rawY; y = 4095 - rawX; }
    else if (rotation_ == 3) { x = 4095 - rawX; y = 4095 - rawY; }
    point_ = TS_Point(x, y, pressure);
  }

  int cs_, sck_, miso_, mosi_;
  SPIClass* bus_;
  uint8_t rotation_ = 0;
  uint32_t sampledAt_ = 0;
  TS_Point point_;
};
