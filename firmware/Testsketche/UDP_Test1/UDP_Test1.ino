// ============================================================
// ClearBell – UDP-Zweiplatinentest v0.1 (Portierung von ESP-NOW)
// Ping/Pong mit Anwendungsquittung + Tuerbefehl, Transport: UDP
// ueber das Heim-/Gastnetz. Jede Einheit liefert eine Status-
// webseite (Handy-Browser), Innen mit Tuerbefehl-Ausloesung.
//
// Ablauf:
// 1. WLAN-Daten eintragen; DHCP-Reservierungen im Router anlegen
//    und die festen IPs unten bei INNEN_IP / AUSSEN_IP eintragen
// 2. Einheit per #define waehlen, flashen (beide Einheiten)
// 3. Statusseiten im Browser: http://<IP der Einheit>
// 4. Tuerbefehl: Verweis auf der Innen-Statusseite (oder 't' im
//    Seriellen Monitor)
// Erfolgskriterium bleibt die Anwendungsquittung, nicht der
// blosse UDP-Versand (UDP ist verbindungslos, kein Transport-ACK).
// ============================================================

#define EINHEIT_INNEN          // genau eine Zeile aktiv lassen
//#define EINHEIT_AUSSEN

#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>

#define WLAN_SSID     "MEIN_WLAN"
#define WLAN_PASSWORT "G4stzug4ng#"
#define INNEN_IP      "192.168.1.51"   // nach DHCP-Reservierung eintragen
#define AUSSEN_IP     "192.168.1.50"   // nach DHCP-Reservierung eintragen
#define UDP_PORT      4210

#define PING_INTERVALL_MS 1000
#define QUITT_TIMEOUT_MS  300
#define MAX_WIEDERHOLUNGEN 3

#ifdef EINHEIT_INNEN
  const char* EINHEIT_NAME = "INNEN";
  const char* PEER_IP_STR  = AUSSEN_IP;
#else
  const char* EINHEIT_NAME = "AUSSEN";
  const char* PEER_IP_STR  = INNEN_IP;
#endif

// Tueroeffner-MOSFET (GPIO21, nur Ausseneinheit) real ansteuern?
//#define TUER_MOSFET_AKTIV
#define TUER_GPIO    21
#define TUER_PULS_MS 200

enum NachrichtTyp : uint8_t { PING = 1, PONG = 2, TUER_AUF = 3, TUER_QUITT = 4 };

typedef struct __attribute__((packed)) {
  uint8_t  typ;
  uint32_t seq;
} Nachricht;

WiFiUDP udp;
WebServer server(80);
IPAddress peerIp;

// Statistik / Zustand
uint32_t seqZaehler = 0, gesendet = 0, quittiert = 0, verloren = 0;
uint32_t pingsEmpfangen = 0, letztePingSeq = 0, letzteTuerSeq = 0;
uint32_t tuerAusgeloest = 0, wlanAbrisse = 0;
uint32_t offeneSeq = 0, sendeZeit = 0, letzterPing = 0;
uint8_t  offenerTyp = 0, wiederholungen = 0;
bool     wartetAufQuitt = false;
uint32_t startZeit = 0;

void sende(uint8_t typ, uint32_t seq) {
  Nachricht n = { typ, seq };
  udp.beginPacket(peerIp, UDP_PORT);
  udp.write((uint8_t*)&n, sizeof(n));
  if (udp.endPacket() == 1) gesendet++;
}

void starteUebertragung(uint8_t typ, uint32_t seq) {
  offenerTyp = typ;
  offeneSeq = seq;
  wiederholungen = 0;
  wartetAufQuitt = true;
  sendeZeit = millis();
  sende(typ, seq);
}

void verarbeite(const Nachricht &n) {
#ifdef EINHEIT_AUSSEN
  if (n.typ == PING) {
    pingsEmpfangen++;
    letztePingSeq = n.seq;
    sende(PONG, n.seq);
  } else if (n.typ == TUER_AUF) {
    bool duplikat = (n.seq == letzteTuerSeq);
    letzteTuerSeq = n.seq;
    sende(TUER_QUITT, n.seq);
    if (duplikat) return;
    tuerAusgeloest++;
    Serial.printf("TUER_AUF seq=%lu -> Tueroeffner\n", (unsigned long)n.seq);
#ifdef TUER_MOSFET_AKTIV
    digitalWrite(TUER_GPIO, HIGH);
    delay(TUER_PULS_MS);
    digitalWrite(TUER_GPIO, LOW);
#endif
  }
#else
  if ((n.typ == PONG || n.typ == TUER_QUITT) && wartetAufQuitt && n.seq == offeneSeq) {
    quittiert++;
    wartetAufQuitt = false;
    if (n.typ == TUER_QUITT)
      Serial.printf("TUER_QUITT seq=%lu erhalten\n", (unsigned long)n.seq);
  }
#endif
}

void statusSeite() {
  uint32_t s = (millis() - startZeit) / 1000;
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta http-equiv='refresh' content='2'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ClearBell UDP-Test</title></head><body style='font-family:sans-serif'>"
    "<h2>ClearBell UDP-Test: " + String(EINHEIT_NAME) + "</h2>"
    "<p>RSSI: <b>" + String(WiFi.RSSI()) + " dBm</b> | Kanal: " + String(WiFi.channel())
    + " | Zugangspunkt: " + WiFi.BSSIDstr() + "</p>"
    "<p>Betriebszeit: " + String(s / 60) + " min | WLAN-Abrisse: <b>"
    + String(wlanAbrisse) + "</b></p>";
#ifdef EINHEIT_INNEN
  html += "<p>Gesendet: " + String(gesendet) + " | Quittiert: " + String(quittiert)
    + " | <b>Verloren: " + String(verloren) + "</b></p>"
    "<form method='POST' action='/tuer'>"
    "<button style='font-size:1.5em;padding:0.5em 1em'>Tuerbefehl senden</button>"
    "</form>";
#else
  html += "<p>Pings empfangen: " + String(pingsEmpfangen) + " | letzte seq: "
    + String(letztePingSeq) + " | Tuer ausgeloest: " + String(tuerAusgeloest) + "</p>";
#endif
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void tuerSeite() {
#ifdef EINHEIT_INNEN
  // Sperrzeit: fruehestens alle 3 s eine Ausloesung annehmen
  static uint32_t letzteAusloesung = 0;
  if (millis() - letzteAusloesung >= 3000 && !wartetAufQuitt) {
    letzteAusloesung = millis();
    starteUebertragung(TUER_AUF, ++seqZaehler);
    Serial.printf("TUER_AUF per Webseite, seq=%lu\n", (unsigned long)seqZaehler);
  } else {
    Serial.println("Tuerbefehl verworfen (Sperrzeit/offene Uebertragung)");
  }
#endif
  server.sendHeader("Location", "/");
  server.send(303);
}

void wlanVerbinden() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WLAN_SSID, WLAN_PASSWORT);
  Serial.print("Verbinde mit WLAN");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nVerbunden. IP: %s | RSSI: %d dBm | Kanal: %d\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel());
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\n=== ClearBell UDP-Test | Einheit: %s ===\n", EINHEIT_NAME);

  peerIp.fromString(PEER_IP_STR);
  wlanVerbinden();
  udp.begin(UDP_PORT);

  server.on("/", statusSeite);
  server.on("/tuer", HTTP_POST, tuerSeite);   // nur POST: kein Ausloesen durch Vorladen/Aktualisieren
  server.begin();

#ifdef TUER_MOSFET_AKTIV
  pinMode(TUER_GPIO, OUTPUT);
  digitalWrite(TUER_GPIO, LOW);
#endif
  startZeit = millis();
}

void loop() {
  // WLAN-Ueberwachung: Abrisse zaehlen und neu verbinden
  if (WiFi.status() != WL_CONNECTED) {
    wlanAbrisse++;
    Serial.println("WLAN-Abriss -> Neuverbindung");
    WiFi.disconnect();
    wlanVerbinden();
    udp.begin(UDP_PORT);
  }

  server.handleClient();

  // UDP-Empfang
  int laenge = udp.parsePacket();
  if (laenge == (int)sizeof(Nachricht)) {
    Nachricht n;
    udp.read((uint8_t*)&n, sizeof(n));
    verarbeite(n);
  } else if (laenge > 0) {
    udp.flush();   // Fremdpaket verwerfen
  }

#ifdef EINHEIT_INNEN
  uint32_t jetzt = millis();

  if (!wartetAufQuitt && jetzt - letzterPing >= PING_INTERVALL_MS) {
    letzterPing = jetzt;
    starteUebertragung(PING, ++seqZaehler);
  }

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 't' && !wartetAufQuitt) {
      starteUebertragung(TUER_AUF, ++seqZaehler);
      Serial.printf("TUER_AUF gesendet, seq=%lu\n", (unsigned long)seqZaehler);
    }
  }

  if (wartetAufQuitt && jetzt - sendeZeit >= QUITT_TIMEOUT_MS) {
    if (wiederholungen < MAX_WIEDERHOLUNGEN) {
      wiederholungen++;
      sende(offenerTyp, offeneSeq);
      sendeZeit = jetzt;
    } else {
      verloren++;
      wartetAufQuitt = false;
      Serial.printf("seq=%lu ENDGUELTIG VERLOREN\n", (unsigned long)offeneSeq);
    }
  }
#endif
  delay(2);
}
