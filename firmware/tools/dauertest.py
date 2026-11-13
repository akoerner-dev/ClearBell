"""Lautloser Dauertest (SA 10.3, Schritt 11): beide Boards laufen normal, der PC liest nur mit.

Kein Klingeln, kein Türpuls, kein Ton - nur `info` und `net` je Board im festen Takt. Gemessen wird, ob ein Board
neu startet, ob Speicher verloren geht und ob Pakete verloren gehen (Herzschlag-Verkehr: Wiederholungen, verworfene
Pakete, Gegenstelle erreichbar, gesendete Pakete der einen Seite gegen empfangene der anderen).

    python tools/dauertest.py --stunden 7 --takt 300 --ausgabe <Ordner>

Schreibt <Ordner>/dauertest_<Zeit>.csv (eine Zeile je Board und Abfrage) und <Ordner>/dauertest_<Zeit>.log (Rohtext),
am Ende eine Auswertung auf stdout und in die Log-Datei.
"""
import argparse
import csv
import datetime as dt
import pathlib
import re
import subprocess
import sys
import time

HIER = pathlib.Path(__file__).resolve().parent
BOARDS = {"AU-1": "COM4", "IN-1": "COM3", "IN-2": "COM5"}  # IN-2 (OG) seit 12.11.2026

MUSTER = {
    "uptime": r"uptime (\d+) s",
    "boot": r"boot #(\d+)",
    "reset": r"last reset: ([^,]+),",
    "heap_free": r"heap free (\d+) B",
    "heap_min": r"\(min (\d+) B\)",
    "psram_free": r"PSRAM free (\d+) B",
    "coredump": r"core dump stored: (\w+)",
    "selftest": r"selftest (\w+)",
    "rssi": r"RSSI (-?\d+) dBm",
    "disconnects": r"disconnects (\d+)",
    "rx_ok": r"rx ok (\d+)",
    "dropped": r"dropped tag (\d+) addr (\d+) ver (\d+) len (\d+) magic (\d+)",
    "unknown_session": r"unknown session (\d+)",
    "dup": r"dup (\d+)",
    "queue_full": r"queue full (\d+)",
    "tx": r"net: tx (\d+)",
    "retries": r"retries (\d+)",
    "rings_failed": r"rings ok \d+ failed (\d+)",
    "nvs": r"haertung: storage (plain|encrypted[^,]*?)(?: \(key source| \(|,)",  # Härtung Stufe B
}
FELDER = ["zeit", "board"] + list(MUSTER) + ["peer_reachable", "fehler"]
# Welcher Konsolenbefehl welche Felder liefert (für das Wiederholen fehlender Zeilen)
QUELLE = {
    "info": ["uptime", "boot", "reset", "heap_free", "heap_min", "psram_free", "coredump", "selftest"],
    "net": ["rssi", "disconnects", "rx_ok", "dropped", "unknown_session", "dup", "queue_full", "tx", "retries",
            "rings_failed"],
    "haertung": ["nvs"],
}


def abfragen(port, befehl):
    r = subprocess.run([sys.executable, str(HIER / "cb.py"), "konsole", port, befehl, "4"],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=60)
    return r.stdout + r.stderr


def auswerten(text):
    werte = {}
    for name, m in MUSTER.items():
        t = re.search(m, text)
        if not t:
            werte[name] = ""
        elif name == "dropped":
            werte[name] = sum(int(g) for g in t.groups())
        else:
            werte[name] = t.group(1).strip()
    peers = re.findall(r"net: peer (\S+)\s+(\S+)\s+\(\w+\) session \w+, (reachable|unreachable)", text)
    # nur Gegenstellen mit Adresse zählen (früher fehlte OG im Aufbau)
    # dict: bei wiederholter Abfrage jede Gegenstelle nur einmal (der letzte Stand zählt)
    werte["peer_reachable"] = ";".join(f"{n}={z}" for n, z in {n: z for n, ip, z in peers if ip != "0.0.0.0"}.items())
    return werte


def zahl(z, k):
    try:
        return int(z[k])
    except (KeyError, ValueError, TypeError):
        return None


def zusammenfassung(zeilen):
    aus = []
    for board in BOARDS:
        z = [r for r in zeilen if r["board"] == board]
        ok = [r for r in z if not r["fehler"]]
        aus.append(f"== {board}: {len(z)} Abfragen, davon {len(ok)} lesbar")
        if len(ok) < 2:
            aus.append("   zu wenig Daten")
            continue
        a, e = ok[0], ok[-1]
        boots = {r["boot"] for r in ok}
        ups = [zahl(r, "uptime") for r in ok]
        monoton = all(x is not None and y is not None and y > x for x, y in zip(ups, ups[1:]))
        aus.append(f"   Neustart: {'keiner' if len(boots) == 1 and monoton else 'JA'} "
                   f"(Start #{sorted(boots)}, Laufzeit {ups[0]} -> {ups[-1]} s)")
        aus.append(f"   Coredump: {sorted({r['coredump'] for r in ok})}, Selbsttest: {sorted({r['selftest'] for r in ok})}")
        hf = [x for x in (zahl(r, "heap_free") for r in ok) if x is not None]
        if hf:
            aus.append(f"   Heap frei: Anfang {hf[0]}, Ende {hf[-1]}, kleinster Abfragewert {min(hf)}, "
                       f"Firmware-Minimum Ende {e['heap_min']}; Differenz Ende-Anfang {hf[-1] - hf[0]} B "
                       f"({len(hf)} Werte)")
        for k in ("disconnects", "retries", "dropped", "unknown_session", "dup", "queue_full", "rings_failed"):
            va, ve = zahl(a, k), zahl(e, k)
            aus.append(f"   {k}: {a[k]} -> {e[k]} (Zuwachs {ve - va if None not in (va, ve) else '?'})")
        unerreichbar = [r["zeit"] for r in ok if "unreachable" in r["peer_reachable"]]
        aus.append(f"   Gegenstelle unerreichbar bei {len(unerreichbar)} von {len(ok)} Abfragen"
                   + (f": {unerreichbar[:5]}" if unerreichbar else ""))
        rs = [x for x in (zahl(r, "rssi") for r in ok) if x is not None]
        aus.append(f"   RSSI: {min(rs)} .. {max(rs)} dBm" if rs else "   RSSI: keine Werte")
        aus.append(f"   Speicher (Härtung Stufe B): {sorted({r['nvs'] for r in ok})}")
    # Pakete: gesendet von der einen Seite gegen empfangen auf der anderen (gleiche Zeitspanne, grob)
    def delta(board, k):
        ok = [r for r in zeilen if r["board"] == board and not r["fehler"]]
        va, ve = (zahl(ok[0], k), zahl(ok[-1], k)) if len(ok) >= 2 else (None, None)
        return ve - va if None not in (va, ve) else None
    aus.append(f"== Pakete AU-1 tx {delta('AU-1', 'tx')} / IN-1 + IN-2 rx ok {delta('IN-1', 'rx_ok')} + "
               f"{delta('IN-2', 'rx_ok')}; IN-1 + IN-2 tx {delta('IN-1', 'tx')} + {delta('IN-2', 'tx')} / AU-1 rx ok "
               f"{delta('AU-1', 'rx_ok')} (grob: Abfragezeitpunkte der Boards liegen einige Sekunden auseinander)")
    return "\n".join(aus)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--stunden", type=float, default=7.0)
    p.add_argument("--takt", type=int, default=300, help="Sekunden zwischen den Abfragen")
    p.add_argument("--ausgabe", required=True)
    arg = p.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    if sys.platform == "win32":  # PC nicht einschlafen lassen, solange dieser Prozess läuft (keine Einstellung geändert)
        import ctypes
        ctypes.windll.kernel32.SetThreadExecutionState(0x80000000 | 0x00000001)  # ES_CONTINUOUS | ES_SYSTEM_REQUIRED

    ordner = pathlib.Path(arg.ausgabe)
    ordner.mkdir(parents=True, exist_ok=True)
    stempel = dt.datetime.now().strftime("%Y%m%d_%H%M")
    csv_pfad = ordner / f"dauertest_{stempel}.csv"
    log_pfad = ordner / f"dauertest_{stempel}.log"
    ende = time.monotonic() + arg.stunden * 3600
    zeilen = []
    print(f"Dauertest {arg.stunden} h, Takt {arg.takt} s -> {csv_pfad}", flush=True)

    with open(csv_pfad, "w", newline="", encoding="utf-8") as fc, open(log_pfad, "w", encoding="utf-8") as fl:
        w = csv.DictWriter(fc, fieldnames=FELDER, delimiter=";")
        w.writeheader()
        while True:
            runde = time.monotonic()
            for board, port in BOARDS.items():
                jetzt = dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                try:
                    text = abfragen(port, "info") + abfragen(port, "net") + abfragen(port, "haertung")
                    werte = auswerten(text)
                    # Das Logging der Firmware verwirft Zeilen, wenn viele auf einmal kommen (blockiert nie) -
                    # fehlt eine Zeile, die Abfrage bis zu zweimal wiederholen statt "unvollstaendig" zu melden.
                    for befehl, felder in QUELLE.items():
                        for _ in range(3):
                            if all(werte[f] != "" for f in felder):
                                break
                            text += abfragen(port, befehl)
                            werte = auswerten(text)
                    werte["fehler"] = "" if werte["uptime"] and werte["tx"] else "unvollstaendig"
                except Exception as ex:  # Port weg, Zeitüberschreitung ...
                    text, werte = repr(ex), {k: "" for k in MUSTER} | {"peer_reachable": "", "fehler": repr(ex)}
                werte |= {"zeit": jetzt, "board": board}
                zeilen.append(werte)
                w.writerow(werte)
                fl.write(f"--- {jetzt} {board}\n{text}\n")
                fc.flush()
                fl.flush()
                print(f"{jetzt} {board} up {werte['uptime']} s boot #{werte['boot']} heap {werte['heap_free']} "
                      f"retries {werte['retries']} {werte['peer_reachable']} {werte['fehler']}", flush=True)
            if time.monotonic() + arg.takt > ende:
                break
            time.sleep(max(0, arg.takt - (time.monotonic() - runde)))
        z = zusammenfassung(zeilen)
        fl.write("\n=== Auswertung\n" + z + "\n")
    print("\n=== Auswertung\n" + z, flush=True)


if __name__ == "__main__":
    main()
