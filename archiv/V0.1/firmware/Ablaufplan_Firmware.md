# ClearBell – Firmware Entwicklungsplan

## Ziel
Vollständige, eigenständige Firmware für Außen- und Inneneinheit.
Kein ESPHome, kein Home Assistant, kein externer Server.

---

## Übersicht: Phasen

```
Phase 0  │  Vorbereitung         │  Jetzt  ─────────────────────────────────────┐
Phase 1  │  Hardware IBN         │  wenn PCBs da                                │
Phase 2  │  Komponententests     │  Bottom-up, je Modul                         │
Phase 3  │  Integration          │  alles zusammen                              │
Phase 4  │  Robustheit           │  Dauerbetrieb, Fehlerverhalten               │
Phase 5  │  Abschluss            │  Einbau, Dokumentation fertig                │
```

---

## Phase 0 – Vorbereitung  ✅ (jetzt erledigt)

| # | Schritt | Status |
|---|---|---|
| 0.1 | Firmware-Architektur definieren | ✅ |
| 0.2 | Kommunikationsprotokoll (ESP-NOW Struct) definieren | ✅ |
| 0.3 | GPIO-Belegung dokumentieren | ✅ |
| 0.4 | Bibliotheken auswählen (ESP32-audioI2S, Arduino Core) | ✅ |
| 0.5 | Code-Skelett schreiben (config.h + .ino, kompilierbar) | ✅ |
| 0.6 | Arduino IDE einrichten: Board "ESP32 Dev Module", Core ≥ 2.0 | ☐ |
| 0.7 | Bibliothek installieren: **ESP32-audioI2S** (schreibfaul1) | ☐ |
| 0.8 | Testklingeltöne als MP3 vorbereiten (klingel_eg.mp3, klingel_og.mp3) | ☐ |
| 0.9 | SD-Karte formatieren (FAT32), MP3-Dateien drauf | ☐ |

**Abhängigkeit:** nichts – alles ohne Hardware möglich.

---

## Phase 1 – Hardware Inbetriebnahme  (wenn PCBs eintreffen)

Reihenfolge: **erst messen, dann flashen** – nie blind bestückte Platine anlegen.

| # | Schritt | Messmittel |
|---|---|---|
| 1.1 | Außeneinheit: 12V anlegen, Spannung messen: 3,3V am ESP32 VCC | Multimeter |
| 1.2 | Inneneinheit: USB-C anlegen, 3,3V am ESP32 VCC | Multimeter |
| 1.3 | **MAC_Scanner.ino** auf Außeneinheit flashen → MAC notieren | Serial Monitor |
| 1.4 | MAC_Scanner auf Inneneinheit 1 flashen → MAC notieren | Serial Monitor |
| 1.5 | MAC_Scanner auf Inneneinheit 2 flashen → MAC notieren | Serial Monitor |
| 1.6 | Alle 3 MACs in die jeweiligen config.h eintragen | Editor |

**Ergebnis:** Alle 3 Boards booten, Serial Monitor zeigt MAC-Adresse.
**Fallstrick:** Kein Board anlegen bevor 3,3V bestätigt – ESP32 ist nicht 5V-tolerant.

---

## Phase 2 – Komponententests  (Bottom-up, ein Modul nach dem anderen)

Jeder Test hat einen eigenen Test-Sketch. Erst wenn ein Modul grünes Licht hat, geht es weiter.

### 2.1 – Touch-Kalibrierung  (Außeneinheit)

**Ziel:** Reale Schwellwerte für GPIO32 + GPIO33 ermitteln.

| # | Schritt |
|---|---|
| 2.1.1 | Test-Sketch: `touchRead(32)` + `touchRead(33)` jede Sekunde auf Serial ausgeben |
| 2.1.2 | Wert **ohne** Finger notieren (Ruhewert, z.B. 60) |
| 2.1.3 | Wert **mit** Finger direkt auf Pad notieren (z.B. 8) |
| 2.1.4 | Wert **mit** Finger durch Holzdicke notieren (montierter Zustand) |
| 2.1.5 | Schwellwert = Mitte zwischen Ruhe- und Berührt-Wert → in config.h eintragen |
| 2.1.6 | Debounce-Zeit testen (Doppelauslösung verhindern) |

**Ergebnis:** `TOUCH_THRESHOLD_EG` und `TOUCH_THRESHOLD_OG` final.

---

### 2.2 – SD-Karte  (Außeneinheit + Inneneinheit)

**Ziel:** SD-Karte lesen und Datei öffnen.

| # | Schritt |
|---|---|
| 2.2.1 | Test-Sketch: SD.begin() + Dateinamen-Listing auf Serial |
| 2.2.2 | Prüfen: klingel_eg.mp3 und klingel_og.mp3 werden gefunden |
| 2.2.3 | Gleicher Test auf Inneneinheit |

**Ergebnis:** `SD.begin()` → true, Dateien sichtbar.

---

### 2.3 – I2S Audio  (Außeneinheit + Inneneinheit)

**Ziel:** Ton aus Lautsprecher hören.

| # | Schritt |
|---|---|
| 2.3.1 | Test-Sketch: Sinuston über I2S (ohne SD) → Lautsprecher piept |
| 2.3.2 | Lautstärke und Klang grob beurteilen |
| 2.3.3 | MP3 von SD abspielen (ESP32-audioI2S) |
| 2.3.4 | Gleicher Test auf Inneneinheit |

**Ergebnis:** Klingelton wird klar und ohne Verzerrung ausgegeben.
**Fallstrick:** I2S + SD teilen sich SPI-Bus nicht – aber BCLK/LRCLK/DATA sind eigene Pins, kein Konflikt.

---

### 2.4 – WiFi + ntfy.sh  (nur Außeneinheit)

**Ziel:** Smartphone-Notification kommt an.

| # | Schritt |
|---|---|
| 2.4.1 | WiFi-Verbindung im Serial Monitor bestätigen |
| 2.4.2 | Manuell HTTP POST an ntfy.sh absetzen (aus Test-Sketch) |
| 2.4.3 | Notification auf Smartphone prüfen: Titel, Text, 3 Buttons |
| 2.4.4 | WIFI_SSID / WIFI_PASS in config.h final eintragen |

**Ergebnis:** ntfy-Notification erscheint < 3 Sekunden nach POST.

---

### 2.5 – ESP-NOW  (Außen → Innen)

**Ziel:** Nachricht kommt auf beiden Inneneinheiten an.

| # | Schritt |
|---|---|
| 2.5.1 | Außeneinheit sendet Test-Message im 3-Sekunden-Takt |
| 2.5.2 | Inneneinheit 1: empfangene Message auf Serial ausgeben |
| 2.5.3 | Inneneinheit 2: gleich |
| 2.5.4 | Sendestatus-Callback auf Außeneinheit prüfen (ESP_NOW_SEND_SUCCESS) |
| 2.5.5 | Reichweite testen (durch Wand, typischer Montageort) |

**Ergebnis:** Beide Inneneinheiten empfangen zuverlässig, RSSI ausreichend.
**Fallstrick:** WiFi-Kanal der Außeneinheit muss mit dem Kanal der Inneneinheiten übereinstimmen → deshalb auch Inneneinheiten mit WiFi verbinden.

---

## Phase 3 – Integration

Alle Komponenten in der finalen Firmware zusammenführen.

| # | Schritt | Test |
|---|---|---|
| 3.1 | Außeneinheit: Touch → Audio + ESP-NOW + ntfy gleichzeitig | Touch drücken, alle 3 Reaktionen beobachten |
| 3.2 | Inneneinheit: ESP-NOW empfangen → Audio abspielen | Außeneinheit Touch → Inneneinheit Ton |
| 3.3 | Beide Inneneinheiten gleichzeitig | EG-Taste klingelt in allen Einheiten |
| 3.4 | Inneneinheit Touch → Stummschaltung | Touch → kein Ton beim nächsten Klingeln |
| 3.5 | Vollständiger Durchlauf EG-Taste: Touch → Ton Außen → Ton Innen → ntfy | Protokoll im Serial Monitor |
| 3.6 | Vollständiger Durchlauf OG-Taste | identisch |

**Ergebnis:** System funktioniert end-to-end als Einheit.

---

## Phase 4 – Robustheit & Fehlerverhalten

**Ziel:** System bleibt im Dauerbetrieb stabil.

| # | Szenario | Erwartetes Verhalten |
|---|---|---|
| 4.1 | WiFi temporär weg | ntfy-Notification wird übersprungen, System bleibt stabil, Reconnect |
| 4.2 | SD-Karte nicht eingesteckt | Kein Absturz, Fehlermeldung auf Serial, ESP-NOW + ntfy laufen weiter |
| 4.3 | Inneneinheit nicht erreichbar (aus) | Außeneinheit: SEND_FAILED im Callback, kein Absturz |
| 4.4 | Dauer-Touch (Taste klemmt) | Debounce verhindert Dauer-Spam |
| 4.5 | 24h Dauerbetrieb | Kein Memory Leak, kein Absturz |
| 4.6 | Watchdog einbauen | Auto-Reboot bei Hänger (esp_task_wdt) |
| 4.7 | OTA-Update einrichten (ArduinoOTA) | Firmware-Update ohne UART-Zugang |

---

## Phase 5 – Abschluss

| # | Schritt |
|---|---|
| 5.1 | Finale config.h mit allen realen Werten sichern |
| 5.2 | Firmware-Version in Code eintragen (#define FW_VERSION "1.0.0") |
| 5.3 | Alle 3 Boards final flashen |
| 5.4 | Einbau in Gehäuse |
| 5.5 | Montage und Funktionstest am finalen Einbauort |
| 5.6 | README_Firmware.md finalisieren |

---

## Abhängigkeitsbaum

```
Phase 0 (Vorbereitung)
    └── Phase 1 (Hardware IBN + MACs)
            ├── Phase 2.1 (Touch)
            ├── Phase 2.2 (SD)
            ├── Phase 2.3 (Audio)  ← benötigt 2.2
            ├── Phase 2.4 (WiFi + ntfy)
            └── Phase 2.5 (ESP-NOW)
                    └── Phase 3 (Integration)
                            └── Phase 4 (Robustheit)
                                    └── Phase 5 (Abschluss)
```

---

## Risiken

| Risiko | Wahrscheinlichkeit | Maßnahme |
|---|---|---|
| Touch durch Holz zu unempfindlich | mittel | Schwellwert + Holzdicke anpassen; Kupferpads vergrößern |
| I2S-Konflikt mit SPI (SD) | gering | eigene Pins, kein Konflikt – trotzdem early test |
| ESP-NOW Kanal-Mismatch | mittel | Inneneinheiten auch mit WiFi verbinden |
| ntfy-Verzögerung > 5s | gering | Topic/Server wechseln (self-hosted ntfy) |
| Zu wenig Heap für Audio + WiFi + ESP-NOW | mittel | ESP32 hat 520KB RAM – ausreichend, trotzdem im Blick behalten |
