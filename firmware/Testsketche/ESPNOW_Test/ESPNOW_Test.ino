// ============================================================
// ClearBell – ESP-NOW Zweiplatinentest v0.1 (Testphase 1+2)
// Ping/Pong mit Anwendungsquittung + Tuerbefehl mit Wiederholung.
// Einheit per #define waehlen. Serial: 115200 Baud.
//
// Ablauf:
// 1. Als AUSSEN kompilieren/flashen -> eigene MAC wird ausgegeben
// 2. Diese MAC unten bei EINHEIT_INNEN in peerMac eintragen
// 3. Als INNEN kompilieren/flashen
// 4. Innen sendet 1 Ping/s; 't' im Seriellen Monitor = Tuerbefehl
//
// WICHTIG: Der MAC-ACK (Sende-Callback) bestaetigt nur den Empfang
// auf MAC-Ebene, NICHT die Verarbeitung (Espressif-Doku, ESP-NOW).
// Erfolgskriterium ist die Anwendungsquittung (PONG / TUER_QUITT).
// ============================================================

#define EINHEIT_INNEN          // genau eine Zeile aktiv lassen
//#define EINHEIT_AUSSEN

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>   // esp_read_mac (MAC aus eFuse, unabhaengig vom WLAN-Status)

#define KANAL 1                // fester Funkkanal, Testphase ohne WLAN
#define PING_INTERVALL_MS 1000
#define QUITT_TIMEOUT_MS  300
#define MAX_WIEDERHOLUNGEN 3
#define STATISTIK_ALLE 20      // Statistikausgabe alle N Pings

#ifdef EINHEIT_INNEN
  const char* EINHEIT_NAME = "INNEN";
  // MAC der AUSSENEINHEIT hier eintragen (Serial-Ausgabe beim Start):
  uint8_t peerMac[6] = {0x4C, 0xC3, 0x82, 0xA9, 0xD1, 0xE0}; //4C:C3:82:A9:D1:E0
#else
  const char* EINHEIT_NAME = "AUSSEN";
  // MAC der Inneneinheit (per esptool verifiziert, 03.07.2026):
  uint8_t peerMac[6] = {0x4C, 0xC3, 0x82, 0xA9, 0xD3, 0x24};
#endif

// Tueroeffner-MOSFET (GPIO21, nur Ausseneinheit) real ansteuern?
// Standard: AUS -> nur Serial-Ausgabe. Erst aktivieren, wenn die
// Funkstrecke validiert ist und der Ausgang gefahrlos schalten darf.
//#define TUER_MOSFET_AKTIV
#define TUER_GPIO    21
#define TUER_PULS_MS 200

enum NachrichtTyp : uint8_t { PING = 1, PONG = 2, TUER_AUF = 3, TUER_QUITT = 4, STATUSMELDUNG = 5 };

typedef struct __attribute__((packed)) {
  uint8_t  typ;
  uint32_t seq;
} Nachricht;

// Fern-Statistik: Innen -> Aussen, damit die Innen-Zaehler ohne
// UART-Anschluss am Monitor der Ausseneinheit sichtbar sind.
typedef struct __attribute__((packed)) {
  uint8_t  typ;      // STATUSMELDUNG
  uint32_t gesendet, quittiert, verloren, macAckOk, macAckFehl;
  int8_t   rssi;
} StatusNachricht;

QueueHandle_t empfangsQueue;
QueueHandle_t statusQueue;
volatile int letzterRssi = 0;   // fuer Reichweitentest Geschossdecke

// Statistik (beide Einheiten)
uint32_t gesendet = 0, macAckOk = 0, macAckFehl = 0;

// Zustand Inneneinheit
uint32_t seqZaehler = 0, quittiert = 0, verloren = 0, pingsSeitStat = 0;
uint32_t offeneSeq = 0, sendeZeit = 0, letzterPing = 0;
uint8_t  offenerTyp = 0, wiederholungen = 0;
bool     wartetAufQuitt = false;

// Zustand Ausseneinheit: Duplikaterkennung Tuerbefehl + Empfangsstatistik
uint32_t letzteTuerSeq = 0;
uint32_t pingsEmpfangen = 0, letztePingSeq = 0, letzteStatAussen = 0;

// ---- Sende-Callback -----------------------------------------
// ACHTUNG: Signatur haengt von der Core-Version ab. Ab Core 3.3
// (IDF 5.5) wurde der erste Parameter geaendert. Falls der
// Compiler hier meckert: die jeweils andere Variante nutzen.
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 0)
void beimSenden(const wifi_tx_info_t *info, esp_now_send_status_t status) {
#else
void beimSenden(const uint8_t *mac, esp_now_send_status_t status) {
#endif
  if (status == ESP_NOW_SEND_SUCCESS) macAckOk++;
  else macAckFehl++;
}

// ---- Empfangs-Callback (laeuft im WiFi-Task: kurz halten!) ---
void beimEmpfang(const esp_now_recv_info_t *info, const uint8_t *daten, int laenge) {
  if (info->rx_ctrl) letzterRssi = info->rx_ctrl->rssi;
  if (laenge == (int)sizeof(Nachricht)) {
    Nachricht n;
    memcpy(&n, daten, sizeof(n));
    xQueueSend(empfangsQueue, &n, 0);
  } else if (laenge == (int)sizeof(StatusNachricht) && daten[0] == STATUSMELDUNG) {
    StatusNachricht s;
    memcpy(&s, daten, sizeof(s));
    xQueueSend(statusQueue, &s, 0);
  }
}

// ---- Senden -------------------------------------------------
void sende(uint8_t typ, uint32_t seq) {
  Nachricht n = { typ, seq };
  esp_err_t r = esp_now_send(peerMac, (uint8_t*)&n, sizeof(n));
  if (r == ESP_OK) gesendet++;
  else Serial.printf("esp_now_send Fehler: 0x%X\n", r);
}

void starteUebertragung(uint8_t typ, uint32_t seq) {
  offenerTyp = typ;
  offeneSeq = seq;
  wiederholungen = 0;
  wartetAufQuitt = true;
  sendeZeit = millis();
  sende(typ, seq);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();   // Testphase 1: kein WLAN
  esp_wifi_set_channel(KANAL, WIFI_SECOND_CHAN_NONE);

  uint8_t eigeneMac[6];
  esp_read_mac(eigeneMac, ESP_MAC_WIFI_STA);
  Serial.printf("\n=== ClearBell ESP-NOW-Test | Einheit: %s ===\n", EINHEIT_NAME);
  Serial.printf("Eigene MAC: %02X:%02X:%02X:%02X:%02X:%02X | Kanal: %d\n",
                eigeneMac[0], eigeneMac[1], eigeneMac[2],
                eigeneMac[3], eigeneMac[4], eigeneMac[5], KANAL);

  // Tatsaechliche maximale Sendeleistung ausgeben (Rohwert x 0,25 dBm)
  int8_t txp = 0;
  if (esp_wifi_get_max_tx_power(&txp) == ESP_OK)
    Serial.printf("Max. Sendeleistung: %.2f dBm (Rohwert %d)\n", txp * 0.25, txp);

#ifdef EINHEIT_INNEN
  // Schutz gegen Start mit nicht eingetragener Peer-MAC
  bool macLeer = true;
  for (int i = 0; i < 6; i++) if (peerMac[i] != 0) macLeer = false;
  if (macLeer) {
    Serial.println("FEHLER: peerMac ist 00:00:00:00:00:00 —");
    Serial.println("MAC der Ausseneinheit im Sketch eintragen, dann neu flashen!");
    while (true) delay(1000);
  }
#endif

  empfangsQueue = xQueueCreate(8, sizeof(Nachricht));
  statusQueue   = xQueueCreate(4, sizeof(StatusNachricht));

  if (esp_now_init() != ESP_OK) {
    Serial.println("FEHLER: ESP-NOW-Initialisierung fehlgeschlagen");
    while (true) delay(1000);
  }
  esp_now_register_send_cb(beimSenden);
  esp_now_register_recv_cb(beimEmpfang);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, peerMac, 6);
  peer.channel = KANAL;
  peer.ifidx   = WIFI_IF_STA;
  peer.encrypt = false;        // Testphase 1: unverschluesselt!
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("FEHLER: Peer konnte nicht hinzugefuegt werden");
  }

#ifdef TUER_MOSFET_AKTIV
  pinMode(TUER_GPIO, OUTPUT);
  digitalWrite(TUER_GPIO, LOW);
#endif
#ifdef EINHEIT_INNEN
  Serial.println("Befehl: 't' + Enter = Tueroeffner-Befehl senden");
#endif
}

// ---- Empfangene Nachricht verarbeiten -----------------------
void verarbeite(const Nachricht &n) {
#ifdef EINHEIT_AUSSEN
  if (n.typ == PING) {
    pingsEmpfangen++;
    letztePingSeq = n.seq;
    sende(PONG, n.seq);
  } else if (n.typ == TUER_AUF) {
    bool duplikat = (n.seq == letzteTuerSeq);
    letzteTuerSeq = n.seq;
    sende(TUER_QUITT, n.seq);   // auch bei Duplikat quittieren
                                // (erste Quittung ging evtl. verloren)
    if (duplikat) {
      Serial.printf("TUER_AUF seq=%lu: Duplikat, ignoriert (Quittung erneut gesendet)\n",
                    (unsigned long)n.seq);
      return;
    }
    Serial.printf("TUER_AUF seq=%lu empfangen -> Tueroeffner ausloesen (RSSI %d dBm)\n",
                  (unsigned long)n.seq, letzterRssi);
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
      Serial.printf("TUER_QUITT seq=%lu erhalten (Wiederholungen: %u, RSSI %d dBm)\n",
                    (unsigned long)n.seq, wiederholungen, letzterRssi);
  }
#endif
}

void loop() {
  Nachricht n;
  while (xQueueReceive(empfangsQueue, &n, 0) == pdTRUE) verarbeite(n);

#ifdef EINHEIT_INNEN
  uint32_t jetzt = millis();

  // Zyklischer Ping (nur wenn keine Uebertragung offen)
  if (!wartetAufQuitt && jetzt - letzterPing >= PING_INTERVALL_MS) {
    letzterPing = jetzt;
    starteUebertragung(PING, ++seqZaehler);
    pingsSeitStat++;
  }

  // Serieller Befehl: Tueroeffner
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 't' && !wartetAufQuitt) {
      starteUebertragung(TUER_AUF, ++seqZaehler);
      Serial.printf("TUER_AUF gesendet, seq=%lu\n", (unsigned long)seqZaehler);
    }
  }

  // Timeout -> Wiederholung oder Verlust
  if (wartetAufQuitt && jetzt - sendeZeit >= QUITT_TIMEOUT_MS) {
    if (wiederholungen < MAX_WIEDERHOLUNGEN) {
      wiederholungen++;
      Serial.printf("Timeout seq=%lu -> Wiederholung %u/%u\n",
                    (unsigned long)offeneSeq, wiederholungen, MAX_WIEDERHOLUNGEN);
      sende(offenerTyp, offeneSeq);
      sendeZeit = jetzt;
    } else {
      verloren++;
      wartetAufQuitt = false;
      Serial.printf("seq=%lu ENDGUELTIG VERLOREN (%u Wiederholungen erfolglos)\n",
                    (unsigned long)offeneSeq, MAX_WIEDERHOLUNGEN);
    }
  }

  // Statistik
  if (pingsSeitStat >= STATISTIK_ALLE) {
    pingsSeitStat = 0;
    Serial.printf("--- Statistik: gesendet=%lu quittiert=%lu verloren=%lu | "
                  "MAC-ACK ok=%lu fehl=%lu | RSSI %d dBm ---\n",
                  (unsigned long)gesendet, (unsigned long)quittiert,
                  (unsigned long)verloren, (unsigned long)macAckOk,
                  (unsigned long)macAckFehl, letzterRssi);
    // Zusaetzlich per Funk an die Ausseneinheit melden (ohne Quittung)
    StatusNachricht s = { STATUSMELDUNG, gesendet, quittiert, verloren,
                          macAckOk, macAckFehl, (int8_t)letzterRssi };
    esp_now_send(peerMac, (uint8_t*)&s, sizeof(s));
  }
#endif

#ifdef EINHEIT_AUSSEN
  // Fern-Statistik der Inneneinheit ausgeben
  StatusNachricht s;
  while (xQueueReceive(statusQueue, &s, 0) == pdTRUE) {
    Serial.printf("=== FERN-STATISTIK INNEN: gesendet=%lu quittiert=%lu "
                  "verloren=%lu | MAC-ACK ok=%lu fehl=%lu | RSSI innen %d dBm ===\n",
                  (unsigned long)s.gesendet, (unsigned long)s.quittiert,
                  (unsigned long)s.verloren, (unsigned long)s.macAckOk,
                  (unsigned long)s.macAckFehl, (int)s.rssi);
  }

  // Empfangsstatistik alle 10 s (auch als Lebenszeichen ohne Empfang)
  if (millis() - letzteStatAussen >= 10000) {
    letzteStatAussen = millis();
    Serial.printf("--- AUSSEN: Pings empfangen=%lu | letzte seq=%lu | "
                  "MAC-ACK ok=%lu fehl=%lu | RSSI %d dBm ---\n",
                  (unsigned long)pingsEmpfangen, (unsigned long)letztePingSeq,
                  (unsigned long)macAckOk, (unsigned long)macAckFehl, letzterRssi);
  }
#endif

  delay(10);
}
