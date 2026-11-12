# ClearBell V0.2 – Firmware

> **Einstieg für alle, die neu dazukommen.** Diese Seite reicht, um die Firmware zu bauen, auf die Boards zu
> bringen, zu testen und zu wissen, was fertig ist. Das *Warum* steht in der Softwarearchitektur
> ([`../docs/Softwarearchitektur_V0.2.md`](../docs/Softwarearchitektur_V0.2.md)); auf ihre Kapitel wird hier
> mit „SA 5.1“ usw. verwiesen. Lesen musst du sie nur, wenn du eine Entscheidung verstehen oder ändern willst.

## 1 Was die Firmware tut – in fünf Sätzen

Es gibt **eine Außeneinheit** (Haustür, zwei Klingeltaster, Türöffner) und **zwei baugleiche Inneneinheiten**
(Wohnung EG und OG, je ein Türöffner-Taster und ein Lautsprecher). Drückt ein Besucher außen, schickt die
Außeneinheit per WLAN/UDP ein `RING` an genau eine Wohnung; die Wohnung quittiert und klingelt. Drückt man innen,
läuft ein Türbefehl per Challenge–Response zur Außeneinheit, die dann den Türöffner pulst. Jede Nachricht ist mit
einem Schlüssel je Wohnung signiert (HMAC); ohne Schlüssel kann niemand klingeln oder die Tür öffnen. Es gibt
**ein Image für außen** und **ein Image für innen**; ob ein Innengerät EG oder OG ist, steht nicht im Image, sondern
wird bei der Einrichtung (Provisionierung) auf das Gerät geschrieben. Seit R28 heißt das für den Käufer
„Klingeltaste unten“ (EG) bzw. „oben“ (OG); angezeigt wird ein frei wählbarer Name der Wohnung.

## 2 Stand der Portierung

Reihenfolge nach SA 10.2. Ein Schritt gilt erst als fertig, wenn er **auf beiden Boards getestet** ist; die
Belege stehen im [Testprotokoll](docs/Testprotokoll.md).

| Schritt | Inhalt | Stand |
|---|---|---|
| 1 Gerüst | IDF-Projekt, Versionen, Partitionen, Watchdog, Coredump, PC-Tests | ✓ bis auf Handmessung Türöffner-Pin beim Einschalten; Signaturprüfung für Updates seit 10d im Image |
| 2 Grundstellung | Türöffner-Pin, Verstärker stumm, Betriebsmodi, Provisionierung | ✓ bis auf Handprüfung Boot-Taster/LED |
| 3 Netz | WLAN, Protokoll v2, Sitzungen, Herzschlag | ✓ (Dauertest erst in Schritt 11) |
| 4 Türbefehl | Challenge–Response, Ratenbegrenzung | ✓ (Auslöser vorerst Konsole `door`; GPIO47 schaltet erst mit Schritt 7) |
| 5 Audio | LittleFS, WAV, Audio-Task, Rangfolge, Lautstärkegrenze 40 % | ✓ (per eigenem Mikrofon geprüft; Ohrtest und Taster offen) |
| 6 Touch | Baseline, Schwellen, Plausibilität außen, Scharfschaltfenster innen | ✓ ohne Finger (`touch-sim`); echte Berührung in der Sammelliste; Schwellen Entwurf bis zur Montage |
| 7 Türöffner | Tür-Task, Puls, Sperrzeit | ✓ per Rücklesen des Pins; LED-Last und echter Öffner in der Sammelliste |
| 8 Diagnose | Statusseite mit Gerätepasswort, Protokoll der Türöffnungen, LED-PWM, Ton-Upload in der Wartung | ✓ per Skript und Konsole; Browser-Anmeldung und LED-Abgleich in der Sammelliste |
| 9 Fern-Öffnen | Push über ntfy.sh mit TLS-Prüfung und Knopf „Tür öffnen“, geheimes Topic-Paar je Handy, Einmal-Token; beide Wohnungen; Ausfallmeldung der Inneneinheiten (R27) | ✓ mit echtem Handy (EG) und PC als Handy (EG, OG); iOS offen |
| 10 Updates | Upload in der Wartung, Image-Prüfung, Rollback nach Probezeit | ✓ inkl. Rückfall nach Absturz |
| 10d Signierte Updates | nur signierte Dateien (2 Herstellerschlüssel, optional eigener Schlüssel des Besitzers), Versionsschutz (R26) | ✓ alle Fälle auf beiden Boards; Bedienung im Browser in der Sammelliste |
| 10a Inbetriebnahme | Werk ↔ Käufer, Einrichtungs-WLAN mit Captive Portal, Werksreset per Taster ([Produktreife](docs/Produktreife.md)) | ✓ übers Heimnetz und per Scan; Handy/QR in der Sammelliste |
| 10b Kopplung | per Gerätecode der Inneneinheit, eindeutige Namen, Wartung außen von innen, Pulslänge einstellbar | ✓ |
| 10c Handys | über die Webseite der eigenen Inneneinheit (EG und OG, je bis 4): ein ntfy-Link, Test, Entfernen (R27) | ✓ echtes Handy an IN-1, PC als Handy an IN-1 und IN-2 |
| 11 Abnahme | 7 h, dann 72 h Dauertest (`tools/dauertest.py`) | 7 h lautlos ✓ (08.11.2026), 72 h offen |

## 3 Schnellstart

Voraussetzungen (auf dem TOWER eingerichtet): ESP-IDF **v5.5.5** in `C:\Espressif\esp-idf-v5.5.5`, Python mit
`pyserial`, für die PC-Tests ein `g++`. Alle Befehle aus dem Ordner `firmware`:

```bash
python tools/cb.py test
```

PC-Tests ohne Board: Protokoll, HMAC, Zustandsautomaten. Muss mit `0 failed` enden.

```bash
python tools/cb.py bauen alle
```

Baut beide Images nach `%USERPROFILE%\clearbell-build\aussen` bzw. `…\innen` – bewusst **außerhalb von
OneDrive**, weil tausende Objektdateien sonst synchronisiert würden und OneDrive-Dateisperren den Build stören.
Der erste Lauf lädt den Arduino-Core als Komponente nach `managed_components/` und übersetzt ihn komplett
(≈ 20 min), danach geht es schnell. Liegt der Hauptschlüssel unter `ClearBell\Geheimnisse\signatur\`, wird jedes
Image gleich signiert: `clearbell-signiert.bin` (für `cb.py update`) und `clearbell-<Version>-<Einheit>.bin` (für eine
Release-Seite). Nur signierte Dateien kommen per Update auf eine Einheit; USB-Flashen braucht keine Signatur.

```bash
python tools/cb.py flashen aussen COM4
```

Flasht die Außeneinheit (AU-1 hängt am TOWER an **COM4**, IN-1 an **COM3**; ändert sich das, im Geräte-Manager
nachsehen). Für die Inneneinheit `flashen innen COM3`.

```bash
python tools/cb.py konsole COM4 "info" 5
```

Schickt einen Konsolenbefehl und liest 5 s mit. `help` zeigt alle Befehle.

```bash
python tools/cb.py toene COM4
```

Erzeugt die Tondateien neu (`tools/toene/erzeuge_toene.py`), packt sie in ein LittleFS-Abbild und schreibt es ins
Dateisystem des Boards. Firmware, Einstellungen und Schlüssel bleiben unberührt. Ohne Port nur erzeugen.

### So richtet ein Käufer ClearBell ein (ohne PC)

Alles über Webseiten, Grundlage: [docs/Produktreife.md](docs/Produktreife.md).

1. **Einschalten.** Eine neue Einheit öffnet das WLAN `ClearBell-<Gerätenummer>`. QR-Code vom Etikett mit dem Handy
   scannen (oder WLAN wählen und den **Gerätecode** vom Etikett eingeben) – die Einrichtungsseite öffnet sich.
2. **WLAN wählen**, Passwort, innen **Klingeltaste oben oder unten** und wahlweise einen **Namen der Wohnung**
   (z. B. „Familie Müller“, steht in den Klingel-Nachrichten; später in der Wartung änderbar) → „Speichern und verbinden“. Danach ist die Einheit unter
   `http://clearbell-<Gerätenummer>.local` erreichbar (Benutzer `clearbell`, Passwort = Gerätecode).
3. **Koppeln** auf der Seite der Außeneinheit: „Inneneinheiten suchen“, Gerätecode vom Etikett der Inneneinheit.
4. **Handys** auf der Seite der eigenen Inneneinheit in der Wartung: „Handy hinzufügen“ – ein Link, die App ntfy abonniert.
5. **Später:** Wartung = Boot-Taster 5 s (außen auch von innen über die gekoppelte Inneneinheit): Töne, Update,
   Pulslänge. Nach einem Routertausch geht das Einrichtungs-WLAN nach 5 min von selbst auf. Werksreset: Taster
   2 × 5 s, dann 15 s halten.

### Werksschritt (einmal je Gerät, PC + USB)

```bash
python tools/cb.py werk aussen COM4 --name AU-1
```

Flasht Firmware und Töne (mit Werkskopien), schreibt einen zufälligen Gerätecode und legt das Etikett unter
`ClearBell\Geheimnisse\etiketten\<Gerätenummer>.txt` und `.png` ab (Etikett für **Avery Zweckform 3657**,
48,5 × 25,4 mm, Tintenstrahl/Laser; braucht `pip install qrcode[pil]`). Der Code steht darauf genau so, wie man ihn
tippt – 16 Zeichen ohne Trennzeichen. Druckbogen (A4-PDF, in 100 % drucken):

```bash
python tools/cb.py etiketten a1b2c3 --ab 5
```

Ohne Nummer alle Einheiten des Werksregisters, `--ab N` = ab Feld N (angebrochener Bogen), `--rahmen` = Rahmen für
einen Probedruck auf Normalpapier (gegen den Etikettenbogen halten). Am Ende prüft er, ob die
Firmware startet; meldet er „UNIT DID NOT START“, USB ab- und wieder anstecken (kam bei einem fabrikneuen Board vor).
Steht die Einheit (`--name`) schon in der Anlagendatei, trägt er Gerätenummer und Gerätecode dort ein; sonst holt
`cb.py anlage-einheit <Name> eg|og` beides aus dem Werksregister – die PC-Werkzeuge melden sich damit an. Für Prüfplatz-Boards ohne
Handy ersetzt dieser Befehl die Einrichtungsseite (WLAN und Wohnung aus der Anlagendatei, über USB):

```bash
python tools/cb.py einrichten IN-1 COM3
```

### Alter Entwicklerweg (Schema 1) – nur noch zur Übersicht

> Vor Schritt 10a kam alles per `cb.py provisionieren` aus der Anlagendatei. Solche Boards werden beim ersten Start
> automatisch übernommen. Für neue Boards gilt der Werksschritt oben.

Ein frisch geflashtes Board kennt weder WLAN noch Schlüssel und startet im **Einrichtungsmodus** (LED rot/grün im
Wechsel). Rolle, WLAN-Zugang, Gerätepasswort und Schlüssel kommen aus der **Anlagendatei**
`ClearBell\Geheimnisse\clearbell_anlage.json` im OneDrive-Projektordner – bewusst außerhalb des Repos. Sie enthält
alle Geheimnisse der Installation: nie teilen, nie einchecken.

```bash
python tools/cb.py anlage-zeigen
```

Zeigt die eingetragenen Einheiten (ohne Geheimnisse). Heute: AU-1 = `aussen`, IN-1 = `eg`.

```bash
python tools/cb.py anlage-einheit IN-2 og
```

Trägt eine weitere Einheit ein (Rolle `aussen`, `eg` oder `og`); das Gerätepasswort wird zufällig erzeugt.

```bash
python tools/cb.py provisionieren IN-2 COM5
```

Schreibt die Daten in die Partition `fctry` des Boards. Danach `reboot` – das Log zeigt Name und Rolle, nie
Schlüssel. Passt die Rolle nicht zum Image (Innendaten auf dem Außen-Image), bleibt das Board im Einrichtungsmodus
und sagt warum.

### Fern-Öffnen per Handy (R27, seit 12.11.2026)

Klingelt jemand an einer der beiden Tasten, schickt die Außeneinheit über den Dienst **ntfy.sh** jedem Handy dieser
Wohnung eine Push „Es klingelt – ‹Name der Wohnung›“ mit dem Knopf „Tür öffnen“ (Anwendungsfall: Kind steht ausgesperrt vor der Tür). Auf dem Handy braucht es nur
die App **ntfy**: Der Knopf schickt das Einmal-Token selbst an das Befehls-Topic des Handys zurück. Nur mit einem Token,
das für genau dieses Handy und dieses Klingeln ausgegeben wurde, frisch (5 min) und außerhalb der Sperrzeit öffnet die
Tür; danach kommt an alle Handys der Wohnung die Push „Tür per App geöffnet“. Jedes Handy hat ein eigenes, geheimes
Topic-Paar aus seinem Schlüssel (Begründung und Restrisiko im Kopf von `cb_remote_logic.h`, SA 4.5). Ohne Internet gibt
es nur das Fern-Öffnen nicht; Klingeln und Türtaste innen laufen weiter.

Handys werden auf der Seite der **eigenen** Inneneinheit hinzugefügt (Schritt 4 oben, je Wohnung bis 4). Ist die
Außeneinheit ≈ 2 min nicht erreichbar, meldet die Inneneinheit das ihren Handys, und wieder, wenn sie zurück ist.
`anlage-fern` und `anlage-handy` sind Überbleibsel aus Schritt 9 und wirken auf die R27-Firmware nicht.

Ohne Handy testen – der PC trägt sich über die Webseite als Handy ein und spielt es dann:

```bash
python tools/cb.py fern-handy-web IN-1 PC2
```

```bash
python tools/cb.py fern-test PC2 60
```

Der PC drückt dabei den Knopf der Push wie das Handy. `--fremd` schickt ein erfundenes Token (darf nicht öffnen),
`--replay` drückt den Knopf nach der Öffnung nochmal (darf nicht nochmal öffnen), `--nochmal` wiederholt eine wegen
Sperrzeit abgelehnte Antwort.

### Statusseite (ab Schritt 8)

Jede Einheit hat eine kleine Webseite im WLAN: `http://clearbell-<Gerätenummer>.local/`, z. B.
`http://clearbell-f3ac94.local/` (geht der Name nicht, die IP aus dem Konsolenbefehl `net` nehmen). Anmeldung mit Benutzer
`clearbell` und dem **Gerätecode** vom Etikett (Prüfplatz):

```bash
python tools/cb.py anlage-zeigen --passwort IN-1
```

Die Seite zeigt Funk, Protokoll-Zähler, Touch-Rohwert und Baseline je Taste, das Protokoll der Türöffnungen, Audio und
System; sie lädt sich alle 5 s neu. Sie ist **nur lesend** – eine Tür öffnen kann sie nicht, auch nicht mit Passwort
(R17). Töne tauschen geht nur in der **Wartung** (Boot-Taster 5 s): dann erscheint unter „Audio“ ein Upload-Feld
(16-Bit-PCM-WAV, höchstens 1 MB). Ein abgebrochener oder ungültiger Upload lässt den alten Ton stehen.

Dasselbe vom PC aus (auch für Skripte):

```bash
python tools/cb.py status AU-1
```

```bash
python tools/cb.py ton-hochladen IN-1 ring meine_klingel.wav
```

**Protokoll der Türöffnungen** (SA 6.4): Die Inneneinheit führt jeden Türbefehl ihrer eigenen Wohnung mit Uhrzeit und
Ergebnis. Die Außeneinheit zählt Öffnungen über die Innentasten **nur als Summe** (ohne Zeit, ohne Wohnung) und führt
nur Service-Pulse (`door-test`) und später Fern-Öffnungen einzeln. Uhrzeit per NTP; ohne Internet steht „Start #n + s“
da. Die Anmeldung läuft per HTTP Digest – das Passwort geht nicht im Klartext über das WLAN; nach 5 falschen
Passwörtern ist die Seite 30 s gesperrt.

## 4 Konsolenbefehle

Die Konsole läuft über die USB-Buchse und braucht damit physischen Zugang. Ausgaben sind englisch (wie der Code,
R23), jede Zeile beginnt mit der Laufzeit in Sekunden.

| Befehl | Wirkung |
|---|---|
| `help` | alle Befehle |
| `info` | Version, Image-Typ, Partition, Neustartgrund, Startzähler, Speicher, Coredump vorhanden? |
| `selftest` | Krypto-Selbsttest auf der echten Hardware: HMAC gegen RFC 4231, Update-Signatur gegen einen `espsecure`-Vektor |
| `reboot` | Neustart |
| `wdt-test` | blockiert die Hauptschleife 10 s – der Watchdog **muss** nach 5 s neu starten |
| `crash-test` | absichtlicher Absturz – danach muss `info` einen Coredump zeigen |
| `coredump-clear` | gespeicherten Coredump löschen |
| `mode` | Betriebsmodus: normal / maintenance (Wartung) / setup (Einrichtung) |
| `factory-reset JA` | Werksreset: löscht die Käuferdaten (WLAN, Kopplung, Handys, eigene Töne, Einstellungen), Gerätecode bleibt – **nur im Einrichtungsmodus** |
| `net` | WLAN, Gegenstellen (Adresse, Sitzung, erreichbar?), alle Protokoll-Zähler |
| `ring eg` / `ring og` | nur außen: jetzt bei EG/OG klingeln, Quittungszeit im Log |
| `ring eg 50` | Messreihe: 50 Klingeln im 2-s-Takt, danach Median/95 %/Max der Quittungszeit |
| `sleep 0` / `sleep 1` / `sleep auto` | Modem-Sleep für Messungen erzwingen; `auto` = Normalbetrieb |
| `wifi-drop` | WLAN-Verbindung trennen – muss sich ohne Neustart wieder verbinden |
| `door` | nur innen: Türbefehl an die Außeneinheit ohne Touch (Diagnose), Antwort und Zeit im Log |
| `door-test` / `door-test 300` | nur außen: **Trockentest des Türöffners** über USB, Puls 100–5000 ms (Standard 2000), mit Sperrzeit |
| `door-status` | Anzahl Öffnungen, gemessene Länge des letzten Pulses, Sperrzeit, Pin-Pegel |
| `touch` | Rohwert, Baseline, Abweichung, Zustand je Elektrode; Fenster offen?; Dauer von `touchRead()` |
| `touch-log 30` | 30 s lang alle 100 ms eine Zeile je Elektrode (für die Handprüfung und die Schwellenwahl) |
| `touch-relearn` | Baseline neu lernen (Elektroden dabei nicht berühren) |
| `touch-sim eg 400 300` | **Test:** 300 ms lang „Baseline + 400 %“ einspeisen – prüft die ganze Kette ohne Finger. Außen `eg`/`og`/`both`, innen ohne Kanal: `touch-sim 400 300` |
| `play ring` / `ok` / `error` / `nonet` / `tick` / `2000` | Ton abspielen (Zahl = Sinus in Hz, 1 s) |
| `vol` / `vol next` / `vol 75` | Lautstärke zeigen / weiterschalten / setzen (15, 30, 50, 75, 100 %; wird gespeichert) |
| `files` | Tondateien im Dateisystem prüfen, Audio-Zähler |
| `audio-check ring` / `audio-check 2000` | **Selbstprüfung:** spielt den Ton und misst ihn mit dem eigenen Mikrofon → „HEARD“ / „NOT HEARD“ |
| `listen 5` / `listen 5 2000` | Mikrofonpegel alle 250 ms, z. B. um zu hören, ob das *andere* Board klingelt |
| `web` | Statusseite: läuft sie?, Anmeldungen ok/falsch, Sperren, Uploads, Dauer der Momentaufnahme |
| `web-pass <neu>` / `web-pass label` | eigenes Passwort der Statusseite setzen / zurück zum Etikett-Passwort (danach für die PC-Werkzeuge `cb.py anlage-webpasswort <Einheit> <neu>`) |
| `doorlog` / `doorlog clear JA` | Protokoll der Türöffnungen dieser Einheit zeigen / löschen |
| `led-balance` / `led-balance 30` | Helligkeit grün relativ zu rot (5–100 %, Standard 40, wird gespeichert) |
| `setup-wifi <ssid> <pw>` / `setup-role eg` | Entwicklung/Werk: WLAN bzw. Wohnung (eg = Taste unten, og = oben) ohne Einrichtungsseite (danach `reboot`) |
| `setup-name <Name>` / `setup-name` | Entwicklung: freier Name der Wohnung (innen) / zurück zum Standard; die Außeneinheit bekommt ihn nach dem nächsten Start |
| `wifi-scan` | WLAN-Netze in Reichweite (auch Einrichtungs-WLANs anderer Einheiten) |
| `setup-ap on` / `setup-ap auto` | **Test:** Einrichtungs-WLAN erzwingen |
| `update-status` | Firmware-Slot, Probezeit, letzter Upload |
| `update-keys` | Schlüssel, denen die Einheit bei Updates vertraut (Haupt, Ersatz, eigener) |
| `update-owner <64 Hex>` / `update-owner clear` | eigenen Update-Schlüssel des Besitzers eintragen / entfernen (wie auf der Seite in der Wartung) |
| `update-owner-only on` / `off` | nur noch den eigenen Schlüssel annehmen / Herstellerschlüssel wieder annehmen |
| `update-fault crash` / `nowifi` / `off` | **Test:** das nächste Update scheitert absichtlich in der Probezeit – muss zurückfallen |
| `remote` | außen: Fern-Öffnen – Handys je Wohnung, Befehlskanal verbunden?, Push gesendet/fehlgeschlagen, Befehle je Ergebnis; innen: Ausfallmeldung |
| `remote-push` | Test-Push an die Handys dieser Einheit |
| `button-sim long` / `short` / `reset` | **Test:** Boot-Taster-Geste über USB (long = 5 s, reset = 15 s) – z. B. Wartung ohne Finger |

**So arbeitet der Touch** (Details und Entwurfswerte im Kopf von `cb_touch_logic.h`): Ausgewertet wird relativ zur
Ruhe-Baseline, die nach dem Einschalten in der ersten ruhigen Sekunde gelernt und danach langsam nachgeführt wird
(nicht, solange etwas aufliegt). Gedrückt ab +100 %, losgelassen unter +50 %. **Außen:** ein Klingeln je Druck; beide
Tasten zugleich = Nässe, kein Klingeln. **Innen:** ausgelöst wird erst beim Loslassen; bis 2 min nach einem Klingeln
an dieser Wohnung öffnet ein kurzer Tipp, sonst 3 s halten bis zum Tipp-Ton und dann loslassen; länger als 7 s oder
etwas liegt auf → nichts.

**Lautstärke:** 100 % in der Bedienung entspricht 40 % digital – darüber übersteuert der Verstärker an 3,3 V
(Bring-up AU-B-02). Die Skala ist linear: 15 % → 100 % sind 16,5 dB. Ändern nur an einer Stelle:
`kMaxDigitalPercent` in `cb_audio_logic.h`.

**Mikrofon:** läuft im Normalbetrieb **nie** (SA 5.7). Nur `audio-check` und `listen` schalten es für wenige
Sekunden ein; gespeichert oder gesendet wird nichts, nur Pegelzahlen stehen im Log.

**Betriebsmodi per Boot-Taster** (im Gehäuse, SA 5.9): 5 s halten → Wartung (LED grün blinkend, endet nach 10 min
oder kurzem Druck); in der Wartung nochmals 5 s → Einrichtung (rot/grün im Wechsel). Kurzer Druck im Normalbetrieb =
Lautstärke weiter (ab Schritt 5). **Beim Einschalten gedrückt** = Download-Modus des Chips, nicht Einrichtung.

**Status-LED im Normalbetrieb:** grün = WLAN da und alle Gegenstellen antworten; rot = kein WLAN oder eine
Gegenstelle fehlt (am Prüfplatz ist AU-1 deshalb rot: es gibt keine OG-Einheit); bernstein 2 s = Klingeln
quittiert bzw. empfangen. Beide Farben laufen per PWM; grün ist auf 40 % gedimmt, weil es bei gleichem Strom heller
wirkt (SA 8.2) – Feinabgleich per `led-balance` mit dem Diffusor.

## 5 Wo was liegt

```
firmware/
├── README.md               diese Seite
├── CHANGELOG.md            was sich je Version geändert hat
├── docs/Testprotokoll.md   Belege: was wurde wann auf welchem Board geprüft
├── CMakeLists.txt          ESP-IDF-Projekt (Arduino als Komponente, R21)
├── sdkconfig.defaults      Build-Konfiguration, jede Abweichung begründet
├── sdkconfig.defaults.aussen / .innen   Einheitentyp je Image
├── partitions.csv          Flash-Aufteilung – vor dem Einbau endgültig
├── version.txt             Firmware-Version (SemVer)
├── main/                   Arduino-Schicht: Startsequenz, Konsole, Module mit Hardwarebezug
│   ├── main.cpp            ZUERST LESEN: Startsequenz (SA 5.1) und Hauptschleife
│   ├── cb_config.h         Pins (= Netznamen des Schaltplans), Zeitkonstanten
│   ├── cb_log.*            Log über USB, blockiert nie
│   ├── cb_console.*        Befehlskonsole
│   ├── cb_system.*         Version, Neustartgrund, Startzähler, Selbsttest, Update-Bestätigung
│   ├── cb_led.*            Status-LED (PWM, Helligkeitsabgleich grün/rot) und Blinkmuster
│   ├── cb_provisioning.*   liest Rolle, Schlüssel, WLAN aus der Partition fctry
│   ├── cb_modes.*          Boot-Taster und Betriebsmodi auf dem Gerät
│   ├── cb_net.*            WLAN, UDP 4210, mDNS, Adressen; bindet die Protokoll-Engine an
│   ├── cb_audio.*          Audio-Task: WAV aus LittleFS, Verstärker (I2S1, SD_MODE), Lautstärke
│   ├── cb_mic.*            Mikrofon nur für Diagnose: audio-check, listen
│   ├── cb_touch.*          Touch-Elektroden abtasten, Klingeln/Türbefehl auslösen
│   ├── cb_door.*           Tür-Task: einziger Eigentümer von GPIO47, Puls per Timer-Interrupt
│   ├── cb_remote.*         Fern-Öffnen über ntfy.sh (HTTPS): außen Push und Befehlskanal, innen Ausfallmeldung
│   ├── cb_setup.*          Einrichtungs-WLAN, Captive Portal, Einrichtungsseite, Werksreset (10a)
│   ├── cb_setup_page.h     das HTML der Einrichtungsseite
│   ├── cb_update.*         Firmware-Update per Upload, Signatur, Versionsschutz, Probezeit, Rollback (10, 10d)
│   ├── cb_pairing.*        Kopplung per Gerätecode, Wartung von innen, Pulslänge (10b)
│   ├── cb_phones.*         Handys über die Webseite der eigenen Inneneinheit (10c, R27)
│   ├── cb_status.*         Statusseite (HTTP, Digest-Anmeldung, Upload), Protokoll der Türöffnungen
│   └── cb_status_page.h    das HTML der Statusseite (deutsch, ohne externe Dateien)
├── components/cb_core/     hardwareunabhängiger Kern, auf dem PC getestet
│   ├── include/cb_protocol.h   Protokoll v2: Rahmenaufbau (Tabelle im Kopf der Datei)
│   ├── include/cb_link.h       Protokoll-Engine: Sitzungen, Duplikate, Quittung, Wiederholung, Herzschlag
│   ├── include/cb_modes_logic.h  Taster-Gesten und Modus-Automat
│   ├── include/cb_touch_logic.h  Baseline, Schwellen, Klingeltasten, Türtaste mit Fenster
│   ├── include/cb_door_logic.h   Sperrzeit und Pulsgrenzen des Türöffners
│   ├── include/cb_door_log.h     Protokoll der Türöffnungen (Ringpuffer, wer was sehen darf)
│   ├── include/cb_http_auth.h    Gerätepasswort: HTTP Digest, Nonces, Replay-Schutz, Sperre
│   ├── include/cb_remote_logic.h Fern-Öffnen: Topics je Handy, Einmal-Token, Leser für ntfy-Zeilen
│   ├── include/cb_update_logic.h Image-Kopf prüfen, Versionsvergleich, Probezeit-Entscheidung
│   ├── include/cb_update_sig.h   Signatur prüfen: Format, vertraute Schlüssel (R26) – ZUERST LESEN für Updates
│   ├── include/cb_update_keys.h  Kennungen der beiden Herstellerschlüssel (erzeugt, öffentlich, nie ersetzen)
│   ├── include/cb_setup_logic.h  Gerätecode, Formulardaten, WLAN-QR-Text
│   ├── include/cb_pairing_logic.h Kopplung per Code, Schlüssel verpacken, abgeleitete Namen
│   ├── include/cb_json.h         kleiner JSON-Schreiber für die Statusseite
│   ├── include/cb_wav.h        WAV-Parser (gefuzzt)
│   ├── include/cb_audio_logic.h  Tonrangfolge, Lautstärkestufen und -kappung
│   ├── include/cb_dsp.h        Pegel und Goertzel-Filter für die Mikrofon-Prüfung
│   ├── include/cb_ratelimit.h  Ratenbegrenzung je Absender
│   ├── include/cb_crypto.h     HMAC und Zufall, je Plattform implementiert
│   └── src/esp/                die ESP32-Bindung (mbedtls, Hardware-Zufall)
├── test/host/              PC-Unit-Tests (eigener Mini-Testrahmen, keine Downloads)
└── tools/
    ├── cb.py               bauen, flashen, testen, Konsole, Provisionierung, Töne
    ├── toene/erzeuge_toene.py  erzeugt die eigenen Töne (Regeln gegen das Klirren im Kopf der Datei)
    ├── provisionieren.py   Anlagendatei und Werkszustand-Partition (Gegenstück zu cb_provisioning.cpp)
    ├── statusseite.py      Statusseite vom PC: status, ton-hochladen, Gerätepasswort anzeigen
    ├── fern.py             Fern-Öffnen vom PC: PC als Test-Handy (über die Webseite eingetragen)
    ├── signatur.py         Herstellerschlüssel erzeugen, Images signieren, Besitzer-Kennung, Testvektor (10d)
    └── werk.py             Werksschritt: Firmware, Töne, Gerätecode, Etikett
```

**Faustregel:** Alles, was ohne Hardware entschieden werden kann (Protokoll, Zustandsautomaten, Parser), gehört
nach `components/cb_core` und bekommt einen PC-Test. `main/` enthält nur, was Pins, Funk oder Flash anfasst.

## 6 Regeln, die man kennen muss

- **Der Türöffner-Pin GPIO47** wird beim Start als Allererstes auf LOW gezogen (`main.cpp`), danach schaltet ihn
  **nur** das Modul `cb_door`. Prüfung „wer kann die Tür öffnen?“ = Textsuche nach `CB_PIN_DOOR_DRV`. Der Puls
  endet per Hardware-Timer-Interrupt, auch wenn die Hauptschleife hängt; Obergrenze 5 s, Sperrzeit 5 s für alle
  Wege (`cb_door_logic.h`).
- **Nichts blockiert die Hauptschleife.** Der Watchdog startet nach 5 s ohne Lebenszeichen neu.
- **Keine Geheimnisse im Repo und im Image.** WLAN-Zugang und Schlüssel kommen per Provisionierung (Schritt 2).
  Die Datei `config.h` ist in `.gitignore` gesperrt – deshalb heißt die Pin-Datei `cb_config.h`.
- **Signierschlüssel** (R26) liegen nur in `ClearBell\Geheimnisse\signatur\`, der **Ersatzschlüssel nur offline**
  (USB-Stick und Papier an zwei Orten). Die Kennungen in `cb_update_keys.h` **nie ersetzen**: Einheiten nehmen nur
  Updates mit Schlüsseln an, die sie schon kennen. Gehen beide Schlüssel verloren, hilft nur der eigene Schlüssel je
  Einheit (Wartung).
- **Code und Kommentare englisch** (R23), Dokumentation deutsch.
- **Konstanten mit Präfix `CB_`**, Pins mit `CB_PIN_` + Netzname aus dem Schaltplan.

## 7 Fallen

- Läuft der serielle Monitor der Arduino IDE (Prozess `serial-monitor`) oder ein zweites `konsole`, ist der Port
  belegt und das Flashen scheitert mit „Zugriff verweigert“. Nur diesen Prozess beenden.
- `managed_components` (der heruntergeladene Arduino-Core, ≈ 600 MB) ist eine **Junction** nach
  `%USERPROFILE%\clearbell-build\managed_components`, damit er nicht in OneDrive landet. `cb.py` legt sie beim
  ersten Bauen auf jedem Rechner selbst an. Nie den Ordner per Hand löschen und neu anlegen.
- Direkt nach dem Flashen meldet sich der USB-Port neu an; ein sofort gestartetes `konsole` sieht nur die erste
  Zeile. Einfach den Befehl wiederholen.
- In PowerShell-`Start-Job` gehen leere Argumente verloren: zum reinen Mitlesen `konsole COM3 - 30` statt `""`.
- Ein geänderter Wert in `sdkconfig.defaults` wirkt erst, wenn die erzeugte `sdkconfig` im Build-Ordner gelöscht wird;
  danach baut der Arduino-Core komplett neu (≈ 20 min je Image).
- `cb.py bauen`/`flashen` aus **Git Bash** scheitert mit „idf.py wurde nicht als Name eines Cmdlet … erkannt“: Das
  `export.ps1` von ESP-IDF verweigert sich unter MSYS. Aus PowerShell oder `cmd` aufrufen.
- Wird **IO0 (Boot-Taster) beim Einschalten** gedrückt, startet der Chip im Download-Modus statt der Firmware.
- Die IDF-Umgebung lädt `cb.py` selbst; `idf.py` direkt aufzurufen geht auch, dann aber mit denselben
  `-B`/`SDKCONFIG`-Angaben wie in `cb.py`, sonst mischen sich die Konfigurationen der beiden Images.
