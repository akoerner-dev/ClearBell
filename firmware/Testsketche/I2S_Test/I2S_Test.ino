// ClearBell – Testsketch: I2S-Audio (MAX98357A)
// Einheit unten waehlen! Pinbelegungen unterscheiden sich.
// Innen: aus Schaltplan gelesen (Raster grenzwertig) – bei Stummheit
//        zuerst LRCLK/BCLK tauschen, dann Hardware pruefen
// Aussen: TODO – Pins aus Aussen-Schaltplan uebernehmen (hier NICHT verifiziert)

#include <ESP_I2S.h>
#include <math.h>

#define INNENEINHEIT
//#define AUSSENEINHEIT

#ifdef INNENEINHEIT
  #define I2S_BCLK  26
  #define I2S_LRCLK 25
  #define I2S_DOUT  22
#endif
#ifdef AUSSENEINHEIT
  #error "Aussen-Pins vor Gebrauch aus Schaltplan eintragen und diese Zeile loeschen"
#endif

I2SClass i2s;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ClearBell I2S-Test ===");
  i2s.setPins(I2S_BCLK, I2S_LRCLK, I2S_DOUT);

  if (!i2s.begin(I2S_MODE_STD, 44100, I2S_DATA_BIT_WIDTH_16BIT,
                 I2S_SLOT_MODE_STEREO)) {
    Serial.println("FEHLER: I2S-Start fehlgeschlagen.");
    return;
  }
  Serial.println("440-Hz-Ton, 3 Sekunden, geringe Lautstaerke...");

  const int     fs  = 44100;
  const float   f   = 440.0f;
  const int16_t amp = 3000;  // ~9 % Vollaussteuerung – bewusst leise
  // Phasenakkumulator statt sin(2*pi*f*n/fs): grosses n zerstoert
  // die float-Aufloesung -> Ton wird zum Ende hin kratzig (IBN-Befund 03.07.)
  float       phase = 0.0f;
  const float dphi  = 2.0f * (float)PI * f / fs;
  for (uint32_t n = 0; n < (uint32_t)fs * 3; n++) {
    int16_t s = (int16_t)(amp * sinf(phase));
    phase += dphi;
    if (phase >= 2.0f * (float)PI) phase -= 2.0f * (float)PI;
    i2s.write((uint8_t*)&s, 2);  // links
    i2s.write((uint8_t*)&s, 2);  // rechts
  }
  Serial.println("=== I2S-Test abgeschlossen ===");
}

void loop() {}
