# ClearBell – Firmware Architektur

> Stand: 2026-07-10. Maßgebliche Kurz-Referenz ist der detaillierten Firmware-Doku.
> Verifizierte Werte; bei Widersprüchen gilt die jeweils neuere Angabe.

## Framework & Bibliotheken

| Komponente | Bibliothek | Quelle |
|---|---|---|
| ESP32 Kern | Arduino Core für ESP32 **3.3.10** | Arduino Board Manager |
| WiFi / UDP | WiFi.h, WiFiUdp.h | im Core enthalten |
| Statuswebseite | WebServer.h | im Core enthalten |
| I2S Audio | **ESP_I2S** (Core-3.x built-in), eigener WAV-Player | im Core enthalten |
| SD-Karte | SD.h | im Core enthalten |

**Kein ESPHome. Kein Home Assistant. Kein MQTT. Kein ESP-NOW.**

> **Audio-Hinweis:** Kein `ESP32-audioI2S` (schreibfaul1) – diese Bibliothek
> setzt PSRAM voraus, das der verbaute ESP32-WROOM-32E-N8 **nicht** hat.
> Stattdessen eigener WAV-Player über `ESP_I2S` mit 1-KB-Chunks direkt von SD.
> Audioformat daher **WAV**, nicht MP3.

---

## Transport (ENTSCHIEDEN – nicht neu aufrollen)

- **WLAN + UDP** direkt zwischen den Einheiten über den Heim-Router.
- ESP-NOW wurde nach vollständiger Funkvalidierung **verworfen** (Strecke an
  den Montageorten funktional tot). Details:
  `Funkvalidierung_ESPNOW_2026-07-05.md`. Bitte nicht erneut vorschlagen.
- Netz: Hauptnetz "MEIN_WLAN", feste IPs via DHCP-Reservierung, UDP-Port 4210.
- Funk-Unterbau ist abgenommen (2026-07-06): Klingel (Außen→Innen) und Tür
  (Innen→Außen) beidseitig quittiert, 0 Verluste am verschärften Testpunkt.

---

## Grundprinzip: zwei unabhängige Wohnungen

Das Haus hat **zwei getrennte Wohnungen** (EG, OG) mit je eigener
Inneneinheit. Die Außeneinheit trägt **zwei Klingeltaster**:

- **EG-Taster (GPIO32)** → nur **EG-Inneneinheit**
- **OG-Taster (GPIO33)** → nur **OG-Inneneinheit**

Beide Klingelwege sind **vollständig unabhängig** (1:1-Zuordnung, kein
Broadcast). Ein Klingeln ist jeweils eine eigenständige Transaktion zu genau
einer Inneneinheit. Der Türöffner an der Außeneinheit ist **gemeinsam**
(Haustür): beide Inneneinheiten können ihn per Touch-Taster auslösen.

---

## Systemarchitektur

```
┌─────────────────────────────────────────────┐
│              Außeneinheit                   │
│  ESP32-WROOM-32E-N8  (12V → 3,3V LMR33630)  │
│                                             │
│  Touch EG (GPIO32) ──┐                      │
│  Touch OG (GPIO33) ──┼→ State Machine       │
│                      │      │               │
│                      │      ├─ I2S Audio    │
│                      │      │  MAX98357A    │
│                      │      │  SD /klingel  │
│                      │      │               │
│                      │      ├─ Türöffner    │
│                      │      │  IRLML6344    │
│                      │      │  (GPIO21)     │
│                      │      │               │
│                      │      └─ WLAN + UDP ──┼──→ Inneneinheit 1 (EG)
│                      │         Port 4210    │──→ Inneneinheit 2 (OG)
└──────────────────────┼──────────────────────┘
                       │  (über Heim-Router, feste IPs)
┌──────────────────────┼──────────────────────┐
│           Inneneinheit (2×)                 │
│  ESP32-WROOM-32E-N8  (USB-C 5V → 3,3V)      │
│                      │                      │
│  WLAN + UDP ◄────────┘                      │
│      │                                      │
│      ├→ I2S Audio (MAX98357A + SD)          │
│      │                                      │
│  Touch (GPIO32) → Türöffner-Befehl ─────────┼──→ Außeneinheit (UDP)
└─────────────────────────────────────────────┘
```

---

## Kommunikationsprotokoll (UDP)

Validiertes Referenzprotokoll aus Testsketch
`Testsketche\UDP_Test2\UDP_Test2.ino` (v0.2). **Testcode – Protokoll gilt,
Struktur wird für die Produktivfirmware neu aufgebaut.**

```
Nachricht {
    typ : uint8_t   (PING / PONG / TUER_AUF / TUER_QUITT / KLINGEL / KLINGEL_QUITT)
    seq : uint32_t  (laufender Zähler, Duplikaterkennung)
}  packed
```

- **Anwendungsquittung** ist das einzige Erfolgskriterium (UDP ist
  verbindungslos, keine Transportbestätigung).
- Timeout 300 ms, max. 3 Wiederholungen, dann "endgültig verloren".
- Duplikaterkennung über `seq` beim Empfänger; bei Duplikat trotzdem
  quittieren (verlorene Quittung).
- Web-Auslösung nur per HTTP **POST** (nie GET) + 3 s Sperrzeit.
- Statuswebseite je Einheit (RSSI, Kanal, BSSID, Zähler, WLAN-Abrisse).

> **Offen für Produktivfassung:** Anwendungs-Authentifizierung des Türbefehls
> (gemeinsamer Schlüssel + laufender Zähler gegen Replay; WPA2 sichert nur die
> Netzgrenze). Dies ist die nächste Design-Aufgabe.

---

## Smartphone-Push (im Scope, 2026-07-10 bestätigt)

Push aufs Smartphone via ntfy.sh (HTTP POST). Benötigt Internet-Zugang der
Außeneinheit.

- **Nur EG-Wohnung** bekommt aktuell einen Push (eigenes ntfy-Topic). Die
  OG-Wohnung läuft rein lokal über ihre Inneneinheit, kein Push.
- Pro Klingeltaster konfigurierbar (Topic je Taster; OG-Taster = kein Topic),
  damit OG später ohne Umbau ergänzt werden kann.
- **Bei fehlendem Internet:** Push wird übersprungen (Best-Effort, kein
  Blockieren/Absturz). Der Besucher hört ohnehin den Tür-Feedbackton.

---

## Akustisches Feedback (entschieden 2026-07-10)

Beide Richtungen geben ergebnisabhängige Töne – jeweils gesteuert davon, ob
die Anwendungsquittung innerhalb des Retry-Fensters eintrifft.

**Klingeln (Außeneinheit → Wohnung):** Besucher-Feedback an der Tür
- Erfolg (Inneneinheit quittiert) → Bestätigungston an der Außeneinheit
- Fehlschlag (keine Quittung: Wohnung aus / kein WLAN) → deutlich anderer
  „konnte nicht klingeln"-Ton

**Türöffner (Wohnung → Außeneinheit):** Bewohner-Feedback in der Wohnung
- Erfolg (Außeneinheit quittiert) → Bestätigungston in der Inneneinheit
- Fehlschlag (keine Quittung) → Fehlerton in der Inneneinheit

> UX-Hinweis: Bestätigungston kommt im Normalfall praktisch sofort (Quittung in
> wenigen ms). Der Fehlerton kommt erst nach Ablauf der 3 Retries (~1 s), da
> ein Fehlschlag erst dann feststeht. Ohne WLAN → sofort Fehlerpfad.
>
> Benötigte Sounds daher je Einheit: Erfolgs- und Fehlerton (Umsetzung beim
> Audio-Schritt: eigene WAV-Dateien oder generierte Töne).

---

## GPIO-Belegung (verifiziert)

### Außeneinheit

| GPIO | Funktion | Anmerkung |
|---|---|---|
| 5  | SD CS   | SPI Chip-Select |
| 18 | SD SCK  | SPI Clock |
| 19 | SD MISO | SPI |
| 23 | SD MOSI | SPI |
| 26 | I2S BCLK  | MAX98357A |
| 25 | I2S LRCLK/WS | MAX98357A |
| 22 | I2S DIN   | MAX98357A |
| 32 | Touch T9 | Klingeltaster EG, 1 kΩ Serie |
| 33 | Touch T8 | Klingeltaster OG, 1 kΩ Serie |
| 21 | Türöffner | IRLML6344 (Low-Side), 100 Ω Gate |

### Inneneinheit

| GPIO | Funktion |
|---|---|
| 23 | SD CS |
| 21 | SD MOSI |
| 19 | SD SCK |
| 18 | SD MISO |
| 32 | Touch (Türöffner-Befehl) |

> Hinweis: Die SD-SPI-Pinbelegung unterscheidet sich zwischen Außen- und
> Inneneinheit (unterschiedliche PCB-Layouts).

---

## SD-Karte – Dateistruktur

Format: **FAT32**

```
/ (Root)
├── klingel_eg.wav    ← Klingelton EG-Taste
└── klingel_og.wav    ← Klingelton OG-Taste (kann identisch sein)
```

WAV-Dateien (eigener Player über ESP_I2S, 1-KB-Chunks). Endgültige
Dateinamen/Abtastrate beim Audio-Schritt festlegen.

---

## MAC-Adressen (WLAN-STA, per eFuse)

| Einheit | MAC |
|---|---|
| Innen  | `4C:C3:82:A9:D3:24` |
| Außen  | `4C:C3:82:A9:D1:E0` |

Auslesen mit `esp_read_mac(mac, ESP_MAC_WIFI_STA)` (aus `esp_mac.h`).
`WiFi.macAddress()` liefert vor WLAN-Start `00:00:...` – nicht verwenden.

---

## Firmware-Fallstricke (im Test verifiziert)

- **Keine** generischen Großbuchstaben-Bezeichner (`STATUS`, `OK`, `ERROR`) –
  kollidieren mit ESP32-ROM-Headern (z. B. `ets_sys.h`). Projektpräfix nutzen.
- Touch (Core 3.x): `touchRead()` liefert ~1000er-Bereich. Baseline ~1080–1185,
  berührt ~190–290. Testschwellen: EIN=600 / AUS=800, 2-fach entprellt bei
  50 ms. Werte gelten für blanke Elektroden → nach Endmontage neu bestimmen.
- I2S-Sinus: 2·π-gewrappter Phasenakkumulator, kein wachsender Index
  (float-Präzisionsverlust → hörbare Verzerrung).

---

## Offene Verhaltensfragen (vor Produktivfirmware klären)

| Frage | Status |
|---|---|
| Smartphone-Push (ntfy) im Scope? | ✅ ja, nur EG-Wohnung (2026-07-10) |
| Push-Verhalten bei fehlendem Internet | ✅ Best-Effort übersprungen (2026-07-10) |
| Klingel-Retry bei unerreichbarer Inneneinheit | ✅ Standard-Retry pro Einheit (2026-07-10) |
| Türbefehl-Rückmeldung an Bewohner | ✅ immer: Erfolgs-/Fehlerton (2026-07-10) |
| Impulsdauer Türöffner | ✅ ~2 s Startwert, final am Türöffner (2026-07-10) |
| Außeneinheit ohne WLAN | ✅ Fehlerton an der Tür (2026-07-10) |
| Türbefehl-Authentifizierung (Schlüssel + Zähler) – konkrete Ausgestaltung | offen |
