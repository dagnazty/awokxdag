// Cardputer v1.1 bring-up probe for the AxD port. This does not transmit test
// frames or modify the AxD firmware. Flash only to a Cardputer v1.1.
#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

constexpr int kSdCs = 12;
constexpr int kSdMosi = 14;
constexpr int kSdSck = 40;
constexpr int kSdMiso = 39;

bool sdReady = false;
String lastAction = "Ready";

void logMemory() {
  Serial.printf("Cardputer v1.1: SD=%d PSRAM=%u internal_free=%u largest=%u\n",
                sdReady, unsigned(heap_caps_get_total_size(MALLOC_CAP_SPIRAM)),
                unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
}

void drawStatus() {
  auto& screen = M5Cardputer.Display;
  screen.fillScreen(BLACK);
  screen.setTextColor(WHITE, BLACK);
  screen.setTextSize(1);
  screen.setCursor(4, 4);
  screen.println("AxD / Cardputer v1.1 probe");
  screen.printf("SD: %s  PSRAM: %u KB\n", sdReady ? "OK" : "missing",
                unsigned(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024));
  screen.printf("Free RAM: %u KB\n",
                unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
  screen.printf("Largest block: %u KB\n",
                unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
  screen.println();
  screen.println("W: scan nearby Wi-Fi");
  screen.println("Any other key: key test");
  screen.println();
  screen.println(lastAction);
}

void scanWifi() {
  lastAction = "Scanning Wi-Fi...";
  drawStatus();
  WiFi.mode(WIFI_STA);
  const int count = WiFi.scanNetworks(false, true);
  lastAction = count < 0 ? "Scan failed" : String("APs found: ") + count;
  Serial.println(lastAction);
  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
  logMemory();
  drawStatus();
}

void setup() {
  Serial.begin(115200);
  M5Cardputer.begin();
  M5Cardputer.Display.setRotation(1);
  SPI.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
  sdReady = SD.begin(kSdCs, SPI, 10000000);
  logMemory();
  drawStatus();
}

void loop() {
  static uint32_t lastLogMs = 0;
  if (millis() - lastLogMs >= 10000) {
    lastLogMs = millis();
    logMemory();
  }
  M5Cardputer.update();
  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    const auto& keys = M5Cardputer.Keyboard.keysState();
    if (!keys.word.empty()) {
      const char key = keys.word.front();
      Serial.printf("Key: %c\n", key);
      if (key == 'w' || key == 'W') scanWifi();
      else {
        lastAction = String("Key: ") + key;
        drawStatus();
      }
    } else if (keys.enter) {
      lastAction = "Enter detected";
      drawStatus();
    }
  }
  delay(10);
}
