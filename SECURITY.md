# Sicherheitsrichtlinie · Security Policy

🇩🇪 [Deutsch](#deutsch) · 🇬🇧 [English](#english)

---

## Deutsch

ClearBell ist ein privates, nicht kommerzielles Projekt: eine Türklingel mit Türöffner für ein Zweifamilienhaus.
Weil die Anlage eine Haustür öffnen kann, sind Hinweise auf Sicherheitslücken ausdrücklich willkommen — auch zum
Entwurf, bevor er gebaut ist.

### Schwachstelle melden

**Bitte nicht als öffentliches Issue, Pull Request oder Diskussion.** Melde vertraulich über GitHub:

1. Den Reiter [**Security**](https://github.com/akoerner-dev/ClearBell/security) dieses Repositorys öffnen
   (je nach Ansicht „Security and quality“).
2. [**Report a vulnerability**](https://github.com/akoerner-dev/ClearBell/security/advisories/new) wählen, das
   Formular ausfüllen und mit **Submit report** absenden.

Die Meldung ist nicht öffentlich; sie sehen nur du und ich. Hilfreich sind:

- betroffene Version und Stelle (Datei, Kapitel der Softwarearchitektur, Bauteil),
- was passiert und unter welchen Voraussetzungen,
- Schritte zum Nachvollziehen,
- die mögliche Auswirkung — im Zweifel: Kann jemand die Tür öffnen, ein Klingeln fälschen oder unterdrücken?

Deutsch oder Englisch, beides ist in Ordnung.

### Was du erwarten kannst

| Schritt | Frist |
|---|---|
| Eingangsbestätigung | innerhalb von **7 Tagen** |
| Statusmeldungen | mindestens **alle 30 Tage**, bis die Meldung erledigt ist |
| Behebung | Ziel **90 Tage** für eine Softwarelösung. Was eine neue Platinenrevision braucht, dauert länger — dann mit Zwischenstand und, wo möglich, einer Übergangsmaßnahme. |

Veröffentlicht wird abgestimmt: Den Zeitpunkt legen wir gemeinsam fest, in der Regel sobald die Lösung verfügbar
ist. Wer möchte, wird im Sicherheitshinweis und in der Änderungshistorie genannt.

### Geltungsbereich

| Version | Stand | Sicherheitskorrekturen |
|---|---|---|
| **V0.2** | aktuelle Generation — Hardware in `hardware/`, Firmware nach der [Softwarearchitektur](docs/Softwarearchitektur_V0.2.md) | ja, auch schon für den Entwurf |
| **V0.1** | archiviert unter `archiv/V0.1/` | nein — ihre bekannten Schwächen stehen in Kapitel 7.4 der Softwarearchitektur und werden mit V0.2 behoben |

Dazu gehören Firmware, Schaltpläne und Platinen sowie das Sicherheitskonzept selbst
([Kapitel 7](docs/Softwarearchitektur_V0.2.md#7-sicherheitskonzept)). Dazu gehören auch Zugangsdaten, die
versehentlich im Repository gelandet sind, auch in der Git-Historie. Bewusst getragene Restrisiken beschreibt
Kapitel 7.5; Hinweise, wie sie sich verkleinern lassen, sind trotzdem willkommen.

**Beim jeweiligen Projekt melden, nicht hier:** Schwachstellen in fremden Komponenten — ESP-IDF, Arduino-ESP32, der
Push-Dienst ntfy. Betrifft eine solche Lücke ClearBell, gern zusätzlich hier.

Diese Richtlinie folgt ETSI EN 303 645 V3.1.3, Bestimmung 5.2-1: Kontaktweg sowie Fristen für Eingangsbestätigung und
Statusmeldungen.

---

## English

ClearBell is a private, non-commercial project: a doorbell with a door opener for a two-family house. Because the
system can open a front door, reports of security vulnerabilities are explicitly welcome — including reports on the
design before it is built.

### Reporting a vulnerability

**Please do not open a public issue, pull request or discussion.** Report privately via GitHub:

1. Open the [**Security**](https://github.com/akoerner-dev/ClearBell/security) tab of this repository (labelled
   “Security and quality” in some views).
2. Choose [**Report a vulnerability**](https://github.com/akoerner-dev/ClearBell/security/advisories/new), fill in the
   form and send it with **Submit report**.

The report is not public; only you and I can see it. Helpful details:

- affected version and location (file, section of the software architecture, component),
- what happens and under which conditions,
- steps to reproduce,
- the possible impact — if in doubt: can someone open the door, forge a ring or suppress one?

English or German, both are fine.

### What to expect

| Step | Timeline |
|---|---|
| Acknowledgement of receipt | within **7 days** |
| Status updates | at least **every 30 days** until the report is resolved |
| Fix | target **90 days** for a software fix. Anything that needs a new board revision takes longer — then with an interim status and, where possible, a mitigation. |

Disclosure is coordinated: we agree on the date together, usually as soon as the fix is available. If you wish, you
will be credited in the security advisory and in the change history.

### Scope

| Version | Status | Security fixes |
|---|---|---|
| **V0.2** | current generation — hardware in `hardware/`, firmware following the [software architecture](docs/Softwarearchitektur_V0.2.en.md) | yes, including for the design |
| **V0.1** | archived in `archiv/V0.1/` | no — its known weaknesses are listed in section 7.4 of the software architecture and are resolved by V0.2 |

In scope are the firmware, the schematics and boards, and the security concept itself
([section 7](docs/Softwarearchitektur_V0.2.en.md#7-security-concept)). This includes credentials that ended up in the
repository by mistake, including in the Git history. Residual risks that are deliberately accepted are described in
section 7.5; suggestions for reducing them are still welcome.

**Report to the respective project, not here:** vulnerabilities in third-party components — ESP-IDF, Arduino-ESP32,
the ntfy push service. If such a vulnerability affects ClearBell, feel free to report it here as well.

This policy follows ETSI EN 303 645 V3.1.3, provision 5.2-1: a contact channel and timelines for acknowledgement of
receipt and status updates.

---

*Stand / as of 27.09.2026*
