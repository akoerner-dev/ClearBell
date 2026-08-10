// ============================================================
// ClearBell – Produktivfirmware
//
// Transport WLAN + UDP. Enthalten & getestet:
//   - nicht-blockierendes WLAN (läuft auch ohne Netz weiter)
//   - UDP-Nachrichten (einheitliches 14-Byte-Paket) + Retry/Quittung
//   - Klingel- und Türbefehl-Fluss mit Erfolgs-/Fehlerton
//   - Türbefehl authentifiziert (HMAC-SHA256 + rollierender Zähler in
//     NVS gegen Replay; je Einheit eigener Schlüssel)
//   - Touch-Taster als Auslöser (Klingel bzw. Türbefehl)
//   - Audio: nicht-blockierender WAV-Player von SD über I2S
//   - Lautstärke per BOOT-Taster (GPIO0), in NVS gespeichert
//   - ntfy-Push (nur EG) asynchron über FreeRTOS-Task
//
// NOCH STUB:
//   - oeffneTuer(): GPIO-Puls des Türöffners (Hardware noch nicht vorhanden)
//
// Serial-Testbefehle (115200), zusätzlich zu den Touch-Tastern:
//   AUSSEN:  'e' = Klingel EG,  'o' = Klingel OG
//   INNEN:   't' = Türbefehl,   'x' = Türbefehl mit falschem Schlüssel
// ============================================================

// === EINHEIT WÄHLEN: genau eine Zeile aktiv lassen ===
#define CB_UNIT_AUSSEN
//#define CB_UNIT_INNEN_EG
//#define CB_UNIT_INNEN_OG

#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <string.h>
#include <stddef.h>
#include <SPI.h>
#include <SD.h>
#include <ESP_I2S.h>
#include "mbedtls/md.h"
#include "esp_random.h"
#include "config.h"

// ── eigene Identität aus der gewählten Einheit ableiten ─────────────────────
#if defined(CB_UNIT_AUSSEN)
  #define CB_MY_ID   CB_ID_AUSSEN
  static const char* CB_MY_NAME = "AUSSEN";
#elif defined(CB_UNIT_INNEN_EG)
  #define CB_MY_ID   CB_ID_EG
  #define CB_MEIN_KLINGELSOUND CB_SND_KLINGEL_EG
  #define CB_MEIN_KEY          CB_KEY_EG
  static const char* CB_MY_NAME = "INNEN_EG";
#elif defined(CB_UNIT_INNEN_OG)
  #define CB_MY_ID   CB_ID_OG
  #define CB_MEIN_KLINGELSOUND CB_SND_KLINGEL_OG
  #define CB_MEIN_KEY          CB_KEY_OG
  static const char* CB_MY_NAME = "INNEN_OG";
#endif

// ── Nachrichtentypen ─────────────────────────────────────────────────────────
enum CbTyp : uint8_t {
  CB_PING = 1, CB_PONG = 2,
  CB_KLINGEL = 3, CB_KLINGEL_QUITT = 4,
  CB_TUER_AUF = 5, CB_TUER_QUITT = 6
};

// Einheitliches Paket (14 Byte). Türbefehl nutzt tag (HMAC über die ersten
// 6 Byte: typ|senderId|seq). Bei allen anderen Typen ist tag = 0.
typedef struct __attribute__((packed)) {
  uint8_t  typ;
  uint8_t  senderId;
  uint32_t seq;                    // Türbefehl: rollierender Zähler
  uint8_t  tag[CB_HMAC_TAG_LEN];   // Türbefehl: HMAC, sonst 0
} CbNachricht;

// Anzahl der HMAC-relevanten Bytes am Paketanfang (typ|senderId|seq)
#define CB_HMAC_INPUT_LEN  (offsetof(CbNachricht, tag))

// ── Sende-/Retry-Verwaltung (mehrere gleichzeitig, z.B. Klingel EG+OG) ──────
#define CB_MAX_SENDUNGEN 3
struct CbSendung {
  bool      aktiv;
  uint8_t   typ;
  uint8_t   quittTyp;
  uint32_t  seq;
  uint8_t   tag[CB_HMAC_TAG_LEN];
  IPAddress ziel;
  uint32_t  sendeZeit;
  uint8_t   wiederholungen;
};
CbSendung sendungen[CB_MAX_SENDUNGEN];

// Touch-Pad-Zustand (Definition hier oben, damit Arduinos automatische
// Funktions-Prototypen den Typ kennen – sonst "TouchPad not declared").
struct TouchPad {
  uint8_t  pin;
  bool     gedrueckt;
  uint8_t  zaehler;
  uint32_t letzteMessung;
};

// WAV-Header-Infos (Definition oben, wegen Auto-Prototypen)
struct WavInfo { uint32_t sampleRate; uint16_t kanaele; uint16_t bits; uint32_t datenLen; };

WiFiUDP     udp;
Preferences prefs;                // NVS
IPAddress   ipAussen, ipEG, ipOG;
uint32_t    seqZaehler = 0;       // nur für Klingel/Ping (Türbefehl: NVS-Zähler)

// Audio (Etappe E4)
I2SClass  i2s;
File      audioDatei;
bool      audioAktiv   = false;
bool      sdBereit     = false;
uint32_t  audioRest    = 0;       // verbleibende PCM-Bytes
uint16_t  audioKanaele = 1;
uint32_t  i2sRate      = 0;       // aktuell konfigurierte I2S-Rate (0 = aus)
uint8_t   audioVolume  = CB_AUDIO_VOLUME;  // Laufzeit-Lautstärke (aus NVS, per BOOT-Taster)

// ── Krypto-Helfer (beide Einheiten) ─────────────────────────────────────────
static bool hmacSha256(const uint8_t* key, size_t keyLen,
                       const uint8_t* data, size_t dataLen, uint8_t out[32]) {
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == nullptr) return false;
  return mbedtls_md_hmac(info, key, keyLen, data, dataLen, out) == 0;
}

// Konstante-Zeit-Vergleich (kein early-return -> keine Timing-Lecks)
static bool gleichKonstanteZeit(const uint8_t* a, const uint8_t* b, size_t n) {
  uint8_t d = 0;
  for (size_t i = 0; i < n; i++) d |= (uint8_t)(a[i] ^ b[i]);
  return d == 0;
}

// Türbefehl-Tag = HMAC-SHA256(key, typ|senderId|seq)[0..CB_HMAC_TAG_LEN-1]
static void berechneTuerTag(const uint8_t* key, const CbNachricht& n,
                            uint8_t tag[CB_HMAC_TAG_LEN]) {
  uint8_t voll[32];
  hmacSha256(key, 32, (const uint8_t*)&n, CB_HMAC_INPUT_LEN, voll);
  memcpy(tag, voll, CB_HMAC_TAG_LEN);
}

#if defined(CB_UNIT_AUSSEN)
// Schlüssel des jeweiligen Senders (Außeneinheit kennt beide)
static const uint8_t* keyFuerSender(uint8_t senderId) {
  if (senderId == CB_ID_EG) return CB_KEY_EG;
  if (senderId == CB_ID_OG) return CB_KEY_OG;
  return nullptr;
}
static const char* nvsKeyFuer(uint8_t senderId) {
  return (senderId == CB_ID_EG) ? "lastEG" : "lastOG";
}
#else
uint32_t letzteKlingelSeq = 0;
#endif

// ── Hilfen ───────────────────────────────────────────────────────────────────
static bool wlanOk() { return WiFi.status() == WL_CONNECTED; }

// ── Audio: nicht-blockierender WAV-Player (Etappe E4) ───────────────────────
static bool audioLeseU16(File& f, uint16_t& v) {
  uint8_t b[2]; if (f.read(b, 2) != 2) return false; v = b[0] | (b[1] << 8); return true;
}
static bool audioLeseU32(File& f, uint32_t& v) {
  uint8_t b[4]; if (f.read(b, 4) != 4) return false;
  v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
  return true;
}
// Parst RIFF/WAVE, sucht fmt+data. Nach Erfolg steht f auf den PCM-Daten.
static bool audioParseWav(File& f, WavInfo& wi) {
  char id[4]; uint32_t dummy;
  if (f.read((uint8_t*)id, 4) != 4 || memcmp(id, "RIFF", 4)) return false;
  if (!audioLeseU32(f, dummy)) return false;
  if (f.read((uint8_t*)id, 4) != 4 || memcmp(id, "WAVE", 4)) return false;
  bool fmtOk = false;
  while (true) {
    if (f.read((uint8_t*)id, 4) != 4) return false;
    uint32_t len; if (!audioLeseU32(f, len)) return false;
    if (!memcmp(id, "fmt ", 4)) {
      uint16_t af, ba; uint32_t br;
      audioLeseU16(f, af); audioLeseU16(f, wi.kanaele); audioLeseU32(f, wi.sampleRate);
      audioLeseU32(f, br); audioLeseU16(f, ba); audioLeseU16(f, wi.bits);
      if (len > 16) f.seek(f.position() + (len - 16));
      fmtOk = true;
    } else if (!memcmp(id, "data", 4)) {
      wi.datenLen = len; return fmtOk;
    } else {
      f.seek(f.position() + len + (len & 1));
    }
  }
}
// I2S nur (um)konfigurieren, wenn sich die Rate ändert.
static void audioI2SRate(uint32_t rate) {
  if (i2sRate == rate) return;
  if (i2sRate != 0) i2s.end();
  i2sRate = 0;
  if (i2s.begin(I2S_MODE_STD, rate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO))
    i2sRate = rate;
}

// Startet die Wiedergabe – kehrt SOFORT zurück (Streaming in audioPflege()).
void spieleSound(const char* datei) {
  if (!sdBereit) { Serial.printf("   [Audio] keine SD -> %s stumm\n", datei); return; }
  if (audioAktiv) { audioDatei.close(); audioAktiv = false; }   // laufenden Sound ersetzen
  audioDatei = SD.open(datei);
  if (!audioDatei) { Serial.printf("   [Audio] %s nicht gefunden\n", datei); return; }
  WavInfo wi;
  if (!audioParseWav(audioDatei, wi) || wi.bits != 16) {
    Serial.printf("   [Audio] %s kein 16-bit-WAV\n", datei); audioDatei.close(); return;
  }
  audioI2SRate(wi.sampleRate);
  if (i2sRate == 0) { Serial.println("   [Audio] I2S-Start fehlgeschlagen"); audioDatei.close(); return; }
  audioRest    = wi.datenLen;
  audioKanaele = wi.kanaele;
  audioAktiv   = true;
  Serial.printf("   [Audio] spiele %s (%lu Hz, %u Kanal)\n",
                datei, (unsigned long)wi.sampleRate, wi.kanaele);
}

// Füttert pro loop()-Durchlauf EINEN kleinen Block an I2S (nicht-blockierend).
void audioPflege() {
  if (!audioAktiv) return;
  uint8_t buf[512];
  uint32_t chunk = audioRest < sizeof(buf) ? audioRest : sizeof(buf);
  int gelesen = audioDatei.read(buf, chunk);
  if (gelesen <= 0) { audioDatei.close(); audioAktiv = false; return; }
  audioRest -= gelesen;
  int16_t* s = (int16_t*)buf;
  size_t anzahl = gelesen / 2;
  if (audioKanaele == 1) {
    int16_t stereo[512];                    // buf=512B -> max 256 Samples -> 512 int16
    for (size_t i = 0; i < anzahl; i++) {
      int16_t v = (int16_t)((int32_t)s[i] * audioVolume / 100);  // Lautstärke
      stereo[2*i] = v; stereo[2*i+1] = v;
    }
    i2s.write((uint8_t*)stereo, anzahl * 4); // mono -> L+R
  } else {
    for (size_t i = 0; i < anzahl; i++)
      s[i] = (int16_t)((int32_t)s[i] * audioVolume / 100);      // Lautstärke
    i2s.write(buf, gelesen);                 // bereits stereo-interleaved
  }
  if (audioRest == 0) { audioDatei.close(); audioAktiv = false; }
}

// ── Lautstärke per BOOT-Taster (GPIO0) durchschalten ────────────────────────
const uint8_t VOL_STUFEN[] = {15, 30, 50, 75, 100};
uint32_t volTasteZeit   = 0;
bool     volTasteVorher = false;

void naechsteVolumeStufe() {
  uint8_t next = VOL_STUFEN[0];                       // Standard: kleinste (Wrap)
  for (uint8_t v : VOL_STUFEN) { if (v > audioVolume) { next = v; break; } }
  audioVolume = next;
  prefs.putUInt("vol", audioVolume);                  // dauerhaft in NVS merken
  Serial.printf("Lautstärke -> %u %%\n", audioVolume);
  spieleSound(CB_SND_OK);                             // Feedback im neuen Pegel
}

// Entprellter Flankenerkenner (BOOT-Taster zieht GPIO0 auf LOW).
void volTasterPflege() {
  bool gedrueckt = (digitalRead(CB_VOL_TASTER_GPIO) == LOW);
  uint32_t jetzt = millis();
  if (gedrueckt && !volTasteVorher && (jetzt - volTasteZeit > 250)) {
    volTasteZeit = jetzt;
    naechsteVolumeStufe();
  }
  volTasteVorher = gedrueckt;
}

void erfolgTon() { Serial.println(">> Erfolg  -> Bestätigungston");  spieleSound(CB_SND_OK); }
void fehlerTon() { Serial.println(">> Fehler  -> Fehlerton");        spieleSound(CB_SND_FEHLER); }

#if defined(CB_UNIT_AUSSEN)
void oeffneTuer() {
  // Etappe: GPIO-Puls nicht-blockierend (CB_TUER_GPIO, CB_TUER_PULS_MS).
  Serial.printf("   [STUB Tür] Türöffner-Puls %d ms\n", CB_TUER_PULS_MS);
}

// ── Fern-Türöffnen: Einmal-Token (ring-gebunden) ────────────────────────────
// Bei jedem EG-Klingeln wird ein 128-bit-Zufallstoken erzeugt und kurz
// gespeichert. Das Token selbst ist die Capability (unfälschbar); die
// Außeneinheit ist Aussteller UND Prüfer. Einmalig + zeitlich begrenzt.
#define CB_MAX_TOKENS 4
struct TuerToken { char token[33]; uint32_t ausgestellt; bool aktiv; };
TuerToken tuerTokens[CB_MAX_TOKENS];

// Erzeugt ein neues Token (32 Hex-Zeichen), speichert es, liefert es in out[33].
void neuesTuerToken(char out[33]) {
  for (int i = 0; i < 16; i++) sprintf(out + i * 2, "%02x", (uint8_t)(esp_random() & 0xFF));
  out[32] = '\0';
  int slot = 0; uint32_t aeltest = UINT32_MAX;      // freier oder ältester Slot
  for (int i = 0; i < CB_MAX_TOKENS; i++) {
    if (!tuerTokens[i].aktiv) { slot = i; break; }
    if (tuerTokens[i].ausgestellt < aeltest) { aeltest = tuerTokens[i].ausgestellt; slot = i; }
  }
  strcpy(tuerTokens[slot].token, out);
  tuerTokens[slot].ausgestellt = millis();
  tuerTokens[slot].aktiv = true;
}

// ── ntfy-Push (asynchron via FreeRTOS-Task, damit loop() nicht blockiert) ───
volatile bool pushAngefordert = false;
char          pushText[80];
char          pushToken[33];      // Token für den "Tür öffnen"-Button ("" = kein Button)

void pushTask(void*) {
  for (;;) {
    if (pushAngefordert) {
      pushAngefordert = false;
      if (wlanOk()) {
        WiFiClientSecure client;
        client.setInsecure();                    // ntfy.sh: ohne Zertifikatsprüfung
        HTTPClient http;
        String url = String(CB_NTFY_SERVER) + "/" + CB_NTFY_TOPIC_EG;
        if (http.begin(client, url)) {
          http.addHeader("Title", CB_NTFY_TITLE);
          http.addHeader("Priority", CB_NTFY_PRIORITY);
          if (pushToken[0]) {
            // "Tür öffnen"-Button: postet das Einmal-Token ans Kommando-Topic
            String actions = String("http, ") + CB_TUER_BTN_LABEL + ", " +
                             CB_NTFY_SERVER + "/" + CB_NTFY_CMD_TOPIC +
                             ", method=POST, body=" + pushToken + ", clear=true";
            http.addHeader("Actions", actions);
          }
          int code = http.POST((uint8_t*)pushText, strlen(pushText));
          Serial.printf("   [Push] ntfy HTTP %d\n", code);
          http.end();
        } else {
          Serial.println("   [Push] Verbindung fehlgeschlagen");
        }
      } else {
        Serial.println("   [Push] kein WLAN -> übersprungen");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// Fordert einen Push an – kehrt SOFORT zurück (Task erledigt den HTTP-POST).
// token==nullptr -> ohne Button; sonst mit "Tür öffnen"-Aktion.
void sendePush(const char* text, const char* token) {
  strncpy(pushText, text, sizeof(pushText) - 1);
  pushText[sizeof(pushText) - 1] = '\0';
  if (token) { strncpy(pushToken, token, sizeof(pushToken) - 1); pushToken[sizeof(pushToken) - 1] = '\0'; }
  else         pushToken[0] = '\0';
  pushAngefordert = true;
}

// EG-Klingel-Push: erzeugt ein frisches Einmal-Token und schickt die Push
// mit "Tür öffnen"-Button.
void klingelPushEG() {
  char tok[33];
  neuesTuerToken(tok);
  sendePush(CB_NTFY_TEXT, tok);
}

// ── Fern-Türöffnen: Kommando-Empfang (SSE/raw in eigenem Task) ──────────────
// Prüft eine empfangene Zeile gegen die Token-Tabelle: einmalig (sofort
// verbraucht) + zeitlich begrenzt. Kein Treffer -> stillschweigend ignoriert
// (deckt Chunk-Größen/Keepalives/Fremdmüll ab).
void verarbeiteKommando(const String& zeile) {
  for (int i = 0; i < CB_MAX_TOKENS; i++) {
    if (!tuerTokens[i].aktiv) continue;
    if (zeile == tuerTokens[i].token) {
      tuerTokens[i].aktiv = false;                       // one-time: sofort verbrauchen
      if (millis() - tuerTokens[i].ausgestellt <= CB_TUER_TOKEN_TTL_MS) {
        Serial.println("   [Cmd] gültiges Token -> Tür öffnen");
        oeffneTuer();
      } else {
        Serial.println("   [Cmd] Token abgelaufen -> verworfen");
      }
      return;
    }
  }
}

// Hält eine Streaming-Verbindung zum Kommando-Topic offen und reicht jede
// empfangene Zeile an verarbeiteKommando(). Reconnect bei Abriss.
void cmdTask(void*) {
  for (;;) {
    if (!wlanOk()) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    WiFiClientSecure client;
    client.setInsecure();
    if (!client.connect("ntfy.sh", 443)) {
      Serial.println("   [Cmd] Verbindung zu ntfy fehlgeschlagen");
      vTaskDelay(pdMS_TO_TICKS(3000));
      continue;
    }
    client.printf("GET /%s/raw HTTP/1.1\r\nHost: ntfy.sh\r\nConnection: keep-alive\r\n\r\n",
                  CB_NTFY_CMD_TOPIC);
    Serial.println("   [Cmd] lausche auf Kommando-Topic");

    bool     headerFertig = false;
    String   zeile;
    uint32_t letzteDaten = millis();
    while (client.connected()) {
      while (client.available()) {
        char c = client.read();
        letzteDaten = millis();
        if (c == '\r') continue;
        if (c == '\n') {
          if (!headerFertig) { if (zeile.length() == 0) headerFertig = true; }  // Leerzeile = Header-Ende
          else if (zeile.length() > 0) verarbeiteKommando(zeile);
          zeile = "";
        } else {
          zeile += c;
          if (zeile.length() > 200) zeile = "";          // Schutz gegen Müll
        }
      }
      if (millis() - letzteDaten > 60000) break;          // 60 s still -> neu verbinden
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    client.stop();
    Serial.println("   [Cmd] Verbindung getrennt -> Neuaufbau");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
#endif

void sendePaket(uint8_t typ, uint32_t seq, const uint8_t* tag, IPAddress ziel) {
  CbNachricht n;
  memset(&n, 0, sizeof(n));
  n.typ = typ;
  n.senderId = CB_MY_ID;
  n.seq = seq;
  if (tag) memcpy(n.tag, tag, CB_HMAC_TAG_LEN);
  udp.beginPacket(ziel, CB_UDP_PORT);
  udp.write((const uint8_t*)&n, sizeof(n));
  udp.endPacket();
}

// Startet eine quittungspflichtige Sendung. Ohne WLAN sofort Fehler.
void starteSendung(uint8_t typ, uint8_t quittTyp, uint32_t seq,
                   const uint8_t* tag, IPAddress ziel) {
  if (!wlanOk()) {
    Serial.println("WLAN offline -> Sendung sofort als Fehler gewertet");
    fehlerTon();
    return;
  }
  CbSendung* s = nullptr;
  for (auto& e : sendungen) if (!e.aktiv) { s = &e; break; }
  if (!s) { Serial.println("Keine freie Sendung -> Fehler"); fehlerTon(); return; }

  s->aktiv = true;
  s->typ = typ;
  s->quittTyp = quittTyp;
  s->seq = seq;
  memset(s->tag, 0, CB_HMAC_TAG_LEN);
  if (tag) memcpy(s->tag, tag, CB_HMAC_TAG_LEN);
  s->ziel = ziel;
  s->wiederholungen = 0;
  s->sendeZeit = millis();
  sendePaket(typ, seq, tag, ziel);
  Serial.printf("Sendung typ=%u seq=%lu -> %s\n",
                typ, (unsigned long)seq, ziel.toString().c_str());
}

#if !defined(CB_UNIT_AUSSEN)
// Türbefehl bauen: NVS-Zähler hochzählen+persistieren, HMAC berechnen, senden.
// falscherSchluessel=true nur für den Sicherheits-Test ('x'): die Außeneinheit
// muss den Befehl dann wegen HMAC-Fehler verwerfen.
void sendeTuerbefehl(bool falscherSchluessel = false) {
  uint32_t counter = prefs.getUInt("tuerCtr", 0) + 1;
  prefs.putUInt("tuerCtr", counter);   // VOR dem Senden persistieren -> nie doppelt

  CbNachricht tmp;
  memset(&tmp, 0, sizeof(tmp));
  tmp.typ = CB_TUER_AUF;
  tmp.senderId = CB_MY_ID;
  tmp.seq = counter;

  uint8_t key[32];
  memcpy(key, CB_MEIN_KEY, 32);
  if (falscherSchluessel) {
    key[0] ^= 0xFF;   // Schlüssel verfälschen -> HMAC passt nicht zur Außeneinheit
    Serial.println("!! TEST: Türbefehl mit ABSICHTLICH falschem Schlüssel");
  }
  uint8_t tag[CB_HMAC_TAG_LEN];
  berechneTuerTag(key, tmp, tag);

  Serial.printf("Türbefehl counter=%lu (NVS), HMAC berechnet\n", (unsigned long)counter);
  starteSendung(CB_TUER_AUF, CB_TUER_QUITT, counter, tag, ipAussen);
}
#endif

void pruefeSendungen() {
  uint32_t jetzt = millis();
  for (auto& s : sendungen) {
    if (!s.aktiv) continue;
    if (jetzt - s.sendeZeit < CB_QUITT_TIMEOUT_MS) continue;
    if (s.wiederholungen < CB_MAX_WIEDERHOLUNGEN) {
      s.wiederholungen++;
      sendePaket(s.typ, s.seq, s.tag, s.ziel);   // gleicher tag bei Wiederholung
      s.sendeZeit = jetzt;
    } else {
      s.aktiv = false;
      Serial.printf("seq=%lu endgültig verloren\n", (unsigned long)s.seq);
      fehlerTon();
    }
  }
}

void verarbeite(const CbNachricht& n, IPAddress von) {
  // 1) Quittung zu einer eigenen offenen Sendung?
  for (auto& s : sendungen) {
    if (s.aktiv && n.typ == s.quittTyp && n.seq == s.seq && von == s.ziel) {
      s.aktiv = false;
      Serial.printf("Quittung typ=%u seq=%lu erhalten\n", n.typ, (unsigned long)n.seq);
      erfolgTon();
      return;
    }
  }

  // 2) Eingehende Anfragen (einheitsspezifisch)
#if defined(CB_UNIT_AUSSEN)
  if (n.typ == CB_TUER_AUF) {
    // 2a) Echtheit: HMAC mit dem Schlüssel des angegebenen Senders prüfen
    const uint8_t* key = keyFuerSender(n.senderId);
    if (key == nullptr) {
      Serial.printf("TUER_AUF: unbekannter Sender %u -> verworfen\n", n.senderId);
      return;
    }
    uint8_t erwartet[CB_HMAC_TAG_LEN];
    berechneTuerTag(key, n, erwartet);
    if (!gleichKonstanteZeit(erwartet, n.tag, CB_HMAC_TAG_LEN)) {
      Serial.printf("TUER_AUF seq=%lu: HMAC UNGÜLTIG -> verworfen (Fälschung?)\n",
                    (unsigned long)n.seq);
      return;   // kein Quittieren, kein Öffnen
    }
    // 2b) Frische: Replay-Schutz über rollierenden Zähler (NVS)
    uint32_t last = prefs.getUInt(nvsKeyFuer(n.senderId), 0);
    if (n.seq > last) {
      prefs.putUInt(nvsKeyFuer(n.senderId), n.seq);
      sendePaket(CB_TUER_QUITT, n.seq, nullptr, von);
      Serial.printf("TUER_AUF von ID %u seq=%lu -> echt & frisch -> öffnen\n",
                    n.senderId, (unsigned long)n.seq);
      oeffneTuer();
    } else if (n.seq == last) {
      sendePaket(CB_TUER_QUITT, n.seq, nullptr, von);   // Retransmit: nur requittieren
      Serial.printf("TUER_AUF seq=%lu == last -> Retransmit, nur requittiert\n",
                    (unsigned long)n.seq);
    } else {
      Serial.printf("TUER_AUF seq=%lu < last=%lu -> REPLAY -> verworfen\n",
                    (unsigned long)n.seq, (unsigned long)last);
      // bei Replay bewusst NICHT quittieren
    }
  }
#else
  if (n.typ == CB_KLINGEL) {
    bool dup = (n.seq == letzteKlingelSeq);
    letzteKlingelSeq = n.seq;
    sendePaket(CB_KLINGEL_QUITT, n.seq, nullptr, von); // Quittung ZUERST (µs) – blockiert Ton nicht
    if (!dup) {
      Serial.printf("KLINGEL seq=%lu -> Klingelton SOFORT\n", (unsigned long)n.seq);
      spieleSound(CB_MEIN_KLINGELSOUND);
    } else {
      Serial.printf("KLINGEL seq=%lu -> Duplikat, nur requittiert\n", (unsigned long)n.seq);
    }
  }
#endif
}

// ── WLAN nicht-blockierend halten ───────────────────────────────────────────
uint32_t letzterWlanVersuch = 0;
bool     warVerbunden = false;
void wlanPflege() {
  if (wlanOk()) {
    if (!warVerbunden) {
      warVerbunden = true;
      udp.begin(CB_UDP_PORT);
      Serial.printf("WLAN verbunden. IP: %s | RSSI: %d dBm | Kanal: %d\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel());
    }
    return;
  }
  if (warVerbunden) { warVerbunden = false; Serial.println("WLAN verloren -> Neuverbindung im Hintergrund"); }
  uint32_t jetzt = millis();
  if (jetzt - letzterWlanVersuch >= 3000) {
    letzterWlanVersuch = jetzt;
    WiFi.disconnect();
    WiFi.begin(CB_WIFI_SSID, CB_WIFI_PASS);
    Serial.println("WLAN-Verbindungsversuch...");
  }
}

void empfange() {
  int len = udp.parsePacket();
  if (len == (int)sizeof(CbNachricht)) {
    CbNachricht n;
    udp.read((uint8_t*)&n, sizeof(n));
    verarbeite(n, udp.remoteIP());
  } else if (len > 0) {
    udp.flush();   // Fremdpaket verwerfen
  }
}

// Etappe D ersetzt das durch echte Touch-Auslösung
void serielleTestbefehle() {
  if (!Serial.available()) return;
  char c = Serial.read();
#if defined(CB_UNIT_AUSSEN)
  if (c == 'e') {
    Serial.println("[Test] Klingel EG");
    klingelPushEG();  // Push nur EG, best-effort parallel
    starteSendung(CB_KLINGEL, CB_KLINGEL_QUITT, ++seqZaehler, nullptr, ipEG);
  } else if (c == 'o') {
    Serial.println("[Test] Klingel OG");
    starteSendung(CB_KLINGEL, CB_KLINGEL_QUITT, ++seqZaehler, nullptr, ipOG);
  }
#else
  if (c == 't') {
    Serial.println("[Test] Türbefehl (gültig)");
    sendeTuerbefehl(false);
  } else if (c == 'x') {
    Serial.println("[Test] Türbefehl (FALSCHER Schlüssel -> muss verworfen werden)");
    sendeTuerbefehl(true);
  }
#endif
}

// ── Touch-Taster (Etappe D) ─────────────────────────────────────────────────
// Zum Kalibrieren der Schwellen einkommentieren -> Rohwerte auf Serial:
//#define CB_TOUCH_DEBUG

#if defined(CB_UNIT_AUSSEN)
TouchPad padEG = { CB_TOUCH_EG_GPIO, false, 0, 0 };
TouchPad padOG = { CB_TOUCH_OG_GPIO, false, 0, 0 };
#else
TouchPad padTuer = { CB_TOUCH_GPIO, false, 0, 0 };
#endif

// Liefert true genau bei einer NEUEN Berührung (steigende Flanke).
// Klassischer ESP32: touchRead() sinkt beim Berühren. Hysterese: gedrückt
// unter EIN (600), losgelassen erst wieder über AUS (800). CB_TOUCH_READS-fach
// entprellt im CB_TOUCH_INTERVALL_MS-Takt.
bool touchFlanke(TouchPad& t) {
  uint32_t jetzt = millis();
  if (jetzt - t.letzteMessung < CB_TOUCH_INTERVALL_MS) return false;
  t.letzteMessung = jetzt;

  uint32_t wert = touchRead(t.pin);
#ifdef CB_TOUCH_DEBUG
  Serial.printf("touch pin %u = %lu\n", t.pin, (unsigned long)wert);
#endif
  bool zielGedrueckt = t.gedrueckt ? (wert < CB_TOUCH_AUS)    // bleibt gedrückt bis über AUS
                                   : (wert < CB_TOUCH_EIN);   // wird gedrückt unter EIN
  if (zielGedrueckt != t.gedrueckt) {
    if (++t.zaehler >= CB_TOUCH_READS) {
      t.gedrueckt = zielGedrueckt;
      t.zaehler = 0;
      if (t.gedrueckt) return true;   // steigende Flanke
    }
  } else {
    t.zaehler = 0;
  }
  return false;
}

void touchPflege() {
#if defined(CB_UNIT_AUSSEN)
  if (touchFlanke(padEG)) {
    Serial.println("[Touch] EG -> Klingel EG");
    klingelPushEG();
    starteSendung(CB_KLINGEL, CB_KLINGEL_QUITT, ++seqZaehler, nullptr, ipEG);
  }
  if (touchFlanke(padOG)) {
    Serial.println("[Touch] OG -> Klingel OG");
    starteSendung(CB_KLINGEL, CB_KLINGEL_QUITT, ++seqZaehler, nullptr, ipOG);
  }
#else
  if (touchFlanke(padTuer)) {
    Serial.println("[Touch] Türtaster -> Türbefehl");
    sendeTuerbefehl(false);
  }
#endif
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\n=== ClearBell %s | FW %s ===\n", CB_MY_NAME, CB_FW_VERSION);

  prefs.begin("clearbell", false);   // NVS-Namespace
#if defined(CB_UNIT_AUSSEN)
  Serial.printf("NVS: lastEG=%lu lastOG=%lu\n",
                (unsigned long)prefs.getUInt("lastEG", 0),
                (unsigned long)prefs.getUInt("lastOG", 0));
#else
  Serial.printf("NVS: Türzähler=%lu\n", (unsigned long)prefs.getUInt("tuerCtr", 0));
#endif

  ipAussen.fromString(CB_IP_AUSSEN);
  ipEG.fromString(CB_IP_INNEN_EG);
  ipOG.fromString(CB_IP_INNEN_OG);   // Hinweis: OG-IP in config.h noch TODO

  for (auto& s : sendungen) s.aktiv = false;

  // Audio: SD-Karte + I2S initialisieren (Gerät läuft auch ohne SD weiter)
  SPI.begin(CB_SD_SCK, CB_SD_MISO, CB_SD_MOSI, CB_SD_CS);
  sdBereit = SD.begin(CB_SD_CS, SPI, 4000000);
  Serial.println(sdBereit ? "SD bereit." : "SD fehlt -> Sounds stumm (läuft weiter).");
  i2s.setPins(CB_I2S_BCLK, CB_I2S_LRCLK, CB_I2S_DIN);

  // Lautstärke aus NVS (Startwert CB_AUDIO_VOLUME) + BOOT-Taster als Eingang
  audioVolume = prefs.getUInt("vol", CB_AUDIO_VOLUME);
  if (audioVolume > 100) audioVolume = CB_AUDIO_VOLUME;
  pinMode(CB_VOL_TASTER_GPIO, INPUT_PULLUP);
  Serial.printf("Lautstärke: %u %% (BOOT-Taster schaltet durch)\n", audioVolume);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);                       // Modem-Sleep AN (Standard) -> weniger Strom.
                                             // Latenz war im Test unkritisch; falls doch mal
                                             // nötig, auf false setzen (kostet mehr Strom).
  WiFi.begin(CB_WIFI_SSID, CB_WIFI_PASS);   // nicht blockierend – wlanPflege() übernimmt

#if defined(CB_UNIT_AUSSEN)
  xTaskCreate(pushTask, "push", 16384, nullptr, 1, nullptr);  // ntfy-Push asynchron
  xTaskCreate(cmdTask,  "cmd",  16384, nullptr, 1, nullptr);  // Kommando-Empfang (SSE)
#endif

  Serial.println("Starte... (WLAN im Hintergrund, Gerät läuft auch ohne Netz)");
}

void loop() {
  wlanPflege();
  empfange();
  pruefeSendungen();
  audioPflege();          // WAV-Streaming (nicht-blockierend)
  volTasterPflege();      // BOOT-Taster -> Lautstärke durchschalten
  touchPflege();          // echte Auslöser
  serielleTestbefehle();  // Test-Backup ('t'/'x' bzw. 'e'/'o')
  delay(2);
}
