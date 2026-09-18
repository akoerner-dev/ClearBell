#include <SPI.h>
#include <SD.h>
#define SD_CS 17
#define SD_SCK 18
#define SD_MISO 19
#define SD_MOSI 23

void tryMount(uint32_t freq) {
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  bool ok = SD.begin(SD_CS, SPI, freq);
  Serial.printf("SD.begin(%lu Hz): %s\n", freq, ok ? "OK" : "FAIL");

  uint8_t t = SD.cardType();
  Serial.print("cardType: ");
  switch (t) {
    case CARD_NONE: Serial.println("NONE  -> Karte antwortet NICHT ueber SPI"); break;
    case CARD_MMC:  Serial.println("MMC");  break;
    case CARD_SD:   Serial.println("SDSC"); break;
    case CARD_SDHC: Serial.println("SDHC/SDXC"); break;
    default:        Serial.println("UNKNOWN");
  }
  if (t != CARD_NONE)
    Serial.printf("cardSize: %llu MB\n", SD.cardSize() / (1024ULL * 1024ULL));

  SD.end();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== SD-Diagnose (Aussen) ===");
}

void loop() {
  Serial.println("---");
  tryMount(400000);   // 400 kHz = SD-Init-Speed, schliesst Speed-Probleme aus
  delay(3000);
}