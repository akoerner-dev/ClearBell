// ============================================================
// ClearBell – Testsketch: WAV von SD über I2S  (Firmware-Etappe E3)
//
// Spielt eine WAV-Datei von der SD-Karte über den MAX98357A ab.
// Liest Abtastrate/Kanäle/Bittiefe aus dem WAV-Header (kein Raten).
// Mono wird auf beide I2S-Kanäle dupliziert.
//
// Baut auf den bereits validierten Sketchen SD_Test + I2S_Test auf.
// Blockierende Wiedergabe (Standalone-Test); die nicht-blockierende
// Fassung folgt in Etappe E4 in der Produktivfirmware.
//
// Vorbereitung: eine 16-bit-WAV-Datei unter WAV_DATEI auf die SD legen.
// Bedienung: Serial 115200, Taste 'p' spielt ab.
// ============================================================

#include <SPI.h>
#include <SD.h>
#include <ESP_I2S.h>

#define INNENEINHEIT           // SD-Pins je Einheit; genau eine wählen
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

// I2S-Pins (beide Einheitstypen gleich, verifiziert)
#define I2S_BCLK  26
#define I2S_LRCLK 25
#define I2S_DOUT  22

#define WAV_DATEI "/klingel_eg.wav"   // an vorhandene Datei anpassen

I2SClass i2s;
bool i2sBereit = false;

struct WavInfo { uint32_t sampleRate; uint16_t kanaele; uint16_t bits; uint32_t datenLen; };

static bool leseU16(File& f, uint16_t& v) {
  uint8_t b[2]; if (f.read(b, 2) != 2) return false; v = b[0] | (b[1] << 8); return true;
}
static bool leseU32(File& f, uint32_t& v) {
  uint8_t b[4]; if (f.read(b, 4) != 4) return false;
  v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
  return true;
}

// Parst RIFF/WAVE, sucht fmt- und data-Chunk. Nach Erfolg steht die
// Dateiposition auf dem Anfang der PCM-Daten.
bool parseWav(File& f, WavInfo& wi) {
  char id[4];
  uint32_t dummy;
  if (f.read((uint8_t*)id, 4) != 4 || memcmp(id, "RIFF", 4)) return false;
  if (!leseU32(f, dummy)) return false;                       // Gesamtlänge
  if (f.read((uint8_t*)id, 4) != 4 || memcmp(id, "WAVE", 4)) return false;

  bool fmtOk = false;
  while (true) {
    if (f.read((uint8_t*)id, 4) != 4) return false;           // Chunk-ID
    uint32_t len; if (!leseU32(f, len)) return false;         // Chunk-Länge
    if (!memcmp(id, "fmt ", 4)) {
      uint16_t audioFormat, blockAlign; uint32_t byteRate;
      leseU16(f, audioFormat);
      leseU16(f, wi.kanaele);
      leseU32(f, wi.sampleRate);
      leseU32(f, byteRate);
      leseU16(f, blockAlign);
      leseU16(f, wi.bits);
      if (len > 16) f.seek(f.position() + (len - 16));        // evtl. Rest überspringen
      fmtOk = true;
    } else if (!memcmp(id, "data", 4)) {
      wi.datenLen = len;
      return fmtOk;                                           // Position = PCM-Start
    } else {
      f.seek(f.position() + len + (len & 1));                 // unbekannter Chunk (+Padding)
    }
  }
}

void spieleWav(const char* pfad) {
  File f = SD.open(pfad);
  if (!f) { Serial.printf("FEHLER: %s nicht gefunden\n", pfad); return; }

  WavInfo wi;
  if (!parseWav(f, wi)) { Serial.println("FEHLER: kein gültiges WAV"); f.close(); return; }
  Serial.printf("WAV: %lu Hz, %u Kanal(e), %u bit, %lu Datenbytes (%.1f s)\n",
                (unsigned long)wi.sampleRate, wi.kanaele, wi.bits,
                (unsigned long)wi.datenLen,
                wi.datenLen / (float)(wi.sampleRate * wi.kanaele * (wi.bits / 8)));
  if (wi.bits != 16) { Serial.println("Nur 16-bit unterstützt."); f.close(); return; }

  if (i2sBereit) { i2s.end(); i2sBereit = false; }
  if (!i2s.begin(I2S_MODE_STD, wi.sampleRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("FEHLER: I2S-Start."); f.close(); return;
  }
  i2sBereit = true;

  uint8_t buf[1024];
  uint32_t rest = wi.datenLen;
  while (rest > 0) {
    uint32_t chunk = rest < sizeof(buf) ? rest : sizeof(buf);
    int gelesen = f.read(buf, chunk);
    if (gelesen <= 0) break;
    rest -= gelesen;
    int16_t* s = (int16_t*)buf;
    size_t anzahl = gelesen / 2;
    for (size_t i = 0; i < anzahl; i++) {
      i2s.write((uint8_t*)&s[i], 2);                 // linker Kanal
      if (wi.kanaele == 1) i2s.write((uint8_t*)&s[i], 2);  // mono -> auch rechts
    }
  }
  f.close();
  Serial.println("Wiedergabe fertig.");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ClearBell WAV-Test (E3) ===");

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, SPI, 4000000)) {
    Serial.println("FEHLER: SD.begin() – Karte/Pins/Einheit prüfen.");
    return;
  }
  i2s.setPins(I2S_BCLK, I2S_LRCLK, I2S_DOUT);
  Serial.printf("Bereit. Taste 'p' spielt %s ab.\n", WAV_DATEI);
}

void loop() {
  if (Serial.available() && Serial.read() == 'p') spieleWav(WAV_DATEI);
}
