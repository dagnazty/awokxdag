#pragma once

// Explicit board profiles; reject mismatched silicon before touching GPIOs.
#if (defined(AWOK_DUAL_C5_TOUCH) + defined(AWOK_DUAL_C5_MINI) + \
     defined(AWOK_DUAL_C5_BRIDGE) + defined(AWOK_LILYGO_T_DONGLE_C5_BRIDGE) + \
     defined(AWOK_DUAL_ESP32_TOUCH_V1) + defined(AWOK_DUAL_ESP32_TOUCH_V2) + \
     defined(AWOK_DUAL_ESP32_TOUCH_V3) + \
     defined(AWOK_DUAL_ESP32_MINI_V1) + defined(AWOK_DUAL_ESP32_MINI_V2) + \
     defined(AWOK_DUAL_ESP32_MINI_V3) + \
     defined(AWOK_DUAL_ESP32_TOUCH_BRIDGE_V1) + \
     defined(AWOK_DUAL_ESP32_TOUCH_BRIDGE_V2) + \
     defined(AWOK_DUAL_ESP32_TOUCH_BRIDGE_V3) + \
     defined(AWOK_DUAL_ESP32_MINI_BRIDGE_V1) + \
     defined(AWOK_DUAL_ESP32_MINI_BRIDGE_V2) + \
     defined(AWOK_DUAL_ESP32_MINI_BRIDGE_V3) + \
     defined(AWOK_CARDPUTER_V11)) != 1
#error "Select exactly one AWOK board profile"
#endif
// Bridge profiles are driven entirely over BLE. AWOK_HEADLESS makes normal
// menu/input rendering no-ops; T-Dongle-C5 separately shows a status badge.
#if defined(AWOK_DUAL_C5_BRIDGE) || defined(AWOK_LILYGO_T_DONGLE_C5_BRIDGE) || \
    defined(AWOK_DUAL_ESP32_TOUCH_BRIDGE_V1) || \
    defined(AWOK_DUAL_ESP32_TOUCH_BRIDGE_V2) || \
    defined(AWOK_DUAL_ESP32_TOUCH_BRIDGE_V3) || \
    defined(AWOK_DUAL_ESP32_MINI_BRIDGE_V1) || \
    defined(AWOK_DUAL_ESP32_MINI_BRIDGE_V2) || \
    defined(AWOK_DUAL_ESP32_MINI_BRIDGE_V3)
#define AWOK_HEADLESS
#endif
#if defined(AWOK_CARDPUTER_V11) || defined(AWOK_DUAL_C5_MINI) || defined(AWOK_DUAL_ESP32_MINI_V1) || \
    defined(AWOK_DUAL_ESP32_MINI_V2) || defined(AWOK_DUAL_ESP32_MINI_V3)
#define AWOK_MINI_DISPLAY
#endif
#if defined(AWOK_DUAL_ESP32_MINI_V1) || defined(AWOK_DUAL_ESP32_MINI_V2) || \
    defined(AWOK_DUAL_ESP32_MINI_V3) || \
    defined(AWOK_DUAL_ESP32_MINI_BRIDGE_V1) || \
    defined(AWOK_DUAL_ESP32_MINI_BRIDGE_V2) || \
    defined(AWOK_DUAL_ESP32_MINI_BRIDGE_V3)
#define AWOK_CLASSIC_MINI_WIRING
#endif
#if defined(AWOK_CARDPUTER_V11)
#define AWOK_CLASSIC_ESP32  // existing 2.4 GHz / low-memory feature paths
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Cardputer v1.1 requires ESP32-S3"
#endif
#elif defined(AWOK_DUAL_ESP32_TOUCH_V1) || defined(AWOK_DUAL_ESP32_TOUCH_V2) || \
    defined(AWOK_DUAL_ESP32_TOUCH_V3) || defined(AWOK_DUAL_ESP32_MINI_V1) || \
    defined(AWOK_DUAL_ESP32_MINI_V2) || defined(AWOK_DUAL_ESP32_MINI_V3) || \
    defined(AWOK_DUAL_ESP32_TOUCH_BRIDGE_V1) || \
    defined(AWOK_DUAL_ESP32_TOUCH_BRIDGE_V2) || \
    defined(AWOK_DUAL_ESP32_TOUCH_BRIDGE_V3) || \
    defined(AWOK_DUAL_ESP32_MINI_BRIDGE_V1) || \
    defined(AWOK_DUAL_ESP32_MINI_BRIDGE_V2) || \
    defined(AWOK_DUAL_ESP32_MINI_BRIDGE_V3)
#define AWOK_CLASSIC_ESP32
#if !defined(CONFIG_IDF_TARGET_ESP32)
#error "Original AWOK Dual boards require ESP32 Dev Module (not C5/S2/S3)"
#endif
#else
#if !defined(CONFIG_IDF_TARGET_ESP32C5)
#error "AWOK C5 profiles require ESP32C5 Dev Module"
#endif
#endif

namespace AwokPins {
#ifdef AWOK_CARDPUTER_V11
// M5Stack Cardputer v1.1 pin map. The screen uses M5GFX on its own SPI bus;
// global SPI is reserved for the microSD socket.
constexpr int kSpiSck = 40;
constexpr int kSpiMiso = 39;
constexpr int kSpiMosi = 14;
constexpr int kDisplayCs = 37;
constexpr int kDisplayDc = 34;
constexpr int kDisplayReset = 33;
constexpr int kBacklight = 38;
constexpr bool kBacklightOn = true;
constexpr int kTouchCs = -1;
// Logical keys, handled by M5Cardputer.Keyboard instead of GPIO reads.
constexpr int kButtonLeft = -1;
constexpr int kButtonCenter = -2;
constexpr int kButtonUp = -3;
constexpr int kButtonRight = -4;
constexpr int kButtonDown = -5;
constexpr int kSdCs = 12;
constexpr int kGpsUart = 1;
constexpr int kGpsRx = 1;  // optional Grove UART: GPS TX -> GPIO1
constexpr int kGpsTx = 2;  // optional Grove UART: GPS RX <- GPIO2
constexpr unsigned long kGpsBaud = 9600;
constexpr char kBoardLabel[] = "Cardputer v1.1";
constexpr char kChipLabel[] = "ESP32-S3";
constexpr bool kDualBand = false;
#elif defined(AWOK_CLASSIC_ESP32)
// Original Dual Touch white-port profiles: Marauder v6 (v1), v6.1 (v2/v3).
// Pin sources and validation status: docs/dual-esp32-touch.md (Mini: docs/dual-esp32-mini.md).
constexpr int kSpiSck = 18;
constexpr int kSpiMiso = 19;
constexpr int kSpiMosi = 23;
constexpr int kDisplayCs = 17;
constexpr int kDisplayDc = 16;
constexpr int kDisplayReset = 5;
constexpr int kBacklight = 32;
#ifdef AWOK_CLASSIC_MINI_WIRING
constexpr bool kBacklightOn = false;
constexpr int kTouchCs = -1;
constexpr int kButtonLeft = 13;
constexpr int kButtonCenter = 34;
constexpr int kButtonUp = 36;
constexpr int kButtonRight = 39;
constexpr int kButtonDown = 35;
#if defined(AWOK_DUAL_ESP32_MINI_BRIDGE_V1)
constexpr char kBoardLabel[] = "Dual ESP32 Mini Bridge v1";
#elif defined(AWOK_DUAL_ESP32_MINI_BRIDGE_V2)
constexpr char kBoardLabel[] = "Dual ESP32 Mini Bridge v2";
#elif defined(AWOK_DUAL_ESP32_MINI_BRIDGE_V3)
constexpr char kBoardLabel[] = "Dual ESP32 Mini Bridge v3";
#elif defined(AWOK_DUAL_ESP32_MINI_V1)
constexpr char kBoardLabel[] = "Dual ESP32 Mini v1";
#elif defined(AWOK_DUAL_ESP32_MINI_V2)
constexpr char kBoardLabel[] = "Dual ESP32 Mini v2";
#else
constexpr char kBoardLabel[] = "Dual ESP32 Mini v3";
#endif
constexpr int kSdCs = 4;
constexpr int kGpsUart = 2;
constexpr int kGpsRx = 21;
constexpr int kGpsTx = 22;
constexpr unsigned long kGpsBaud = 9600;  // verified on original Mini v3
#else
constexpr bool kBacklightOn = true;
constexpr int kTouchCs = 21;
#if defined(AWOK_DUAL_ESP32_TOUCH_BRIDGE_V1)
constexpr char kBoardLabel[] = "Dual ESP32 Touch Bridge v1";
constexpr int kSdCs = 12;
#elif defined(AWOK_DUAL_ESP32_TOUCH_BRIDGE_V2)
constexpr char kBoardLabel[] = "Dual ESP32 Touch Bridge v2";
constexpr int kSdCs = 14;
#elif defined(AWOK_DUAL_ESP32_TOUCH_BRIDGE_V3)
constexpr char kBoardLabel[] = "Dual ESP32 Touch Bridge v3";
constexpr int kSdCs = 14;
#elif defined(AWOK_DUAL_ESP32_TOUCH_V1)
constexpr char kBoardLabel[] = "Dual ESP32 Touch v1";
constexpr int kSdCs = 12;
#elif defined(AWOK_DUAL_ESP32_TOUCH_V3)
constexpr char kBoardLabel[] = "Dual ESP32 Touch v3";
constexpr int kSdCs = 14;
#else
constexpr char kBoardLabel[] = "Dual ESP32 Touch v2";
constexpr int kSdCs = 14;
#endif
constexpr int kGpsUart = 2;
constexpr int kGpsRx = 4;  // ESP RX <- GPS TX
constexpr int kGpsTx = 13;
constexpr unsigned long kGpsBaud = 115200;  // selectable on the GPS screen
#endif
constexpr char kChipLabel[] = "ESP32";
constexpr bool kDualBand = false;
#else
constexpr char kChipLabel[] = "ESP32-C5";
constexpr bool kDualBand = true;
constexpr int kSpiSck = 6;
#ifdef AWOK_LILYGO_T_DONGLE_C5_BRIDGE
// LILYGO T-Dongle-C5 SD shares the display SPI clock, but reverses the
// Dual C5 board's data pins. The LCD is a bridge-status display only.
constexpr int kSpiMiso = 7;
constexpr int kSpiMosi = 2;
constexpr int kDisplayCs = 10;
constexpr int kDisplayDc = 3;
constexpr int kDisplayReset = 1;
constexpr int kBacklight = 0;  // active-low LCD backlight
constexpr bool kBacklightOn = false;
constexpr int kTouchCs = -1;
constexpr int kSdCs = 23;
constexpr int kGpsUart = 1;
constexpr int kGpsRx = -1;  // no onboard GPS; leave the USB pins untouched
constexpr int kGpsTx = -1;
constexpr unsigned long kGpsBaud = 115200;
constexpr char kBoardLabel[] = "LILYGO T-Dongle-C5 Bridge";
#else
constexpr int kSpiMiso = 2;
constexpr int kSpiMosi = 7;

constexpr int kDisplayCs = 23;
constexpr int kDisplayDc = 24;
constexpr int kDisplayReset = -1;  // Reset is not controlled by a GPIO.
#if defined(AWOK_DUAL_C5_BRIDGE)
constexpr char kBoardLabel[] = "Dual C5 Bridge";
constexpr int kBacklight = -1;
constexpr bool kBacklightOn = true;
constexpr int kTouchCs = -1;  // headless: no touch, no buttons (BLE-driven)
#elif defined(AWOK_DUAL_C5_MINI)
constexpr char kBoardLabel[] = "Dual C5 Mini";
constexpr int kBacklight = 5;
constexpr bool kBacklightOn = false;
constexpr int kTouchCs = -1;
constexpr int kButtonLeft = 0;
constexpr int kButtonCenter = 1;
constexpr int kButtonUp = 4;
constexpr int kButtonRight = 8;
constexpr int kButtonDown = 9;
#else
constexpr char kBoardLabel[] = "Dual C5 Touch";
constexpr int kBacklight = 8;
constexpr bool kBacklightOn = true;
constexpr int kTouchCs = 3;
#endif
constexpr int kSdCs = 10;

constexpr int kGpsUart = 1;
constexpr int kGpsRx = 14;  // ESP RX <- GPS TX
constexpr int kGpsTx = 13;  // ESP TX -> GPS RX
constexpr unsigned long kGpsBaud = 115200;  // confirmed on Touch and Mini
#endif
#endif
// Disabled until a board-specific voltage divider is verified.
constexpr int kBatteryAdc = -1;
constexpr float kBatteryDivider = 2.0f;
// GPIO34-39 on classic ESP32 are input-only with no internal pull resistors.
constexpr bool buttonHasInternalPullup(int pin) {
#ifdef AWOK_CLASSIC_ESP32
  return pin < 34;
#else
  return true;
#endif
}
}  // namespace AwokPins

namespace AwokTouchCalibration {
#ifdef AWOK_CLASSIC_ESP32
// Marauder TFT_DIY {339,3470,237,3438,2}, converted from TFT_eSPI's
// origin/span + inverted X to XPT2046 setRotation(0) endpoint coordinates.
// Baseline only: verify all four corners on the physical panel.
constexpr int kXMin = 4095 - (339 + 3470);
constexpr int kXMax = 4095 - 339;
constexpr int kYMin = 237;
constexpr int kYMax = 237 + 3438;
#else
// Confirmed C5 portrait calibration values for the XPT2046 controller.
constexpr int kXMin = 286;
constexpr int kXMax = 3495;
constexpr int kYMin = 437;
constexpr int kYMax = 3449;
#endif
// Accept lighter taps while staying above the XPT2046's idle/noise floor.
constexpr int kPressureMin = 250;
}  // namespace AwokTouchCalibration
