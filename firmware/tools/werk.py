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
  5. with --name and a development installation file: enters unit number and device code for that unit there
     (for the PC tools; a unit added later with `anlage-einheit` gets both from the factory register)
  6. checks that the firmware starts (console "info"); exit code 2 with a clear hint if the board needs replugging

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
    if name:
        anlage_nachtragen(name, serial, code)
    return 0 if check_start(port, idf_python) else 2


def anlage_nachtragen(name: str, serial: str, code: str) -> None:
    """Development installation: the PC tools (status, update) need the unit number and the label's device code.
    Units not yet in the installation file get both later from `anlage-einheit` (it reads the factory register)."""
    if not prov.ANLAGE.exists():
        return
    data = prov._load()
    u = data["einheiten"].get(name)
    if not u:
        print(f"{name} is not in the installation file yet: cb.py anlage-einheit {name} aussen|eg|og "
              "(takes unit number and device code from the factory register)")
        return
    u["seriennummer"], u["geraetepasswort"] = serial, code
    prov._save(data)
    extra = " - it also has an own status-page password recorded, check it still applies" if u.get("webpasswort") else ""
    print(f"installation file: {name} now has unit number {serial} and the new device code{extra}")


def _answers(port: str, seconds: float = 6) -> bool:
    """True if the firmware answers the console command "info" (opening the port does not reset the board)."""
    import time

    import serial  # pyserial

    end = time.time() + seconds + 6  # the USB port may need a few seconds to come back after the reset
    while time.time() < end:
        try:
            s = serial.Serial()
            s.port, s.baudrate, s.timeout = port, 115200, 0.3
            s.dtr = s.rts = False
            s.open()
        except serial.SerialException:
            time.sleep(0.5)
            continue
        try:
            s.write(b"\ninfo\n")
            buf, stop = b"", time.time() + seconds
            while time.time() < stop and b"ClearBell firmware" not in buf:
                buf += s.read(4096)
            return b"ClearBell firmware" in buf
        finally:
            s.close()
    return False


def check_start(port: str, idf_python: Path) -> bool:
    """A brand-new board (IN-2, 12.11.2026) stayed in the ROM download mode after its very first flashing - also
    after another esptool hard reset; only unplugging helped, and it never happened again on the same board.
    So: check that the firmware runs, try the watchdog reset once, otherwise say clearly what to do."""
    if _answers(port):
        print("unit started: firmware answers on the console")
        return True
    print("firmware does not answer - trying a watchdog reset")
    subprocess.run([str(idf_python), "-m", "esptool", "--chip", "esp32s3", "-p", port, "--before", "no_reset",
                    "--after", "watchdog_reset", "read_mac"], capture_output=True)
    if _answers(port):
        print("unit started after the watchdog reset")
        return True
    print("UNIT DID NOT START: unplug USB, plug it in again (LED then blinks red/green = setup mode), then check "
          f"with: cb.py konsole {port} \"info\" 5")
    return False
