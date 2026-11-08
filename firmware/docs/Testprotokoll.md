# Testprotokoll – ClearBell-Firmware V0.2

Belege für jeden Portierungsschritt: **was** geprüft wurde, **womit**, **auf welchem Board**, **Ergebnis**. Ein
Schritt ist erst fertig, wenn alle seine Zeilen bestanden sind. Neueste Schritte oben.

**Boards am TOWER:** AU-1 = Außeneinheit an COM4, IN-1 = Inneneinheit (provisioniert als **EG**) an COM3. Eine
OG-Einheit gibt es auf dem Prüfplatz nicht; Zeilen mit OG prüfen deshalb das Verhalten „Wohnung antwortet nicht“.
Keine MAC-Adressen und keine Schlüssel in dieser Datei (das Repo ist öffentlich); die vollständigen
Konsolen-Mitschnitte liegen unversioniert in `build/konsole/`.

**Befehle:** `python tools/cb.py …` aus dem Ordner `firmware` (README Abschnitt 3). Zeichen: ✓ bestanden,
✗ nicht bestanden, ⏳ offen, – trifft nicht zu.

---

## Sammelliste offene Handprüfungen

Alles, was Finger, Ohr oder Messgerät braucht, wird hier gesammelt und **in einem Termin gemeinsam** geprüft
(Entscheidung Meister 06.11.2026). Die Firmware läuft dabei normal; mitlesen mit `python tools/cb.py konsole COM3 - 60`
bzw. `COM4`. Nach dem Termin die Ergebnisse in die jeweilige Zeile unten eintragen und die Zeile hier streichen.

| Zeile | Board | Was tun | Erwartung |
|---|---|---|---|
| 1.8 | AU-1 | LED-Ersatzlast an `J_DOOR1`, 12 V aus/an, Reset-Taster | Last bleibt dunkel |
| 5.11 | beide | Boot-Taster kurz | Lautstärke eine Stufe weiter, Tipp-Ton, bleibt nach `reboot` |
| 5.12 | beide | `play ring`, `play ok`, `play error`, `play nonet` | gefällt, unterscheidbar, kein Knacken |
| 6.10 | ~~AU-1~~ (beide ✓ 08.11., nach Behebung FW-B-06) – offen nur: Werte für die endgültige Schwelle am montierten Gerät | `touch-log 30` mitlaufen lassen, Elektroden nacheinander antippen, 2 s und 5 s halten | Ruhe ±2 %, Finger deutlich über +100 %; Werte notieren (Grundlage für die endgültige Schwelle) |
| 6.17 | beide | `play error`/`play nonet` mit **75 %**, dabei `touch-log 15`, nichts berühren (tagsüber) | kein „door key ignored“ ohne Berührung |
| 7.8 | AU-1 | LED-Ersatzlast an `J_DOOR1`; `door-test`, dann `door-test 300` nach 6 s | LED leuchtet 2 s bzw. kurz, sonst dunkel; Multimeter an `TP_DOOR_SW1` |
| 7.9 | AU-1 | Ersatzlast, IN-1 Türtaste 3 s halten und loslassen | LED 2 s an |
| 8.14 | beide | **verschoben bis Front/Diffusor vorhanden** (Meister 08.11.2026: ohne Front nur ein vorläufiger Wert; bis dahin Standard 40). LED ansehen (am besten mit Lichtleiter/Diffusor): grün, rot, bernstein; `led-balance 20` … `led-balance 70` probieren | grün und rot wirken gleich hell, bernstein ist als Mischfarbe erkennbar; gewählten Wert notieren (Standard 40) |
| 8.15 | IN-1 (PC ✓ 08.11.; offen: Handy) | Handy im WLAN: `http://clearbell-f3ac94.local/` (seit 10b; `clearbell-eg.local` gibt es nicht mehr) bzw. `http://192.168.1.176/` öffnen, Anmeldung `clearbell` + Passwort aus `cb.py anlage-zeigen --passwort IN-1` | Anmeldefenster, danach die Statusseite; aktualisiert sich alle 5 s; lesbar auf dem Handy |
| 8.16 | IN-1 | echten Boot-Taster 5 s halten (Wartung), auf der Seite einen Ton hochladen (z. B. `ok`), danach Taster kurz | Upload-Feld erscheint nur in der Wartung; Ton ersetzt und hörbar (`play ok`); danach wieder Normalbetrieb |
| 9.12 | AU-1 + Handy | Handy einrichten nach `cb.py fern-handy-anleitung <ID>` (Apps „ntfy“ und „HTTP Shortcuts“), Link setzen (`anlage-fern --link …`), AU-1 neu provisionieren; an EG klingeln, in der Push „Tür öffnen“ tippen | Push „Es klingelt – EG“ mit Knopf; Kurzbefehl läuft; Puls „for app <ID>“; Push „Tür per App geöffnet“ |
| 9.13 | AU-1 + Handy | AU-1 stromlos machen und wieder einschalten | Push „ClearBell neu gestartet. Grund: power-on“ |
| 9.14 | – | ~~echte Topics statt Test-Topics~~ – entfällt: seit 10c werden die Topics aus der EG-Kopplung abgeleitet | – |
| 8.18 | IN-1 + Handy | Wartung, auf dem Handy Passwort ändern (aktuelles `IN-1`, neues frei wählbar), neu anmelden; danach leer speichern → Etikett-Passwort; zum Schluss wieder `IN-1` (Konsole `web-pass IN-1`) | Anmeldung nur mit dem jeweils gültigen Passwort; andere offene Tabs zeigen „Anmeldung nicht mehr gültig“ statt die Sperre auszulösen |
| 10a.12 | IN-1 | Werksreset (Taster 2 × 5 s, dann 15 s), mit dem Handy den QR-Code aus `Geheimnisse/etiketten/f3ac94.png` bzw. die Daten aus der `.txt` scannen | Handy verbindet sich mit `ClearBell-f3ac94`, Einrichtungsseite öffnet sich von selbst (Captive Portal); WLAN wählen, EG, Speichern → Einheit im Heim-WLAN |
| 10a.13 | – | Etikett ansehen (`.txt`/`.png`): passt Inhalt und Größe zum geplanten Aufkleber? | Entscheidung Etikett |
| 10b.10 | AU-1 + IN-1 | nach 10a.12: auf der Seite der Außeneinheit „Inneneinheiten suchen“, Gerätecode vom Etikett eintippen | gekoppelt, beide starten neu, Klingeln geht |
| 10c.8 | IN-1 + Handy | Wartung an IN-1, Seite **auf dem Handy** öffnen, „Handy hinzufügen“, dann die beiden Links antippen (Apps ntfy und HTTP Shortcuts vorher installieren) | ntfy abonniert; HTTP Shortcuts zeigt den Import „ClearBell Tür öffnen“; nach dem nächsten Klingeln öffnet der Knopf in der Push den Kurzbefehl und die Tür |

---

## Schritt 11 – Abnahme: Dauertests (SA 10.3)

**7-h-Dauertest, lautlos (07.11.2026 21:12 – 08.11.2026 04:07, `tools/dauertest.py --stunden 7 --takt 300`).** Beide
Boards im Normalbetrieb mit Modem-Sleep, Gastnetz „DingDong“, AU-1 mit Firmware vom 07.11. (F-3-Text), IN-1 Stand
06.11. Auf Wunsch des Meisters nachts **ohne Klingeln, Töne und Türpulse**: Der PC liest nur alle 5 min `info` und `net`
mit; Netzverkehr ist der Herzschlag zwischen den Einheiten. Rohdaten: `V0.2/05_Inbetriebnahme/Dauertest/dauertest_20261107_2112.csv`
und `.log` im OneDrive-Projektordner.

| # | Prüfung | Erwartung | AU-1 | IN-1 |
|---|---|---|---|---|
| 11.1 | Abfragen gelesen | alle | 84/84 | 84/84 |
| 11.2 | kein Neustart | Startzähler gleich, Laufzeit steigt | ✓ Start #55, 384 → 25 285 s | ✓ Start #53, 2945 → 27 845 s |
| 11.3 | kein Coredump, Selbsttest | no / OK | ✓ | ✓ |
| 11.4 | kein Speicherleck | freier Heap ohne Abwärtstrend | ✓ 8 507 331 → 8 507 371 B (+40), kleinster Wert = Anfang | ✓ 8 569 119 → 8 569 123 B (+4), kleinster Wert 8 568 891 |
| 11.5 | kein Paketverlust | Wiederholungen, verworfene, doppelte Pakete, volle Warteschlange: kein Zuwachs | ✓ alle 0 | ✓ alle 0 |
| 11.6 | Verbindung | WLAN-Abbrüche 0, Gegenstelle bei jeder Abfrage erreichbar | ✓ 0, 84/84 | ✓ 0, 84/84 |
| 11.7 | Herzschlag | gesendet = empfangen | IN-1 → AU-1: 1660 gesendet / 1660 empfangen | AU-1 → IN-1: 1660 empfangen |
| 11.8 | Funk | – | RSSI −43 … −40 dBm | RSSI −52 … −39 dBm |

11.7: AU-1 zählt 6640 gesendete Rahmen, genau das Vierfache der bei IN-1 angekommenen. Vermutlich zählt der Zähler auch
Rahmen an die nicht vorhandene OG-Einheit mit, die ohne Adresse gar nicht gesendet werden (`link_send` verwirft sie).
Nicht im Einzelnen geprüft, kein Verlust: Retries und Erreichbarkeit sind sauber.

**Nicht abgedeckt** (lautlos): Klingelwege und Türbefehl unter Dauerlast. Die laufen vor dem 72-h-Test tagsüber mit,
z. B. `ring eg` in größerem Abstand bei kleiner Lautstärke. **72-h-Test:** offen (vor dem Einbau).

## Schritt 10c – Handys über die Webseite der Inneneinheit (06.11.2026)

Jeder Haushalt verwaltet seine Handys an der eigenen Inneneinheit (nur EG, SA 6.4). Der Schlüssel entsteht innen, geht
verschlüsselt im Event *PhoneAdd* zur Außeneinheit; der fertige Kurzbefehl (HTTP-Shortcuts-Format Version 92, reines
JSON) liegt 10 min / 3 Abrufe unter einem Einmal-Link. Topics und Kurzbefehl-ID leiten beide Seiten aus dem
EG-Schlüssel ab – nichts zu übertragen, nichts abzutippen. Getestet mit dem PC als Handy (`cb.py fern-handy-web`).

| # | Prüfung | Erwartung | Ergebnis |
|---|---|---|---|
| 10c.1 | `cb.py test` | Namen stabil und schlüsselgebunden (UUID v4, Base32-Topics), Stromverschlüsselung, Handys zur Laufzeit hinzufügen/ersetzen/entfernen | ✓ 113 Tests, 2782 Prüfungen |
| 10c.2 | IN-1 Wartung, `fern-handy-web IN-1 PC2` | Außeneinheit bestätigt; Antwort mit ntfy-Link und Import-Link auf den Download | ✓ |
| 10c.3 | Download ohne Anmeldung | Kurzbefehl v92, feste ID, `POST https://ntfy.sh/<Befehls-Topic>`, Variablen `token`, `cbbody`; Schlüssel nur im Skript | ✓ |
| 10c.4 | `fern-test PC2 --replay`, `ring eg` | Push mit Link `http-shortcuts://<ID>?token=…`, signierte Antwort öffnet („for app PC2“), Wiederholung abgelehnt | ✓ 5,5 s Antwort → Bestätigung |
| 10c.5 | „Handy entfernen“ (PC2), danach `fern-test PC2` | sofort gesperrt: „unknown phone“, keine Öffnung | ✓ |
| 10c.6 | ohne Wartung / OG-Einheit / nicht gekoppelt | abgelehnt mit Grund | ✓ (Wartung geprüft; OG nur im Code, keine OG-Einheit) |
| 10c.7 | Statusseite innen (Vorschau 375 px) | Handyliste, Entfernen, Hinzufügen; Links nach Erfolg | ✓ |

## Schritt 10b – Kopplung per Gerätecode, Wartung von innen, Pulslänge (06.11.2026)

| # | Prüfung | Erwartung | Ergebnis |
|---|---|---|---|
| 10b.1 | `cb.py test` | Kopplung: richtiger Code öffnet, falscher Code / anderes Gerät / jedes gekippte Bit nicht; Bestätigung bindet die Wohnung; Event-Nachricht einmal ausgeführt, Replay und Fehlschlag | ✓ 110 Tests |
| 10b.2 | AU-1 `/pair/list` | findet IN-1 per mDNS (`_clearbell._udp`, eg, nicht gekoppelt) | ✓ |
| 10b.3 | Kopplung im Normalbetrieb, EG schon gekoppelt | 403 „zum Ersetzen Wartung öffnen“ | ✓ |
| 10b.4 | 6 falsche Codes (AU-1 in Wartung) | 5 × „Gerätecode passt nicht“, dann gesperrt (10 min) | ✓ |
| 10b.5 | richtiger Code, klein getippt mit Bindestrichen | gekoppelt, beide Neustart; neuer Schlüssel; Klingeln 71 ms, Türbefehl 46 ms | ✓ |
| 10b.6 | IN-1 „Wartung der Außeneinheit öffnen“: im Normalbetrieb / in der Wartung | 403 / AU-1 geht in die Wartung | ✓ |
| 10b.7 | Pulslänge 800 ms, dann 9000 ms | gespeichert, `door-test` misst 800 ms; 9000 abgelehnt | ✓ (zurück auf 2000) |
| 10b.8 | eindeutige Namen | `clearbell-f3ac94.local`, `clearbell-f41138.local`; Statusabruf darüber | ✓ |
| 10b.9 | Regression nach allen Änderungen | 20/20 Klingeln (Median 80 ms), Touch → Tür 55 ms | ✓ |

## Schritt 10a – Werk und Käufer getrennt, Einrichtungs-WLAN, Werksreset (06.11.2026)

Der TOWER hat kein WLAN: Die Einrichtungsseite wurde über das Heimnetz geprüft (dieselben Handler), der Zugangspunkt
per WLAN-Scan des anderen Boards. Handy, QR-Code und Captive Portal in der Sammelliste (10a.12).

| # | Prüfung | Erwartung | Ergebnis |
|---|---|---|---|
| 10a.1 | `cb.py test` | Gerätecode (Erzeugen, Gruppieren, nachsichtiges Tippen), Formulardekodierung, WLAN-QR-Text; Taster 15 s nur in der Einrichtung, Zeitlimit Einrichtung | ✓ 100 Tests |
| 10a.2 | Start mit alten Daten (Schema 1) | einmalig übernommen: IN-1 EG mit WLAN und Kopplung, AU-1 mit Handy | ✓ |
| 10a.3 | IN-1 Einrichtung (`button-sim long` × 2), AU-1 `wifi-scan` | `ClearBell-f3ac94`, verschlüsselt, Kanal des Heim-WLANs | ✓ |
| 10a.4 | Einrichtungsseite übers Heimnetz (13 Fälle) | ohne Anmeldung 401; Seite, Info, Netzliste (3 s); zu kurzes Passwort / keine Wohnung / Reset ohne Bestätigung → 400; Speichern → Neustart; im Normalbetrieb Speichern/Reset → 403, Statusseite statt Einrichtung | ✓ 13/13 (nach Befund FW-B-04) |
| 10a.5 | falsches WLAN-Passwort, 5 min warten | Zugangspunkt geht von selbst auf | ✓ nach > 5 min sichtbar |
| 10a.6 | `cb.py werk innen COM3 --name IN-1` | Firmware, Töne mit `/werk/`-Kopien, Gerätecode (Schema 2), Etikett `.txt`; Statusseite mit neuem Code | ✓ |
| 10a.7 | Werksreset per Taster (Einrichtung, 15 s) | Käuferdaten gelöscht, 5 Werkstöne zurück, Gerätecode bleibt, Zugangspunkt offen | ✓ |
| 10a.8 | `cb.py einrichten IN-1 COM3` (Entwicklerweg statt Handy) | WLAN und Wohnung gesetzt, Einheit wieder im Netz | ✓ |
| 10a.9 | Einrichtungsseite (Vorschau 375 px) | Netzliste, Formular, Wohnung, Werkszustand | ✓ |

**Befund FW-B-04 (behoben):** Die Anmeldung rechnete für POST mit der Methode „GET“ – jedes Formular scheiterte und
löste nach 5 Versuchen die Sperre aus. Jetzt wird die echte Methode verwendet.

## Schritt 10 – Updates per Upload, Rollback nach Probezeit (06.11.2026)

Ohne Signaturschlüssel (Entscheidung 06.11.2026, R25). Schutz: Wartung (physische Anwesenheit), Gerätepasswort,
Prüfung des Image-Kopfs, SHA-256 des Images, Probezeit mit Rückfall.

| # | Prüfung | Erwartung | Ergebnis |
|---|---|---|---|
| 10.1 | `cb.py test` | Image-Kopf: passendes Image ok; anderer Einheitentyp, ohne Typ, fremdes Projekt, anderer Chip, zu kurz, kaputte Beschreibung, WAV abgewiesen; Probezeit: bestätigen, warten, zurück | ✓ 93 Tests |
| 10.2 | Upload im Normalbetrieb | 403 | ✓ |
| 10.3 | Außen-Image auf IN-1, halbes Image, 1 Bit gekippt, WAV | abgewiesen, alte Firmware läuft weiter | ✓ (anderer Typ / VALIDATE_FAILED / VALIDATE_FAILED / kein Image) |
| 10.4 | gültiges Image auf IN-1 | Übertragung 15 s, Neustart in app1, Probezeit, bestätigt nach 60 s | ✓ |
| 10.5 | `update-fault crash`, Update | neue Firmware stürzt ab → Bootloader zurück auf den alten Slot | ✓ |
| 10.6 | `update-fault nowifi`, Update | kein WLAN in der Probezeit → Rückfall nach 5 min | ✓ erst Absturz (Befund FW-B-05), nach der Korrektur: „NOT healthy (WiFi down, peers 0x00 of 0x02) – ROLLBACK“ bei 300 s, zurück in app0, wieder im WLAN |
| 10.7 | Türpuls während Flash-Schreibvorgängen | Timer-Interrupt läuft aus IRAM (`CONFIG_GPTIMER_ISR_IRAM_SAFE`) | ✓ konfiguriert; Pulse 2000/800 ms exakt gemessen |
| 10.8 | Versionsstring | `0.2.0-dev+innen` / `+aussen` im Image | ✓ |
| 10.9 | **Handprüfung** (08.11.2026, Meister): Update im Browser auf AU-1 und IN-1, Wartung, „Firmware aktualisieren“ | Fortschritt, Neustart, Probezeit endet; falsches Image abgewiesen | ✓ beide: falsches Image (innen auf außen) abgewiesen (Meister); danach `update-status` „running … from app1, confirmed“, Start #57 / #56, kein Coredump, Selbsttest OK. Inhalt: Statusseite mit Karte „Einstellungen“, Dunkelgrün, Handy-Layout (siehe 8.17) |

**Befund FW-B-05 (behoben):** Mit dem Testfehler „nowifi“ startete der Webserver ohne Netzwerk-Stack und die Firmware
stürzte ab (sie fiel trotzdem zurück). Der Stack startet jetzt immer; „nowifi“ verhindert nur das Verbinden. Relevant
auch für die Einrichtung ohne WLAN (Zugangspunkt bringt den Stack hoch).

---

## Schritt 9 – Fern-Öffnen per Push (06.11.2026)

Ablauf nach SA 4.5: Klingeln an EG → Einmal-Token → Push über ntfy.sh mit Knopf → das Handy signiert
`HMAC-SHA256(Handy-Schlüssel, "CBR1|token|handy")` und schickt die Antwort auf den Befehls-Topic → die Außeneinheit
prüft Verfahren, Handy, Token, Alter und Signatur und öffnet über den Tür-Task (gleiche Sperrzeit wie alle Wege).
Beide Verbindungen HTTPS mit Prüfung gegen das Zertifikatsbündel. Getestet mit **Test-Topics** (`cbtest-…`, Freigabe
Meister 06.11.2026) und einem Test-Handy „PC1“, das der PC spielt (`cb.py fern-test`); das echte Handy steht in der
Sammelliste.

| # | Prüfung | Erwartung | Ergebnis |
|---|---|---|---|
| 9.1 | `cb.py test` | Signatur gegen Python-Referenz; Öffnen genau einmal; falscher Schlüssel, fremdes Handy, nie ausgegebenes / abgelaufenes / fremdes Token; nur die neuesten 3 Token; Nachrichtenformat; JSON-Leser inkl. „Wert sieht aus wie Schlüssel“, zu tiefe Verschachtelung und Byte-Mutation | ✓ 90 Tests, 2583 Prüfungen |
| 9.2 | Gegenprobe: Signaturvergleich im Code entfernt | Tests werden rot | ✓ 3 Prüfungen rot |
| 9.3 | Start AU-1 | „remote: on, 1 phone(s)“; Befehlskanal per TLS verbunden; Neustart-Push gesendet | ✓ |
| 9.4 | `fern-test PC1 --replay`, dann `ring eg` | Push mit Token-Link → Antwort → Puls 2000 ms „for app PC1“ → Push „Tür per App geöffnet“; dieselbe Antwort 6 s später öffnet **nicht** | ✓ Antwort → Bestätigung auf dem PC 5,3 s; Replay: „unknown or used token“ |
| 9.5 | `fern-test PC1 --falsch` (falscher Schlüssel) | keine Öffnung, keine Push; Log „BAD SIGNATURE“ | ✓ |
| 9.6 | zweites `ring eg` 3 s nach dem ersten | keine zweite Push (höchstens eine je 30 s) | ✓ |
| 9.7 | `ring eg`, 1,5 s später `door-test 300`, `fern-test --nochmal` | gültige Antwort in der Sperrzeit: Push „Tür NICHT geöffnet: Sperrzeit“, Token bleibt gültig; dieselbe Antwort 6 s später öffnet | ✓ |
| 9.8 | 30 gefälschte Befehle in 11 s (Zufallssignatur, unbekanntes Handy, CBR2, 360 Zeichen, korrekt signiert mit nie ausgegebenem Token) | keine Öffnung; Zähler; Klingeln unbeeinflusst | ✓ 0 Öffnungen, `ring eg` danach 56 ms |
| 9.9 | Öffnungsprotokoll AU-1 | Fern-Öffnungen einzeln mit Uhrzeit („remote“) | ✓ |
| 9.10 | `wifi-drop` | Befehlskanal verbindet sich nach dem WLAN von selbst neu | ✓ 2. Verbindung ohne Neustart |
| 9.11 | Speicher mit zwei TLS-Verbindungen | TLS-Puffer im PSRAM, interner RAM bleibt reichlich | ✓ kleinster freier interner RAM 108 KB |

**Festlegungen (Konkretisierung der SA):**
- **Nachricht des Handys:** `CBR1 <Token 32 hex> <Handy-ID> <Signatur 64 hex>`; der Handy-Schlüssel wird **als Text**
  (seine 64 Hex-Zeichen) benutzt, weil jedes Handy-Werkzeug HMAC mit Text-Schlüssel kann (HTTP Shortcuts:
  `toHexString(hmac('SHA-256', key, message))`, laut Doku der App 06.11.2026). 256 bit bleiben es trotzdem.
- **Token** 5 min (Entwurf, SA 4.5), die neuesten 3 bleiben gültig (Besucher klingelt zweimal); eine Öffnung verbraucht
  alle. Eine Ablehnung wegen Sperrzeit verbraucht nichts.
- **Push nur für EG** (SA 6.4), höchstens eine je 30 s; Messreihen (`ring eg 50`) pushen nie. Abgelehnte Befehle
  erzeugen keine Push (sonst könnte jeder, der den Topic kennt, die Handys fluten).
- **Neustart-Push** mit Grund bei jedem Start (SA 7.5).
- **Topics und Handy-Schlüssel** stehen in der Anlagendatei (`fern`), auf dem Gerät in `fctry`; nie im Repo.
- **iOS** bleibt offen (SA 4.5); das Verfahrenskennzeichen `CBR1` lässt ein zweites Verfahren zu.

---

## Schritt 8 – Diagnose: Statusseite, Protokoll der Türöffnungen, LED-PWM, Ton-Upload (06.11.2026)

Statusseite je Einheit im eigenen Task des IDF-HTTP-Servers, Anmeldung per **HTTP Digest** mit dem Gerätepasswort
(das Passwort geht nie im Klartext übers WLAN). Werte liest immer der Task, dem sie gehören: Die Seite fragt die
Hauptschleife nach einer Momentaufnahme. Ohne Finger getestet: Wartung per `button-sim long` (USB-Konsole, wie
`touch-sim`), Abruf per `cb.py status`, Angriffe per Skript mit Python `http.client`. Browser-Ansicht mit echten
Daten lokal geprüft (Desktop und 375 px Handybreite, kein waagrechtes Scrollen); Anmeldung im Browser in der Sammelliste.

| # | Prüfung | Erwartung | Ergebnis |
|---|---|---|---|
| 8.1 | `cb.py test` | MD5 gegen RFC 1321, Digest gegen das Beispiel aus RFC 7616; Parser weist kaputte Kopfzeilen ab; Replay, fremde URI, abgelaufene und vergessene Nonces, Sperre nach 5 Fehlversuchen; Öffnungsprotokoll: Ringpuffer, Speichern/Laden, beschädigte Daten (Bit-Fuzzer); JSON-Schreiber | ✓ 82 Tests, 2526 Prüfungen |
| 8.2 | Gegenprobe: Replay-Prüfung und Nonce-Untergrenze im Code abgeschaltet | Tests werden rot | ✓ 2 Tests rot |
| 8.3 | `cb.py status IN-1` / `AU-1` | Anmeldung per Digest über `clearbell-eg.local` bzw. `-aussen.local`; Funk, Gegenstellen, Touch-Rohwert und Baseline, Audio-Dateien, System; Uhrzeit per NTP | ✓ beide, Uhrzeit gesetzt |
| 8.4 | Angriffe gegen IN-1 (19 Fälle): ohne Anmeldung, Basic-Auth, Replay, Kopfzeile für andere URI, erfundene Nonce, 5 falsche Passwörter | 401 ohne Daten; erfundene Nonce → „stale“; nach 5 Fehlversuchen 429, auch das richtige Passwort 30 s lang | ✓ 19/19 |
| 8.5 | mit gültiger Anmeldung: `GET/POST/PUT /door`, `/open`, `POST /`, `POST /status.json`, `GET/DELETE /ton/ring` | **keine Tür über das Web** (R17): 404/405 | ✓ |
| 8.6 | Upload im Normalbetrieb | 403 „nur in der Wartung“ | ✓ |
| 8.7 | Wartung (`button-sim long`), Upload: keine WAV, 8-Bit-WAV, 1,06 MB, unbekannter Tonname, gültige 1-s-WAV | 422, 422, 413, 404 – alter Ton bleibt; gültige ersetzt `/klingel.wav` | ✓; neuer Ton per Mikrofon gehört (+28 dB) |
| 8.8 | abgebrochener Upload (30 000 von 200 000 Byte, Verbindung getrennt) | alter Ton bleibt, keine Zwischendatei (Belegung = Summe der Töne) | ✓ |
| 8.9 | Upload, während der Klingelton läuft | wartet, bis der Ton fertig ist; Ton nicht abgeschnitten | ✓ 3,3 s statt 0,8 s, Ton gespielt, 0 Fehler |
| 8.10 | Öffnungsprotokoll IN-1: `door` (AU-1 noch im Start), `door`, sofort nochmal | „keine Antwort“, „geöffnet“, „abgelehnt: Sperrzeit“ – mit Uhrzeit | ✓ |
| 8.11 | Öffnungsprotokoll AU-1 nach den Befehlen von IN-1, dann `door-test 300`, `reboot` | Innentasten-Öffnungen **nur als Summe** (2), Service-Puls einzeln mit Uhrzeit; nach Neustart erhalten; JSON ohne „wer zuletzt“ | ✓ |
| 8.12 | 25 Klingeln, währenddessen 90 Abrufe der Statusseite (beide Einheiten) | alle quittiert, Zeiten wie ohne Seite (Schritt 3: Median 71 ms) | ✓ 25/25, Median 73 ms, max 136 ms |
| 8.13 | Dauer der Momentaufnahme in der Hauptschleife (`web`) | wenige ms | ✓ 3–5 ms (vorher 23 ms, siehe Befund) |
| 8.15 | **Handprüfung PC** (08.11.2026, Meister, Firefox): IN-1 im Browser | Anmeldung, Statusseite, lesbar | ✓ über `http://192.168.1.176/`; alle Karten gefüllt (Funk, Protokoll, Touch −1,3 %, Kopplung f41138, Handys, Türöffnungen mit den 3 Öffnungen vom 08.11., Audio 15 %, System Start 55). `clearbell-eg.local` löst nicht auf – richtig, seit 10b heißt die Einheit `clearbell-f3ac94.local` (Anleitung der Sammelliste veraltet, korrigiert). **Meister 08.11.2026: keine Zusatznamen wie `clearbell-eg.local`** – die Nummern-Namen trennen mehrere Anlagen im Verkauf. Nachgemessen: beide Einheiten beantworten mDNS 20/20 (≤ 0,25 s, Mitschnitt); dass Windows auf dem TOWER `clearbell-f3ac94.local` zeitweise sofort ablehnt, liegt an einer zwischengespeicherten Fehlantwort von einer Abfrage während des Neustarts (`ipconfig /flushdns` als Administrator). Kosmetik: Spaltenköpfe der Touch-Tabelle klebten zusammen („RohwertBaselineAbweichung“) → Zellenabstand 10 px ergänzt (`cb_status_page.h`), seit 10.9 (08.11.) auf beiden Einheiten. Handy offen |
| 8.17 | **Statusseite überarbeitet** (Meister 08.11.2026): Karte „Einstellungen“ oben – Lautstärke 15/30/50/75/100 % auch im Normalbetrieb (Entscheidung Meister: der Boot-Taster ändert sie ohnehin ohne Anmeldung), Probeton; LED-Abgleich und Pulslänge nur in der Wartung (`POST /settings`, sonst 403; Werte außerhalb des Bereichs abgelehnt, auch 261 → nicht als 5 gedeutet). Akzent Dunkelgrün (Kopfleiste, Kartenrand, Überschriften, Knöpfe) hell/dunkel; unter 640 px einspaltig, Knöpfe/Felder volle Breite, ≥ 44 px bei Touch, 16-px-Felder | Vorschau mit echter `status.json` von AU-1: Desktop dunkel, Handy 375 px hell, Normalbetrieb und Wartung | ✓ Darstellung (Vorschau); Bedienung am Gerät ✓ (Meister 08.11.2026, Handy) |
| 8.18 | **Eigenes Passwort der Statusseite** (Meister 08.11.2026; Produktreife 8): in der Wartung auf der Karte „Einstellungen“ (aktuelles + neues Passwort), Konsole `web-pass <neu>` / `web-pass label`; gespeichert in `cb_user` „web_pass“, Werksreset → Etikett. Das neue Passwort geht **verschlüsselt** über das WLAN (XOR mit MD5-Schlüsselstrom aus aktuellem Passwort und Einmal-Nonce, MAC prüft das aktuelle Passwort, `cb_http_auth.h`); 5 falsche aktuelle Passwörter → 60 s Sperre | PC-Tests (Hin/Rück, falsches aktuelles, anderer Nonce, leer = Etikett, zu kurz/Umlaut/Leerzeichen); JS-MD5 = C-Ergebnis; am Gerät per HTTP | ✓ 120 Tests / 2821 Prüfungen; Seite und Firmware liefern denselben Vektor (`enc ac46…5b6f`, `mac b3ba…e58e`), md5("abc") und 100 × „a“ richtig. IN-1 über HTTP: falsches aktuelles → „aktuelles Passwort falsch“, „ab“ → abgelehnt, Wechsel → Anmeldung mit neuem ok, Rückwechsel ok. **Gefunden dabei:** urllib wiederholt mit altem Passwort 5× → 30-s-Sperre für alle; ein offener Browser-Tab hätte sie alle 5 s neu ausgelöst → Seite stoppt die Abfrage bei 401/429 und bittet um Neuladen. Gesetzt (Meister): AU-1 → `AU-1`, IN-1 → `IN-1` (`cb.py anlage-webpasswort`, damit die PC-Werkzeuge mitkommen) |

**Befund FW-B-03 (behoben):** Die erste Fassung las die Belegung des Dateisystems bei jeder Momentaufnahme
(`LittleFS.usedBytes()` geht alle Blöcke durch) und hielt die Hauptschleife damit 23 ms auf. Jetzt wird die Belegung
nur beim Start und nach einem Upload ermittelt: 3–5 ms.

**Festlegungen (Konkretisierung der SA, keine Abweichung):**
- **Anmeldung per HTTP Digest (MD5, qop=auth)**, Benutzer `clearbell`. Basic hätte das Passwort im Klartext übers
  WLAN geschickt – lesbar für jedes Gerät mit WLAN-Schlüssel, also auch für den anderen Haushalt (Angreifer F). MD5
  genügt, weil das Gerätepasswort zufällig ist (96 bit). Nonces mit Ablauf 10 min und Zähler gegen Replay;
  5 Fehlversuche → 30 s Sperre für alle.
- **Die Touch-Zeile zeigt die Firmware-Baseline** (bestätigte Abweichung von SA 5.6, Schritt 6).
- **Wartung auch per USB-Konsole** (`button-sim long|short`): speist eine Taster-Geste in die echte Moduslogik. USB
  braucht das Kabel am geöffneten Gerät – gleiche Anwesenheit wie der Taster (Begründung wie bei `door-test`).
- **Uhrzeit per NTP** (`pool.ntp.org`, `time.cloudflare.com`, Zeitzone Deutschland); ohne Internet „Start #n + s“.
- Seite deutsch, JSON-Schlüssel englisch; die Seite lädt nichts von außen (Content-Security-Policy).

---

## Schritt 7 – Tür-Task: Puls an GPIO47 (06.11.2026)

Ab hier schaltet die Außeneinheit GPIO47 wirklich. Am Prüfplatz hängt kein echter Öffner. Geprüft per Rücklesen des
Pins (Ein-/Ausgang) und Zeitmessung im Timer-Interrupt; Last und echter Öffner in der Sammelliste (7.8–7.10).
Werte: Puls 2000 ms (F-3, festgelegt 07.11.2026 nach 7.10), harte Obergrenze 5000 ms, Sperrzeit 5000 ms ab Beginn
(Entwurf), für alle Pfade.

| # | Prüfung | Erwartung | Ergebnis |
|---|---|---|---|
| 7.1 | `cb.py test` | Sperrzeit für alle Pfade, auch über den 49-Tage-Überlauf; Pulslänge begrenzt | ✓ 70 Tests, 275 Prüfungen |
| 7.2 | Textsuche `CB_PIN_DOOR_DRV` | nur `cb_config.h` (Definition), `main.cpp` (LOW beim Start), `cb_door.cpp` | ✓ |
| 7.3 | Start | „door: ready, pin LOW“ | ✓ |
| 7.4 | `door-test` | Pin HIGH zurückgelesen, Ende nach 2000 ms per Interrupt, danach LOW | ✓ gemessen 2000 ms |
| 7.5 | zweiter `door-test` in der Sperrzeit; `door-test 300`; `door-test 9000` | abgelehnt; 300 ms; außerhalb der Grenze abgelehnt | ✓ |
| 7.6 | `door-test 2000`, sofort `wdt-test` (Hauptschleife hängt) | Puls endet trotzdem nach 2000 ms, danach Watchdog-Neustart, Pin LOW | ✓ Ende nach 2000 ms während des Hängers; nach Neustart LOW |
| 7.7 | IN-1 `touch-sim 400 3500`, danach `door` | Puls 2000 ms „for EG“; zweiter Befehl: „refused: lock-out time“ | ✓ OPENED nach 112 ms, Puls 2000 ms, zweiter abgelehnt |
| 7.10 | **echter Öffner effeff 118W** an `J_DOOR1` (12 V Labornetzteil, Spulenstrom 270 mA, Freilauf `D_FLY1` auf der Platine): 10 × `door-test` (2000 ms) mit ≈ 6 s Abstand (07.11.2026, zusammen mit Bring-up 6.5) | Öffner schaltet, kein Neustart, Q2/`D_FLY1` höchstens handwarm | ✓ 10/10 geschaltet (Meister), je 2000 ms gemessen, 0 Pulsfehler, kein Neustart (Start #54 durchgehend), Q2/`D_FLY1` handwarm bestätigt (Meister). Pulslänge F-3 damit auf 2000 ms festgelegt (Meister) |

**Hinweis:** Ein Neustart der Außeneinheit setzt die Sperrzeit zurück (sie liegt im RAM). Ein Neustart dauert länger
als ein paar Sekunden und ist im Log sichtbar; bewusst so belassen.

## Schritt 6 – Touch (06.11.2026)

**Ohne Finger geprüft:** Ruhewerte am echten Board, und die gesamte Kette mit `touch-sim` (setzt für eine Zeit
„Baseline + x %“ in die echte Auswertung ein). Alles mit echter Berührung steht in der Sammelliste oben (6.10–6.16).
Schwellen sind Entwürfe: ein +100 %, aus +50 %, Mindest-Haltedauer außen 60 ms, innen 150 ms.

| # | Prüfung | Erwartung | Ergebnis |
|---|---|---|---|
| 6.1 | `cb.py test` | Baseline nur bei Ruhe gelernt; Drift nachgeführt, Finger nicht; Hand am Draht (+20 %, 25 s) und Wischen (+3…+37 %) kein Druck; Tuch > 10 s → neu lernen und Erholung; ein Klingeln je Druck, Mindest-Haltedauer; beide Tasten → kein Klingeln; Tipp im Fenster öffnet, außerhalb nicht; Fenster endet nach 2 min; 3 s halten öffnet; zu lang / 2,5 s / zu kurz abgelehnt; nasses Tuch öffnet nie | ✓ 67 Tests, 265 Prüfungen |
| 6.2 | Start, `touch` | Baseline nach ≈ 1,1 s; Werte wie im Bring-up | ✓ AU-1 EG 41 954 / OG 38 898, IN-1 31 104; Ruhe −0,3 … +0,1 % |
| 6.3 | Dauer `touchRead()` | Hauptschleife wird nicht gebremst | ✓ Mittel 7–30 µs, max 183 µs; nur der allererste Aufruf (Treiberstart) 18 ms |
| 6.4 | AU-1 `touch-sim eg 400 40` | 40 ms unter der Mindest-Haltedauer: kein Klingeln | ✓ |
| 6.5 | AU-1 `touch-sim eg 400 300` | Klingeln EG, IN-1 klingelt und öffnet das Fenster | ✓ Quittung 72–122 ms, „window armed for 120 s“ |
| 6.6 | IN-1 `touch-sim 400 400` im Fenster, danach nochmal | erster Tipp öffnet, Fenster endet, zweiter Tipp ignoriert | ✓ OPENED nach 92 ms; zweiter ignoriert |
| 6.7 | IN-1 `touch-sim 400 3500` außerhalb des Fensters | nach 3 s Tipp-Ton, beim Loslassen Tür auf | ✓ OPENED nach 97 ms |
| 6.8 | IN-1 `touch-sim 400 9000` / `37 5000` / `20 8000` | zu lang: ignoriert; Wischen und Annäherung: nichts | ✓ |
| 6.9 | AU-1 `touch-sim both 400 1000`, dann `og 400 300` | beide: kein Klingeln (Nässe); OG: Klingeln, Fehler nach 1,2 s (keine OG-Einheit) | ✓ |

**Handprüfungen mit dem Finger (08.11.2026, Meister, Lautstärke 15 %).** Mitschnitte `touch_IN1_20261108*.txt` in
`V0.2/05_Inbetriebnahme/` (OneDrive-Projektordner).

| # | Prüfung | Erwartung | Ergebnis |
|---|---|---|---|
| 6.10 IN-1 | `touch-log 30`, Finger auflegen | Ruhe ±2 %, Finger ≫ +100 % | ✓ Ruhe −0,7 … +1,1 %, Finger +394 … +500 % (Baseline 28 828); beim Loslassen einmal kurz +20 % im Zustand „rest“ |
| 6.13 IN-1 | Klingeln (`ring eg` an AU-1 statt Finger außen), innen kurz tippen | Tür öffnet, Fenster endet | ✓ „tap in the armed window“, OPENED nach 90 ms; zwei weitere Tipps danach ignoriert. Teil „außen mit Finger“ offen (6.11) |
| 6.14 IN-1 | ohne Fenster ≈ 2 s halten; 3,5 s halten und loslassen | ignoriert; nach dem Tipp-Ton öffnet | ✓ ignoriert; „held 3 s“, OPENED nach 49 ms |
| 6.15 IN-1 | Finger ≈ 7,5 s auflegen | öffnet nicht | ✓ „held 3 s“, beim Loslassen nach 7,5 s ignoriert; danach Ruhe +0,7 %. Feuchtes Tuch offen |
| 6.10 AU-1 | `touch-log 30`, EG und OG antippen, EG ≈ 5 s halten | Ruhe ±2 %, Finger ≫ +100 % | ✗ **Ruhe +13,6 % (EG) / +11,0 … +14,1 % (OG) über den ganzen Mitschnitt** → FW-B-06_Baseline_eingefroren. Finger: EG +368 … +450 %, OG +356 % (`touch_AU1_20261108.txt`) |
| 6.11 AU-1 | EG kurz, OG kurz | je ein Klingeln; OG-Fehler | ✓ EG quittiert nach 51 ms; OG „FAILED – flat did not answer“ nach 1201 ms; EG 5 s halten → genau ein Klingeln (38 ms) |
| 6.12 AU-1 | flache Hand auf beide Elektroden ≈ 4,5 s | kein Klingeln | ✗ **EG wurde 21 ms vor OG als gedrückt erkannt, das Klingeln ging trotz „BOTH keys raised – wetting, no ring“ raus** (quittiert 125 ms) → FW-B-07_Beide_Tasten_klingelt. Werte bis +6400 % (EG) / +6200 % (OG), steigend |
| 6.18 | **Nach Behebung FW-B-06 und FW-B-07** (beide Boards neu geflasht 08.11.2026; `cb.py test` 118 Tests, 2800 Prüfungen, 0 fehlgeschlagen; Gegenprobe ohne die Korrekturen: 4 Prüfungen in den neuen Tests rot) | | |
| 6.10 AU-1 (2) | Ruhe nach dem Neustart, Finger | Ruhe ±2 % | ✓ −0,9 … +1,1 % vor den Berührungen; nach der flachen Hand kurz +4 … +17 %, 20 s später wieder −0,8 / −1,1 %. Finger EG +253 … +310 %, OG +235 % (`touch_AU1_20261108_nachpruefung.txt`) |
| 6.11 AU-1 (2) | EG kurz, OG kurz | wie oben | ✓ EG quittiert nach 102 ms, OG-Fehler nach 1201 ms |
| 6.12 AU-1 (2) | flache Hand auf beide Tasten | kein Klingeln | ✓ OG 100 ms vor EG erkannt, „BOTH keys raised“, **kein Klingeln** |
| 6.12b AU-1 | flache Hand von der EG-Seite her über beide Tasten geschoben | (offen, siehe Text) | EG ≈ 0,4 s allein gedrückt → Klingeln EG (98 ms), erst danach OG → „BOTH“. Liegt jenseits der Wartezeit (60 + 60 + 120 ms) und ist von „EG gedrückt, dann OG gestreift“ nicht zu unterscheiden. **Meister 08.11.2026: so lassen** (längere Wartezeit würde SA 1.3 ≤ 0,5 s reißen) |
| 6.12c AU-1 | nasses Tuch ≈ 15 s über beide Tasten, wegnehmen, EG tippen | kein Klingeln; danach normal | ✓ kein Klingeln; Tuch hob beide nur auf **+11,4 … +12,0 %** (beim Auflegen kurz bis +78 % EG / +30 % OG), weit unter +100 % – daher auch kein Neulernen nötig; nach dem Wegnehmen sofort ±0,5 %; EG-Tipp quittiert nach 84 ms (`touch_AU1_20261108_tuch.txt`) |
| 6.15b IN-1 | Finger > 10 s auflegen, loslassen, dann 3 s halten | Neulernen, keine Öffnung; danach öffnet das 3-s-Halten | ✓ (Meister, ohne Mitschnitt) – Zähler danach: relearned 1, rejected 1; Öffnungsprotokoll 06:31:55 „opened“; Ruhe −1,5 % |
| 6.15c IN-1 | nasses Tuch | – | entfällt (Meister 08.11.2026): Inneneinheit hängt innen; das Tuch hob an AU-1 nur +12 % und deckt den Neulern-Pfad nicht ab – der ist mit 6.15b am Gerät belegt |
| 6.16 IN-1 | Finger auflegen, `reboot`, ≈ 40 s weiter halten, loslassen | keine Auslösung; nach dem Loslassen normaler Ruhewert | ✓ mit Einschränkung der Erwartung: Ein still aufgelegter Finger gilt als „Ruhe“ → Baseline **mit Finger** gelernt (187 695, Rohwert ≈ 192 000, Δ +2 … +3 %), keine Auslösung. Nach dem Loslassen schnelle Nachführung nach unten: Baseline 29 204, Δ −2,0 → −1,9 % (danach langsame Nachführung). Kein Ereignis, kein Neulernen. AU-1 nicht eigens geprüft (gleiche `TouchChannel`-Logik) |
| 6.17 IN-1 | Klingelton (15 %) ohne Berührung | Touch unbeeinflusst | ✓ −0,2 … +0,6 %, kein Ereignis. **Offen bei 75 %:** beim Ton-Abspielen mit 75 % (5.12, 08.11.) kam dreimal „door key ignored“ ohne Berührung – Verdacht Lautsprecher → Touch, tagsüber wiederholen |

**Abweichung von SA 5.6, vom Nutzer bestätigt am 06.11.2026** (ebenso: Scharfschaltfenster endet mit der ersten
Öffnung, offen in SA 12): Die Baseline wird **in der Firmware** geführt (`cb_touch_logic`), nicht
die Hardware-Baseline („benchmark“) des Touch-Treibers genutzt. Gründe: (1) SA 5.6 Maßnahme 2 verlangt „keine
Nachführung, solange gedrückt“ – das ist so garantiert und nicht vom Treiberverhalten abhängig, das der Bring-up nie
klären konnte; (2) die Regeln sind auf dem PC testbar (Tuch, Wischen, Drift); (3) die Rohwerte kommen weiter aus dem
Hardware-Filter des Treibers. Der Denoise-Kanal ist über den Arduino-Treiber nicht schaltbar (Bring-up 5.2.6) und
bleibt Standard. Die Statusseite (Schritt 8) zeigt Rohwert und Baseline wie gefordert.

**Entwurfsentscheidung:** Das Scharfschaltfenster endet mit der ersten erfolgreichen Öffnung (SA 12: offen).

## Schritt 5 – Audio (06.11.2026)

**Prüfmethode neu:** Jedes Board prüft seine Töne mit dem **eigenen Mikrofon** (`audio-check`, nur Diagnose, nur
über USB, Mikrofon danach wieder aus). Ein Ton gilt als „HEARD“ ab +10 dB über der Ruhe; bei Sinus-Proben wird
zusätzlich genau die Tonfrequenz gemessen (Goertzel). `listen` lässt ein Board einem anderen zuhören. Die Pegel sind
relativ zum Mikrofon-Vollausschlag (dBFS), keine Schalldruckpegel.

| # | Prüfung | Erwartung | Ergebnis |
|---|---|---|---|
| 5.1 | `cb.py test` | WAV-Parser (unbekannte/ungerade Chunks, falsche Formate, abgeschnitten, Riesengröße), **Fuzzer mit 20 000 beschädigten Dateien** ohne Zugriff außerhalb der Datei, Rangfolge, Lautstärke-Kappung, Sättigung, Pegelmessung | ✓ 55 Tests, 237 Prüfungen |
| 5.2 | IN-1 ohne Ton-Abbild: Start und `play ring` | stumm, Meldung im Log, kein Neustart | ✓ |
| 5.3 | `cb.py toene` auf beide, `files` | 5 Dateien, 44,1 kHz mono, Längen wie erzeugt | ✓ |
| 5.4 | `audio-check` aller Töne, IN-1 | alle HEARD | ✓ +20 … +45 dB |
| 5.5 | `audio-check` aller Töne, AU-1 | alle HEARD | ✓ +17 … +26 dB (Mikrofon hört den eigenen Lautsprecher am Prüfaufbau ≈ 20 dB leiser als bei IN-1) |
| 5.6 | Sinus 2 kHz bei 15/30/50/75/100 % (IN-1) | Abstand 15 → 100 % = 16,5 dB; Gesamtpegel = Tonpegel (keine Oberwellen) | ✓ 16,4 dB; Unterschied Gesamt/Ton ≤ 0,2 dB auf allen Stufen |
| 5.7 | AU-1 `ring eg`, IN-1 `listen 5` | IN-1 klingelt hörbar, Ton ≤ 0,5 s nach der Berührung (SA 1.3) | ✓ Quittung 61 ms, Ton < 140 ms nach „RING“ (250-ms-Fenster), Ding-Dong im Pegelverlauf |
| 5.8 | Rangfolge: `play ring`, dann `tick` und `ok`; danach `ok`, dann `error` | tick/ok während ring verworfen; error ersetzt ok | ✓ dropped 2, replaced 1 |
| 5.9 | AU-1 ohne WLAN (`wifi-drop`), sofort `ring eg` | sofort „NO WIFI“ + Hinweiston, kein Bestätigungston (F-4) | ✓ |
| 5.10 | `info` nach allen Tests | kein Neustart, kein Coredump | ✓ |
| 5.11 | **Handprüfung:** Boot-Taster kurz drücken | Lautstärke eine Stufe weiter, Tipp-Ton, bleibt nach Neustart | ⏳ |
| 5.12 | **Ohrtest:** Klingelton, ok, Fehler, kein Netz | klingen sauber, unterscheidbar; kein Knacken beim Ein-/Ausschalten des Verstärkers (20 ms Vorlauf, 30 ms Nachlauf) | ⏳ |

**Befunde:**
- **FW-B-02_440Hz_klirrt_auch_begrenzt:** Sinus 440 Hz bei 100 % (= 40 % digital): Gesamtpegel 0,7 dB über dem
  Grundton (≈ 17 % Oberwellenleistung); 880 Hz 0,2 dB, 2 kHz ≤ 0,1 dB. Das Band um 440 Hz verzerrt also auch
  unterhalb der Clipping-Grenze – passt zur Lautsprecher-Resonanz (IN-B-02). Folge: Die neuen Töne haben keinen
  Grundton unter 880 Hz (`tools/toene/erzeuge_toene.py`). Die V0.1-Töne werden nicht übernommen.
- Die Ruhemessung vor „ok“ lag bei −60 statt −80 dBFS, weil der vorherige Klingelton (3,55 s) noch ausklang –
  kein Fehler.

## Schritt 4 – Türbefehl per Challenge–Response, Ratenbegrenzung (06.11.2026)

Der Türbefehl wird innen vorerst per Konsolenbefehl `door` ausgelöst (Touch folgt in Schritt 6). Die Außeneinheit
entscheidet und quittiert echt, **schaltet GPIO47 aber noch nicht** („dry“, Puls folgt mit dem Tür-Task in
Schritt 7). Sperrzeit vorläufig 5 s (`CB_DOOR_LOCKOUT_MS`).

| # | Prüfung | Erwartung | Ergebnis |
|---|---|---|---|
| 4.1 | `cb.py test` | Tür öffnet einmal; eine Transaktion zugleich; Sperrzeit wird gemeldet; aufgezeichnete Bestätigung öffnet nicht erneut (gleiche Sitzung, nach Neustart, nach Sekunden); Bestätigung nach > 2 s abgelehnt; verlorene Herausforderung → neue, öffnet einmal; verlorene Quittung → gleiche Antwort, kein zweites Öffnen; EG-Herausforderung mit OG-Schlüssel abgelehnt; geratene Nonce mit richtigem Schlüssel abgelehnt; Außeneinheit weg → „keine Antwort“; Ratenbegrenzung | ✓ 43 Tests, 192 Prüfungen |
| 4.2 | Gegenprobe: Nonce-Prüfung im Code testweise abgeschaltet | Sicherheitstests müssen scheitern | ✓ 4 Tests rot, danach zurückgesetzt |
| 4.3 | `door` an IN-1 | „OPENED“, AU-1 meldet „DOOR OPEN by EG“ | ✓ 116 ms |
| 4.4 | zweites `door` 2 s danach | „refused: lock-out time“ | ✓ |
| 4.5 | 20 × `door` im 6-s-Takt | 20/20 geöffnet, ≤ 1 s (SA 1.3) | ✓ 20/20, min 35, Median 102, max 129 ms |
| 4.6 | 500 gefälschte Rahmen vom PC an AU-1, sofort danach klingeln und `door` | Ratenbegrenzung greift vor dem HMAC, beides funktioniert | ✓ 79 begrenzt, 21 Tag-Fehler; Klingeln 355 ms (1 Wiederholung), Tür 125 ms |

**Offen:** echter Puls an GPIO47 und Sperrzeit im Tür-Task (Schritt 7), Auslösen per Touch mit
Scharfschaltfenster (Schritt 6).

## Schritt 3 – Netz: WLAN, Protokoll v2, Sitzungen, Herzschlag (06.11.2026)

Netz: Gastnetz des Heim-Routers (Name absichtlich nicht im öffentlichen Repo), Kanal 3, RSSI −38 dBm an beiden Boards, Modem-Sleep an
(Entscheidung M-23). Firmware `0.2.0-dev`.

| # | Prüfung | Erwartung | Ergebnis |
|---|---|---|---|
| 3.1 | `cb.py test` (Protokoll-Engine simuliert) | alle Fälle aus SA 11.2 ohne Hardware: Verlust der Quittung, 3 Wiederholungen, Fehler nach 1,2 s, 3 Sendeslots, Neustart beider Seiten, Replay gleiche/alte Sitzung, alte Quittung, EG↔OG, Herzschlag | ✓ 32 Tests, 163 Prüfungen |
| 3.2 | Start, `net` auf beiden | WLAN, Gegenstelle per mDNS gefunden, Sitzung bestätigt, „reachable“ | ✓ |
| 3.3 | `ring eg` | Quittung von IN-1, „RING“ in der IN-1-Konsole | ✓ 32 ms |
| 3.4 | `ring og` (keine OG-Einheit) | Fehler nach 1,2 s, nicht früher | ✓ 1,199 s |
| 3.5 | `ring eg 50` (alle 2 s, beide im Modem-Sleep, Sender wach während des Klingelns) | 50/50, alle ≤ 300 ms | ✓ 50/50, Median 71, 95 % 123, max 209 ms, 0 Wiederholungen |
| 3.6 | AU-1 neu starten zwischen zwei Klingeln | beide kommen in IN-1 an (V0.1-Fehler „quittiert, aber nicht gespielt“) | ✓ 2× „RING“ |
| 3.7 | IN-1 neu starten, dann klingeln | Quittung, „RING“ | ✓ 31 ms |
| 3.8 | Zeit Neustart → Gegenstelle erreichbar | ≤ 30 s (SA 1.3 Selbstheilung) | ✓ WLAN nach 3,5 s, Sitzung nach 5,9 s |
| 3.9 | `wifi-drop` auf AU-1, dann auf IN-1 | Wiederverbindung ohne Neustart, Klingeln danach | ✓ je ≈ 4 s, Startzähler unverändert |
| 3.10 | 20 Rahmen mit gefälschtem Tag, 5 Fremdpakete vom PC an AU-1 | verworfen und gezählt, Klingeln geht weiter | ✓ tag 20, len 5; Klingeln 103 ms |
| 3.11 | Burst von 30 Paketen | Warteschlange (16) läuft über, Gerät bleibt bedienbar | ✓ 5 × „queue full“ – Ratenbegrenzung je Absender folgt in Schritt 4 |

**Befunde:**
- **FW-B-01_Suchping_wartet** (behoben): Nach einem Neustart fand AU-1 die EG-Adresse per mDNS nach ≈ 12 s, die
  Sitzung stand aber erst nach ≈ 20 s, weil der Such-Ping im 5-s-Takt lief. Jetzt wird gepingt, sobald eine Adresse
  neu bekannt ist (`Link::ping_soon`) → Sitzung 2,4 s nach WLAN-Aufbau.
- Der erste WLAN-Versuch nach dem Start scheitert gelegentlich mit Grund 2 (AUTH_EXPIRE); der zweite nach 1 s klappt.
  Kein Handlungsbedarf, beobachten.
- **Offen bis Schritt 11:** Dauertest 7 h / 72 h.

## Schritt 2 – Grundstellung, Betriebsmodi, Provisionierung (06.11.2026)

| # | Prüfung | Erwartung | AU-1 | IN-1 |
|---|---|---|---|---|
| 2.1 | `cb.py test` (Taster-Gesten, Modus-Automat) | kurz/lang/entprellt/mittel ignoriert, Überlauf nach 49 Tagen, Wartung endet nach 10 min | ✓ | ✓ |
| 2.2 | ohne Provisionierung starten | `mode: setup (not provisioned)`, kein WLAN | ✓ | ✓ |
| 2.3 | Innendaten (IN-1) auf die Außeneinheit schreiben | abgelehnt: „provisioned as indoor unit, but this is the outdoor image“ | ✓ | – |
| 2.4 | richtig provisionieren (`cb.py provisionieren AU-1 COM4` / `IN-1 COM3`) | Name, Rolle, WLAN im Log, **keine Geheimnisse im Log**, `mode: normal` | ✓ | ✓ |
| 2.5 | `factory-reset JA` im Normalbetrieb | abgelehnt | ✓ | |
| 2.6 | `factory-reset` im Einrichtungsmodus | erst Rückfrage, mit `JA` gelöscht, Neustart als „not provisioned“ | ✓ | |
| 2.7 | **Handprüfung:** Boot-Taster 5 s halten | Wartung, LED grün blinkend; nochmals 5 s → Einrichtung, rot/grün im Wechsel; kurzer Druck → Normalbetrieb | ✓ 08.11.2026: erstes Halten vor Mitschnitt-Start (Meister), Log setup (AP `ClearBell-f41138` an) → normal (AP aus), WLAN ohne Abbruch | ✓ 08.11.2026: Log maintenance → setup (AP `ClearBell-f3ac94` an) → normal (AP aus), WLAN ohne Abbruch; LED wie erwartet (Meister) |
| 2.8 | **Handprüfung:** kurzer Druck im Normalbetrieb | Log „volume step“ | ⏳ | ⏳ |
| 2.9 | **Handprüfung:** LED im Normalbetrieb | IN-1 grün (Außeneinheit erreichbar); AU-1 **rot**, weil die OG-Einheit fehlt; bernstein 2 s nach `ring eg` | ✓ 08.11.2026 (Meister): rot, 2 × bernstein nach `ring eg` (Quittung 66/117 ms) | ✓ grün, bernstein |

## Schritt 1 – Gerüst (06.11.2026)

| # | Prüfung | Erwartung | AU-1 | IN-1 |
|---|---|---|---|---|
| 1.1 | `cb.py test` | alle PC-Tests bestanden, Anzahl Prüfungen > 0 | ✓ | ✓ |
| 1.2 | `cb.py bauen alle` | beide Images; keine Warnung aus eigenem Code (Warnungen aus IDF/Arduino-Fremdcode bleiben) | ✓ | ✓ |
| 1.3 | flashen, `info` | richtiger Image-Typ, Partition `app0`, Version `0.2.0-dev`, IDF v5.5.5 | ✓ | ✓ |
| 1.4 | `selftest` auf dem Board | RFC 4231 OK (Hardware-SHA über mbedtls) | ✓ | ✓ |
| 1.5 | `wdt-test` | Neustart nach 5 s; überwacht waren `loopTask` **und** `IDLE1`; danach `last reset: TASK WATCHDOG` | ✓ | ✓ |
| 1.6 | `crash-test` | Neustart, `last reset: PANIC`, `core dump stored: YES`; `coredump-clear` löscht | ✓ | ✓ |
| 1.7 | `reboot` | Startzähler steigt je um 1, `last reset: software` | ✓ | ✓ |
| 1.8 | **Handprüfung:** Türöffner-Pin beim Start | `TP_DOOR_DRV1` bleibt bei Einschalten (12 V aus/an) und Reset-Taster LOW, LED-Ersatzlast bleibt dunkel | ⏳ | – |

Befund: `info` meldete beim Start „selftest FAILED“, bevor der Test lief → jetzt „not run yet“ (behoben).
Hinweis: Der Watchdog-Panic legt ebenfalls einen Coredump ab – gewollt, so ist auch ein Hänger nachträglich
analysierbar.
