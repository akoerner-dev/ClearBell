# ClearBell auf GitHub veröffentlichen

Dieser Ordner ist ein fertig aufbereitetes, entschärftes Repository.
Er enthält **keine** echten Passwörter, Schlüssel oder Netznamen mehr.

## 0) Vorab — unabhängig von GitHub

Dein WLAN-Passwort stand im Klartext in den Projektdateien und sah aus wie
ein Geburtsdatum (`08071991`, doppelt). Diese Dateien lagen in einem
OneDrive-synchronisierten Ordner. **Empfehlung: WLAN-Passwort ändern**,
besonders falls du es auch für andere Zugänge verwendest.

## 1) Vor dem Push — Kurz-Checkliste

- [ ] `firmware/ClearBell_produktiv/` enthält **nur** `config.example.h`,
      **keine** `config.h`.
- [ ] Kein Treffer bei einer Volltextsuche nach deinem echten Netznamen,
      Passwort oder deinen ntfy-Topics (in diesem Ordner bereits geprüft:
      sauber).
- [ ] Datenblätter/fremde PDFs bleiben draußen (Urheberrecht).
- [ ] README liest sich für einen fremden Betrachter verständlich.

## 2) GitHub-Repo anlegen

Auf GitHub ein **leeres** Repository anlegen (ohne README/Lizenz, die sind
schon hier), z. B. `clearbell`. Sichtbarkeit **public**.

## 3) Lokal initialisieren und pushen

In diesem Ordner (`clearbell-repo/`) im Terminal:

```bash
git init
git add .
git commit -m "ClearBell: ESP32 Smart-Doorbell – Hardware + Firmware"
git branch -M main
git remote add origin https://github.com/<DEIN-USER>/clearbell.git
git push -u origin main
```

> Tipp: Führe **nach** `git add .` einmal `git status` aus und überzeuge dich,
> dass keine `config.h` in der Liste steht. Die `.gitignore` verhindert das
> bereits, aber Kontrolle schadet nicht.

## 4) Nach dem Push — Feinschliff (optional, wirkt aber stark)

- Repo-Beschreibung + Topics setzen: `esp32`, `kicad`, `arduino`,
  `pcb-design`, `smart-home`, `iot`.
- Ein, zwei **Fotos** hinzufügen: bestückte Platine, Prototyp im Betrieb.
  Bilder machen bei Hardware-Projekten den größten Eindruck. Lege sie unter
  `docs/img/` ab und verlinke sie oben im README.
- Optional: die Schaltplan-PDFs als Bild einbinden.

## Alternative: Push direkt aus dieser App

Sobald du den **GitHub-Connector** in deinen claude.ai-Connector-Einstellungen
autorisiert hast, kann ich Repo-Anlage und Push hier übernehmen. Ohne diese
Autorisierung geht es nur über den Weg oben.
