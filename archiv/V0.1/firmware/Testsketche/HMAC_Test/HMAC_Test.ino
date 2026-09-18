// ============================================================
// ClearBell – HMAC-SHA256 Selbsttest (Firmware-Etappe A)
//
// Zweck: Bestätigt, dass mbedTLS (im ESP32-Arduino-Core enthalten)
// HMAC-SHA256 auf DIESER Toolchain verfügbar ist und einen bekannten
// Testvektor korrekt reproduziert. Das ist die eine bislang nicht am
// realen Setup verifizierte Annahme, bevor die Türauth darauf aufbaut.
//
// Board:  "ESP32 Dev Module"
// Aufbau: keiner nötig – reiner Rechentest.
// Erwartung im Serial Monitor (115200 Baud): ">> PASS".
//
// Testvektor: RFC 4231, Test Case 2
//   Schlüssel = "Jefe"
//   Daten     = "what do ya want for nothing?"
//   HMAC-SHA256 = 5bdcc146 bf60754e 6a042426 089575c7
//                 5a003f08 9d273983 9dec58b9 64ec3843
// ============================================================

#include <string.h>
#include "mbedtls/md.h"

static const uint8_t KEY[]  = { 'J', 'e', 'f', 'e' };
static const char*   DATA   = "what do ya want for nothing?";
static const uint8_t ERWARTET[32] = {
  0x5b, 0xdc, 0xc1, 0x46, 0xbf, 0x60, 0x75, 0x4e,
  0x6a, 0x04, 0x24, 0x26, 0x08, 0x95, 0x75, 0xc7,
  0x5a, 0x00, 0x3f, 0x08, 0x9d, 0x27, 0x39, 0x83,
  0x9d, 0xec, 0x58, 0xb9, 0x64, 0xec, 0x38, 0x43
};

// Berechnet HMAC-SHA256(key, data) -> out[32]. true bei Erfolg.
static bool hmacSha256(const uint8_t* key, size_t keyLen,
                       const uint8_t* data, size_t dataLen,
                       uint8_t out[32]) {
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == nullptr) return false;
  // letzter Parameter 1 = HMAC-Modus
  return mbedtls_md_hmac(info, key, keyLen, data, dataLen, out) == 0;
}

static void druckeHex(const char* label, const uint8_t* p, size_t n) {
  Serial.print(label);
  for (size_t i = 0; i < n; i++) {
    if (p[i] < 0x10) Serial.print('0');
    Serial.print(p[i], HEX);
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== ClearBell HMAC-SHA256 Selbsttest (RFC 4231 TC2) ===");

  uint8_t tag[32];
  if (!hmacSha256(KEY, sizeof(KEY), (const uint8_t*)DATA, strlen(DATA), tag)) {
    Serial.println(">> FAIL: mbedTLS-HMAC-Aufruf fehlgeschlagen.");
    return;
  }

  druckeHex("Berechnet: ", tag, 32);
  druckeHex("Erwartet:  ", ERWARTET, 32);

  bool ok = (memcmp(tag, ERWARTET, 32) == 0);
  Serial.println(ok ? ">> PASS: HMAC-SHA256 korrekt."
                    : ">> FAIL: Abweichung!");

  // So sieht der gekuerzte 8-Byte-Tag aus, wie er spaeter im Tuerbefehl steckt
  // (im Produktivcode ueber CB_HMAC_TAG_LEN aus config.h):
  druckeHex("8-Byte-Tag (unser Format): ", tag, 8);
}

void loop() { }
