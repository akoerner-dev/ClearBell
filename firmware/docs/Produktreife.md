# Produktreife der Firmware – Prüfung der Schritte 1–9 und Konzept „Inbetriebnahme ohne PC“

Stand 06.11.2026. **Umgesetzt in den Portierungsschritten 10, 10a, 10b, 10c (06.11.2026); Belege im
[Testprotokoll](Testprotokoll.md). Offen: ~~eigenes Statusseiten-Passwort wählen~~ (✓ 08.11.2026), Ländercode als Einstellung,
iOS-Verfahren, Etikett-Entscheidung.** Anlass: ClearBell soll unter Umständen verkauft werden. Daraus zwei Maßstäbe (Meister 06.11.2026):

1. **Nichts hängt an einer Person.** Kein Schlüssel, ohne den Käufer festsitzen (deshalb Updates vorerst ohne
   Signaturschlüssel), kein Dienst, den nur der Hersteller betreiben kann.
2. **Der Käufer flasht nie und startet kein Programm.** Eingaben über eine Webseite sind zumutbar, ein PC-Werkzeug,
   USB-Kabel oder Quelltext ändern nicht.

Was heute über `cb.py` und USB läuft, ist damit nur noch der **Werks- und Entwicklerweg**. Der Käufer braucht einen
eigenen Weg.

---

## 1 Wie es große Hersteller machen (Recherche 06.11.2026)

| Vorbild | Erste Einrichtung | Schutz |
|---|---|---|
| **Espressif Provisioning** (ESP-IDF) | Gerät öffnet einen eigenen WLAN-Zugangspunkt (SoftAP + HTTP) oder BLE; Handy-App überträgt WLAN-Daten | „Proof of Possession“: geräteeigener Code, z. B. als **QR-Aufkleber**; Schlüsselaustausch Curve25519 bzw. SRP6a |
| **WLED** | Zugangspunkt `WLED-AP`, Seite unter fester IP, dort WLAN wählen, „Save & Connect“; danach `name.local` | Standardpasswort für **alle** Geräte gleich – ausdrücklich als Schwäche benannt |
| **Shelly** (aus Erfahrung, Seite nicht abrufbar) | Zugangspunkt `Shelly…-<ID>`, Weboberfläche unter fester IP, alternativ App über BLE | Zugangspunkt optional mit Passwort |
| **Matter** | **QR-Code und Zifferncode auf dem Etikett**, Inbetriebnahme über BLE mit dem Handy | Einrichtungscode je Gerät, im Werk erzeugt |
| **ntfy-App** | Abo per Link `ntfy://ntfy.sh/<topic>` | – |
| **HTTP Shortcuts** | Kurzbefehl per Link importierbar: `https://http-shortcuts.rmy.ch/import?url=<zip-Adresse>` | – |

**Gemeinsames Muster:** ein **geräteeigener Code auf dem Etikett** (mit QR), ein **Einrichtungs-Zugangspunkt mit
Webseite**, danach alles im Heimnetz über die Webseite. Ein gemeinsames Standardpasswort ist verpönt (und nach
EN 18031, SA 7.9, unzulässig).

---

## 2 Befunde je Schritt

Schwere: **A** = verhindert den Verkauf, **B** = muss vor dem Verkauf geändert werden, **C** = Komfort / später.

| Schritt | Befund | Schwere | Korrektur |
|---|---|---|---|
| 2 Provisionierung | WLAN, Rolle, Verbindungsschlüssel kommen **nur per PC und USB** (`cb.py provisionieren`, Anlagendatei) | **A** | Trennung **Werk ↔ Käufer** (Abschnitt 3): Werk schreibt nur Identität und Gerätecode; alles andere richtet der Käufer über die Webseite ein |
| 2 Werksreset | löscht die **ganze** Werkszustand-Partition – damit auch das Gerätepasswort vom Etikett: das Gerät wäre für den Käufer unbrauchbar | **A** | Werksreset löscht nur Käuferdaten (WLAN, Schlüssel, Handys, Protokoll, Einstellungen, eigene Töne); Identität und Gerätecode bleiben |
| 2 Werksreset | nur per **Konsole** | **A** | Taster-Geste in der Einrichtung und Knopf auf der Einrichtungsseite |
| 3 Netz | feste Namen `clearbell-aussen/-eg/-og.local`: zwei Anlagen im selben Netz stören sich (Fehlfunktion, kein Sicherheitsproblem – fremde Rahmen scheitern an der Signatur) | **B** | Name mit Gerätenummer, z. B. `clearbell-1a2b3c.local`; die Gegenstelle wird bei der Kopplung gemerkt |
| 3 Netz | Ländercode `DE` fest | C | Einstellung (Standard DE) |
| 3 Netz | kommt die Einheit nach einem **Routertausch** nicht mehr ins WLAN, muss sie trotzdem einrichtbar sein | **A** | Einrichtungs-Zugangspunkt (SA 5.9 sieht ihn schon vor) |
| 4 Türbefehl | produktneutral | – | – |
| 5 Audio | Töne per Webseite tauschbar ✓; nach dem Werksreset müssen die **Werkstöne** zurückkommen | B | Werkstöne als Kopie im Dateisystem, Werksreset stellt sie wieder her |
| 6 Touch | Schwellen sind Entwürfe für **unsere** Holzfront | C | nach der Montage festlegen; später Empfindlichkeit als Einstellung |
| 7 Türöffner | Pulslänge 2000 ms fest – der Käufer hat **einen anderen Türöffner** (F-3) | **B** | Pulslänge auf der Webseite der Außeneinheit einstellbar (100–5000 ms, Wartung) |
| 8 Statusseite | Webseite ✓, Gerätepasswort je Gerät ✓, **eigenes Passwort ✓ (08.11.2026)** | – | Passwort ändern in der Wartung (verschlüsselt übertragen); Rückkehr zum Etikett-Passwort per leerem Feld oder Werksreset |
| 8 Wartung | Wartung **nur über den Boot-Taster im Gehäuse** – für die Außeneinheit heißt das: Gehäuse öffnen | B | Wartung der Außeneinheit zusätzlich **von einer gekoppelten Inneneinheit aus** freigeben (dort Taster = physische Anwesenheit in der Wohnung); Hardwarefrage siehe Abschnitt 5 |
| 9 Fern-Öffnen | Topics und Handy-Schlüssel nur per PC; Link braucht die Kurzbefehl-ID vom Handy | **A** | Handy hinzufügen auf der Webseite der **eigenen Inneneinheit**: zwei Links/QR – ntfy abonnieren, Kurzbefehl importieren (mit Schlüssel und festem Link, ohne Abtippen) |
| 9 Fern-Öffnen | Wer Handys verwaltet: heute die Anlagendatei. Die Außeneinheit gehört beiden Haushalten | B | Handys verwaltet **jeder Haushalt an seiner Inneneinheit**; sie gibt den Schlüssel verschlüsselt über die gekoppelte Verbindung an die Außeneinheit |
| 9 Fern-Öffnen | fest `ntfy.sh` | C | Server als Einstellung (eigener ntfy-Server möglich); Hinweis im Datenschutztext: Klingelzeit läuft über den Dienst |
| 9 Fern-Öffnen | iOS ohne Verfahren | C | offen (SA 4.5, 13) |
| 10 Updates | geplant: Upload in der Wartung ✓ (ohne Schlüssel, Meister 06.11.2026) | – | Datei von der Release-Seite herunterladen und auf der Webseite hochladen; später Update-Suche mit Herstellersignatur |
| alle | Konsole und `cb.py` sind Entwickler-/Werkswerkzeuge | – | so dokumentieren; Käufer-Anleitung getrennt |
| alle | Arduino-ESP32 steht unter LGPL-2.1: beim Verkauf muss der Käufer die Firmware ersetzen können | B | erfüllt, solange das Repo offen ist und kein Secure Boot den Austausch sperrt (Stufe C für Verkaufsgeräte nur mit Besitzer-Schlüssel, siehe Gespräch zu Schritt 10) |

Was **nicht** geändert werden muss: Protokoll v2, Challenge–Response, Sperrzeit, Tür-Task, Öffnungsprotokoll je
Haushalt, Digest-Anmeldung, Mikrofon-Regeln, Rollback – alles geräteneutral.

---

## 3 Konzept: Werk und Käufer

### 3.1 Was das Werk macht (einmal je Gerät, USB, `cb.py`)

Firmware und Werkstöne flashen; in die Werkszustand-Partition `fctry` nur noch: **Gerätenummer** (6 Hex-Zeichen aus
der MAC, nicht geheim) und **Gerätecode** (16 Zeichen, zufällig, z. B. `K7QF-M2XA-9TRD-H4WE`, ≈ 80 bit). Ein
Etikett drucken: Gerätenummer, Gerätecode, QR-Code. Sonst nichts – kein WLAN, kein Schlüssel, keine Rolle.

### 3.2 Was der Käufer macht (nur Webseite)

1. **Einschalten.** Eine nicht eingerichtete Einheit öffnet den Zugangspunkt `ClearBell-<Gerätenummer>`,
   WPA2-Passwort = Gerätecode. Der QR-Code auf dem Etikett ist ein WLAN-QR (`WIFI:T:WPA;S:…;P:…;;`): Handy-Kamera
   draufhalten, verbunden. Die Einrichtungsseite öffnet sich von selbst (Captive Portal), sonst `http://192.168.4.1`.
2. **Heim-WLAN wählen** (Liste), Passwort eingeben; innen zusätzlich **Wohnung EG oder OG**. „Speichern“ – die Einheit
   verbindet sich und ist unter `clearbell-<Gerätenummer>.local` erreichbar. Der Zugangspunkt geht aus.
3. **Koppeln.** Auf der Seite der Außeneinheit „Inneneinheit koppeln“: Sie zeigt die gefundenen Inneneinheiten im
   Netz; man wählt eine und tippt **deren Gerätecode** vom Etikett ein. Die Außeneinheit erzeugt den
   Verbindungsschlüssel und schickt ihn der Inneneinheit, verschlüsselt und signiert mit einem Schlüssel aus deren
   Gerätecode. Ohne den Code vom Etikett der Inneneinheit – also ohne Zugang zu dieser Wohnung – kann sich niemand
   dazwischenschalten, auch nicht der andere Haushalt im selben WLAN (Angreifer F).
4. **Handy (optional, EG).** Auf der Seite der **eigenen Inneneinheit** „Handy hinzufügen“: Name eingeben, dann zwei
   Knöpfe – „ntfy abonnieren“ (`ntfy://…`) und „Kurzbefehl importieren“ (HTTP-Shortcuts-Import, der Kurzbefehl mit
   Schlüssel kommt als einmaliger Download von der Einheit, nur im Heimnetz, 10 min gültig). „Handy sperren“ daneben.
5. **Fertig.** Klingeln, Türtaste, Push funktionieren. Später: Töne tauschen, Pulslänge, Updates – alles in der Wartung
   auf der Webseite.

**Wieder einrichten** (Routertausch): Taster 2 × 5 s → Einrichtung, der Zugangspunkt kommt wieder; Kopplung und
Handys bleiben erhalten. **Werksreset**: in der Einrichtung Taster 15 s oder Knopf auf der Seite – alles des Käufers
ist weg, das Etikett gilt weiter.

### 3.3 Folgen für die Architektur (SA, deutsch und englisch)

- SA 5.9 „Die Erstprovisionierung läuft über USB“ → nur noch Werk; Käufer über den Einrichtungs-Zugangspunkt.
- SA 6.2: `fctry` = nur Werk (Nummer, Code); Käuferdaten in eigenem NVS-Bereich (`cb_user`), den der Werksreset löscht.
- SA 4.x: neuer Nachrichtentyp „Kopplung“ und „Handy-Schlüssel übergeben“ (verschlüsselt, nicht nur signiert).
- SA 7.x: Gerätecode als Besitznachweis (wie Espressifs „Proof of Possession“); Zugangspunkt nur in der Einrichtung,
  mit Zeitlimit 30 min.
- Portierungsplan: neuer Schritt **„Inbetriebnahme per Webseite“** vor der Abnahme.

---

## 4 Was das kostet und in welcher Reihenfolge

| Reihenfolge | Inhalt | Umfang |
|---|---|---|
| 10 | Updates per Upload in der Wartung, Rollback nach Selbsttest (wie besprochen, ohne Schlüssel) | 1 Sitzung |
| 10a (neu) | Werk ↔ Käufer trennen, Werksreset korrigieren, Einrichtungs-Zugangspunkt mit Captive Portal, WLAN-Auswahl, Rolle | 1–2 Sitzungen |
| 10b (neu) | Kopplung per Gerätecode, eindeutige Namen, Pulslänge einstellbar, Wartung der Außeneinheit von innen | 1–2 Sitzungen |
| 10c (neu) | Handy hinzufügen/sperren über die Inneneinheit, ntfy- und Import-Link | 1 Sitzung |
| 11 | Abnahme, Dauertests | wie geplant |

Die heutigen Ergebnisse bleiben gültig; die Schritte 10a–c bauen darauf auf.

---

## 5 Offene Punkte für den Meister

- **Etikett:** Gerätenummer + Gerätecode + QR je Einheit – passt das zur geplanten Fertigung (Aufkleber auf der
  Rückseite, Kopie in der Verpackung)?
- **Boot-Taster der Inneneinheit:** Ist er im fertigen Gehäuse ohne Werkzeug erreichbar (Loch für eine Büroklammer)?
  Davon hängt ab, ob Wartung/Einrichtung innen „ohne Öffnen“ geht.
- **Ein Code oder zwei:** Ich schlage **einen** Gerätecode für Einrichtungs-WLAN und Statusseite vor (einfacher für den
  Käufer); das Statusseiten-Passwort kann der Käufer danach selbst ändern.
