"""ClearBell V0.2 - remote opening (port step 9) from the PC side. Called through cb.py:

    python tools/cb.py fern-handy-anleitung H1     how to set up phone H1 (shows its key - your screen only)
    python tools/cb.py fern-test H1 [s] [--falsch] [--replay]
                                                   the PC plays phone H1: waits up to s seconds (default 120)
                                                   for a ring push, signs the token, sends the answer and waits
                                                   for "Tür per App geöffnet". --falsch signs with a wrong key
                                                   (must NOT open); --replay sends the same answer again after
                                                   the opening (must NOT open a second time); --nochmal: if
                                                   refused (lock-out), send the same answer again after 6 s.

Security argument and message format: components/cb_core/include/cb_remote_logic.h. Everything here goes over
HTTPS to ntfy.sh. Python behind Avast: truststore makes Python trust the Windows certificate store.
"""

import hashlib
import hmac
import json
import re
import sys
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

try:
    import truststore  # Avast intercepts TLS on this PC; without this every HTTPS call fails

    truststore.inject_into_ssl()
except ImportError:
    pass

sys.path.insert(0, str(Path(__file__).parent))
import provisionieren as prov  # noqa: E402

SERVER = "https://ntfy.sh"
SCHEME = "CBR1"


def _fern() -> dict:
    data = prov._load()
    if "fern" not in data:
        raise SystemExit("no remote section - cb.py anlage-fern first")
    return data["fern"]


def sign(key_hex: str, token: str, phone: str) -> str:
    """Same as cb::RemoteAuth::sign - the key is used as TEXT (its 64 hex characters)."""
    return hmac.new(key_hex.encode(), f"{SCHEME}|{token}|{phone}".encode(), hashlib.sha256).hexdigest()


def publish(topic: str, text: str) -> None:
    req = urllib.request.Request(f"{SERVER}/{topic}", data=text.encode(), method="POST")
    with urllib.request.urlopen(req, timeout=15) as r:
        r.read()


class Listener(threading.Thread):
    """Reads the JSON stream of a topic and collects the messages."""

    def __init__(self, topic: str):
        super().__init__(daemon=True)
        self.topic, self.messages, self.ready = topic, [], threading.Event()

    def run(self) -> None:
        with urllib.request.urlopen(f"{SERVER}/{self.topic}/json", timeout=300) as r:
            for raw in r:
                m = json.loads(raw)
                if m.get("event") == "open":
                    self.ready.set()
                elif m.get("event") == "message":
                    m["_rx"] = time.monotonic()
                    self.messages.append(m)

    def wait_for(self, title_part: str, timeout: float, after: int = 0) -> dict | None:
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            for m in self.messages[after:]:
                if title_part in m.get("title", ""):
                    return m
            time.sleep(0.1)
        return None


def anleitung(phone: str) -> int:
    f = _fern()
    if phone not in f["handys"]:
        raise SystemExit(f"phone {phone} not registered (cb.py anlage-handy {phone})")
    key = f["handys"][phone]
    print(f"""Phone {phone} - set up once (Android, app "HTTP Shortcuts" and app "ntfy"):

1. ntfy app: subscribe to topic   {f['topic_aus']}   (server ntfy.sh). The bell pushes arrive there.
2. HTTP Shortcuts: new shortcut "ClearBell öffnen"
   - Method POST, URL   {SERVER}/{f['topic_ein']}
   - Variable "token" (type: text, empty)
   - Body (text):   {{{{body}}}}   with a variable "body" set by this script ("Run before execution"):
        const token = getVariable('token');
        const sig = toHexString(hmac('SHA-256', '{key}', 'CBR1|' + token + '|{phone}'));
        setVariable('body', 'CBR1 ' + token + ' {phone} ' + sig);
3. Long-press the shortcut -> "Show Info" -> copy its ID, then on the PC:
        python tools/cb.py anlage-fern --link "http-shortcuts://<ID>?token={{token}}"
        python tools/cb.py provisionieren AU-1 COM4
4. Test: ring at EG, tap "Tür öffnen" in the push -> the shortcut runs -> push "Tür per App geöffnet".

The key above opens the door together with a fresh ring: keep it only in that app; a lost phone is locked out with
   python tools/cb.py anlage-handy-sperren {phone}   and provisioning the outdoor unit again.""")
    return 0


def test(phone: str, seconds: float, wrong_key: bool, replay: bool, retry: bool = False) -> int:
    f = _fern()
    if phone not in f["handys"]:
        raise SystemExit(f"phone {phone} not registered")
    key = "0" * 64 if wrong_key else f["handys"][phone]
    lst = Listener(f["topic_aus"])
    lst.start()
    if not lst.ready.wait(20):
        print("could not subscribe to the push topic")
        return 1
    print(f"listening as phone {phone}{' WITH A WRONG KEY' if wrong_key else ''} - ring at EG now "
          f"(e.g. console: ring eg) ...")
    ring = lst.wait_for("Es klingelt", seconds)
    if not ring:
        print("no ring push received")
        return 1
    url = next((a.get("url", "") for a in ring.get("actions", []) if a.get("action") == "view"), "")
    m = re.search(r"token=([0-9a-f]{32})", url)
    if not m:
        print(f"ring push without token link: {ring.get('actions')}")
        return 1
    token = m.group(1)
    print(f"ring push received, token {token[:6]}..., button link ok")
    answer = f"{SCHEME} {token} {phone} {sign(key, token, phone)}"
    n = len(lst.messages)
    t0 = time.monotonic()
    publish(f["topic_ein"], answer)
    res = lst.wait_for("Tür", 20, after=n)
    if wrong_key:
        ok = res is None
        print("PASS - wrong key: no opening, no push" if ok else f"FAIL - opened with a wrong key: {res}")
        return 0 if ok else 1
    if retry and res and res.get("title", "").startswith("Tür NICHT"):
        # Refused (lock-out): the token must still be valid - same answer again after the lock-out time.
        print(f"refused as expected: '{res.get('message')}' - same answer again in 6 s")
        time.sleep(6)
        n = len(lst.messages)
        t0 = time.monotonic()
        publish(f["topic_ein"], answer)
        res = lst.wait_for("Tür", 20, after=n)
    if not res or "geöffnet (Handy" not in res.get("message", "") or res.get("title", "").startswith("Tür NICHT"):
        print(f"no opening: {res.get('title') if res else 'no answer within 20 s'} {res.get('message') if res else ''}")
        return 1
    print(f"OPENED: '{res['message']}' {time.monotonic() - t0:.1f} s after sending the answer")
    if replay:
        time.sleep(6)  # past the lock-out time, so only the token can stop it
        n = len(lst.messages)
        publish(f["topic_ein"], answer)
        res2 = lst.wait_for("Tür", 15, after=n)
        print("PASS - replay refused (no second opening)" if res2 is None else f"FAIL - replay: {res2}")
        return 0 if res2 is None else 1
    return 0


def handy_web(unit: str, phone: str) -> int:
    """Adds a phone over the EG indoor unit's page (unit must be in maintenance) - exactly the buyer's path - and
    takes key and topics from the downloaded shortcut, so fern-test can play that phone afterwards."""
    import urllib.parse
    import statusseite as st

    base, pw = st._base(unit, None)
    op = st._opener(base, pw)
    req = urllib.request.Request(base + "/phone/add", data=urllib.parse.urlencode({"name": phone}).encode(),
                                 method="POST", headers={"Content-Type": "application/x-www-form-urlencoded"})
    try:
        r = json.loads(op.open(req, timeout=20).read().decode())
    except urllib.error.HTTPError as e:
        print(f"refused (HTTP {e.code}): {json.loads(e.read().decode()).get('message')}")
        return 1
    print(r["message"])
    imp = urllib.parse.parse_qs(urllib.parse.urlparse(r["import"]).query)["url"][0]
    print(f"import link points to {imp == r['download'] and 'the download' or 'SOMETHING ELSE'}; ntfy link {r['ntfy'][:22]}...")
    sc = json.loads(urllib.request.urlopen(r["download"], timeout=10).read().decode())  # no login: one-time token
    short = sc["categories"][0]["shortcuts"][0]
    key = re.search(r"hmac\('SHA-256', '([0-9a-f]{64})'", short["codeOnPrepare"]).group(1)
    topic_in = short["url"].rsplit("/", 1)[1]
    topic_out = re.match(r"ntfy://ntfy\.sh/([^?]+)", r["ntfy"]).group(1)
    print(f"shortcut: version {sc['version']}, id {short['id']}, method {short['method']}, "
          f"variables {[v['key'] for v in sc['variables']]}")
    data = prov._load()
    f = data.setdefault("fern", {"handys": {}, "link_vorlage": ""})
    f["topic_aus"], f["topic_ein"] = topic_out, topic_in
    f["handys"][phone] = key
    prov._save(data)
    print(f"phone {phone}: key and topics stored in the installation file (not shown)")
    return 0


def main(argv: list[str]) -> int:
    if argv[0] == "fern-handy-web" and len(argv) == 3:
        return handy_web(argv[1], argv[2])
    if argv[0] == "fern-handy-anleitung" and len(argv) == 2:
        return anleitung(argv[1])
    if argv[0] == "fern-test" and len(argv) >= 2:
        nums = [a for a in argv[2:] if not a.startswith("--")]
        return test(argv[1], float(nums[0]) if nums else 120, "--falsch" in argv, "--replay" in argv,
                    "--nochmal" in argv)
    print(__doc__)
    return 2
