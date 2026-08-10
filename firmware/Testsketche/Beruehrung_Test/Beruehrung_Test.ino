// ClearBell – Testsketch: Beruehrungseingang (kapazitiv)
// Innen: J_TOUCH1 -> R_TOUCH1 (1k) -> IO32 (TOUCH9), gegen Schaltplan verifiziert
// Aussen: IO32 (TOUCH9, EG) und IO33 (TOUCH8, OG), je 1k in Serie
// Nur Rohwert-Beobachtung! Schwellwerte werden erst nach Gehaeusemontage
// kalibriert (Projektbeschluss). Bankwerte Aussen als Anhalt: AN=600 / AUS=800.
// Beruehrung SENKT den Rohwert (klassischer ESP32).

#define INNENEINHEIT
//#define AUSSENEINHEIT

#ifdef INNENEINHEIT
  #define TOUCH_PIN_1 32
#endif
#ifdef AUSSENEINHEIT
  #define TOUCH_PIN_1 32
  #define TOUCH_PIN_2 33
#endif

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ClearBell Beruehrungs-Test ===");
  Serial.println("Rohwerte im 200-ms-Takt. Beruehrung muss den Wert deutlich senken.");
}

void loop() {
  Serial.printf("T(%d)=%lu", TOUCH_PIN_1, (unsigned long)touchRead(TOUCH_PIN_1));
#ifdef TOUCH_PIN_2
  Serial.printf("  T(%d)=%lu", TOUCH_PIN_2, (unsigned long)touchRead(TOUCH_PIN_2));
#endif
  Serial.println();
  delay(200);
}
