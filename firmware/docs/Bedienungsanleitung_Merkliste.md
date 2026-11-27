# Merkliste Bedienungsanleitung (Käufer) – ClearBell V0.2

Eine Käuferanleitung gibt es noch nicht. Hier wird gesammelt, was **unbedingt** hinein muss, damit sie später
vollständig ist. Jede Zeile nennt die Quelle (Befund, Entscheidung, Testzeile). Neue Punkte unten anhängen.

| Nr. | Was in die Anleitung muss | Warum | Quelle |
|---|---|---|---|
| A-1 | **Neue Handys: Klingel-Nachrichten kommen erst nach einigen Minuten zuverlässig.** Nach dem Hinzufügen eines Handys und dem Abonnieren in ntfy kann es ein paar Minuten dauern, bis Klingel-Nachrichten bei gesperrtem Handy von selbst erscheinen; vorher liegen sie nur in der App (sichtbar nach Aktualisieren). Empfehlung: nach dem Einrichten **5–10 min warten** und dann einmal Probe klingeln. Wer es sofort braucht: in ntfy beim Abo **„Sofortige Zustellung“** einschalten. | Meister 27.11.2026: „Sonst hat man am laufenden Band deswegen verärgerte Kunden.“ Zweimal beobachtet (Abo 1–5 min alt), Gegenprobe mit 25 bzw. 10 min altem Abo kam sofort; die Firmware verschickt korrekt | Testprotokoll, Sammeltermin Teil 4 (FW-B-34_Handy_Bestaetigung) |
| A-2 | **Alte ntfy-Abos löschen.** Kommt „ClearBell: Abo ungültig“ oder trägt ein Abo eine andere Kennung als die Statusseite, dieses Abo in der ntfy-App löschen. | Mehrere gleich aussehende Abos verwirrten den Meister (Testnachricht „kam nie an“) | FW-B-34 |
| A-3 | **Adressen immer mit `http://` eingeben** (z. B. `http://192.168.4.1`), sonst macht Chrome still `https://` daraus und die Seite lädt nicht. | Bis R29_HTTPS_mit_Werkszertifikat nicht in der Firmware lösbar | FW-B-31 |
| A-4 | **Einrichtungs-WLAN per QR-Code verbinden**, nicht abtippen. | Abgetippt mit Bindestrichen verbindet das Handy nicht | FW-B-14, 10a.13 |
| A-5 | **Neukopplung verlangt das Öffnen des Außengehäuses** (Boot-Taster der Außeneinheit ist von außen nicht erreichbar). | Entscheidung R31 | Produktreife Abschnitt 5 |
| A-6 | **Kleines Fenster „Im Netzwerk anmelden“ kann keine Passwörter**: dort die Anleitung lesen, dann im Browser `http://192.168.4.1` öffnen. | Die Einheit zeigt diese Hilfe selbst; die Anleitung sollte sie vorwegnehmen | FW-B-30 |
| A-7 | **Nach der Ersteinrichtung**: Die Erfolgsmeldung zeigt Adresse und Namen `http://clearbell-<Gerätenummer>.local`; wenn die Adresse nach dem Neustart nicht geht, den Namen nehmen (beide als Lesezeichen speichern). | Der Router vergibt nach dem Neustart nicht garantiert dieselbe Adresse | SW-Entscheid_Adresse_in_Erfolgsmeldung |
| A-8 | **Nach einem Werksreset der Inneneinheit** muss sie an der Außeneinheit neu gekoppelt werden; die Handys dieser Wohnung neu hinzufügen. | Die Außeneinheit behält die alte Kopplung bis zur Neukopplung (VK-01) | Testprotokoll, Sammeltermin Teil 4; FW-B-37 |
