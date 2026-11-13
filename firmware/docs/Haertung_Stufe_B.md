# Härtung Stufe B – Konzept und Testplan

Stand 12.11.2026 abends, **Entscheidungen HB-E1 bis HB-E3 getroffen (Abschnitt 7), umkehrbarer Teil umgesetzt,
noch nichts gebrannt.** Grundlage: SA 7.7 (Stufen A/B/C) und R22 („B nach der
Abnahme der Portierung“ – P6 abgenommen am 12.11.2026). Reihenfolge nach Meister 12.11.2026: erst dieses Konzept, dann
alles Umkehrbare ausprobieren, **gebrannt wird erst nach ausdrücklicher Freigabe, zuerst nur an IN-2.**
Geprüft gegen ESP-IDF v5.5.5 (Quelltext und Kconfig im lokalen IDF), nicht nur gegen Doku.

---

## 1 Ausgangslage (ausgelesen 12.11.2026 mit `espefuse summary`, nur lesend)

AU-1, IN-1 und IN-2 sind sicherheitstechnisch im Auslieferungszustand: alle sechs Schlüsselplätze `BLOCK_KEY0..5` frei
(Zweck `USER`), `SECURE_VERSION` = 0, kein Secure Boot, keine Flash-Verschlüsselung (`SPI_BOOT_CRYPT_CNT` = 0),
Download-Modus und JTAG offen, keine Lese- oder Schreibsperren (`RD_DIS` = 0, `WR_DIS` = 0).

## 2 Was Stufe B endgültig festschreibt – und was nicht

| Teil | Was gebrannt wird | Endgültig heißt | Umkehrbar bleibt |
|---|---|---|---|
| **B1 NVS-Schlüssel** | ein zufälliger 256-bit-Schlüssel in `BLOCK_KEY0`, Zweck `HMAC_UP`, lese- und schreibgesperrt | der Platz ist für immer belegt; der Schlüssel ist nie lesbar oder änderbar; der NVS-Inhalt ist an diesen Chip gebunden | Firmware, USB-Flashen, JTAG, Werksreset (löscht die Daten, der Schlüssel bleibt ungenutzt liegen) |
| **B2 Anti-Rollback** | je Erhöhung der Sicherheitsversion ein Bit in `SECURE_VERSION` (16 Bit = 16 Erhöhungen) | eine Firmware mit kleinerer Sicherheitsversion startet nie wieder, auch nicht per USB | alles mit gleicher oder höherer Sicherheitsversion; solange die Version 0 bleibt, wird **nichts** gebrannt |

**Nicht in Stufe B** (erst Stufe C, nur außen, frühestens nach Monaten stabilen Betriebs): Secure Boot,
Flash-Verschlüsselung, Sperren von Download-Modus und JTAG. Der Gerätecode bleibt in der Partition `fctry`
(im eFuse brächte er wenig: er ändert sich nie und steht ohnehin auf dem Etikett).

## 3 eFuse-Belegung (Vorschlag, gilt für alle Einheiten)

| Platz | Stufe | Zweck |
|---|---|---|
| `BLOCK_KEY0` | B | `HMAC_UP` – NVS-Schlüssel (B1) |
| `BLOCK_KEY1`, `BLOCK_KEY2` | C, nur außen | Secure-Boot-v2-Digests: Haupt- und Ersatz-Herstellerschlüssel (passt zu R26) |
| `BLOCK_KEY3` | C, nur außen, nur falls dann begründet | Flash-Verschlüsselung |
| `BLOCK_KEY4`, `BLOCK_KEY5` | – | Reserve (z. B. dritter Secure-Boot-Schlüssel des Besitzers, Ersatz für einen verbrannten Platz) |

## 4 B1: NVS-Verschlüsselung mit HMAC-Schlüssel

**Was geschützt wird:** der NVS-Bereich `nvs` – WLAN-Passwort, Verbindungsschlüssel, Handy-Schlüssel, eigenes
Statusseiten-Passwort, Einstellungen, Öffnungsprotokoll. Wer heute den Flash ausliest (Gerät in der Hand, USB oder
Chip), liest das im Klartext. Danach stehen dort nur XTS-AES-verschlüsselte Daten; der Schlüssel entsteht zur Laufzeit
im HMAC-Baustein aus dem eFuse-Schlüssel und verlässt den Chip nie.

**Falle in der ESP-IDF (aus dem Quelltext, `nvs_sec_provider.c`):** Mit `CONFIG_NVS_ENCRYPTION` und HMAC-Schema erzeugt
`nvs_flash_init()` beim ersten Start **selbst** einen Zufallsschlüssel und brennt ihn, wenn der Platz leer ist –
schon das bloße Aufspielen der Firmware würde brennen. **Entwurf deshalb:**

- `CONFIG_NVS_ENCRYPTION` bleibt aus. Die Firmware prüft beim Start selbst (`nvs_flash_read_security_cfg_v2` mit dem
  HMAC-Schema, das **nie** brennt): Schlüssel vorhanden → NVS verschlüsselt öffnen (`nvs_flash_secure_init`), sonst wie
  heute im Klartext. **Ein Image für gebrannte und ungebrannte Boards.**
- Gebrannt wird nur über einen ausdrücklichen Befehl über USB (physische Anwesenheit), mit Gerätenummer als
  Bestätigung, z. B. Konsole `haertung nvs-schluessel BRENNEN <Gerätenummer>`, verpackt in `cb.py haertung <Einheit>`
  mit Rückfrage. Nie über die Webseite, nie automatisch.
- **Schlüssel auf dem Chip erzeugt** (Vorschlag, Entscheidung HB-E1 unten): niemand kennt ihn, auch der Hersteller
  nicht. Folge: PC-Werkzeuge können keine fertigen NVS-Abbilder mehr schreiben. Heute schreiben `werk` und
  `provisionieren` nur `fctry`; die Käuferdaten entstehen ohnehin auf der Einrichtungsseite (R24) → kein Verlust.
- **Bootablauf (umgesetzt in `main/cb_nvs_secure`):** Der Arduino-Kern öffnet `nvs` schon vor `setup()` im
  Klartext (`initArduino` → `nvs_flash_init`), und ein Klartext-Öffnen über verschlüsselten Daten **löscht** die
  Einträge (Kopf-CRC passt nicht, `nvs_page.cpp`). Deshalb wird `nvs_flash_init` beim Linken umgeleitet
  (`--wrap`); die Entscheidung Klartext/verschlüsselt/Übernahme fällt genau dort. Eine Markierung (Kennung +
  8-Byte-Fingerabdruck der abgeleiteten Schlüssel) in der bisher reservierten Partition `nvs_keys` sagt, ob und mit
  welchem Schlüssel der NVS verschlüsselt ist. Fälle: kein Schlüssel → Klartext; Schlüssel + passende Markierung →
  verschlüsselt; Schlüssel ohne Markierung → Übernahme; Markierung eines anderen Schlüssels → löschen, leer
  verschlüsselt; Markierung ohne Schlüssel → nicht öffnen (Einheit bleibt unkonfiguriert statt Daten zu löschen).
  Das WLAN speichert nichts im NVS (`WiFi.persistent(false)`).
- **Übernahme der bestehenden Daten** (Vorschlag, HB-E2): Beim ersten Start mit Schlüssel liest die Firmware alle
  Einträge des Klartext-NVS in den RAM, löscht die Partition, öffnet sie verschlüsselt und schreibt alles zurück.
  Stromausfall genau in diesem Moment = Daten weg → Einrichtung neu (Werksreset-Zustand, nichts Schlimmeres). Die
  Alternative ohne Übernahme: Werksreset und Neueinrichtung je Board (WLAN, Kopplung, Handys).
- **Wenn etwas schiefgeht:** Schlüssel vorhanden, aber NVS nicht lesbar (z. B. Klartext-NVS per USB geschrieben) →
  wie „nicht eingerichtet“ (Einrichtungsmodus), Meldung in Log und Statusseite, nie ein Absturz.
- `fctry` (nur Gerätecode) bleibt unverschlüsselt: Der Code steht auf dem Etikett. Restrisiko außen: Wer die
  Außeneinheit öffnet und den Flash ausliest, kennt ihren Gerätecode (Statusseiten-Login ohne eigenes Passwort,
  Einrichtungs-WLAN). Dagegen hilft die Montage (SW-01) und das eigene Passwort; in Stufe C ggf. Flash-Verschlüsselung.
- Statusseite: Zeile „Speicher verschlüsselt: ja/nein“ in der Diagnose.

## 5 B2: Anti-Rollback

- `CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=y`, `CONFIG_BOOTLOADER_APP_SECURE_VERSION=0`, Feldgröße 16. Solange die
  Sicherheitsversion 0 bleibt, brennt nichts – die Funktion steht bereit.
- Der Bootloader kommt nicht per Update; jedes Board braucht ihn **einmal per USB** (Werks-/Entwicklerweg).
- Gebrannt wird erst, wenn eine Firmware mit höherer Sicherheitsversion ihre Probezeit besteht
  (`esp_ota_mark_app_valid_cancel_rollback` → `esp_efuse_update_secure_version`). Eine abgebrochene Probezeit brennt nichts.
- **Regel (Vorschlag, HB-E3):** Sicherheitsversion nur erhöhen, wenn eine ältere Firmware eine ernste Lücke hat, die
  nicht wiederkommen darf; jede Erhöhung mit Grund im CHANGELOG. Normale Updates behalten sie.
- Zusammenspiel mit R26: „ältere Version zulassen“ wirkt weiter, aber nur bis zur Sicherheitsversion im eFuse; die
  Update-Seite lehnt kleinere Sicherheitsversionen mit Klartext ab, bevor geschrieben wird (die IDF prüft es zusätzlich
  beim Umschalten des Slots).
- **Test ohne Brennen:** Die IDF kann die eFuses im Flash nachbilden (`BOOTLOADER_EFUSE_SECURE_VERSION_EMULATE`, braucht
  eine Partition `emul_efuse` mit 0x2000 Byte). Nur für einen Testbau an IN-2, mit eigener Partitionstabelle (die
  Coredump-Partition gibt 0x2000 ab); danach wieder die normale Tabelle.

## 6 Testplan

Erst umkehrbar (HB.1–HB.6), dann endgültig an IN-2 (HB.7–HB.10), dann die übrigen Boards (HB.11).

| Zeile | Was | umkehrbar? |
|---|---|---|
| HB.1 | PC-Tests: Sicherheitsversion aus dem Image-Kopf, Vergleich mit dem eFuse-Wert (die Übernahme-Logik braucht die echte NVS-Bibliothek → am Gerät, HB.3) | ja |
| HB.2 | NVS verschlüsselt mit einem **Testschlüssel aus der Software** (kein eFuse) an IN-2: Einrichtung, Kopplung, Handys, Neustart, Werksreset | ja (danach Klartext zurück) |
| HB.3 | Übernahme Klartext → verschlüsselt mit Testschlüssel, inkl. Stromausfall mitten drin | ja |
| HB.4 | Image mit B1-Code auf ungebranntem Board: läuft wie heute (Klartext), Statusseite „verschlüsselt: nein“ | ja |
| HB.5 | Anti-Rollback mit eFuse-Nachbildung an IN-2: Version 0 → 1 → Update mit 0 abgelehnt, Probezeit abgebrochen brennt nicht | ja |
| HB.6 | Brennbefehl in der eFuse-Nachbildung (`innen-hbemu`): Bestätigung, richtiger Platz und Zweck; echte eFuses danach gegenlesen | ja |
| HB.7 | **IN-2: NVS-Schlüssel brennen** (nach Freigabe), `espefuse summary` vorher/nachher | **nein** |
| HB.8 | IN-2: Übernahme der echten Daten, Klingeln, Tür, Handy, Neustart, Werksreset, erneute Einrichtung | – |
| HB.9 | IN-2: Bootloader mit Anti-Rollback per USB, Sicherheitsversion 0 (brennt nichts) | ja |
| HB.10 | IN-2: Flash auslesen (`esptool read_flash` des Bereichs `nvs`) – kein WLAN-Passwort im Klartext mehr | – |
| HB.11 | IN-1, dann AU-1 (nach eigener Freigabe je Board) | **nein** |

Danach 24 h Normalbetrieb aller drei, bevor Stufe B als fertig gilt.

**Stand 13.11.2026 morgens:** **IN-2 gebrannt** (HB.7, Freigabe Meister „Ja, IN-2 brennen“), Übernahme und
Flash-Gegenprobe bestanden (HB.8 teilweise, HB.10). Offen: HB.8 Werksreset mit erneuter Einrichtung (löscht das
eingetragene Handy), HB.9 Bootloader mit Anti-Rollback, HB.11 IN-1 und AU-1 (je eigene Freigabe), 24 h Normalbetrieb.

**Stand 13.11.2026 (davor):** HB.1–HB.6 bestanden (Belege: Testprotokoll „Härtung Stufe B – umkehrbarer Teil“); nicht geprüft
sind der Werksreset im verschlüsselten Zustand und ein Stromausfall mitten in der Übernahme. Alle drei Boards laufen
mit dem B1-Code ungebrannt im Klartext. **Nächster Schritt HB.7 wartet auf die Freigabe des Meisters.**

## 7 Entscheidungen des Meisters

**12.11.2026: „Ja, alle drei“** (Vorschläge angenommen):

- **HB-E1_Schluessel_auf_dem_Chip:** Der Schlüssel wird auf dem Chip erzeugt; niemand kennt ihn.
- **HB-E2_Daten_uebernehmen:** Die bestehende Einrichtung wird beim ersten Start mit Schlüssel automatisch übernommen.
- **HB-E3_Sicherheitsversion_nur_bei_Luecken:** Die Sicherheitsversion steigt nur bei einer ernsten Lücke, mit Grund
  im CHANGELOG.

Offen: **Freigabe zum Brennen** je Board (HB.7, HB.11) – erst nach HB.1–HB.6.

## 7a Testbauten

`cb.py bauen innen-hbtest` (Softwareschlüssel + eFuse-Nachbildung + Tabelle `partitions_hbtest.csv`) und
`cb.py bauen innen-hbemu` (nur eFuse-Nachbildung, für den Brennbefehl). Eigene Bauordner; beide nie auf einer Einheit im
echten Einsatz lassen. Zurück: `haertung klartext JA` (nur im Testbau mit Softwareschlüssel), dann normal per USB flashen.

## 8 Fallen (12.11.2026)

- `espefuse summary` lässt das Board im Download-Modus; danach `esptool --after watchdog_reset read_mac` (wie FW-B-18).
  Die pySerial-Fehlermeldung beim Neuverbinden ist dabei harmlos.
- `nvs_flash_init()` mit `CONFIG_NVS_ENCRYPTION` brennt selbst (Abschnitt 4).
- **Anti-Rollback wirkt auch im Test:** Nach Sicherheitsversion 1 in der Nachbildung startete der Bootloader eine
  per USB geflashte Stufe-0-Firmware nicht, sondern die Stufe-1-Firmware aus dem anderen Slot. Zurück: Bereich
  `emul_efuse` löschen (`esptool erase_region 0xffe000 0x2000`), danach den anderen App-Slot löschen, damit kein
  Testimage mehr startbar ist.
- Das Logging verwirft Zeilen, wenn viele auf einmal kommen (gewollt, blockiert nie) – lange Listen auf der Konsole
  takten (`nvs-liste` wartet 15 ms je Zeile).
- `Out-File` in PowerShell bricht lange Zeilen um → Konsolenausgaben aus `build/konsole/*.log` auswerten.
- Die eFuse-Nachbildung ändert nicht, was der HMAC-Baustein sieht – B1 lässt sich nur mit Softwareschlüssel (HB.2)
  oder echt (HB.7) testen, nicht in der Nachbildung.
