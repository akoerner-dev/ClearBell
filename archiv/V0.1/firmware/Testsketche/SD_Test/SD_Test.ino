// ClearBell – Testsketch: SD-Karte ueber SPI
// Einheit unten waehlen! Pinbelegungen unterscheiden sich.
// Innen: gegen Schaltplan verifiziert + IBN 2026-07-03 bestanden
// Aussen: laut Projektdokumentation, IBN bestanden (Phase Aussen-IBN)

#include <SPI.h>
#include <SD.h>

#define INNENEINHEIT
//#define AUSSENEINHEIT

#ifdef INNENEINHEIT
  #define SD_CS   23
  #define SD_SCK  19
  #define SD_MOSI 21
  #define SD_MISO 18
#endif
#ifdef AUSSENEINHEIT
  #define SD_CS   5
  #define SD_SCK  18
  #define SD_MOSI 23
  #define SD_MISO 19
#endif

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ClearBell SD-Test ===");
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  if (!SD.begin(SD_CS, SPI, 4000000)) {  // 4 MHz, konservativer Start
    Serial.println("FEHLER: SD.begin() fehlgeschlagen.");
    Serial.println("Pruefen: Karte eingelegt? FAT32/FAT16? Kontakte? Einheit-#define?");
    return;
  }

  uint8_t typ = SD.cardType();
  Serial.print("Kartentyp: ");
  switch (typ) {
    case CARD_MMC:  Serial.println("MMC"); break;
    case CARD_SD:   Serial.println("SDSC"); break;
    case CARD_SDHC: Serial.println("SDHC/SDXC"); break;
    default:        Serial.println("unbekannt/keine Karte"); return;
  }
  Serial.printf("Groesse: %llu MB\n", SD.cardSize() / (1024ULL * 1024ULL));

  File f = SD.open("/ibn_test.txt", FILE_WRITE);
  if (!f) { Serial.println("FEHLER: Datei nicht anlegbar."); return; }
  f.println("ClearBell SD-Test");
  f.close();

  f = SD.open("/ibn_test.txt");
  if (!f) { Serial.println("FEHLER: Datei nicht lesbar."); return; }
  Serial.println("Dateiinhalt:");
  while (f.available()) Serial.write(f.read());
  f.close();

  Serial.println("Wurzelverzeichnis:");
  File root = SD.open("/");
  File e = root.openNextFile();
  while (e) {
    Serial.printf("  %s (%u Bytes)\n", e.name(), (unsigned)e.size());
    e = root.openNextFile();
  }
  root.close();
  Serial.println("=== SD-Test abgeschlossen ===");
}

void loop() {}
