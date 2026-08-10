// ClearBell – Testsketch: UART-Herzschlag
// Einheiten-neutral (keine Pinbelegung noetig)
// IBN-Nachweis: Versorgung, EN/GPIO0, UART-TX, Programmstart
// Nach dem Flashen manuell S_RST druecken (kein Auto-Reset auf ClearBell-Platinen)

void setup() {
  Serial.begin(115200);
}

void loop() {
  static uint32_t n = 0;
  Serial.printf("ClearBell lebt – %lu s\n", n++);
  delay(1000);
}
