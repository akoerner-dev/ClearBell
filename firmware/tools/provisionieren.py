"""ClearBell V0.2 - provisioning: write identity and secrets into a unit's factory partition `fctry`.

Called through cb.py:
    python tools/cb.py anlage-anlegen --wlan-aus <secrets.h>   create the installation file once
    python tools/cb.py anlage-einheit IN-2 og                  add a unit (aussen | eg | og)
    python tools/cb.py anlage-zeigen                           list units, no secrets shown
    python tools/cb.py provisionieren AU-1 COM4                write AU-1's data to the board on COM4

Remote opening (port step 9, Softwarearchitektur 4.5) - only the outdoor unit gets these:
    python tools/cb.py anlage-fern [--test|--echt] [--link <template>]   create ntfy topics / set the app link
    python tools/cb.py anlage-handy H1                          add a phone key (prints how to set up the phone)
    python tools/cb.py anlage-handy-sperren H1                  remove a phone key (lost phone)
After each of them: provisionieren <outdoor unit> again.

THE INSTALLATION FILE holds every secret of one ClearBell installation (WiFi access, the two link keys,
the device passwords). It lives OUTSIDE the repository; default:
    <ClearBell project folder>/Geheimnisse/clearbell_anlage.json
(override with the environment variable CB_ANLAGE). Lose it and you have to re-provision all units with
new keys; publish it and anyone in WiFi range of the house can ring and open the door.

The firmware side that reads this data is main/cb_provisioning.cpp - change both together.
"""

import csv
import json
import os
import re
import secrets
import subprocess
import tempfile
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parents[1]
ANLAGE = Path(os.environ.get("CB_ANLAGE", FIRMWARE.parents[1] / "Geheimnisse" / "clearbell_anlage.json"))

FCTRY_OFFSET = 0xFE0000  # must match partitions.csv
FCTRY_SIZE = 0xF000
SCHEMA = 1               # must match kSchema in cb_provisioning.cpp
ROLES = {"aussen": 1, "eg": 2, "og": 3}


def _load() -> dict:
    if not ANLAGE.exists():
        raise SystemExit(f"Installation file not found: {ANLAGE}\nCreate it with: cb.py anlage-anlegen")
    return json.loads(ANLAGE.read_text(encoding="utf-8"))


def _save(data: dict) -> None:
    ANLAGE.parent.mkdir(parents=True, exist_ok=True)
    ANLAGE.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def _read_secrets_h(path: Path) -> tuple[str, str]:
    """Reads WLAN_SSID and WLAN_PASSWORT from the bring-up sketch's secrets.h (never printed)."""
    text = path.read_text(encoding="utf-8")
    ssid = re.search(r'#define\s+WLAN_SSID\s+"([^"]*)"', text)
    pw = re.search(r'#define\s+WLAN_PASSWORT\s+"([^"]*)"', text)
    if not ssid or not pw:
        raise SystemExit(f"WLAN_SSID / WLAN_PASSWORT not found in {path}")
    return ssid.group(1), pw.group(1)


def anlage_anlegen(wlan_aus: str | None, ssid: str | None, passwort: str | None) -> int:
    if ANLAGE.exists():
        raise SystemExit(f"{ANLAGE} already exists - not overwritten (it holds the keys of all units).")
    if wlan_aus:
        ssid, passwort = _read_secrets_h(Path(wlan_aus))
    if not ssid:
        raise SystemExit("WiFi missing: --wlan-aus <secrets.h> or --ssid <name> --passwort <pw>")
    data = {
        "_hinweis": "ClearBell-Anlagendatei: alle Geheimnisse der Installation. Nie ins Repo, nie teilen.",
        "wlan": {"ssid": ssid, "passwort": passwort or ""},
        # One key per link (Softwarearchitektur 4.2): outdoor<->EG and outdoor<->OG. 256 bit from the OS CSPRNG.
        "schluessel": {"eg": secrets.token_hex(32), "og": secrets.token_hex(32)},
        # Optional fixed IPv4 per role; empty = mDNS name / learned address.
        "ip": {"aussen": "", "eg": "", "og": ""},
        "einheiten": {},
    }
    _save(data)
    print(f"created {ANLAGE} (WiFi '{ssid}', 2 new link keys). Next: cb.py anlage-einheit <NAME> aussen|eg|og")
    return 0


WERK_REGISTER = ANLAGE.parent / "werk_geraete.json"  # written by werk.py: unit number -> type, name, device code


def werk_eintrag(name: str) -> tuple[str, str] | None:
    """(unit number, device code) of the unit that the factory step (werk --name) registered under this name."""
    if not WERK_REGISTER.exists():
        return None
    reg = json.loads(WERK_REGISTER.read_text(encoding="utf-8"))
    hits = [(serial, e["geraetecode"]) for serial, e in reg.items() if e.get("name") == name]
    if len(hits) > 1:
        raise SystemExit(f"{name} is registered for several units in {WERK_REGISTER.name} "
                         f"({', '.join(s for s, _ in hits)}) - fix the register first")
    return hits[0] if hits else None


def anlage_einheit(name: str, rolle: str) -> int:
    if rolle not in ROLES:
        raise SystemExit(f"role must be one of {', '.join(ROLES)}")
    data = _load()
    if name in data["einheiten"]:
        raise SystemExit(f"unit {name} already exists")
    werk = werk_eintrag(name)
    if werk:
        # Factory step already done: the PC tools log in with the label's device code (status page, update).
        serial, code = werk
        data["einheiten"][name] = {"rolle": rolle, "geraetepasswort": code, "seriennummer": serial}
        note = f", unit number {serial} and device code from the factory register"
    else:
        # Old schema-1 way: device password unique per unit (EN 303 645 5.1 - no universal default passwords).
        data["einheiten"][name] = {"rolle": rolle, "geraetepasswort": secrets.token_urlsafe(12)}
        note = " (not in the factory register yet - cb.py werk ... --name " + name + " fills in number and code)"
    _save(data)
    print(f"unit {name} added as {rolle}{note}")
    return 0


def anlage_zeigen() -> int:
    data = _load()
    print(f"installation file: {ANLAGE}")
    print(f"WiFi: '{data['wlan']['ssid']}', keys: {', '.join(data['schluessel'])}")
    for name, u in data["einheiten"].items():
        print(f"  {name:8} {u['rolle']}")
    if "fern" in data:
        f = data["fern"]
        test = " (TEST topics)" if f["topic_aus"].startswith("cbtest-") else ""
        print(f"remote opening{test}: phones {', '.join(f['handys']) or 'none'}; link {f['link_vorlage']}")
    return 0


LINK_DEFAULT = "http-shortcuts://SHORTCUT-ID?token={token}"


def _topic(prefix: str) -> str:
    # ntfy topics: [-_A-Za-z0-9]{1,64}. 128 bit random - nobody guesses them, although they are not
    # secret in the strict sense (the phone signature protects the door, SA 4.5).
    return prefix + secrets.token_urlsafe(16)


def anlage_fern(test: bool, link: str | None, echt: bool = False) -> int:
    data = _load()
    fern = data.get("fern")
    if fern and link is None and not test and not echt:
        print("remote section exists already: --link <template> changes the app link, --test makes new test topics,")
        print("--echt makes new final topics (phones must then subscribe to the new push topic)")
        return 1
    if not fern or test or echt:
        prefix = "cbtest-" if test else "cb-"
        fern = {"topic_aus": _topic(prefix), "topic_ein": _topic(prefix), "link_vorlage": LINK_DEFAULT,
                "handys": (fern or {}).get("handys", {})}
    if link is not None:
        if "{token}" not in link:
            raise SystemExit("the link template needs the placeholder {token}")
        fern["link_vorlage"] = link
    data["fern"] = fern
    _save(data)
    print(f"remote opening: push topic and command topic set{' (TEST topics)' if test else ''}, "
          f"link template: {fern['link_vorlage']}")
    print("next: cb.py anlage-handy <ID>, then provisionieren <outdoor unit> <port>")
    return 0


def anlage_handy(phone: str, remove: bool) -> int:
    data = _load()
    if "fern" not in data:
        raise SystemExit("no remote section - first: cb.py anlage-fern")
    handys = data["fern"]["handys"]
    if not re.fullmatch(r"[A-Za-z0-9-]{1,8}", phone):
        raise SystemExit("phone id: 1-8 characters A-Z a-z 0-9 -")
    if remove:
        if handys.pop(phone, None) is None:
            raise SystemExit(f"phone {phone} not registered")
        _save(data)
        print(f"phone {phone} removed - provision the outdoor unit again, then its signatures are refused")
        return 0
    if phone in handys:
        raise SystemExit(f"phone {phone} exists already")
    if len(handys) >= 4:
        raise SystemExit("at most 4 phones (firmware limit kRemotePhones)")
    handys[phone] = secrets.token_hex(32)
    _save(data)
    print(f"phone {phone} added. Set it up with: cb.py fern-handy-anleitung {phone}  (shows the key - only on")
    print("your own screen), then provisionieren <outdoor unit> <port>")
    return 0


def _csv_rows(data: dict, name: str) -> list[list[str]]:
    unit = data["einheiten"][name]
    role = unit["rolle"]
    rows = [
        ["key", "type", "encoding", "value"],
        ["cb", "namespace", "", ""],
        ["schema", "data", "u8", str(SCHEMA)],
        ["role", "data", "u8", str(ROLES[role])],
        ["name", "data", "string", name],
        ["wifi_ssid", "data", "string", data["wlan"]["ssid"]],
        ["wifi_pass", "data", "string", data["wlan"]["passwort"]],
        ["dev_pass", "data", "string", unit["geraetepasswort"]],
    ]
    keys = data["schluessel"]
    if role == "aussen":
        rows += [["key_eg", "data", "hex2bin", keys["eg"]], ["key_og", "data", "hex2bin", keys["og"]]]
        fern = data.get("fern")
        if fern:  # remote opening (SA 4.5): optional, without it the unit simply sends no push
            rows += [["topic_out", "data", "string", fern["topic_aus"]],
                     ["topic_in", "data", "string", fern["topic_ein"]],
                     ["fern_link", "data", "string", fern["link_vorlage"]]]
            for i, (pid, key) in enumerate(sorted(fern["handys"].items())):
                rows += [[f"pid{i}", "data", "string", pid], [f"pk{i}", "data", "string", key]]
    else:
        rows += [["key_au", "data", "hex2bin", keys[role]]]  # an indoor unit knows only its own link
    for r, nvs_key in (("aussen", "ip_au"), ("eg", "ip_eg"), ("og", "ip_og")):
        if data["ip"].get(r):
            rows.append([nvs_key, "data", "string", data["ip"][r]])
    return rows


def provisionieren(name: str, port: str, idf_python: Path, idf_path: Path) -> int:
    data = _load()
    if name not in data["einheiten"]:
        raise SystemExit(f"unit {name} unknown - add it with: cb.py anlage-einheit {name} aussen|eg|og")
    gen = idf_path / "components" / "nvs_flash" / "nvs_partition_generator" / "nvs_partition_gen.py"
    # The CSV and the binary contain the secrets: build them in a private temp dir and always delete.
    with tempfile.TemporaryDirectory(prefix="cb_prov_") as tmp:
        csv_path, bin_path = Path(tmp) / "fctry.csv", Path(tmp) / "fctry.bin"
        with open(csv_path, "w", newline="", encoding="utf-8") as f:
            csv.writer(f).writerows(_csv_rows(data, name))
        r = subprocess.call([str(idf_python), str(gen), "generate", str(csv_path), str(bin_path), hex(FCTRY_SIZE)],
                            stdout=subprocess.DEVNULL)
        if r != 0:
            print("nvs_partition_gen failed")
            return r
        print(f"writing factory data of {name} ({data['einheiten'][name]['rolle']}) to {port} at {hex(FCTRY_OFFSET)}")
        return subprocess.call([str(idf_python), "-m", "esptool", "--chip", "esp32s3", "-p", port,
                                "write_flash", hex(FCTRY_OFFSET), str(bin_path)])


def einrichten(name: str, port: str) -> int:
    """DEVELOPMENT shortcut for boards without a phone: sends WiFi (and the flat) from the installation file over
    the USB console - what a buyer does on the setup page. Values are never printed."""
    import serial  # pyserial
    import time

    data = _load()
    unit = data["einheiten"].get(name)
    if not unit:
        raise SystemExit(f"unit {name} unknown")
    cmds = [f"setup-wifi {data['wlan']['ssid']} {data['wlan']['passwort']}"]
    if unit["rolle"] in ("eg", "og"):
        cmds.append(f"setup-role {unit['rolle']}")
    cmds.append("reboot")
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.5
    s.dtr = s.rts = False
    s.open()
    for c in cmds:
        s.write((c + "\n").encode())
        time.sleep(0.5)
        print(s.read(2000).decode(errors="replace").strip().splitlines()[-1:] or "")
    s.close()
    print(f"{name}: WiFi{' and flat' if len(cmds) > 2 else ''} written, rebooting")
    return 0
