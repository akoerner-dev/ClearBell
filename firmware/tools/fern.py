"""ClearBell V0.2 - remote opening (R27) from the PC side: the PC plays a phone. Called through cb.py:

    python tools/cb.py fern-handy-web IN-1 PC2     add phone "PC2" over the indoor unit's page (maintenance) -
                                                   exactly the buyer's path - and remember its push topic
    python tools/cb.py fern-test PC2 [s] [--fremd] [--replay] [--nochmal]
                                                   wait up to s seconds (default 120) for a ring push on PC2's
                                                   topic, press its "Tür öffnen" button (the ntfy http action:
                                                   POST of its body to its url) and wait for "Tür per App
                                                   geöffnet". --fremd sends a made-up token instead (must NOT
                                                   open); --replay presses the button again after the opening
                                                   (must NOT open a second time); --nochmal: if refused (lock-out),
                                                   press again after 6 s.

Security argument and message format: components/cb_core/include/cb_remote_logic.h. Everything here goes over
HTTPS to ntfy.sh. Python behind Avast: truststore makes Python trust the Windows certificate store.
"""

import json
import re
import secrets
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


def _phone(name: str) -> dict:
    p = prov._load().get("fern_r27", {}).get(name)
    if not p:
        raise SystemExit(f"phone {name} unknown - add it first: cb.py fern-handy-web <indoor unit> {name}")
    return p


def post(url: str, text: str) -> None:
    req = urllib.request.Request(url, data=text.encode(), method="POST")
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


def test(name: str, seconds: float, foreign: bool, replay: bool, retry: bool = False) -> int:
    p = _phone(name)
    lst = Listener(p["push"])
    lst.start()
    if not lst.ready.wait(20):
        print("could not subscribe to the push topic")
        return 1
    print(f"listening as phone {name} ({p['unit']}) - ring at its flat now (e.g. AU-1 console: ring eg) ...")
    ring = lst.wait_for("Es klingelt", seconds)
    if not ring:
        print("no ring push received")
        return 1
    act = next((a for a in ring.get("actions", []) if a.get("action") == "http"), None)
    if not act or act.get("method") != "POST" or not re.fullmatch(r"CBR2 [0-9a-f]{32}", act.get("body", "")):
        print(f"ring push without a proper http button: {ring.get('actions')}")
        return 1
    url, body = act["url"], act["body"]
    if foreign:
        body = "CBR2 " + secrets.token_hex(16)
    print(f"ring push '{ring.get('title')}' received, button: POST {url[:30]}... body {body[:11]}...")
    n = len(lst.messages)
    t0 = time.monotonic()
    post(url, body)
    res = lst.wait_for("Tür", 20, after=n)
    if foreign:
        ok = res is None
        print("PASS - made-up token: no opening, no push" if ok else f"FAIL - opened with a made-up token: {res}")
        return 0 if ok else 1
    if retry and res and res.get("title", "").startswith("Tür NICHT"):
        print(f"refused as expected: '{res.get('message')}' - press again in 6 s")
        time.sleep(6)
        n = len(lst.messages)
        t0 = time.monotonic()
        post(url, body)
        res = lst.wait_for("Tür", 20, after=n)
    if not res or "geöffnet (Handy" not in res.get("message", "") or res.get("title", "").startswith("Tür NICHT"):
        print(f"no opening: {res.get('title') if res else 'no answer within 20 s'} {res.get('message') if res else ''}")
        return 1
    print(f"OPENED: '{res['message']}' {time.monotonic() - t0:.1f} s after pressing the button")
    if replay:
        time.sleep(6)  # past the lock-out time, so only the token can stop it
        n = len(lst.messages)
        post(url, body)
        res2 = lst.wait_for("Tür", 15, after=n)
        print("PASS - replay refused (no second opening)" if res2 is None else f"FAIL - replay: {res2}")
        return 0 if res2 is None else 1
    return 0


def handy_web(unit: str, name: str) -> int:
    """Adds a phone over the indoor unit's page (unit must be in maintenance) and remembers its push topic."""
    import urllib.parse

    import statusseite as st

    base, pw = st._base(unit, None)
    op = st._opener(base, pw)
    req = urllib.request.Request(base + "/phone/add", data=urllib.parse.urlencode({"name": name}).encode(),
                                 method="POST", headers={"Content-Type": "application/x-www-form-urlencoded"})
    try:
        r = json.loads(op.open(req, timeout=20).read().decode())
    except urllib.error.HTTPError as e:
        print(f"refused (HTTP {e.code}): {json.loads(e.read().decode()).get('message')}")
        return 1
    print(r["message"])
    topic = re.match(r"ntfy://ntfy\.sh/(cb[0-9a-f]{32})\?", r["ntfy"]).group(1)
    data = prov._load()
    data.setdefault("fern_r27", {})[name] = {"push": topic, "unit": unit}
    prov._save(data)
    print(f"phone {name}: push topic stored in the installation file (not shown)")
    return 0


def main(argv: list[str]) -> int:
    if argv[0] == "fern-handy-web" and len(argv) == 3:
        return handy_web(argv[1], argv[2])
    if argv[0] == "fern-test" and len(argv) >= 2:
        nums = [a for a in argv[2:] if not a.startswith("--")]
        return test(argv[1], float(nums[0]) if nums else 120, "--fremd" in argv, "--replay" in argv,
                    "--nochmal" in argv)
    print(__doc__)
    return 2
