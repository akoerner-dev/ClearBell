"""ClearBell - the FACTORY step for one unit (port step 10a). Called through cb.py:

    python tools/cb.py werk aussen COM4 [--name AU-1]

What it does, once per unit, over USB (this is the only step that needs a PC - the buyer never does it):
  1. flashes the firmware of the unit type and the tone image (incl. the factory copies under /werk/)
  2. draws a random DEVICE CODE (16 characters Crockford Base32, ~80 bit) and writes it into the factory
     partition `fctry` (schema 2) - nothing else: no WiFi, no keys, no role
  3. reads the unit number from the MAC and writes the LABEL: unit number, device code, WiFi QR text, into
     <ClearBell>/Geheimnisse/etiketten/<number>.txt (plus .png if the Python package "qrcode" is installed)
  4. notes number -> code in <ClearBell>/Geheimnisse/werk_geraete.json (the factory register; whoever sells the
     units decides whether to keep it - the buyer does not depend on it, the code is on the label and in the box)

Afterwards the unit starts in setup mode with the access point "ClearBell-<number>" (docs/Produktreife.md).
For development boards without a phone at hand: cb.py konsole <port> "setup-wifi <ssid> <pw> | setup-role eg | reboot".
"""

import csv
import json
import re
import secrets
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import provisionieren as prov  # noqa: E402

ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"  # Crockford Base32, same as cb_setup_logic.cpp
GEHEIM = prov.ANLAGE.parent


def new_code() -> str:
    n = int.from_bytes(secrets.token_bytes(10), "big")
    return "".join(ALPHABET[(n >> (5 * (15 - i))) & 31] for i in range(16))


def grouped(code: str) -> str:
    return "-".join(code[i:i + 4] for i in range(0, 16, 4))


def qr_text(ssid: str, password: str) -> str:
    esc = lambda s: re.sub(r'([\\;,:"])', r"\\\1", s)  # noqa: E731
    return f"WIFI:T:WPA;S:{esc(ssid)};P:{esc(password)};;"


def read_serial(port: str, idf_python: Path) -> str:
    out = subprocess.run([str(idf_python), "-m", "esptool", "--chip", "esp32s3", "-p", port, "read_mac"],
                         capture_output=True, text=True).stdout
    m = re.search(r"MAC:\s*([0-9a-f:]{17})", out, re.I)
    if not m:
        raise SystemExit("could not read the MAC (board connected? port free?)")
    return m.group(1).replace(":", "").lower()[-6:]


def werk(unit: str, port: str, name: str | None, idf_python: Path, idf_path: Path, flash_fw, flash_tones) -> int:
    if unit not in ("aussen", "innen"):
        raise SystemExit("unit type: aussen or innen")
    serial = read_serial(port, idf_python)
    print(f"unit number {serial}")
    if flash_fw(unit, port) != 0 or flash_tones(port) != 0:
        print("flashing failed")
        return 1
    code = new_code()
    gen = idf_path / "components" / "nvs_flash" / "nvs_partition_generator" / "nvs_partition_gen.py"
    with tempfile.TemporaryDirectory(prefix="cb_werk_") as tmp:
        csv_path, bin_path = Path(tmp) / "fctry.csv", Path(tmp) / "fctry.bin"
        rows = [["key", "type", "encoding", "value"], ["cb", "namespace", "", ""],
                ["schema", "data", "u8", "2"], ["dev_code", "data", "string", code]]
        if name:
            rows.append(["name", "data", "string", name])
        with open(csv_path, "w", newline="", encoding="utf-8") as f:
            csv.writer(f).writerows(rows)
        if subprocess.call([str(idf_python), str(gen), "generate", str(csv_path), str(bin_path),
                            hex(prov.FCTRY_SIZE)], stdout=subprocess.DEVNULL) != 0:
            print("nvs_partition_gen failed")
            return 1
        if subprocess.call([str(idf_python), "-m", "esptool", "--chip", "esp32s3", "-p", port, "write_flash",
                            hex(prov.FCTRY_OFFSET), str(bin_path)]) != 0:
            return 1
    ssid = f"ClearBell-{serial}"
    labels = GEHEIM / "etiketten"
    labels.mkdir(parents=True, exist_ok=True)
    text = (f"ClearBell {'Außeneinheit' if unit == 'aussen' else 'Inneneinheit'}\n"
            f"Gerätenummer: {serial}\nGerätecode:   {grouped(code)}\n"
            f"Einrichten: QR-Code scannen oder WLAN „{ssid}“ mit dem Gerätecode verbinden.\n"
            f"QR-Inhalt: {qr_text(ssid, code)}\n")
    (labels / f"{serial}.txt").write_text(text, encoding="utf-8")
    try:
        import qrcode  # optional

        qrcode.make(qr_text(ssid, code)).save(labels / f"{serial}.png")
    except ImportError:
        pass
    reg_path = GEHEIM / "werk_geraete.json"
    reg = json.loads(reg_path.read_text(encoding="utf-8")) if reg_path.exists() else {}
    reg[serial] = {"typ": unit, "name": name or "", "geraetecode": code}
    reg_path.write_text(json.dumps(reg, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"factory step done: label {labels / (serial + '.txt')} (device code not shown here)")
    return 0
