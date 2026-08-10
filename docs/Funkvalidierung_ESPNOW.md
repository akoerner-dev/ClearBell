# ClearBell – Funkvalidierung ESP-NOW-Direktfunk (Abschluss)

Datum: 2026-07-05 · Testsketch: 03_Firmware\Testsketche\ESPNOW_Test
Tags: #ClearBell #Funkvalidierung #ESPNOW #Architekturentscheidung

## Testaufbau
- Ping/Pong-Protokoll mit Anwendungsquittung, Sequenznummern,
  3 Wiederholungen / 300 ms Timeout, Duplikaterkennung
- Fern-Statistik: Inneneinheit meldet Zaehler per Funk an Ausseneinheit
- Sendeleistung am Geraet verifiziert: 19,5 dBm (Rohwert 78) = Maximum
- MACs: Aussen 4C:C3:82:A9:D1:E0 · Innen 4C:C3:82:A9:D3:24
- Kanal 1 fest, unverschluesselt, ohne WLAN (Testphase 1)

## Ergebnisse
- Tischdistanz: fehlerfrei, RSSI −33 dBm; Tuerbefehl quittiert in ~32 ms;
  Langzeitlauf >25 min ohne Ausfall
- Quer durch Wohnung (Grenzstrecke): RSSI −88…−100 dBm; Betrieb nur dank
  Wiederholungslogik, vereinzelt endgueltige Verluste
- Positionsaenderung um Zentimeter → mehrere dB (Mehrwegeausbreitung);
  Metallbolzen-Hypothese (Lautsprechermontage) als grosser Hebel
  widerlegt (kontrollierter Vergleich Lauf B/C)
- Lange Strecke (Worst-Case-Naeherung): funktional tot, <2 % Zustellung,
  Rueckkanal ebenfalls tot; Inneneinheit lief nachweislich durch
  (Fern-Statistik: gesendet=5614) → Ursache Distanz/Pfad, nicht Hardware
- Referenz Datenblatt WROOM-32E: RX-Empfindlichkeit −97 dBm typ.
  (802.11b / 1 Mbit/s, definiert bei 8 % Paketfehlerrate)

## Bewertete Hebel (alle quellengeprueft)
- Sendeleistung: bereits Maximum (19,5 dBm); EU-Grenze 20 dBm EIRP
  (ETSI EN 300 328) → Verstaerker nicht zulassungsfaehig, Hebel tot
- Long-Range-Modus: lt. Espressif-Doku ~4 dB Empfindlichkeitsgewinn
  ggue. 802.11b → deckt das Defizit nicht
- Externe Antenne (WROOM-32UE, pinkompatibel, bis 2,33 dBi zertifiziert):
  mehrere dB Linkbudget moeglich, auch als unsichtbare Flachantenne im
  Gehaeuse → VERWORFEN (Aufwand, Entscheidung Meister 2026-07-05)

## Anforderung (neu festgehalten)
- Platzierung der Inneneinheiten ist unkontrolliert (zweite Wohneinheit) und
  Daempfung variabel (z. B. Kleidung im Flur) → Auslegung muss
  unguenstige Strecken abdecken. Direktfunk 2,4 GHz mit Platinen-
  antenne erfuellt das nachweislich nicht.

## Entscheidung (Tendenz, Bestaetigung offen)
- ESP-NOW-Direktfunk fuer Klingel/Tueroeffner verworfen zugunsten
  WLAN + MQTT ueber lokalen Broker (Home Assistant, Raspberry Pi 3B)
- ESP32 kann als Station nur EIN WLAN gleichzeitig → Zwei-Haushalte-
  Loesung nur ueber gemeinsames (IoT-)WLAN oder Cloud-Broker
  (Cloud = Produktpfad, lokal = Hauspfad)
- Offen: Rueckfallverhalten bei Routerausfall (Tueroeffner!),
  Absicherung MQTT-Pfad (Auth/TLS), RSSI-Messung aller drei
  Montageorte zum Router (Mess-Sketch mit MQTT-Meldung geplant)

## Lehren fuer die Firmware (verifiziert im Test)
- WiFi.macAddress() lieferte 00:00:… vor vollstaendigem WLAN-Start →
  esp_read_mac() aus esp_mac.h nutzen (eFuse, zustandsunabhaengig)
- Bezeichner STATUS kollidiert mit ROM-Header (ets_sys.h) →
  projektspezifische Namen fuer globale Bezeichner
- Sende-Callback-Signatur ab Core 3.3.x: wifi_tx_info_t* statt
  uint8_t* (Versionsguard im Sketch, unter 3.3.10 bestaetigt)
- MAC-ACK != Anwendungsquittung; esp_now_send an 00:00:00:00:00:00
  liefert keinen Fehler → Quittung auf Anwendungsebene ist Pflicht
- Fern-Statistik ueber die Messstrecke selbst ist bei Totalausfall
  blind → lokales Lebenszeichen (Zeilenausgabe alle 10 s) beibehalten
- Messmethodik: nur eine Variable aendern; ±5 dB Schwankung ist
  Rauschen; Anlaufphase (Traeger unterwegs) aus Zaehlern ausklammern;
  Streckenverschiebung ersetzt keine Originalstrecke

## Nachtrag: RSSI-Messung Montageorte zum Router (2026-07-05)
Messung mit WLAN_RSSI_Test.ino (Innen-Platine als Wanderplatine),
SSID «Heimnetz», Kanal 8, Mesh mit Repeater vorhanden.
- Montageort OG: −67 dBm → solide
- Montageort Ausseneinheit: −70 dBm → solide (kurze Uebergabe-
  verzoegerung beim Wechsel Router→Repeater beobachtet, danach stabil)
- Max. Entfernung EG (bewusster Extrempunkt, kein Montageort):
  −80 dBm typ., −87 dBm bei Koerperabdeckung → grenzwertig
- Merkposten: Gleiche Strecke, die bei ESP-NOW tot war, traegt zum
  Router: bessere Router-Antennen/Position + automatische MAC-
  Wiederholungen und Ratenanpassung des 802.11-Stapels

## Entscheidung (bestaetigt 2026-07-05)
- Transport: UDP direkt ueber den Router, kein Broker, kein Pi
- Bewusst getragene Schwaeche: entfernte Einheit haengt im WLAN des
  Betreibers (Passwort im Geraeteflash, Wartungskopplung) →
  Milderung: eigene IoT-SSID, sofern Klienten-Isolation abschaltbar
  (Pruefauftrag Router + Repeater), DHCP-Reservierungen
- Tuerbefehl wird auf Anwendungsebene authentifiziert (gemeinsamer
  Schluessel + Zaehler gegen Wiederholung) — ersetzt die alte
  ESP-NOW-Verschluesselungsfrage
- Naechste Schritte: Nachrichtensatz-Entwurf, Wiederverbindungs-
  logik + Repeater-Testfall, Volllasttest 3V3 (WLAN dauerhaft aktiv
  + Audio, rst:0x10-Verdacht), Reparatur Inneneinheit 2


## Nachtrag 2026-07-06 (Abend): «Heimnetz»-Test v0.2
- Beide Einheiten am Router (BSSID ...8B:F8; per Repeater-Abschalttest
  eindeutig als Router identifiziert). OG-Einheit haengt direkt am
  Router, NICHT am Repeater -> keine Mesh-Grenze am OG-Pfad,
  Repeater ist Reserve.
- Tuerort -63/-64 dBm; OG-Ort bewusst verschaerft (ca. 1 m mehr
  Stahlbetondecke als realer Ort) -82/-83 dBm -> Haertefall,
  0 endgueltige Verluste, 0 WLAN-Abrisse, ~5% Wiederholungen.
- Klingel (Aussen->Innen) und Tuer (Innen->Aussen) beidseitig
  quittiert, Sperrzeit + Duplikatpfad unter Schnelldruck bestaetigt.
- FUNK-UNTERBAU ABGENOMMEN.

## Bewusste Entscheidungen (keine Messung, Meister 2026-07-06)
- Realer entfernter Montageort nicht messbar (Zutritt) -> Worst Case
  wird angenommen; abgedeckt durch verschaerften OG-Testpunkt.
  Offen: Gehaeusedaempfung am schlechtesten Punkt nachstellen (F8).
- Langzeitstabilitaet NICHT gemessen (keine offene Platine ueber
  Nacht vor der Haustuer). Entscheidung: Dauerbetrieb wird als
  gegeben angenommen. Empfehlung offen: gefahrloser Dauertest beider
  Einheiten am Schreibtisch (Firmware-Stabilitaet: Speicher,
  Zaehlerueberlauf, Reconnect-Verhalten).


## Nachtrag 2026-07-07: Dauertest 7 h bestanden
- Innen 422 min, Aussen 434 min Betriebszeit, KEIN Neustart/rst.
- 0 WLAN-Abrisse, 0 endgueltige Verluste (Innen 25838 gesendet,
  Aussen 26763 Pings empfangen). Verlustrate << 0,1 %.
- RSSI -45/-69 dBm (Nahbereich) -> Firmware-Dauerstabilitaet belegt
  (kein Leck bis Absturz, kein Zaehlerueberlauf bei 25k+), NICHT die
  Funkstrecke an der Reichweitengrenze.
- Damit ersetzt: fruehere "Langzeit als Annahme" -> jetzt gemessen.
- Weiterhin offen (kein Blocker): Mehrtagestest (Heap-Fragmentierung,
  millis()-Ueberlauf ~49 d), Reconnect-Verhalten an Funkgrenze/bei
  Repeater-Neustart.
