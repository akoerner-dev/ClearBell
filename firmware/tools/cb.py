"""ClearBell V0.2 - one entry point for building, flashing, testing and the console.

    python tools/cb.py test                        PC unit tests (g++), no board needed
    python tools/cb.py bauen aussen|innen|alle     build the firmware image(s) with ESP-IDF
    python tools/cb.py bauen innen-hbtest          TEST build Härtung Stufe B (software NVS key, emulated eFuses;
                                                   flashen innen-hbtest COMx - never on a unit in real use)
    python tools/cb.py bauen innen-hbemu           TEST build: emulated eFuses only (try the burn command)
    python tools/cb.py flashen aussen COM4         build if needed, then flash over USB
    python tools/cb.py toene COM4                  generate the tone files and flash them (LittleFS)
    python tools/cb.py konsole COM4                read the console for 15 s
    python tools/cb.py konsole COM4 "info" 5       send a command, read 5 s
    python tools/cb.py konsole COM4 - 30           read 30 s without sending ("" works too)
    python tools/cb.py konsole COM3 "a | @2 | b" 8 command sequence in one connection (@2 = wait 2 s)

Factory step (once per unit, the only step with a PC - details in tools/werk.py):
    python tools/cb.py werk aussen COM4 [--name AU-1]          firmware + tones + device code, writes the label
    python tools/cb.py etiketten [<number> ...] [--ab N] [--rahmen]  A4 print sheet (PDF) for Avery Zweckform 3657;
                                                                default all units, from field N; --rahmen = test frames
    python tools/cb.py einrichten IN-1 COM3                     DEV: WiFi/flat from the installation file via USB

Development provisioning (old schema-1 way, keeps working via migration; details in tools/provisionieren.py):
    python tools/cb.py anlage-anlegen --wlan-aus <secrets.h>    once per installation
    python tools/cb.py anlage-einheit AU-1 aussen               register a unit (aussen | eg | og)
    python tools/cb.py anlage-zeigen                            list units (no secrets)
    python tools/cb.py provisionieren AU-1 COM4                 write the unit's factory data

Status page (port step 8, details in tools/statusseite.py; --ip <address> if .local does not resolve):
    python tools/cb.py status AU-1 [--json]                     what the unit's status page shows
    python tools/cb.py ton-hochladen IN-1 ring datei.wav        replace a tone (unit in maintenance mode)
    python tools/cb.py anlage-zeigen --passwort IN-1            device password for the browser login
    python tools/cb.py anlage-webpasswort IN-1 [<pw>|label]     record an own status-page password for the tools
                                                                (without <pw>: asks hidden, then tests one login)
    python tools/cb.py update IN-1 [datei.bin] [--aelter]       firmware update over the status page (maintenance);
                                                                default: the signed own build; --aelter allows an
                                                                older version (same as the box on the page)

Signed updates (port step 10d, R26, details in tools/signatur.py):
    python tools/cb.py schluessel-erzeugen                      ONCE: manufacturer main + spare key
    python tools/cb.py signieren aussen|innen|alle [--ersatz]   sign the built image(s) (bauen does it by itself)
    python tools/cb.py besitzer-schluessel <key.pem>            key digest of an own key (buyer, status page)
    python tools/cb.py signatur-testvektor                      regenerate the self-test vector (rarely)

Remote opening (R27, details in tools/fern.py) - the PC plays a phone:
    python tools/cb.py fern-handy-web IN-1 PC2                  add phone PC2 over the indoor page (maintenance)
    python tools/cb.py fern-test PC2 [s] [--fremd|--replay|--nochmal]  wait for a ring push, press "Tür öffnen"
    (anlage-fern / anlage-handy are left-overs of the old schema-1 way and have no effect on R27 firmware)

Run from anywhere; paths are relative to this file. ESP-IDF is expected in C:\\Espressif\\esp-idf-v5.5.5
(override with the environment variable CB_IDF_PATH). The script loads the IDF environment itself, a
special "ESP-IDF PowerShell" is not needed.

Console logs are appended to firmware/build/konsole/<port>_<date>.log (not committed: they may contain
MAC and IP addresses). Opening the port does NOT reset the board (DTR/RTS stay off).

Known trap: a running serial monitor (Arduino IDE "serial-monitor" process, another konsole call)
holds the port; flashing then fails with "Zugriff verweigert" / "Access is denied".
"""

import datetime
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace", line_buffering=True)

FIRMWARE = Path(__file__).resolve().parents[1]
IDF_PATH = Path(os.environ.get("CB_IDF_PATH", r"C:\Espressif\esp-idf-v5.5.5"))
# Build output lives OUTSIDE OneDrive: thousands of object files would be synced, and OneDrive's file
# locks during upload make builds fail at random. Override with CB_BUILD_DIR.
BUILD_ROOT = Path(os.environ.get("CB_BUILD_DIR", Path.home() / "clearbell-build"))
UNITS = ("aussen", "innen")
HB_TEST_UNITS = ("innen-hbtest", "innen-hbemu")  # Härtung Stufe B test builds, docs/Haertung_Stufe_B.md


def ensure_managed_components_outside_onedrive() -> None:
    """The IDF component manager always downloads into <project>/managed_components (≈ 600 MB for the
    Arduino core) - its location cannot be configured. To keep that out of OneDrive, the folder is a
    junction to BUILD_ROOT/managed_components. Created here on first use; an existing real folder is moved."""
    link = FIRMWARE / "managed_components"
    target = BUILD_ROOT / "managed_components"
    target.mkdir(parents=True, exist_ok=True)
    if link.exists() and not link.is_junction():
        for item in link.iterdir():
            shutil.move(str(item), str(target / item.name))
        link.rmdir()
    if not link.exists():
        subprocess.check_call(["cmd", "/c", "mklink", "/J", str(link), str(target)], stdout=subprocess.DEVNULL)


def idf(unit: str, *args: str) -> int:
    """Runs idf.py for one unit type inside a PowerShell that has the IDF environment loaded.
    unit "innen-hbtest" = test build of Härtung Stufe B (own build dir, software NVS key, emulated eFuses);
    unit "innen-hbemu"  = the same without the software key: tries the burn command against the emulated eFuses."""
    ensure_managed_components_outside_onedrive()
    build = BUILD_ROOT / unit
    test = unit in HB_TEST_UNITS
    testkey = unit == "innen-hbtest"
    if test:
        unit = "innen"
    defaults = f"sdkconfig.defaults;sdkconfig.defaults.{unit}" + (";sdkconfig.defaults.hbtest" if test else "")
    # Each unit has its own build dir AND its own generated sdkconfig, so switching between the two
    # images never mixes configurations.
    # Note: no $ErrorActionPreference='Stop' - Windows PowerShell 5.1 would treat every stderr line of a
    # native program (export.ps1 prints "Activating ESP-IDF" there) as a fatal error.
    cmd = (
        f". '{IDF_PATH / 'export.ps1'}' *> $null; "
        f"Set-Location '{FIRMWARE}'; "
        f"idf.py -B '{build}' -D SDKCONFIG='{build / 'sdkconfig'}' -D SDKCONFIG_DEFAULTS='{defaults}' "
        f"-D CB_UNIT_NAME={unit} "
        + (f"-D CB_HB_TESTKEY={1 if testkey else 0} " if test else "")
        + " ".join(args)
        + "; exit $LASTEXITCODE"
    )
    return subprocess.call(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", cmd])


def cmd_test() -> int:
    out = FIRMWARE / "build" / "host"
    out.mkdir(parents=True, exist_ok=True)
    sources = sorted(str(p) for p in (FIRMWARE / "test" / "host").glob("*.cpp"))
    sources += sorted(str(p) for p in (FIRMWARE / "components" / "cb_core" / "src").glob("*.cpp"))
    exe = out / "cb_tests.exe"
    gpp = shutil.which("g++")
    if not gpp:
        print("g++ not found (PC compiler for the unit tests).")
        return 2
    flags = ["-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined"]
    inc = ["-I", str(FIRMWARE / "components" / "cb_core" / "include"), "-I", str(FIRMWARE / "test" / "host")]
    r = subprocess.call([gpp, *flags, *inc, *sources, "-o", str(exe)], stderr=subprocess.DEVNULL)
    if r != 0:
        # MinGW often ships without the UBSan runtime; retry without sanitizer rather than not testing.
        # (Real compile errors show up in this second run, whose output is not suppressed.)
        flags.remove("-fsanitize=undefined")
        r = subprocess.call([gpp, *flags, *inc, *sources, "-o", str(exe)])
        if r != 0:
            return r
    return subprocess.call([str(exe)])


def cmd_bauen(which: str) -> int:
    sys.path.insert(0, str(Path(__file__).parent))
    import signatur

    for unit in UNITS if which == "alle" else (which,):
        print(f"=== bauen {unit} ===")
        r = idf(unit, "build")
        if r != 0:
            return r
        # Every build also yields the signed update file - as long as the main key is on this PC. USB flashing does
        # not need a signature (only uploads are checked, R26), so a missing key is a warning, not an error.
        # Test builds of Härtung Stufe B carry a PUBLIC test key or emulated eFuses: they must never get a valid
        # manufacturer signature (security catalogue RV-18) - flash them over USB only.
        if unit in HB_TEST_UNITS:
            print(f"{unit}: test build - deliberately NOT signed (USB only, RV-18)")
        elif signatur.MAIN.exists():
            r = signatur.signieren(unit, idf_python(), BUILD_ROOT)
            if r != 0:
                return r
        else:
            print(f"note: main signing key not on this PC ({signatur.MAIN}) - image NOT signed, updates will refuse it")
    return 0


def cmd_flashen(unit: str, port: str) -> int:
    return idf(unit, "-p", port, "flash")


def cmd_konsole(port: str, command: str | None, seconds: float) -> int:
    import serial  # pyserial

    log_dir = FIRMWARE / "build" / "konsole"
    log_dir.mkdir(parents=True, exist_ok=True)
    log = log_dir / f"{port}_{datetime.date.today()}.log"
    # RV-20: commands with secret arguments are logged masked, known secret values are blacked out (log and screen)
    sys.path.insert(0, str(Path(__file__).parent))
    import geheim
    secret_values, _ = geheim.werte()
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.2
    s.dtr = False
    s.rts = False
    s.open()
    with open(log, "a", encoding="utf-8") as f:
        f.write(f"--- {datetime.datetime.now():%H:%M:%S} konsole {geheim.maskiere_befehl(command or '')}\n")
        # A command may be a sequence: "touch-sim 400 400 | @2 | door" = send, wait 2 s, send - all in ONE
        # connection, so the reader keeps the port while commands go out.
        steps = [c.strip() for c in command.split("|")] if command else []
        end = time.monotonic() + seconds
        next_at = time.monotonic()
        buf = b""
        while time.monotonic() < end:
            while steps and time.monotonic() >= next_at:
                step = steps.pop(0)
                if step.startswith("@"):
                    next_at = time.monotonic() + float(step[1:])
                elif step:
                    s.write((step + "\n").encode())
            buf += s.read(512)
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text, _ = geheim.schwaerze(line.decode(errors="replace").rstrip(), secret_values)
                print(text)
                f.write(f"{datetime.datetime.now():%H:%M:%S} {text}\n")
    s.close()
    return 0


LITTLEFS_OFFSET = 0xC90000  # must match partitions.csv
LITTLEFS_SIZE = 0x350000


def cmd_toene(port: str | None) -> int:
    """Generates the tone files, packs them into a LittleFS image and (with a port) flashes it."""
    out = BUILD_ROOT / "toene"
    files = out / "dateien"
    r = subprocess.call([sys.executable, str(FIRMWARE / "tools" / "toene" / "erzeuge_toene.py"), str(files)])
    if r != 0:
        return r
    # Factory copies: the factory reset restores the tones from /werk/ (cb_audio_restore_factory_tones).
    werk = files / "werk"
    werk.mkdir(exist_ok=True)
    for wav in files.glob("*.wav"):
        shutil.copyfile(wav, werk / wav.name)
    tools = Path(os.environ["LOCALAPPDATA"]) / "Arduino15" / "packages" / "esp32" / "tools" / "mklittlefs"
    exes = sorted(tools.glob("*/mklittlefs.exe"))
    if not exes:
        print(f"mklittlefs not found below {tools}")
        return 2
    image = out / "littlefs.bin"
    # Block 4096 and page 256 are what the ESP32 LittleFS driver uses by default.
    r = subprocess.call([str(exes[-1]), "-c", str(files), "-b", "4096", "-p", "256", "-s", str(LITTLEFS_SIZE),
                         str(image)], stdout=subprocess.DEVNULL)
    if r != 0 or not port:
        print(f"image: {image}" if r == 0 else "mklittlefs failed")
        return r
    print(f"flashing tone image to {port} at {hex(LITTLEFS_OFFSET)} (settings, keys and firmware stay untouched)")
    return subprocess.call([str(idf_python()), "-m", "esptool", "--chip", "esp32s3", "-p", port, "write_flash",
                            hex(LITTLEFS_OFFSET), str(image)])


def idf_python() -> Path:
    """The Python of the IDF environment (has esptool and the NVS generator's dependencies)."""
    envs = sorted((Path.home() / ".espressif" / "python_env").glob("idf5.5_*_env"))
    if not envs:
        raise SystemExit("IDF Python environment not found - run install.ps1 of ESP-IDF first")
    return envs[-1] / "Scripts" / "python.exe"


def main(argv: list[str]) -> int:
    if len(argv) < 2 or argv[1] in ("-h", "--help", "hilfe"):
        print(__doc__)
        return 0
    action = argv[1]
    if action.startswith("anlage-") or action in ("provisionieren", "einrichten"):
        sys.path.insert(0, str(Path(__file__).parent))
        import provisionieren as prov

        if action == "anlage-anlegen":
            opts = dict(zip(argv[2::2], argv[3::2]))
            return prov.anlage_anlegen(opts.get("--wlan-aus"), opts.get("--ssid"), opts.get("--passwort"))
        if action == "anlage-einheit" and len(argv) == 4:
            return prov.anlage_einheit(argv[2], argv[3])
        if action == "anlage-zeigen" and len(argv) == 4 and argv[2] == "--passwort":
            import statusseite

            return statusseite.passwort(argv[3])
        if action == "anlage-zeigen":
            return prov.anlage_zeigen()
        if action == "anlage-webpasswort" and len(argv) in (3, 4):
            # Records the own status-page password of a unit (set on the page / console "web-pass") so the PC tools
            # log in with it; "label" removes it again (label password). Without a password it asks for it hidden
            # (no placeholder on the command line, nothing in the shell history), then tries one login.
            data = prov._load()
            if argv[2] not in data["einheiten"]:
                raise SystemExit(f"unit {argv[2]} not in the installation file")
            if len(argv) == 4:
                pw = argv[3]
            else:
                import getpass
                pw = getpass.getpass(f"own status-page password of {argv[2]} (empty = label password): ")
                if pw and pw != getpass.getpass("again: "):
                    raise SystemExit("the two entries differ - nothing changed")
                pw = pw or "label"
            if pw == "label":
                data["einheiten"][argv[2]].pop("webpasswort", None)
            else:
                data["einheiten"][argv[2]]["webpasswort"] = pw
            prov._save(data)
            print(f"{argv[2]}: tools now log in with the {'label' if pw == 'label' else 'own'} password - checking:")
            import statusseite
            return statusseite.status(argv[2], None, False)
        if action == "anlage-fern":
            link = argv[argv.index("--link") + 1] if "--link" in argv[:-1] else None
            return prov.anlage_fern("--test" in argv, link, "--echt" in argv)
        if action in ("anlage-handy", "anlage-handy-sperren") and len(argv) == 3:
            return prov.anlage_handy(argv[2], action == "anlage-handy-sperren")
        if action == "einrichten" and len(argv) == 4:  # development: setup page values over USB
            return prov.einrichten(argv[2], argv[3])
        if action == "provisionieren" and len(argv) == 4:
            return prov.provisionieren(argv[2], argv[3], idf_python(), IDF_PATH)
    if action in ("fern-test", "fern-handy-web"):
        sys.path.insert(0, str(Path(__file__).parent))
        import fern

        return fern.main(argv[1:])
    if action in ("status", "ton-hochladen", "update"):
        sys.path.insert(0, str(Path(__file__).parent))
        import statusseite

        args = list(argv[2:])
        ip = None
        if "--ip" in args:
            i = args.index("--ip")
            ip = args[i + 1] if i + 1 < len(args) else None
            del args[i:i + 2]
        if action == "status" and args:
            return statusseite.status(args[0], ip, "--json" in args)
        if action == "ton-hochladen" and len(args) == 3:
            return statusseite.ton_hochladen(args[0], args[1], args[2], ip)
        if action == "update" and args:
            older = "--aelter" in args
            args = [a for a in args if a != "--aelter"]
            if len(args) > 1:
                image = args[1]
            else:  # default: the signed image built for the unit's type
                import provisionieren as prov
                rolle = prov._load()["einheiten"][args[0]]["rolle"]
                image = str(BUILD_ROOT / ("aussen" if rolle == "aussen" else "innen") / "clearbell-signiert.bin")
            return statusseite.update(args[0], image, ip, older)
    if action in ("schluessel-erzeugen", "signieren", "besitzer-schluessel", "signatur-testvektor"):
        sys.path.insert(0, str(Path(__file__).parent))
        import signatur

        if action == "schluessel-erzeugen":
            return signatur.schluessel_erzeugen(idf_python())
        if action == "signatur-testvektor":
            return signatur.testvektor(idf_python())
        if action == "besitzer-schluessel" and len(argv) == 3:
            return signatur.besitzer_schluessel(idf_python(), argv[2])
        if action == "signieren" and len(argv) >= 3 and argv[2] in (*UNITS, "alle"):
            for unit in UNITS if argv[2] == "alle" else (argv[2],):
                r = signatur.signieren(unit, idf_python(), BUILD_ROOT, "--ersatz" in argv)
                if r != 0:
                    return r
            return 0
    if action == "werk" and len(argv) >= 4:
        sys.path.insert(0, str(Path(__file__).parent))
        import werk

        name = argv[argv.index("--name") + 1] if "--name" in argv[:-1] else None
        return werk.werk(argv[2], argv[3], name, idf_python(), IDF_PATH, cmd_flashen, cmd_toene)
    if action == "etiketten":
        sys.path.insert(0, str(Path(__file__).parent))
        import werk

        args = argv[2:]
        start = 1
        if "--ab" in args:
            i = args.index("--ab")
            start = int(args[i + 1])
            del args[i:i + 2]
        rahmen = "--rahmen" in args
        return werk.etiketten([a for a in args if a != "--rahmen"], start, rahmen)
    if action == "test":
        return cmd_test()
    if action == "toene":
        return cmd_toene(argv[2] if len(argv) > 2 else None)
    if action == "bauen" and len(argv) == 3 and argv[2] in (*UNITS, "alle", *HB_TEST_UNITS):
        return cmd_bauen(argv[2])
    if action == "flashen" and len(argv) == 4 and argv[2] in (*UNITS, *HB_TEST_UNITS):
        return cmd_flashen(argv[2], argv[3])
    if action == "konsole" and len(argv) >= 3:
        # "" or "-" = only read. ("-" exists because PowerShell's Start-Job drops empty arguments.)
        command = argv[3] if len(argv) > 3 and argv[3] not in ("", "-") else None
        seconds = float(argv[4]) if len(argv) > 4 else (5 if command else 15)
        return cmd_konsole(argv[2], command, seconds)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
