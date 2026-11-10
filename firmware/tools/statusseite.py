"""ClearBell V0.2 - the status page from the PC (port step 8). Called through cb.py:

    python tools/cb.py status AU-1                  summary of the unit's status page
    python tools/cb.py status IN-1 --json           the raw /status.json
    python tools/cb.py ton-hochladen IN-1 ring datei.wav   replace a tone (unit must be in maintenance mode)
    python tools/cb.py anlage-zeigen --passwort IN-1      show the device password (for the browser login)
    python tools/cb.py update IN-1 [datei.bin] [--aelter] firmware update (maintenance; default: own signed build)

Every command takes --ip <address> if the .local name does not resolve (the IP is in the console: "net").

Login: user "clearbell", password = the unit's device password from the installation file (never in the
repository). HTTP Digest (Python's urllib does it out of the box), so the password does not cross the WiFi.
"""

import json
import sys
import urllib.error
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import provisionieren as prov  # noqa: E402  (installation file access)

USER = "clearbell"
HOSTS = {"aussen": "clearbell-aussen.local", "eg": "clearbell-eg.local", "og": "clearbell-og.local"}


def _unit(name: str) -> dict:
    data = prov._load()
    if name not in data["einheiten"]:
        raise SystemExit(f"unit {name} not in the installation file ({', '.join(data['einheiten'])})")
    return data["einheiten"][name]


def _opener(base: str, password: str) -> urllib.request.OpenerDirector:
    mgr = urllib.request.HTTPPasswordMgrWithDefaultRealm()
    mgr.add_password(None, base, USER, password)
    return urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(mgr))


def _host(u: dict) -> str:
    # Since step 10a every unit is clearbell-<unit number>.local; the number is in the installation file
    # ("seriennummer", from the label / console "info" MAC). Old fixed names only as fallback.
    return f"clearbell-{u['seriennummer']}.local" if u.get("seriennummer") else HOSTS[u["rolle"]]


def _password(u: dict) -> str:
    # Own status-page password (set on the page or with the console "web-pass"), recorded in the installation file
    # as "webpasswort" by `cb.py anlage-webpasswort`; otherwise the label password.
    return u.get("webpasswort") or u["geraetepasswort"]


def _base(name: str, ip: str | None) -> tuple[str, str]:
    u = _unit(name)
    return f"http://{ip or _host(u)}", _password(u)


def fetch_status(name: str, ip: str | None = None) -> dict:
    base, pw = _base(name, ip)
    with _opener(base, pw).open(base + "/status.json", timeout=10) as r:
        return json.loads(r.read().decode())


def status(name: str, ip: str | None, raw: bool) -> int:
    try:
        d = fetch_status(name, ip)
    except urllib.error.HTTPError as e:
        print(f"HTTP {e.code}: {e.read().decode(errors='replace').strip()}")
        return 1
    except OSError as e:
        print(f"not reachable: {e} - try --ip <address> (console: net)")
        return 1
    if raw:
        print(json.dumps(d, indent=2, ensure_ascii=False))
        return 0
    s = d["system"]
    print(f"{s['name']} ({s['image']}, {s['version']}), mode {s['mode']}, up {s['uptime_s']} s, boot #{s['boot']}, "
          f"last reset: {s['reset_reason']}, time {s['time']}")
    print(f"  heap {s['heap_free']} B (min {s['heap_min']}), slot {s['ota_slot']}, core dump {s['coredump']}, "
          f"selftest {s['selftest']}")
    if "radio" in d:
        r = d["radio"]
        print(f"  WiFi {r['wifi']}, RSSI {r['rssi']} dBm, channel {r['channel']}, disconnects {r['disconnects']}")
        p = d["protocol"]
        peers = ", ".join(f"{x['unit']} {'ok' if x['reachable'] else 'UNREACHABLE'}" for x in p["peers"])
        print(f"  peers: {peers}; rings ok {p['rings_ok']} failed {p['rings_failed']}, rate-limited "
              f"{p['rx_rate_limited']}, bad tag {p['rx_bad_tag']}")
    for c in d["touch"]["channels"]:
        print(f"  touch {c['name']}: raw {c['raw']}, baseline {c['baseline']}, {c['delta_percent']:+.1f} %, {c['state']}")
    if "door" in d:
        o = d["door"]
        print(f"  door: ready {o['ready']}, openings since boot {o['openings_since_boot']}, last pulse "
              f"{o['last_pulse_ms']} ms, errors {o['pulse_errors']}")
    lg = d["doorlog"]
    extra = f", indoor-key openings (sum) {lg['indoor_key_openings']}" if "indoor_key_openings" in lg else ""
    print(f"  door log: {len(lg['entries'])} shown of {lg['total']}{extra}")
    for e in lg["entries"][:5]:
        print(f"    {e['time']:22} {e['event']}")
    a = d["audio"]
    files = ", ".join(f"{f['name']} {f.get('seconds', '-')} s" if f["present"] else f"{f['name']} MISSING"
                      for f in a["files"])
    print(f"  audio: volume {a['volume']} %, last {a['last_file']}, files: {files}")
    return 0


def ton_hochladen(name: str, ton: str, wav: str, ip: str | None) -> int:
    base, pw = _base(name, ip)
    body = Path(wav).read_bytes()
    req = urllib.request.Request(f"{base}/ton/{ton}", data=body, method="PUT",
                                 headers={"Content-Type": "audio/wav"})
    try:
        with _opener(base, pw).open(req, timeout=60) as r:
            print(json.loads(r.read().decode())["message"])
            return 0
    except urllib.error.HTTPError as e:
        try:
            msg = json.loads(e.read().decode())["message"]
        except (ValueError, KeyError):
            msg = ""
        print(f"refused (HTTP {e.code}): {msg}")
        return 1


def update(name: str, image: str, ip: str | None, older: bool = False) -> int:
    """Uploads a firmware image (unit must be in maintenance). Same path as the button on the status page.
    Since port step 10d only signed files are accepted (cb.py signieren); older=True allows a lower version."""
    base, pw = _base(name, ip)
    req = urllib.request.Request(f"{base}/update" + ("?older=1" if older else ""), data=Path(image).read_bytes(),
                                 method="PUT",
                                 headers={"Content-Type": "application/octet-stream"})
    try:
        with _opener(base, pw).open(req, timeout=180) as r:
            print(json.loads(r.read().decode())["message"])
            return 0
    except urllib.error.HTTPError as e:
        try:
            msg = json.loads(e.read().decode())["message"]
        except (ValueError, KeyError):
            msg = ""
        print(f"refused (HTTP {e.code}): {msg}")
        return 1


def passwort(name: str) -> int:
    u = _unit(name)
    own = f"  (own password: {u['webpasswort']})" if u.get("webpasswort") else ""
    print(f"{name}: http://{_host(u)}/  user {USER}  label password {u['geraetepasswort']}{own}")
    return 0
