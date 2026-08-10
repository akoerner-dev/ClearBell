# ClearBell – Verbesserungen für v0.2

Lebende Datei. Wird während Prototypenbau / Inbetriebnahme (IBN) laufend ergänzt.
Stand: 2026-07-03.

---

## Offene Punkte

| Nr. | Bereich | Beschreibung | Betrifft | Priorität | Status |
|-----|---------|--------------|----------|-----------|--------|
| V01 | Layout | Testpunkte einplanen (3V3-Rail, GND-Referenzen, Touch-Leitungen, I2S-Bus BCLK/DIN/LRCLK, SPI-SD-Bus) | Außen + Innen | Mittel | Offen |
| V02 | Schematic | **Q1 (DMG2305UX-7) Symbol-Pinbelegung korrigieren** – SnapEDA-Symbol hat D/S/G falsch nummeriert | Außeneinheit | **Kritisch** | Offen |
| V03 | Schematic | **Q2 (IRLML6344) gegen Datenblatt prüfen** – gleiches SnapEDA-Fehlerbild wie Q1 möglich; Q2 schaltet den Türöffner | Außeneinheit | Hoch | Offen |
| V04 | Schematic | MAX98357 SD_MODE über GPIO steuerbar machen (aktuell fest auf 3V3 → keine SW-Abschaltung) | Außen + Innen | Niedrig–Mittel | Offen |
| V05 | Layout | Antenne von Variante B → A (Überhang) wechseln, **falls Reichweite unzureichend**; 15-mm-Keepout inkl. GND-Flood/Vias auf allen Lagen sicherstellen | Außen + Innen | Mittel (bedingt) | Offen |
| V06 | Schematic | Pull-ups (10 kΩ) auf SD-SPI-Leitungen (MISO, MOSI, SCK, CS) nach 3V3 ergänzen | Außen + Innen | Mittel | Offen |
| V07 | BOM/HW | Modul N8 → **N8R2** (2 MB PSRAM) erwägen – reine RAM-Reserve | Außen + Innen | **Optional / Niedrig** | Offen |
| V08 | Layout | **S_RST + S_BOOT (PTS526) um 90° verdreht platziert** – EN und GPIO0 liegen auf GND, ESP startet nicht | Inneneinheit | **Kritisch** | Nacharbeit v0.1 erledigt / Layoutfix offen |
| V09 | BOM/Layout | Klemmen 2086-1202 (THT) → **WAGO 2060-452/998-404 (SMD)** – Bauhöhe reduzieren; Prüfpunkte siehe Detail | Außen + Innen | Mittel | Offen |
| V10 | BOM/HW | Lautsprecher vereinheitlichen: **K 36 WP auch innen?** – Hörtest erforderlich | Inneneinheit | Niedrig–Mittel | Prüfen |
| V11 | Schematic/BOM | **S_RST/S_BOOT durch 2-polige Taster ersetzen**; zusätzlich prüfen: Auto-Programmierung über DTR/RTS → Taster ggf. ganz entfallen | Außen + Innen | Mittel–Hoch | Offen |

---

## Detail

### V02 – Q1 (DMG2305UX-7) Symbol-Pinbelegung falsch  *(kritisch)*
**Einheit:** Außeneinheit, Power-Block.

SnapEDA-**Symbol** hat die Pin-Nummern falsch den Funktionen (D/S/G) zugeordnet →
Netze lagen im Layout rotiert auf den Pads:

| Pad (Footprint) | Pin lt. Datenblatt DS36196 | Netz v0.1 (falsch) | Soll |
|---|---|---|---|
| Pad 3 (Einzelpin) | Drain | +12V | Net-(Q1-D) → LMR-VIN |
| Pad 1 | Gate | Net-(Q1-D) → LMR-VIN | Gate-Netz (DZ1/Rg1) |
| Pad 2 | Source | Gate-Netz (DZ1-A) | +12V |

Footprint korrekt, **Fehler liegt im Symbol.**

**Auswirkung v0.1:** Q1 leitete nicht (kein Stromfluss → keine 3,3 V). Gemessen
VGS = −11,5 V gegen VGS(max) ±8 V → **Q1 vermutlich gate-oxid-geschädigt, ersetzen.**
DZ1 (BZX84-C7V5) konnte nicht klemmen (lag nicht über echter GS-Strecke).

**Korrektur v0.2:**
1. Symbol-Pin-Nummern **G=1, S=2, D=3** (Datenblatt DS36196, Top View) – im Bibliotheks-Symbol.
2. „PCB aus Schaltplan aktualisieren" (erlaubt).
3. **NICHT** „Symbole aus Bibliothek aktualisieren" (überschreibt Fixes).
4. Neu verdrahten + DRC.

**v0.1-Workaround (vorhandene Platine):** Q1 ausgelötet, Lötbrücke Pad 3 (+12V) →
Pad 1 (LMR-VIN). **Verpolschutz entfällt → nur Laborbetrieb mit fester Polarität,
nicht für Endeinbau.** D1 (SMBJ15A) bleibt wirksam.

**Prozess-Lehre:** SnapEDA-Symbol-Pinnummern beim Import IMMER gegen das Datenblatt
prüfen. DRC fängt semantische Pin-Mapping-Fehler nicht.

### V03 – Q2 (IRLML6344) Datenblatt-Verifizierung ausstehend
Q2 (Türöffner-MOSFET, Gate an GPIO21 via R_GATE1 100 Ω) ist noch **nicht** gegen sein
Datenblatt auf dasselbe SnapEDA-Pin-Mapping-Fehlerbild wie Q1 geprüft. Ein vertauschtes
Source/Drain kann beim Schalten Schaden anrichten. **Vor erneuter Bestückung/Fertigung
bzw. vor dem Türöffner-Test prüfen.**

### V04 – MAX98357 SD_MODE fest auf 3V3
SD_MODE hart auf +3V3 → Amp dauerhaft aktiv, Ausgang (L+R)/2, GAIN auf GND (15 dB),
**keine GPIO-Abschaltung** aus der Firmware. Für v0.1 funktional, aber kein SW-Mute.
**v0.2:** SD_MODE GPIO-steuerbar (Mode-Select-Widerstand: GPIO-Low = Shutdown,
GPIO-High = (L+R)/2). **Vor Umbau verifizieren:** MAX98357-Datenblatt – Auto-Sleep bei
fehlendem I2S-Takt + exakte SD_MODE-Schwellen/Widerstandswerte.

### V05 – Antenne Variante B → A / Keepout
Aktuell **Variante B** (Antenne vollständig auf PCB). Plan: in v2 auf **Variante A**
(über Platinenrand) wechseln, **falls** Reichweite im Zwei-Familienhaus unzureichend.
Bei Variante B ist die 15-mm-Keepout Pflicht – auf **allen Lagen**, inkl.
GND-Flood-Aussparung auf Bottom und keine Vias in der Zone. **Voraussetzung für die
Entscheidung:** systematische Reichweitenmessung (RSSI im Firmware-Test berücksichtigen).

### V06 – Pull-ups auf SD-SPI-Leitungen
Auf v0.1 fehlen Pull-ups auf den SD-Leitungen. **v0.2:** je 10 kΩ von MISO (GPIO19),
MOSI (GPIO23), SCK (GPIO18) und CS (GPIO5) nach 3V3.
**Hinweis:** Empfohlene Praxis für SD-über-SPI, **nicht** die Ursache des v0.1-NONE-Problems
(das war ein falscher CS-Pin im Test-Sketch, GPIO17 statt GPIO5 – HW war korrekt).
Bewusst ergänzt, da robust und schadlos. Pin-Mapping Innen ≠ Außen beachten.

### V07 – Modul N8 → N8R2 (mit PSRAM) erwägen  *(optional)*
**Begründung:** Reine **RAM-Reserve** (2 MB PSRAM). Entspannt den einzigen erkannten
Heap-Wachpunkt (MQTTS/TLS bei gleichzeitig WiFi + Audio + ESP-NOW). Aufpreis ~0,10 €
(Digikey: N8 4,65 € vs. N8R2 4,75 €). Footprint, 8 MB Flash, Pinout sonst identisch.

**Caveat (Datenblatt Fußnote 3, S. 12):** Beim **N8R2 ist GPIO16 fest mit dem PSRAM
verbunden und nicht mehr frei nutzbar.**
- Außeneinheit: GPIO16 wird **nicht** genutzt (Schaltplan: X) → sauberer Drop-in.
- **Inneneinheit: GPIO16-Nutzung vor Umstieg prüfen!**

**Ausdrücklich KEIN Kamera-Enabler:** Eine ESP32-Kamera braucht ~12–16 GPIOs (auf der
Außeneinheit nicht frei) und eher eine **ESP32-S3-Klasse** mit dediziertem
Kamera-Interface → eigenes **v2-Redesign**, kein v0.2-Bauteiltausch. R2 also nur wegen
RAM-Reserve wählen, nicht als Kamera-Begründung. (Aktuelle Kamera = externe Tapo via HA.)

**Audio unabhängig davon:** ClearBell nutzt einen schlanken eigenen WAV-Player (ESP_I2S,
PSRAM-frei). PSRAM macht die schwere `ESP32-audioI2S`-Lib nur *möglich*, nicht *passend* –
für kurze WAV-Klingeltöne bleibt der Mini-Player das richtige Werkzeug, R2 hin oder her.

### V08 – S_RST + S_BOOT (PTS526) um 90° verdreht  *(kritisch)*
**Einheit:** Inneneinheit. **Gefunden:** IBN 2026-07-02.

**Befund IBN v0.1:** EN (via R_EN1 10 kΩ Pull-up) und GPIO0/Pad 25 (via R_BOOT1 10 kΩ)
liegen bestromt auf ~0 V, obwohl beide Pull-ups auf der 3V3-Seite versorgt sind
(3,425 V gemessen). Beide Taster im Layout um 90° verdreht → interne Kontaktbrücke
des PTS526 verbindet Signalnetz permanent mit GND. Außeneinheit (gleicher Footprint,
korrekte Orientierung) nicht betroffen.

**Elektrische Verifikation (02.07.2026): bestätigt.** Nach 90°-Drehung beider Taster
(Nacharbeit v0.1) startet der ESP; UART-Bootmeldungen empfangen, beide Boot-Modi
(SPI_FAST_FLASH_BOOT + DOWNLOAD_BOOT) erreicht. Ungeklärt bleibt die stromlose
In-Circuit-Messung ~5 kΩ statt ~0 Ω vor der Nacharbeit — gegenstandslos, aber Lehre:
In-Circuit-Ohm-Messungen an Halbleiterknoten sind nicht belastbar.

**Korrektur v0.2:**
1. Orientierung S_RST/S_BOOT im Inneneinheit-Layout um 90° korrigieren.
2. Pin-1-/Orientierungsmarkierung auf Silkscreen ergänzen (beide Einheiten) –
   verhindert Wiederholung bei Bestückung.
3. DRC/ERC fängt diesen Fehler nicht (Prozess-Lehre analog V02: Orientierung
   4-poliger Taster ist semantisch, nicht regelprüfbar).

**v0.1-Workaround (vorhandene Platine):** Taster auslöten oder um 90° gedreht
neu auflöten (Padbild 7,0×6,4 mm – Machbarkeit prüfen). Ohne Taster: Pull-ups
halten EN/GPIO0 auf High; Reset per Spannungszyklus, Boot-Modus per manueller
Brücke GPIO0→GND.

### V09 – Klemmen: 2086-1202 (THT) → WAGO 2060-452/998-404 (SMD)
**Motivation:** Bauhöhe der 2086 unnötig hoch für flache Gehäuse.
**Verifiziert (WAGO-Produktseite 03.07.26):** 0,75 mm², RM 4 mm, 2-polig,
Push-in CAGE CLAMP mit Betätigungsdrückern, Liefereinheit Gurt.
**Vor Umstellung prüfen:**
1. Bauhöhe lt. Datenblatt – tatsächlicher Gewinn ggü. 2086 beziffern
2. Strombelastbarkeit + zulässiger Leiterquerschnitt (min!) vs.
   vorhandener Klingeldraht/Türöffnerleitung
3. Handlötbarkeit der SMD-Pads (Bestückung erfolgt von Hand)
4. Einzelstückzahlen bei Digikey/Mouser (Gurt → ggf. nur Abschnitte)
5. **Mechanik:** SMD-Pads nehmen Zugkräfte der Feldverdrahtung auf –
   anders als THT keine Formschluss-Verankerung. Zugentlastung im
   Gehäuse zwingend vorsehen (Platine ist ohnehin verklebt)
6. Neuer Footprint → gegen Datenblatt verifizieren (Lehre V02/V08)

### V10 – Lautsprecher vereinheitlichen: K 36 WP auch innen?
**Motivation:** eine Lautsprechertype für alle Einheiten (BOM-Vereinfachung);
kleinerer Treiber → Synergie mit offenem Gehäusekonzept F8.
**Hinweis:** Revidiert die dokumentierte Designentscheidung K 50 SQ innen.
**Prüfkriterium (Hörtest auf Inneneinheit-Hardware):** identische
WAV-Datei bei identischer Aussteuerung auf K 50 SQ und K 36 WP;
Klingelton muss bei geschlossener Zimmertür wohnungsweit hörbar sein.
Erst danach entscheiden. WP-Eigenschaft (wasserdicht) innen funktionslos –
Preisdifferenz gegenprüfen.

### V11 – Taster: 2-polig statt PTS526 / optional ganz entfallen
**Motivation:** 4-polige Taster sind orientierungsanfällig (Beleg: V08)
und mit dem Finger schlecht zu betätigen.
**Stufe 1 (sicher):** 2-polige Taster – Verdrehung elektrisch folgenlos,
Fehlerklasse eliminiert. Größere Bauform (z. B. 6×6 mm, längerer Stößel)
für Fingerbedienung. Ersetzt den reinen Orientierungsfix aus V08.
**Stufe 2 (prüfen):** Auto-Programmierschaltung wie auf Dev-Kits:
DTR/RTS des Adapters über zwei Transistoren auf EN und GPIO0
(Standard-Schaltung NodeMCU/DevKitC). Dann Flashen ohne Tasten;
Reset im Feld per Spannungszyklus.
**Voraussetzungen Stufe 2:** (a) FT232-Adapter muss DTR **und** RTS
herausführen – am vorhandenen Adapter prüfen; (b) J_UART wächst um
2 Pins; (c) Zusatzschaltung = neue Fehlerquelle, gegen Referenz-
schaltplan verifizieren. Spannungsfeld zum Leitsatz "Keep it simple"
bewusst abwägen: Stufe 1 allein löst das erlebte Problem bereits.

---

## Erledigt

*(leer – wird nach Umsetzung befüllt)*

---

## Hinweise
- Priorität: Kritisch / Hoch / Mittel / Niedrig / Optional
- Bereich: Layout / Schematic / Firmware / Mechanik / BOM/HW / Sonstiges
- Status: Offen / In Arbeit / Erledigt
- **Nicht enthalten:** reine BOM-/Doku-Flags (F1 = DZ1 Digikey-Nr., F4 = TLV62568DBVR + SDER041H-2R2MS Digikey-Nr.) – gehören ins Flags-/BOM-Tracking.
