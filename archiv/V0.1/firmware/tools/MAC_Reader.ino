// Board: ESP32C6 Dev Module (Arduino IDE Board-Auswahl für ESP32-C6-DevKitM-1)
#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  //delay(3000);
  WiFi.mode(WIFI_STA);
  Serial.println("\nMAC-Adresse dieses Boards:");
  Serial.println(WiFi.macAddress());
}

void loop() {}
