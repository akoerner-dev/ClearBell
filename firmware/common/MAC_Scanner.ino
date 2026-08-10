/**
 * ClearBell – MAC-Adress-Scanner
 * ================================
 * Diesen Sketch auf jede Platine flashen um die MAC-Adresse auszulesen.
 * Danach in der jeweiligen config.h eintragen.
 *
 * Serial Monitor: 115200 Baud
 */

#include <WiFi.h>

void setup() {
    Serial.begin(115200);
    delay(1000);
    WiFi.mode(WIFI_STA);
    Serial.println("\n=== ClearBell MAC-Scanner ===");
    Serial.print("MAC-Adresse: ");
    Serial.println(WiFi.macAddress());
    Serial.println("\nDiesen Wert in config.h eintragen!");
    Serial.println("Beispiel: uint8_t MAC_xxx[] = {0xXX, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX};");
}

void loop() {}
