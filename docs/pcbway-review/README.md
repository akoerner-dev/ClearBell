# Bewertung: Leiterplatten und Bestückung von PCBWay (ClearBell V0.2)

🇬🇧 [English version](README.en.md)

> **Offenlegung:** Die Leiterplatten der ClearBell V0.2 hat [PCBWay](https://www.pcbway.com) im Rahmen eines
> Sponsorings kostenlos gefertigt. Bestückung und Bauteile habe ich regulär selbst bezahlt. Diese Bewertung ist
> Teil der Vereinbarung. Inhalt und Urteil sind meine eigenen; PCBWay hatte vorab keinen Einfluss darauf.

Ich arbeite beruflich als Leiterplattenentwickler und habe fast täglich Platinen aus verschiedenen Fertigungen
in der Hand. Diese Bewertung stützt sich auf eine Sichtprüfung beider Platinentypen, bestückt und unbestückt, und auf
die vollständige Inbetriebnahme mit anschließendem 24-Stunden-Dauertest.

**Kurzfassung:** Die Leiterplatten und die Bestückung sind qualitativ sehr gut. Ich habe weder an den Leiterplatten noch
an der Bestückung einen Mangel gefunden. Alle Fehler, die bei der Inbetriebnahme auftraten, gehen auf mein eigenes
Design oder auf Bedienfehler zurück, keiner auf die Fertigung.

---

## Der Auftrag

| | Außeneinheit | Inneneinheit |
|---|---|---|
| Abmessungen | 110 × 60 mm | 98 × 60 mm |
| Lagenaufbau | 4 Lagen, 1,6 mm, FR-4 (S1000H, TG150), 1 oz außen und innen | dito |
| Oberfläche | ENIG, Lötstopplack grün, Bestückungsdruck weiß (nur oben) | dito |
| Bohrungen | 432 durchkontaktiert, 7 nicht durchkontaktiert | 379 durchkontaktiert, 12 nicht durchkontaktiert |
| Bestückung | einseitig, 56 Bauteile | einseitig, 33 Bauteile |
| Kritische Bauteile | ESP32-S3-Modul, LMR33630 (Abwärtsregler), MAX98357A (Audioverstärker, QFN), MEMS-Mikrofon, Service-USB-C | ESP32-S3-Modul, TLV62568 (Abwärtsregler), MAX98357A (QFN), MEMS-Mikrofon, USB-C |

Der kleinste Abstand im Layout liegt bei 0,2 mm (am ESP32-S3-Modul) und damit an der Fertigungsgrenze von PCBWay.
Schaltpläne, Layouts und Fertigungsdaten liegen in [`hardware/`](../../hardware/).

**Ablauf:** Fertigungsdaten hochgeladen am 18.09.2026, Rückfragen zu einzelnen Bauteilen am 19.09., Fertigung und
Bestückung bestätigt am 26.09., Lieferung am 24.10.2026.

---

## Bestellung und Kommunikation

PCBWay hat die Stückliste vor der Bestückung geprüft und zu vier Positionen gezielt nachgefragt (Bestellnummern und
zulässiger Ersatz). Die Rückfragen waren fachlich berechtigt und liefen über eine Excel-Liste, die sich in kurzer Zeit
beantworten ließ. Meine Stückliste enthält eine Spalte „Ersatz zulässig“; PCBWay hat sie berücksichtigt, bei den als
„kein Ersatz“ markierten Teilen wurde nichts getauscht.

Mit der Kommunikation war ich sehr zufrieden. Auch die Website überzeugt: Sie zeigt detailliert, in welchem
Arbeitsschritt sich die Bestellung gerade befindet, sodass man den Fortschritt jederzeit selbst verfolgen kann.

Für das MEMS-Mikrofon hatte ich besondere Verarbeitungshinweise mitgegeben (keine Ultraschall- oder Waschreinigung, keine
Druckluft, No-Clean-Paste). Das Mikrofon funktioniert auf allen geprüften Boards — die Hinweise wurden offensichtlich
beachtet.

---

## Leiterplatte (unbestückt)

| Innen, Oberseite | Innen, ENIG-Detail | Innen, Unterseite |
|---|---|---|
| ![Inneneinheit unbestückt, Oberseite](img/01_innen_unbestueckt_oben.jpg) | ![Inneneinheit unbestückt, Pads des ESP32-Moduls und Mikrofonbohrung](img/02_innen_unbestueckt_enig_detail.jpg) | ![Inneneinheit unbestückt, Unterseite](img/03_innen_unbestueckt_unten.jpg) |

| Außen, Oberseite | Außen, Versorgungsteil | Außen, Unterseite |
|---|---|---|
| ![Außeneinheit unbestückt, Oberseite](img/09_aussen_unbestueckt_oben.jpg) | ![Außeneinheit unbestückt, Versorgungsteil mit Wärmepad des Reglers](img/10_aussen_unbestueckt_versorgung.jpg) | ![Außeneinheit unbestückt, Unterseite](img/11_aussen_unbestueckt_unten.jpg) |

- **Lötstopplack:** gleichmäßig deckend, ohne Blasen, Kratzer oder Fehlstellen. Die Freistellungen um die Pads sitzen
  passgenau, auch an der feinen Pad-Reihe des ESP32-Moduls und am QFN des Audioverstärkers.
- **Bestückungsdruck:** scharf und vollständig lesbar, auch die kleinsten Referenzbezeichner und Testpunkt-Namen.
  Kein Druck auf Pads.
- **ENIG-Oberfläche:** glatt und plan, auch auf den großen Wärmepads (Regler, ESP32-Modul mit Via-Feld). Keine
  Verfärbungen.
- **Bohrungen und Kontur:** Bohrungen sauber und mittig im Restring, die Mikrofon-Schallbohrung mit intaktem Kupferring.
  Die Fräskontur einschließlich Ausschnitt für die Modulantenne und abgerundeter Ecken ist maßhaltig und gratfrei.
- **Material:** keine Delamination, keine Blasen, keine Verwölbung erkennbar. Beide Platinen liegen plan.

## Bestückung

| Innen, bestückt | Innen, ESP32-S3-Modul | Innen, Unterseite |
|---|---|---|
| ![Inneneinheit bestückt, Oberseite](img/04_innen_bestueckt_oben.jpg) | ![Inneneinheit, ESP32-S3-WROOM-1](img/05_innen_esp32_modul.jpg) | ![Inneneinheit bestückt, Unterseite](img/08_innen_bestueckt_unten.jpg) |

| Außen, bestückt | Außen, ESP32-S3-Modul | Außen, Unterseite |
|---|---|---|
| ![Außeneinheit bestückt, Oberseite](img/12_aussen_bestueckt_oben.jpg) | ![Außeneinheit, ESP32-S3-WROOM-1](img/16_aussen_esp32_modul.jpg) | ![Außeneinheit bestückt, Unterseite](img/17_aussen_bestueckt_unten.jpg) |

**Kritische Stellen im Detail:**

| USB-C und Regler (innen) | Audioverstärker QFN (innen) |
|---|---|
| ![Inneneinheit, USB-C-Buchse, ESD-Diode und Abwärtsregler](img/06_innen_usb_c_regler.jpg) | ![Inneneinheit, MAX98357A im QFN-Gehäuse](img/07_innen_audioverstaerker_qfn.jpg) |

| Versorgung 12 V → 3,3 V (außen) | Service-USB-C (außen) | Audioverstärker QFN (außen) |
|---|---|---|
| ![Außeneinheit, Versorgungsteil mit LMR33630](img/13_aussen_versorgung.jpg) | ![Außeneinheit, Service-USB-C](img/14_aussen_usb_c_service.jpg) | ![Außeneinheit, MAX98357A im QFN-Gehäuse](img/15_aussen_audioverstaerker_qfn.jpg) |

- **Platzierung:** Alle Bauteile sitzen gerade und mittig auf ihren Pads, ohne Versatz oder Verdrehung. Polarität und
  Lage stimmen bei allen gepolten Bauteilen (Dioden, Elko, ICs, LED).
- **USB-C:** Die 0,5-mm-Pads der Buchsen sind sauber und ohne Brücken verlötet, die Befestigungslaschen vollständig
  benetzt. Beide Buchsen sitzen fest und bündig an der Kante.
- **Audioverstärker (QFN):** gleichmäßige Lotmenge an allen Anschlüssen, keine Brücken, das Gehäuse liegt plan.
- **ESP32-S3-Modul:** alle Randanschlüsse gleichmäßig benetzt, Modul plan und lagerichtig, Antenne korrekt über dem
  Ausschnitt.
- **Lötstellen allgemein:** gleichmäßig, glänzend, mit sauberem Meniskus. Keine Lotkugeln, keine Grabsteine, kein
  Flussmittelrückstand, der stört.
- **Stückliste:** alle Bauteile vollständig und in der richtigen Ausführung bestückt, auch die Teile mit
  Ersatz-Freigabe nach meinen Vorgaben.

---

## Funktion: Inbetriebnahme und Dauertest

Die Inbetriebnahme lief Stufe für Stufe ab 24.10.2026, zuerst an je einem Board pro Typ (IN-1, AU-1):

| Stufe | Ergebnis |
|---|---|
| Sichtprüfung und Widerstandsmessung | alle Prüfpunkte in Ordnung (innen 14, außen 21), keine Kurzschlüsse |
| Versorgung | +3,3 V innen 3,293 V, außen 3,323 V; Verpolschutz wirkt |
| Controller, USB | ESP32-S3 mit 16 MB Flash und 8 MB PSRAM erkannt, Flashen über natives USB auf Anhieb |
| Audio | Ton sauber, kein Knacken beim Ein- und Ausschalten |
| Mikrofon | funktioniert, Sprachaufnahme gut verständlich |
| Touch | stabile Ruhewerte, klare Berührungserkennung |
| WLAN mit Ton bei voller Lautstärke | 300 von 300 Paketen, kein Neustart |
| 24-Stunden-Dauertest, drei Boards | bestanden: kein Neustart, kein Absturz, 0 fehlgeschlagene Klingelvorgänge |

Inzwischen läuft auf allen drei Boards die neue V0.2-Firmware. **Kein einziger Befund aus der Inbetriebnahme ließ sich auf
die Fertigung oder die Bestückung zurückführen** — die gefundenen Punkte waren Designentscheidungen meinerseits (z. B. ein
kleiner Rückstrom über eine Entkopplungsdiode) oder Bedienfehler beim Flashen. Sie sind im Repo offen dokumentiert und
fließen in V0.3 ein.

---

## Fazit

| Kriterium | Bewertung |
|---|---|
| Leiterplattenqualität | sehr gut |
| Bestückungsqualität | sehr gut |
| Kritische Stellen (USB-C, QFN, Modul) | sehr gut, keine Brücken |
| Kommunikation und Rückfragen | sehr gut, fachlich berechtigt und schnell erledigt |
| Website und Auftragsverfolgung | sehr gut, jeder Arbeitsschritt nachvollziehbar |
| Funktion nach Lieferung | alle drei in Betrieb genommenen Boards auf Anhieb funktionsfähig |

Für ein 4-lagiges Design mit Modul, QFN, USB-C und MEMS-Mikrofon ist das ein Ergebnis, das ich auch im beruflichen
Umfeld so erwarten würde. Ich würde PCBWay für Leiterplatten und Bestückung wieder beauftragen.

**Grenzen dieser Bewertung:** Geprüft habe ich mit bloßem Auge, Handykamera und Messtechnik bei der Inbetriebnahme.
Keine Röntgenprüfung der verdeckten Lötstellen (Wärmepads unter Modul, Regler und QFN), kein Schliffbild. Deren Qualität
belegt indirekt die Funktion: Inbetriebnahme und 24-Stunden-Dauertest ohne Auffälligkeit. Der rötliche Farbstich auf
einigen Fotos kommt von der Beleuchtung, der Lötstopplack ist grün.

---

*André Körner, November 2026 — Projekt [ClearBell](../../README.md)*
