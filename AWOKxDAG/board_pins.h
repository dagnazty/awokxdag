#pragma once

// AWOK Dual C5 Touch / Marauder v8 (white USB, screen ESP32-C5).
// Source cross-check: GhostESP sdkconfig.MarauderV8.
namespace AwokPins {
constexpr int kSpiSck = 6;
constexpr int kSpiMiso = 2;
constexpr int kSpiMosi = 7;

constexpr int kDisplayCs = 23;
constexpr int kDisplayDc = 24;
constexpr int kDisplayReset = -1;  // Reset is not controlled by a GPIO.
constexpr int kBacklight = 8;
constexpr bool kBacklightOn = true;

constexpr int kTouchCs = 3;
constexpr int kSdCs = 10;

// GPS on UART1 (matches Marauder v8 / ESP32-C5 mapping). The macro names follow
// Marauder's convention where kGpsRx is the ESP pin wired to the GPS module's
// TX line. 9600 baud NMEA.
// Battery voltage sense (ADC). Set to the real GPIO if the board exposes a
// battery divider; -1 disables the reading (Status shows "n/a").
constexpr int kBatteryAdc = -1;
constexpr float kBatteryDivider = 2.0f;  // divider ratio if kBatteryAdc is set

constexpr int kGpsUart = 1;
constexpr int kGpsRx = 14;  // ESP RX <- GPS TX
constexpr int kGpsTx = 13;  // ESP TX -> GPS RX
constexpr unsigned long kGpsBaud = 115200;  // confirmed on hardware
}  // namespace AwokPins

namespace AwokTouchCalibration {
// Marauder v8 portrait calibration values for the XPT2046 controller.
constexpr int kXMin = 286;
constexpr int kXMax = 3495;
constexpr int kYMin = 437;
constexpr int kYMax = 3449;
constexpr int kPressureMin = 400;
}  // namespace AwokTouchCalibration
