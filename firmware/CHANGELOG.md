# Changelog – ClearBell-Firmware V0.2

Versionen nach SemVer; `version.txt` ist die Quelle. Solange die Portierung läuft, heißt die Version
`0.2.0-dev`, und jeder abgeschlossene Portierungsschritt bekommt hier einen Eintrag.

## 0.2.0-dev (in Arbeit)

### Statusseite neu, kleine Befunde (12.11.2026)

- **Status- und Einrichtungsseite neu gestaltet:** Kopfzeile mit Modus, Übersichtskacheln, Karten, aufklappbare
  Diagnose, Einblendungen statt Textzeilen; weiter ohne fremde Dateien (CSP), hell/dunkel. Seite 35,6 KB.
- FW-B-09 (leere Kartenkästen), FW-B-10 (Dateiname vorne kürzen), FW-B-12 (Meldung verschwindet), FW-B-13
  („Gerätecode“ statt „Etikett-Passwort“) mit der neuen Seite behoben.
- **FW-B-11_Build-Datum_veraltet:** eigener Bau-Zeitstempel bei jedem Bau (`main/build_stamp.cmake`) für `info` und
  Statusseite.
- **FW-B-14_Geraetecode_mit_Strichen_abgelehnt:** Anmeldung nimmt den Gerätecode auch mit „-“/Leerzeichen und klein
  geschrieben, solange kein eigenes Passwort gesetzt ist.

### Werkzeuge: Werksschritt für weitere Boards (12.11.2026)

- **FW-B-18_Neues_Board_bleibt_im_Download-Modus:** `cb.py werk` prüft am Ende, ob die Firmware auf der Konsole
  antwortet, versucht sonst einen Watchdog-Reset und meldet andernfalls „USB ab- und wieder anstecken“ (Exit-Code 2).
- **FW-B-19_Anlagendatei_ohne_Geraetecode:** `werk --name X` trägt Gerätenummer und Gerätecode in die Anlagendatei ein,
  wenn X dort steht; `anlage-einheit X eg|og` übernimmt beides aus dem Werksregister, wenn `werk` schon lief.
- **FW-B-20_Werkzeug_meldet_sich_mehrfach_an:** `status`, `update`, `ton-hochladen` und `fern-*` melden sich bei
  falschem Passwort nur noch einmal an (vorher 5 Versuche → 30-s-Sperre) und nennen `anlage-webpasswort` als Abhilfe.
- **Etikett (10a.13):** Format Avery Zweckform 3657 (48,5 × 25,4 mm), Code ohne Trennzeichen; `werk` erzeugt das
  Etikettbild, neuer Befehl `cb.py etiketten [<Nummer> …] [--ab N] [--rahmen]` erzeugt den A4-Druckbogen (PDF).
- `anlage-webpasswort <Einheit>` fragt das Passwort verdeckt ab und prüft danach die Anmeldung.
  Firmware unverändert.

### Schritt 10d – Signierte Updates (10.11.2026, R26)

- **Nur signierte Updates:** Das Upload muss einen Signatursektor im Format Secure Boot v2 (RSA-3072, RSA-PSS) tragen;
  angenommen wird, wenn mindestens ein Block zu dieser Datei gehört, von einem vertrauten Schlüssel stammt und gültig
  ist (`cb_update_sig.h`). Vertraut werden zwei **Herstellerschlüssel** (`cb_update_keys.h`: Haupt, Ersatz offline)
  und auf Wunsch ein **eigener Schlüssel** des Besitzers (Wartung, Karte „Einstellungen“ → „Update-Schlüssel“,
  Konsole `update-owner`), optional „nur eigener Schlüssel“. Werksreset entfernt den eigenen Schlüssel.
- **Versionsschutz:** ältere Version nur mit „ältere Version zulassen“ (`PUT /update?older=1`, `cb.py update --aelter`).
- Gerät: SHA-256 läuft beim Empfang mit, RSA über mbedtls; das Image muss vor dem Signatursektor enden. Selbsttest
  `sig_selftest` (Vektor aus `espsecure.py`) bei jedem Start, ≈ 0,25 s. PC-Tests mit eigener RSA-Referenz.
- Werkzeuge: `cb.py schluessel-erzeugen`, `signieren` (macht `bauen` selbst), `besitzer-schluessel`,
  `signatur-testvektor` (`tools/signatur.py`). Release-Datei `clearbell-<Version>-<Einheit>.bin`.
- **FW-B-08_Version_nicht_neu_gelesen:** CMake liest `version.txt` jetzt bei jeder Änderung neu
  (`CMAKE_CONFIGURE_DEPENDS`); vorher behielt das Image nach einer Änderung die alte Version.
- 127 Tests / 2865 Prüfungen. USB-Flashen bleibt ungeprüft (physischer Zugang = Stufe C).

### Touch-Befunde aus den Handprüfungen, Dauertest (08.11.2026)

- **FW-B-06_Baseline_eingefroren:** Ein ruhiger Versatz zwischen +5 und +50 % (Schwankung ≤ 2 %) wird nach 30 s als
  neue Baseline übernommen (`kOffsetAdoptMs`); vorher blieb AU-1 stundenlang bei +11 … +14 % stehen. Log „steady
  offset taken over“, Zähler in `touch`.
- **FW-B-07_Beide_Tasten_klingelt:** Das Klingeln wartet nach der Mindest-Haltedauer noch 120 ms
  (`kBellBothGuardMs`); steigt die andere Taste in der Zeit über +50 %, gilt es als „beide“ → kein Klingeln. Eine
  flache Hand hatte EG 21 ms vor OG erkannt und geklingelt. Langsames Drüberschieben (≈ 0,4 s) klingelt weiter
  (Meister: so lassen).
- Neue PC-Tests (5), 118 Tests / 2800 Prüfungen. `tools/dauertest.py` (lautloser Dauertest), 7 h bestanden.
- **Statusseite:** Karte „Einstellungen“ (`POST /settings`): Lautstärke in jedem Modus, Probeton, LED-Abgleich und
  Pulslänge nur in der Wartung; `settings` im JSON. Akzent Dunkelgrün, Handy-Layout (einspaltig unter 640 px,
  große Bedienflächen), Spaltenabstand in Tabellen. Update per Browser auf beiden Einheiten geprüft (10.9).
- **Eigenes Passwort der Statusseite** (Produktreife 8): Wartung → Karte „Einstellungen“, verschlüsselt übertragen
  (`password_change_open/seal`, `/settings/pw-nonce`, `/settings/password`), Konsole `web-pass`, leer = Etikett,
  Werksreset = Etikett. Seite hört bei 401/429 auf abzufragen (sonst hielte ein alter Tab die Sperre aufrecht).
  PC-Werkzeuge: `cb.py anlage-webpasswort <Einheit> <pw>|label`. `max_uri_handlers` 16 → 24. 120 Tests.

### Türöffner am echten Gerät (07.11.2026)

- **Pulslänge 2000 ms festgelegt** (Entscheidung F-3) nach 10 Pulsen am effeff 118W (12 V, 270 mA, Testprotokoll 7.10). Wert
  unverändert, nur Kommentar in `cb_door_logic.h` und `door-status`-Text („pulse fixed F-3, lock-out draft“).

### Schritte 10, 10a–10c – Updates und Inbetriebnahme ohne PC (06.11.2026, Verkaufsabsicht)

- **10 Updates:** `PUT /update` in der Wartung (Statusseite, `cb.py update`); Image-Kopf geprüft (ESP32-S3, Projekt,
  Einheitentyp aus `Version+aussen|innen`), SHA-256 durch `esp_ota_end`; Probezeit: bestätigt nach ≥ 60 s mit
  Selbsttest, WLAN, Webserver und den vorher erreichbaren Gegenstellen, sonst Rückfall nach 5 min; Absturz →
  Bootloader-Rückfall. Testfehler `update-fault`. Kein Signaturschlüssel (R25). Timer-Interrupt des Türpulses IRAM-sicher.
- **10a Werk ↔ Käufer:** `fctry` nur noch Gerätecode (Werksschritt `cb.py werk`, Etikett mit QR); Käuferdaten in
  `cb_user`; alte Daten (Schema 1) einmalig übernommen. Einrichtungs-WLAN `ClearBell-<Nr>` mit Captive Portal und
  Einrichtungsseite (WLAN-Liste, Wohnung); automatisch nach 5 min ohne WLAN. Werksreset: Taster 15 s in der
  Einrichtung oder Knopf; Werkstöne werden wiederhergestellt, der Gerätecode bleibt.
- **10b Kopplung:** auf der Seite der Außeneinheit per Gerätecode der Inneneinheit (verschlüsselt, bestätigt,
  Sperre nach 5 Fehlversuchen); Ersetzen nur in der Wartung. Eindeutige Namen `clearbell-<Nr>.local`, Dienst
  `_clearbell._udp`. Neuer Protokolltyp *Event*; damit Wartung der Außeneinheit von innen. Pulslänge einstellbar.
- **10c Handys:** auf der Seite der EG-Inneneinheit hinzufügen/entfernen; Schlüssel verschlüsselt im Event zur
  Außeneinheit; ntfy-Link und HTTP-Shortcuts-Import (JSON v92, Einmal-Link); Topics und Kurzbefehl-ID aus dem
  EG-Schlüssel abgeleitet.
- Befunde FW-B-04 (POST-Anmeldung) und FW-B-05 (Webserver ohne Netzwerk-Stack) behoben.

### Schritt 9 – Fern-Öffnen (06.11.2026)

- `cb_remote_logic` (PC-getestet): Einmal-Token 128 bit (5 min, neueste 3), Handy-Signatur
  `HMAC-SHA256(Schlüssel als Text, "CBR1|token|handy")`, Prüfreihenfolge Format → Verfahren → Handy → Token → Alter →
  Signatur, Token erst nach echter Öffnung verbraucht; kleiner JSON-Leser für den ntfy-Stream.
- `cb_remote` (nur außen): Push bei Klingeln an EG (höchstens eine je 30 s, nie bei Messreihen), Befehlskanal als
  JSON-Stream, beides HTTPS mit Zertifikatsbündel; Öffnen über den Tür-Task mit derselben Sperrzeit; Bestätigungs- und
  Neustart-Push; Fern-Öffnungen einzeln im Öffnungsprotokoll; Statusseite und Konsole `remote`, `remote-push`.
- TLS-Puffer im PSRAM (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`).
- Provisionierung: Abschnitt `fern` (Topics, Link-Vorlage, bis 4 Handy-Schlüssel); `cb.py anlage-fern`,
  `anlage-handy`, `anlage-handy-sperren`, `fern-handy-anleitung`, `fern-test`.

### Schritt 8 – Diagnose (06.11.2026)

- Statusseite `cb_status` im eigenen Task des IDF-HTTP-Servers: Funk, Protokoll-Zähler, Touch-Rohwert und Baseline,
  Türöffner, Protokoll der Türöffnungen, Audio, System; deutsch, ohne externe Dateien, auch per `/status.json`.
  Werte liest die Hauptschleife (Momentaufnahme auf Anfrage, 3–5 ms). Keine Tür über das Web (R17).
- Anmeldung mit dem Gerätepasswort per HTTP Digest (`cb_http_auth`, PC-getestet gegen RFC 1321/7616): Nonces mit
  Ablauf, Zähler gegen Replay, URI-Bindung, 30 s Sperre nach 5 Fehlversuchen.
- Protokoll der Türöffnungen `cb_door_log` im NVS (Ringpuffer 32): innen jeder eigene Türbefehl mit Zeit und
  Ergebnis; außen Innentasten-Öffnungen nur als Summe, Service-Pulse einzeln (SA 6.4). Uhrzeit per NTP.
- Ton-Upload `PUT /ton/<name>` nur in der Wartung: Zwischendatei → WAV-Prüfung → Umbenennen, während kein Ton
  spielt (SA 6.3).
- Status-LED per PWM, grün gedimmt (Entwurf 40 %), Konsole `led-balance`.
- Konsole `web`, `doorlog`, `button-sim long|short`; `cb.py status`, `cb.py ton-hochladen`,
  `cb.py anlage-zeigen --passwort`.

### Schritt 7 – Tür-Task (06.11.2026)

- `cb_door` ist der einzige Eigentümer von GPIO47: Puls starten im Task, beenden im gptimer-Interrupt (auch bei
  hängender Schleife), Obergrenze 5 s, Pin als Ein-/Ausgang mit Rücklesen, Fehler → Zwangs-LOW und Log.
- Sperrzeit 5 s ab Beginn für alle Pfade (`cb_door_logic`, PC-getestet), Türbefehl innen löst jetzt echten Puls aus.
- Konsole `door-test [ms]` (Trockentest über USB), `door-status`.

### Schritt 6 – Touch (06.11.2026)

- `cb_touch_logic`: Baseline in der Firmware (Lernen nur bei Ruhe, langsames Nachführen, eingefroren bei Druck,
  Neulernen nach 10 s Auflage), Hysterese +100 / +50 %, Mindest-Haltedauer außen 60 ms / innen 150 ms.
- Außen: ein Klingeln je Druck, beide Tasten zugleich = Nässe → kein Klingeln.
- Innen: Auslösen beim Loslassen; Scharfschaltfenster 2 min nach Klingeln an dieser Wohnung (endet mit der ersten
  Öffnung), sonst 3 s halten mit Tipp-Ton; zu lange / Auflage → nichts (R18, M-34).
- Konsole `touch`, `touch-log`, `touch-relearn`, `touch-sim` (Test der ganzen Kette ohne Finger);
  `cb.py konsole` kann Befehlsfolgen (`a | @2 | b`).

### Schritt 5 – Audio (06.11.2026)

- Audio-Task als einziger Eigentümer von I2S1 und `SD_MODE`; WAV aus LittleFS, Abtastrate aus der Datei,
  20 ms Vorlauf / 30 ms Nachlauf, Verstärker zwischen den Tönen aus.
- WAV-Parser `cb_wav` im Kern, mit Fuzzer getestet; defekte oder fehlende Dateien → stumm, nie Absturz.
- Tonrangfolge Klingeln > Fehler = kein Netz > Erfolg > Tipp; Lautstärkestufen 15–100 %, gekappt auf 40 % digital
  (AU-B-02), im NVS gespeichert, kurzer Druck auf den Boot-Taster schaltet weiter.
- Neue, eigene Töne ohne Grundton unter 880 Hz (`tools/toene/erzeuge_toene.py`), `cb.py toene` flasht sie.
- Reaktionen: innen Klingelton; außen Erfolgs-, Fehler- oder Kein-Netz-Ton (F-4); Türbefehl innen Erfolg/Fehler (F-2).
- Mikrofon-Diagnose `audio-check` und `listen` (Mikrofon nur auf Befehl über USB, sonst aus).

### Schritt 4 – Türbefehl (06.11.2026)

- Challenge–Response in `cb_link`: REQUEST → CHALLENGE (Nonce aus dem Hardware-Zufall, 2 s, einmalig, nur die
  neueste je Wohnung gültig) → CONFIRM → ACK; Antwort vor Aktion; wiederholte Bestätigung bekommt die gespeicherte
  Antwort, öffnet nie zweimal. Kein Zähler im NVS.
- Ratenbegrenzung je Quell-IP vor Warteschlange und HMAC (`cb_ratelimit`, 20 Rahmen Burst, 10/s).
- Außen: Entscheidung mit vorläufiger Sperrzeit 5 s, Öffnen vorerst nur im Log (Puls mit Schritt 7).
- Innen: Konsolenbefehl `door`; Modem-Sleep auf der jeweils wartenden Seite während der Transaktion aus.

### Schritt 3 – Netz (06.11.2026)

- WLAN ereignisgesteuert mit wachsendem Abstand (1 s … 60 s), Ländercode DE, Suche über alle Kanäle,
  Modem-Sleep im Ruhezustand an, beim Klingeln auf der Außeneinheit aus (M-23).
- UDP 4210; Datagramme gehen per Queue an die Hauptschleife, die die Protokoll-Engine allein besitzt.
- Protokoll-Engine `cb_link`: Sitzungen mit Challenge (PING/PONG mit Nonce), 64er-Duplikatfenster, Quittung
  vor Aktion, Quittungen an Sitzung **und** Nummer gebunden, 3 Wiederholungen à 300 ms, 3 Sendeslots,
  Herzschlag 30 s (5 s bis zum ersten Kontakt), drei Ausfälle = nicht erreichbar.
- Adressen: feste IP aus der Provisionierung, sonst gelernt aus authentischen Rahmen, sonst mDNS
  (`clearbell-aussen/-eg/-og.local`); neu bekannte Adresse → sofort pingen.
- Konsole `net`, `ring eg|og [Anzahl]` (mit Messreihe), `sleep`, `wifi-drop`; Status-LED grün/rot/bernstein.

### Schritt 2 – Grundstellung, Modi, Provisionierung (06.11.2026)

- Werkszustand-Partition `fctry` (Rolle, WLAN, Gerätepasswort, Schlüssel, optionale Gegenstellen-IPs), gelesen
  von `cb_provisioning`, geschrieben vom PC (`cb.py provisionieren`, Anlagendatei außerhalb des Repos).
- Rolle passt nicht zum Image oder Daten fehlen → Einrichtungsmodus, sonst nichts.
- Betriebsmodi Normal / Wartung / Einrichtung über den Boot-Taster, Logik auf dem PC getestet.
- Werksreset `factory-reset JA` nur im Einrichtungsmodus.

### Schritt 1 – Gerüst (06.11.2026)

- ESP-IDF-Projekt mit Arduino-ESP32 als Komponente (R21), Versionen fest als Paar: ESP-IDF v5.5.5 ↔
  Arduino-ESP32 3.3.12.
- Ein Image je Einheitentyp (`aussen`, `innen`), Auswahl über Kconfig, Prüfung zur Übersetzungszeit.
- Partitionstabelle mit `fctry` (Werkszustand) und `nvs_keys` (Reserve für Härtungsstufe B).
- Watchdog für Hauptschleife und beide Leerlauf-Tasks, Coredump in den Flash, Rollback-fähiger Bootloader.
- Startsequenz Schritte 1–5 (SA 5.1): Türöffner-Pin LOW schon im statischen Konstruktor, Verstärker stumm, LED
  aus, Log über USB ohne Blockieren, Neustartgrund und Startzähler.
- Konsole mit `info`, `selftest`, `reboot`, `wdt-test`, `crash-test`, `coredump-clear`.
- Kern `cb_core`: Protokoll-v2-Rahmen (Kodieren, Prüfen in der Reihenfolge aus SA 4.2), HMAC-Selbsttest
  RFC 4231; PC-Tests mit eigenem Mini-Testrahmen.
