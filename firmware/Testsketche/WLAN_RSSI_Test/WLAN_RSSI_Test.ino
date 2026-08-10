// ============================================================
// ClearBell – WLAN-RSSI-Messung an Montageorten v0.1
// Verbindet sich mit dem Heim-WLAN und zeigt den Empfangspegel
// zum Router live im Browser (Handy) und auf Serial an.
//
// Ablauf:
// 1. WLAN_SSID / WLAN_PASSWORT eintragen, EINHEIT_NAME anpassen
// 2. Flashen, am PC die per DHCP vergebene IP von Serial ablesen
//    (Router vergibt bei erneutem Verbinden i. d. R. dieselbe IP)
// 3. Einheit per Netzteil/Powerbank am Montageort betreiben,
//    IP im Handy-Browser aufrufen -> Seite aktualisiert alle 2 s
// 4. Je Ort ~1 min beobachten, Min/Max notieren
// Alternativ: http://clearbell-mess.local (mDNS; Aufloesung je
// nach Handy-Betriebssystem nicht garantiert -> IP ist sicher)
// ============================================================

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#define WLAN_SSID     "MEIN_WLAN"
#define WLAN_PASSWORT "G4stzug4ng#"
#define EINHEIT_NAME  "Messeinheit/Inneneinheit"   // z. B. "Aussen", "Innen-EG"

WebServer server(80);
int rssiMin = 0, rssiMax = -127;
uint32_t letzteAusgabe = 0;

void seite() {
  int rssi = WiFi.RSSI();
  if (rssi < rssiMin) rssiMin = rssi;
  if (rssi > rssiMax) rssiMax = rssi;
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta http-equiv='refresh' content='2'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ClearBell RSSI</title></head><body style='font-family:sans-serif'>"
    "<h2>ClearBell RSSI-Messung: " EINHEIT_NAME "</h2>"
    "<p style='font-size:3em;margin:0'>" + String(rssi) + " dBm</p>"
    "<p>Min: " + String(rssiMin) + " dBm | Max: " + String(rssiMax) + " dBm</p>"
    "<p>SSID: " + WiFi.SSID() + " | Kanal: " + String(WiFi.channel()) +
    " | IP: " + WiFi.localIP().toString() + "</p>"
    "<p>Bewertung: &ge; -75 dBm solide | -75 bis -85 grenzwertig | "
    "&lt; -85 unbrauchbar</p>"
    "<p><a href='/reset'>Min/Max zuruecksetzen (neuer Messort)</a></p>"
    "</body></html>";
  server.send(200, "text/html", html);
}

void resetMinMax() {
  rssiMin = 0;
  rssiMax = -127;
  server.sendHeader("Location", "/");
  server.send(303);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\n=== ClearBell WLAN-RSSI-Messung | %s ===\n", EINHEIT_NAME);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WLAN_SSID, WLAN_PASSWORT);
  Serial.print("Verbinde mit WLAN");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nVerbunden. IP: %s | Kanal: %d | RSSI: %d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.channel(), WiFi.RSSI());

  if (MDNS.begin("clearbell-mess"))
    Serial.println("mDNS: http://clearbell-mess.local");

  server.on("/", seite);
  server.on("/reset", resetMinMax);
  server.begin();
}

void loop() {
  server.handleClient();
  if (millis() - letzteAusgabe >= 2000) {
    letzteAusgabe = millis();
    Serial.printf("RSSI: %d dBm (Min %d / Max %d)\n", WiFi.RSSI(), rssiMin, rssiMax);
  }
}
