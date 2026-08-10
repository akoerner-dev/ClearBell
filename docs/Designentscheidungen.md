# ClearBell – Designentscheidungen & Begründungen

Dieses Dokument hält fest, WARUM Bauteile gewählt wurden und welche Alternativen verworfen wurden. Wird zu Beginn jedes neuen Chats gelesen, um vollständigen Kontext herzustellen.

---

## Block 1 – Stromversorgung Außeneinheit (12V → 3,3V)

### IC: LMR33630CDDAR (Texas Instruments)

- **Begründung:** Buck-Converter mit 3A, 36V Eingang, 2,1MHz fsw → kleines Induktivitäts-Footprint, hohe Effizienz
- **Alternativen geprüft:** Keine dokumentiert

### L1: Würth 74438356018HT (1,8µH)

- **Begründung:** Passend zur berechneten Induktivität für fsw=2,1MHz, hoher IDC (6,05A), niedriger DCR
- **Status:** Verifiziert gegen Datenblatt ✓

### Verpolschutz: P-Kanal MOSFET (Stelle: Q1)

- **Konzept:** Passiver Verpolschutz ohne aktive Ansteuerung:
  - Source → 12V Eingang
  - Gate → GND über 100kΩ Pull-down (selber Typ wie Rpg, bereits in BOM)
  - Drain → Eingang Buck-Converter
  - Korrekte Polarität: VGS = GND - 12V = -12V → MOSFET leitet
  - Falsche Polarität: VGS = 0V → MOSFET sperrt
- **Begründung für MOSFET statt Schottky:** Verlust bei 1A: \~40mW (MOSFET) vs \~400mW (Schottky) — relevant bei 24/7 Betrieb
- **MOSFET:** DMG2305UX-7 (Diodes Inc., SOT-23) — bleibt in BOM
- **Problem (Review 1.3):** VGS(max) = ±8V, aber bei Betrieb entsteht VGS = -12V → Überschreitung absolutes Maximum
- **Lösung (Review 1.3 ✓):** Zener-Clamp DZ1 parallel Gate-Source
  - Bauteil: Nexperia BZX84-C7V5,215 (7,5V, 250mW, SOT-23)
  - Wirkung: begrenzt VGS auf max. -7,5V → sicher innerhalb ±8V
  - Sättigung des MOSFET bei VGS = -7,5V vollständig gewährleistet (VGS(th) nur -0,5 bis -0,9V)
  - Industriestandard-Lösung, kein Redesign des MOSFET nötig
- **Digikey-Nr. DZ1:** noch einzutragen (BOM v11, Spalte H, Zeile DZ1)

### D1: SMBJ15A (Vishay)

- **Begründung:** TVS-Diode zum Schutz des LMR33630 vor Überspannungstransienten am 12V-Eingang
- **Standoff 15V:** liegt über Betriebsspannung 12V → TVS leitet im Normalbetrieb nicht
- **Klemmspannung 24,4V max.:** tritt nur bei Transienten auf (Spitzenstrom 24,6A) → LMR33630 verträgt 36V → sicher
- **Spitzenleistung 600W:** ausreichend für Transienten in SELV-Haushaltsnetz (kein KFZ)
- **Status:** Verifiziert ✓ (Review 1.4)

### Kondensatoren (Cin, Cout)

- **Regel:** Mindestgehäuse 0603; X7R bevorzugt, X5R akzeptabel wenn Betriebstemperatur ≤ 60°C
- **Temperaturbetrachtung:** Außeneinheit max. \~35°C Umgebung + \~25°C Eigenerwärmung = realistisch \~60°C am Kondensator → X5R (bis 85°C) wäre ausreichend; X7R-Pflicht war zu pauschal

#### Cin — GELÖST (Review 1.5 ✓)

- **Bauteil:** GRM21BZ71E106KE15L — 10µF, 25V, X7R, 0805
- **SimSurfing-Messung bei 12V DC:** nur 2,78µF effektiv (72% Verlust)
- **TI-Anforderung (LMR33630, Section 9.2.2.6):** min. 10µF effektive Kapazität am Eingang
- **Geprüfte Alternativen (verworfen):**
  - GRM21BR61E226ME44 (22µF, 10V, X5R, 0805) → nur 3,85µF bei 12V, unzureichend
  - Höhere Nennspannung in 0805 bei Murata nicht verfügbar
- **Lösung (gewählt): 4× GRM21BZ71E106KE15L parallel**
  - 4 × 2,78µF = \~11,1µF effektiv → erfüllt TI-Minimum von 10µF ✓
  - Kein neues Bauteil nötig, gleicher Typ bereits in BOM
  - TI empfiehlt ohnehin Aufteilung auf beide VIN-Pins (je 2 Caps pro Pin)
- **Fallback Option B** (nur falls PCB-Fläche kritisch wird): Murata GRM32ER71E106KA12L (10µF, 25V, X7R, **1210**) — größeres Gehäuse, besseres DC-Bias-Verhalten, ein Footprint

#### Cout — OK

- **Aktuell:** GRM21BZ71A226ME15L — 22µF, 10V, X7R, 0805 bei 3,3V
- Verhältnis 3,3V/10V = 33% Nennspannung → DC-Bias-Derating unkritisch ✓

---

## Block 2 – ESP32-WROOM-32 Beschaltung

- GPIO6–11 reserviert (internes SPI-Flash) → nicht verwenden ✓ (Review 2.1)
- ADC2 nicht gleichzeitig mit WiFi nutzbar → nur ADC1 Pins verwenden ✓ (Review 2.2)
- GPIO0 Pull-up: 10kΩ empfohlen, Espressif gibt keinen spezifischen Wert vor → 10kΩ akzeptabel ✓ (Review 2.3)
- GPIO12/GPIO15 Strapping-Pins: WROOM-32 hat eFuses für 3,3V Flash gebrannt, GPIO12 unkritisch. Externe Beschaltung darf Strapping-Level nicht überschreiben ✓ (Review 2.4)
- Bulk-Kondensator 100µF (Panasonic EEEFC0J101P): ausreichend für WiFi-Peaks ✓ (Review 2.5)
- UART0: TX=GPIO1, RX=GPIO3 ✓ (Review 2.6)

---

## Block 3 – Kapazitive Touch-Taster (Außeneinheit)

- Touch-Pins: GPIO32 (T9) + GPIO33 (T8) ✓ (Review 3.1) — Hinweis: T8/T9 Bezeichnung in manchen Arduino-Versionen vertauscht, nur Firmware-relevant
- 1kΩ Serienwiderstand ESD-Schutz: innerhalb Espressif-Empfehlung (470Ω–2kΩ, pref. 510Ω) ✓ (Review 3.2)
- Touch durch 2–3mm Holz: Keine Datenblatt-Angabe für max. Dicke. Muss bei Inbetriebnahme empirisch getestet werden (setup_mode: true in ESPHome) ⚠️ (Review 3.3 — offen)
- Steckverbinder: JST-PH

---

## Block 4 – Audio (Außeneinheit)

- IC: MAX98357AETE+T (Maxim/ADI), Class-D, I2S, TSSOP-16
- I2S-Pins: GPIO25 (LRCLK), GPIO26 (BCLK), GPIO22 (DIN) — gängige ESP32 I2S-Pins, via GPIO-Matrix ✓ (Review 4.1)
- ESPHome: i2s_audio + media_player Komponente unterstützt SD-Karten-Audio ✓ (Review 4.2)
- Lautsprecher: Visaton K 36 WP, 8Ω, wetterfest
- SD-Karte für Klingeltöne

---

## Block 2.8 – USB-C Eingang Inneneinheit

### Buchse: Same Sky UJC-HP-3-SMT-TR

- **Typ:** Power-only, 6 Pins (VBUS, GND, CC1, CC2 + 2× Befestigungslaschen), SMD, rechtwinklig
- **Bewertung:** 20V / 3A — ausreichend, Eingang ist 5V
- **Begründung:** Kein Datentransfer nötig → power-only Variante günstiger, einfacher, weniger Footprint-Pins
- **Verfügbarkeit:** Digikey DE, ships today ✓
- **KiCad Symbol/Footprint:** SnapEDA ✓
- **CC-Terminierung:** 2× 5,1kΩ (R_CC1, R_CC2), je CC1/CC2 nach GND
  - Begründung: Ohne Pull-downs kein VBUS-Ausgabe bei modernen USB-C-Netzteilen. Beide CC-Pins müssen beschaltet sein — ein einziger Widerstand genügt nicht.
  - 5,1kΩ signalisiert dem Netzteil: Sink, 5V, Default Current (500mA) — ausreichend für ESP32 + Audio
- **TVS-Schutz:** Entfällt ✓
  - SMBJ5.0A geprüft (Vishay Datenblatt lokal): VC(max) = 9,2V bei IPPM — liegt weit über TLV62568 Abs.Max 5,5V → schützt nicht
  - TVS für 5V-Systeme mit 5,5V Abs.Max generell ungeeignet (kein Bauteil mit VC < 5,5V verfügbar in SMBJ-Serie)
  - Einsteck-Transienten bei USB-C Netzteil in Innenräumen: unkritisch (kurzes Kabel, niedringe Induktivität, interne PSU-Caps, Cin 4,7µF dämpft zusätzlich)
  - Entscheidung: Kein TVS. Cin (4,7µF) ist ausreichender Schutz.

## Block 2.7 – Stromversorgung Inneneinheit (5V USB-C → 3,3V)

- IC: TLV62568DBVR (TI), Buck-Converter, SOT-23-5
- L1: Cyntec SDER041H-2R2MS, 2,2µH
- Cin: Murata GRM188Z71A475KE15D (4,7µF, 10V, X7R, 0603)
- Cout: GRM21BZ71E106KE15L (10µF, 25V, X7R, 0805) — Wiederverwendung aus Außeneinheit-BOM
- **EN-Pin:** Direkt an VIN (+5V) — kein R_EN nötig. Datenblatt Section 7.4.1 + Figure 5 (Referenzschaltbild): EN direkt an VIN, kein Widerstand. Darf laut Datenblatt nicht floaten, direkte VIN-Verbindung ist Standard.
- **C_HF:** 100nF 0603 auf +3V3 direkt an U1 (Layoutregel Datenblatt Section 10.1)
- **PWR_FLAG:** je 1× auf +5V-Netz und +3V3-Netz für ERC

---

## Allgemeine Designregeln

- Mindestgehäuse: 0603
- Kondensatoren: X7R bevorzugt, X5R akzeptabel bei max. ~60°C Betriebstemperatur
- Passivbauteile: 10er-Gebinde
- Lieferanten: Digikey (primär), Mouser, LCSC
- EDA: KiCad (ab Phase 3)
- Firmware: ESPHome
- Leitsatz: **Keep it simple, but working**
- Keine Exoten: E96-Widerstände und schwer beschaffbare Teile vermeiden
- **ESP-NOW + WiFi:** laufen simultan auf demselben WiFi-Kanal. ESP-NOW Kanal kann nicht separat konfiguriert werden wenn WiFi aktiv — teilt automatisch den WiFi-Kanal. Kein Problem für ClearBell. Belegt: ESPHome Docs espnow-Komponente ✓ (Review 5.1)
- **ESPHome Touch:** esp32_touch Komponente vollständig unterstützt. Schwellwert bei Inbetriebnahme kalibrieren ✓ (Review 5.2)

---

## Phase 4 – PCB-Layout Außeneinheit

### Allgemeine Layout-Grundsätze
- **Einseiteige Bestückung:** Alle Bauteile auf Oberseite (F.Cu). Unterseite bleibt frei — wird mit Doppelklebeband ins Gehäuse geklebt.
- **THT-Pins:** Überstehende Lötpins auf der Unterseite werden nach dem Löten bündig abgeknipst, damit die Klebefläche eben ist.
- **J_UART:** Female Socket 90° (liegend), 1×4, 2,54mm, THT — Würth 61300411021, Digikey 732-61300411021-ND. Öffnung muss zur PCB-Kante zeigen (Breadboard-Kabel-Zugänglichkeit).

### Gehäuse & Platinengröße
- Außenmaß Gehäuse: 150 × 100mm
- Rand (Wandstärke + Montagespielraum): 2× 20mm je Achse
- **Platinengröße final: 98 × 60mm** (optimiert nach BT-Platzierung, war initial 110×60mm)
- Touchflächen: mind. 25 × 25mm je Taste (EG rund, OG eckig laut Frontpanel-Design)
- Frontpanel-Layout: 2 Touchflächen + 2 Beschriftungsfelder (vertikal angeordnet)
- **Lautsprecher Visaton K 36 WP: wird auf die PCB geklebt. Hat keine Schraubbefestigung, keine Lötstifte — nur Lötpads direkt auf der Unterseite des Gehäuses. Verbindung zur Schaltung über kurze Drähte in J_SPK (WAGO 2086-1202).**
- **Sperrfläche Lautsprecher: 45×45mm (Keepout Rule Area) — oben rechts in der PCB-Ecke. Kein Bauteil darf unter dem aufgeklebten Lautsprecher liegen. Wird als erstes Element im PCB-Layout eingezeichnet.**
- Touch-Elektroden: Bronze-Blech extern, per kurzem Kabel an R_TOUCH_EG1/R_TOUCH_OG1 angeschlossen (kein PCB-Pad nötig, kein Positionszwang durch Frontpanel-Geometrie)

### Kritische Layout-Randbedingung
- **Touch-Elektroden auf der PCB müssen exakt hinter den Touchflächen des Frontpanels liegen.**
  - Die Kupferfläche der Touch-Pads ist positionsfest durch das Gehäusedesign.
  - Konsequenz: Touch-Pad-Positionen im PCB-Layout zuerst festlegen, dann restliche Bauteile um diese herum platzieren.
- Lautsprecher (Visaton K 36 WP, ø36mm): Position und Gehäuse-Ausschnitt noch festzulegen.

### Fortschritt Phase 4 Außeneinheit
- [x] PCB-Umriss 110×60mm auf Edge.Cuts eingezeichnet
- [x] Keepout Rule Area 45×45mm Lautsprecher — oben rechts eingezeichnet
- [x] PCB aus Schaltplan aktualisiert (F8) — erster Durchlauf, nur Bauteile mit Footprint
- [x] Footprints alle Bauteile zugewiesen (siehe Tabelle unten)
- [x] PCB erneut aus Schaltplan aktualisiert (F8) — alle Bauteile erscheinen
- [x] Footprints manuell auseinandergezogen (kein automatisches Verteilen in KiCad 10 gefunden)
- [x] Bauteilplatzierung abgeschlossen
- [x] Routing abgeschlossen
- [x] GND-Fläche B.Cu gesetzt
- [x] Thermal Vias unter U1 EP gesetzt (0,3mm Bohrung, 0,5mm Pad, Netz GND)
- [x] DRC bestanden: 0 Fehler, 0 Warnungen, 0 Unverbundene Elemente, 37 Ausschlüsse (bekannte SnapEDA-Kosmetik)
- [x] **Phase 4 Außeneinheit PCB-Layout: ABGESCHLOSSEN ✅**

- [ ] **Nächster Schritt: Phase 4 Inneneinheit PCB-Layout**
  - Voraussetzung: F8 Gehäusekonzept Inneneinheit klären (F8)

### Layoutregeln (verifiziert gegen LMR33630 Datenblatt Section 10.1)

**Buck-Converter:**
- Cin direkt an VIN-Pins U1, je 2× pro VIN-Pin
- SW-Knoten (L1 ↔ SW-Pin) so kurz wie möglich, min. 1,5mm breit
- Feedbackteiler (Rfbt/Rfbb) nah an FB-Pin, aber räumlich getrennt vom SW-Knoten (min. 2–3mm)
- EP (Exposed Pad) U1 per Thermal-Via-Array mit GND-Fläche verbinden: 4–9 Vias, 0,3mm Bohrung, 0,5mm Pad, 1mm Raster, Netz GND, kein Via-Tenting Unterseite
- Leiterbahnbreiten: SW-Knoten 1,5mm, 12V-Eingang 1,0mm, +3V3 0,5mm, Signale 0,25mm

**ESP32:**
- C_HF (100nF) direkt am VCC-Pin — näher als C_bulk
- C_bulk (100µF) direkt daneben
- Antenne freihalten: kein Kupfer unter/neben Antennenbereich (auch B.Cu Massefläche)
- Antenne möglichst Richtung Gehäuseaußenwand
- J_UART, S_RST, S_BOOT an PCB-Kante / gut zugänglich

**Thermal Vias allgemein:** 0,3mm Bohrung, 0,5mm Pad, Netz GND, kein Tenting

#### Footprint-Zuweisung — Status

| Bauteil(e) | Footprint | Status |
|---|---|---|
| Alle Widerstände 0603 | `Resistor_SMD:R_0603_1608Metric` | ✓ Festgelegt |
| Kondensatoren 0603 | `Capacitor_SMD:C_0603_1608Metric` | ✓ Festgelegt |
| Kondensatoren 0805 | `Capacitor_SMD:C_0805_2012Metric` | ✓ Festgelegt |
| C_bulk (Elko Ø6,3×5,4mm) | `Capacitor_SMD:CP_Elec_6.3x5.4` | ✓ Festgelegt |
| Q1, DZ1, Q2 (SOT-23) | `Package_TO_SOT_SMD:SOT-23` | ✓ Festgelegt |
| D1 (SMBJ15A, DO-214AA/SMB) | `Diode_SMD:D_SMB` | ✓ Festgelegt |
| D_FLY1 (1N4148W-E3-18, SOD-123) | `Diode_SMD:D_SOD-123` | ✓ Festgelegt — verifiziert gegen Vishay DB: Package = SOD-123 |
| J_UART (Würth 61300411021, Female 90°) | `Connector_PinSocket_2.54mm:PinSocket_1x04_P2.54mm_Horizontal` | ✓ Festgelegt |
| J_TOUCH1, J_TOUCH2 (Lotpad) | `TestPoint:TestPoint_Pad_2.0x2.0mm` | ✓ Festgelegt |
| **WAGO 2086-1202** (J_PWR, J_DOOR, J_SPK) | Von SnapEDA heruntergeladen, in Projektbibliothek eingebunden | ✓ Erledigt |
| **L1 Würth 74438356018HT** | Von Würth heruntergeladen, in Projektbibliothek eingebunden | ✓ Erledigt |
| **PTS526 SK15 SMTR2 LFS** (S_RST, S_BOOT) | Manuell erstellt im KiCad Footprint-Editor. Maße aus C&K-Datenblatt (01/30/26): Körper 5,2×5,2mm, PCB Land 7,0×6,4mm, 4-Pin Gull-Wing. | ✓ Erledigt |

- [ ] Footprint-Zuweisung Inneneinheit (analog, nach Außeneinheit)
- [ ] Bauteilplatzierung
- [ ] Routing

---

## Phase 3 – KiCad Schaltplan (laufend)

### Setup
- KiCad 10.0, deutsche UI
- Zwei separate Projekte (Option A): Außeneinheit + Inneneinheit
- Außeneinheit-Projekt: `<Projektordner>\KiCad\Ausseneinheit\ClearBell_Ausseneinheit.kicad_pro`
- Blattgröße A3, Titelblock ausgefüllt (Rev. 1.0)
- Bibliotheken (projektspezifisch): `<Projektordner>\KiCad\Bibliotheken\`

### Externe Symbole/Footprints (nicht in KiCad-Standardbibliothek)
- **LMR33630CDDAR:** Von TI-Website heruntergeladen, als projektspezifische Bibliothek eingebunden. Footprint: DDA0008J (.pretty-Ordner). ✓
- **ESP32-WROOM-32E-N4:** Von SnapEDA heruntergeladen (N8-Variante, elektrisch identisch zu N4 — gleiche Pins, gleicher Footprint). Als projektspezifische Bibliothek eingebunden. ✓
  - Hinweis: KiCad 10.0 hat ESP32-Symbole bereits in Standardbibliothek (RF_Module) — für zukünftige Bauteile erst dort suchen.

### KiCad-Hinweise aus Praxis
- Standardbibliothek "Device" war initial leer — Lösung: Globale Bibliotheksliste zurücksetzen
- D_TVS in KiCad = bidirektional; unidirektionale TVS (SMBJ15A) → Symbol Device:D_Zener verwenden
- PWR_FLAG auf 12V-Netz UND GND-Netz notwendig um ERC-Fehler "Input-Power-Pin" zu vermeiden
- ERC 7× Ausschlüsse (false positives): VIN/VCC/BOOT/SW Power-Pin-Fehler + Bibliothekswarnungen aus TI-Symbol — alle unkritisch

### Fortschritt Außeneinheit
- [x] Projekt angelegt, Titelblock, A3
- [x] Block 1 komplett: U1, L1, Cin×4+CinX, Cout1/2/CoutX, Cboot, Cvcc, Cff(DNP), Rfbt/Rfbb/Rpg/Rg, Q1+DZ1, D1, J_PWR, 2×PWR_FLAG — ERC sauber
- [x] Block 2 komplett: U2, C_HF1, C_Bulk1, C_EN1, R_EN1, R_BOOT1, R_GPIO2, R_RXD1, S_RST, S_BOOT, J_UART1 — verdrahtet, ERC sauber (10× pin_not_connected für spätere Blöcke bewusst offen)
  - UART J_UART: Pin 2 = TX (ESP TXD0), Pin 3 = RX (ESP RXD0 via R_RXD). Kreuzung TX↔RX passiert im Kabel zum USB-Adapter, nicht auf der Platine.
- [x] Block 3 komplett: R_TOUCH_EG1 (1k), R_TOUCH_OG1 (1k), J_TOUCH_EG (Conn_01x01), J_TOUCH_OG (Conn_01x01) — GPIO32→R→J_TOUCH_EG, GPIO33→R→J_TOUCH_OG
- [x] Block 4 komplett: U2 MAX98357AETE+T (TSSOP-16), C_VDD_AMP1 (10µF), C_VDD_AMP2 (100n), J_SPK (Conn_01x02). SD-Karte: J_SD (Micro_SD_Card, Molex 475710001, Digikey WM9731TR-ND), C_SD (100n). SD_MODE→+3V3 (Left-Channel, direkte Verbindung, kein R_SDMODE), GAIN_SLOT→GND (12dB), EP→GND, Shield→GND. Kein R_SD1–R_SD4 (SPI-Leitungen kurz, 25MHz unkritisch).
- [x] Block 5 komplett: Q2 IRLML6344TRPBF (N-Kanal MOSFET, SOT-23), R_GATE1 (100R), R_GATE_PD1 (100k Pull-down), D_FLY1 (1N4148W), J_DOOR1 (Conn_01x02). GPIO21→R_GATE→Gate, Gate→R_GATE_PD→GND, Source→GND, Drain→J_DOOR Pin1+Anode D_FLY, Kathode D_FLY→+12V, J_DOOR Pin2→+12V. ERC sauber.
  - Türöffner: 9-16V DC, 480mA. Versorgung über vorhandene 12V-Schiene.
  - Designentscheid: MOSFET statt Relais wegen Baugröße (Außeneinheit soll dünn bleiben).
- [x] Block 6 entfällt — alle Steckverbinder bereits in Blöcken 1–5 integriert. Außeneinheit Schaltplan komplett.
- [ ] Inneneinheit Schaltplan
  - [x] KiCad-Projekt angelegt: `KiCad\Inneneinheit\ClearBell_Inneneinheit.kicad_pro`
  - [x] Titelblock: A3, Rev. 0.1
  - [x] Projektbibliotheken eingebunden (ESP32, MAX98357A, UJC-HP-3-SMT-TR)
  - [x] Block 1 vollständig verdrahtet — ERC sauber (2× ignorierte false positives: V-BUS/+5V Netzname + EN bidirektional)
    - J_USB: VBUS→+5V, GND+Shield→GND, CC1→R_CC1→GND, CC2→R_CC2→GND
    - U1: VIN→+5V, EN→+5V, GND→GND, SW→L1→+3V3, FB→Teiler R_FBT/R_FBB
    - R_FBT (470kΩ): +3V3→FB, R_FBB (100kΩ): FB→GND → VOUT = 3,42V (BOM v13: R1_INN=470kΩ RC0603FR-07470KL, R2_INN=100kΩ RC0603FR-07100KL)
    - C_IN1→+5V/GND, C_OUT1/C_HF1→+3V3/GND, PWR_FLAG 1→+5V, PWR_FLAG 2→+3V3
  - [x] Block 2 komplett verdrahtet — ERC sauber (15× ignorierte false positives: GND Kastell-Pads, 3V3/VCC bidirektional; 8× pin_not_connected für Blöcke 3+4 bewusst offen)
    - J_UART1: Pin1=GND, Pin2=TXD0, Pin3=100Ω→RXD0, Pin4=+3V3
    - R_EN1 (10kΩ): +3V3→EN, C_EN1 (100nF): EN→GND, S_RST: EN→GND
    - R_BOOT1 (10kΩ): +3V3→GPIO0, S_BOOT: GPIO0→GND
    - R_GPIO2 (10kΩ): GPIO2→GND (Pull-down für Boot-Modus), No-Connect auf alle ungenutzten Pins
  - [x] Block 3 komplett: R_TOUCH1 (1kΩ) GPIO32→J_TOUCH1
  - [x] Block 4 komplett: U3 MAX98357AETE+T, C_VDD1 (10µF), C_VDD2 (100nF), J_SPK1 (Conn_01x02), J_SD1 (Molex 1040310811, Digikey WM9731CT-ND), C_SD1 (100nF). Kein R_SD-Serienwiderstände, SD_MODE direkt +3V3.
    - I2S: GPIO22→DIN, GPIO25→LRCLK, GPIO26→BCLK, SD_MODE→+3V3
    - SPI SD: GPIO23→CS(DAT3), GPIO21→MOSI(CMD), GPIO19→SCK(CLK), GPIO18→MISO(DAT0), DAT1/DAT2→No Connect — Pins 37/33/31/30 am Modul, optimiert für kurzes Routing zum SD-Slot
    - ERC: 16 Warnungen (SnapEDA-Symbole), teilweise behoben — restliche Fixes ausstehend (s. F5)
  - [x] Block 5 entfällt — alle Steckverbinder bereits in Blöcken 1–4 integriert. Abgleich mit Projektdokumentation Rev.2 bestätigt Vollständigkeit.
  - [x] Inneneinheit Schaltplan komplett — ERC: 0 Fehler, Warnungen werden bereinigt (F5)

### Phase 5 – PCB Layout Inneneinheit
- [x] Edge.Cuts gesetzt (110×65mm vorläufig → optimiert)
- [x] Alle BT platziert
- [x] Geroutet — DRC fehlerfrei
- [x] **F12 GELÖST: Boardgröße optimiert** — finale Größe noch einzutragen (nach Zusammenschieben). Routing wiederholt, DRC sauber.
- [x] **Phase 5 Inneneinheit PCB-Layout: ABGESCHLOSSEN ✅**

### Phase 4 – PCB Layout Außeneinheit (Nachtrag)
- [x] Boardgröße nach Optimierung auf **98×60mm** reduziert (war 110×60mm)

---

## Offene Punkte / Flags

| # | Thema | Beschreibung |
|---|---|---|
| ~~F1~~ | ~~DZ1 Digikey-Nr.~~ | ✓ Gelöst: BZX84-C7V5,215 → DK **1727-5040-1-ND** — in BOM v13 eingetragen |
| ~~F2~~ | ~~Cin Außeneinheit~~ | ✓ Gelöst: 4× GRM21BZ71E106KE15L parallel → ~11,1µF effektiv. Fallback Option B: GRM32ER71E106KA12L 1210 falls Platz knapp. |
| ~~F7~~ | ~~Steckverbinder J_PWR / J_DOOR~~ | ✓ Gelöst: **WAGO 2086-1202 für alle Drahtanschlüsse** (J_PWR, J_DOOR, J_SPK, J_SPK_INN). Begründung: Push-Wire ohne Crimpen, geeignet für Hausinstallationsquerschnitte bis 1,5mm², SMD liegend, kein Spezialwerkzeug. JST-PH Einträge für J_SPK / J_SPK_INN in BOM v13 ersetzt. J_TOUCH/J_TOUCH_INN bleiben Lotpad (Direktanlötung Bronzeelektrode). J_UART bleibt Pin-Header (Programmierschnittstelle). |
| F3 | Review 3.3 Touch/Holz | **Direktkontakt validiert (08.05.2025):** Bronzeelektrode, 1kΩ Reihenwiderstand, GPIO32 (T9), Arduino-ESP32-Core (neuere Version → Rohwerte ~1000er-Bereich, nicht 60–80). Baseline: ~1060, Touch (Finger direkt): ~200, dünner Handschuh: ~910, dicker Skihandschuh: ~990 (bewusst ausgeschlossen). Firmware-Schwellwerte: TOUCH_ON=950, TOUCH_OFF=1000, READS_NEEDED=2, delay=50ms. Skihandschuh: Designentscheidung — nicht unterstützt. **Kalibrierung am eingebauten Gerät (IBN) ausstehend** — Baseline kann je nach Einbauumgebung abweichen. |
| ~~F4~~ | ~~BOM 2.7.3 Digikey-Nr.~~ | ✓ Gelöst: TLV62568DBVR → DK **296-47359-1-ND** — SDER041H-2R2MS → DK **2037-SDER041H-2R2MSCT-ND** — in BOM v13 eingetragen |
| F8 | Inneneinheit Gehäuse | Gehäusekonzept noch nicht entschieden — Bestückungsseite, Befestigung, Formfaktor offen. Entscheidung vor Phase 4 Inneneinheit erforderlich. |
| ~~F12~~ | ~~Boardgröße Inneneinheit optimieren~~ | ✓ Gelöst: BT zusammengeschoben, Routing wiederholt, DRC fehlerfrei. Finale Boardgröße: siehe Phase 5. |
| ~~F11~~ | ~~L1 Inneneinheit Footprint~~ | ✓ Gelöst: Würth WE-MAPI-4020HT Footprint gegen Cyntec SDER041H-2R2MS Datenblatt geprüft — Pad-Maße passen. |
| F5 | ERC Symbol-Mismatch | SnapEDA-Symbole: Pin-Typen direkt im Schaltplan korrigiert (nicht in Bibliothek, da schreibgeschützt). Ursprüngliche 16 Warnungen (falsche Pin-Typen) behoben. Verbleibend: 3× lib_symbol_mismatch (Außeneinheit) + 4× (Inneneinheit) — ignoriert, rein kosmetisch. **ACHTUNG: Niemals "Symbole aus Bibliothek aktualisieren" ausführen — das würde die Schaltplan-Korrekturen mit den alten Bibliotheksversionen überschreiben!** |
| ~~F6~~ | ~~SD-Halter Footprint~~ | ✓ Gelöst: Würth-Footprint aus KiCad-Symbol war nicht kompatibel mit Molex 1040310811. In beiden Schaltplänen auf passenden Molex-Footprint gewechselt. Molex günstiger und mechanisch besser geeignet. BOM-Teilenummer (WM9731CT-ND) war bereits korrekt. |



---
## REVISION 2026-07-05: Kommunikationsarchitektur
ESP-NOW-Direktfunk fuer Klingel/Tueroeffner VERWORFEN nach
Funkvalidierung (Strecke an Montageorten funktional tot, alle Hebel
bewertet: Sendeleistung bereits Maximum 19,5 dBm / EU-Grenze 20 dBm
EIRP; Long-Range-Modus nur ~4 dB; externe Antenne 32UE verworfen).
NEU: Kommunikation ueber Heim-WLAN, UDP direkt zwischen den
Einheiten (kein Broker, kein Pi — Home Assistant laeuft nicht).
Quittungsprotokoll aus ESPNOW_Test wird auf UDP portiert.
Tuerbefehl: Authentifizierung auf Anwendungsebene (Schluessel +
Zaehler). RSSI an allen Montageorten gemessen und tragfaehig.
Details und Messwerte: Funkvalidierung_ESPNOW_2026-07-05.md
Offene Klaerpunkte vor Umsetzung: IoT-SSID vs. Hauptnetz
(Klienten-Isolation pruefen!), DHCP-Reservierungen, Nachrichtensatz,
Wiederverbindungslogik, Volllasttest 3V3, Inneneinheit 2 reparieren.

2026-07-06: WLAN/UDP ueber «Heimnetz» als Transport bestaetigt und
funktional abgenommen (Klingel + Tuer beidseitig, 0 Verluste am
verschaerften OG-Punkt). Langzeit + reale entfernte Funkstrecke als
Worst-Case-Annahme akzeptiert (siehe Funkvalidierung). Naechster
Schritt: Nachrichtensatz + Anwendungs-Authentifizierung Tuerbefehl.
