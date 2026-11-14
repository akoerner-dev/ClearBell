"""Protection of the installation's secrets in logs and commits (security catalogue 13.11.2026, RV-20, RV-47).

Never prints a secret value - only counts and positions.

    python tools/geheim.py scan            check what would be committed (staged + unstaged changes + untracked files)
    python tools/geheim.py scan --staged   only what is staged (for a pre-commit check)
    python tools/geheim.py logs            count secrets in firmware/build/konsole/*.log
    python tools/geheim.py logs --schwaerzen   ... and replace them by «geheim» (files are rewritten in place)

Secrets = every string value of at least 6 characters in the JSON files of the secrets folder (CB_GEHEIM, default
<ClearBell project folder>/Geheimnisse), plus the header of a private key ("-----BEGIN ... PRIVATE KEY-----").
Shorter values are skipped on purpose (an old test password equals the unit name "AU-1" and would match everywhere).
Exit code of scan/logs: 0 = clean, 1 = hits, 2 = secrets folder not readable (then nothing was checked).
"""
import json
import os
import re
import subprocess
import sys
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parents[1]
REPO = FIRMWARE.parent
GEHEIM = Path(os.environ.get("CB_GEHEIM", FIRMWARE.parents[1] / "Geheimnisse"))
MIN_LEN = 6
PRIVATE_KEY = re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----")
# Commands of the firmware console whose arguments are secret, and from which word on (cb.py konsole logs commands)
SECRET_ARGS = {"web-pass": 1, "setup-wifi": -1, "update-owner": 1}
MARK = "«geheim»"


def werte() -> tuple[list[str], int]:
    """All secret values (longest first) and the number of files read."""
    found, files = set(), 0

    def walk(x):
        if isinstance(x, dict):
            for v in x.values():
                walk(v)
        elif isinstance(x, list):
            for v in x:
                walk(v)
        elif isinstance(x, str) and len(x) >= MIN_LEN:
            found.add(x)

    if GEHEIM.is_dir():
        for p in GEHEIM.rglob("*.json"):
            try:
                walk(json.loads(p.read_text("utf-8")))
                files += 1
            except (OSError, ValueError):
                pass
    return sorted(found, key=len, reverse=True), files


def maskiere_befehl(command: str) -> str:
    """'web-pass Abc123x | @1 | info' -> 'web-pass «geheim» | @1 | info'. Works on the step syntax of cb.py konsole."""
    out = []
    for step in command.split("|"):
        words = step.split()
        if words and words[0] in SECRET_ARGS and len(words) > 1 and not (words[0] == "web-pass" and words[1] == "label"):
            # -1: setup-wifi <ssid, may contain spaces> <password> - only the last word is the password
            words[SECRET_ARGS[words[0]]:] = [MARK]
            lead = step[: len(step) - len(step.lstrip())]
            trail = step[len(step.rstrip()):]
            step = lead + " ".join(words) + trail
        out.append(step)
    return "|".join(out)


def schwaerze(text: str, secret_values: list[str]) -> tuple[str, int]:
    """Replaces every secret value in text; returns the new text and the number of replacements."""
    n = 0
    for v in secret_values:
        c = text.count(v)
        if c:
            text = text.replace(v, MARK)
            n += c
    # a console echo like "unknown command 'web-pass xyz'" carries the typed command
    def fix(m):
        return "'" + maskiere_befehl(m.group(1)) + "'"
    text2 = re.sub(r"'((?:web-pass|setup-wifi|update-owner) [^']*)'", fix, text)
    # the command lines cb.py konsole wrote before RV-20 ("--- 08:25:13 konsole web-pass <pw>") - short passwords too
    text2 = re.sub(r"(?m)^(--- \d\d:\d\d:\d\d konsole )(.*)$", lambda m: m.group(1) + maskiere_befehl(m.group(2)), text2)
    if text2 != text:
        n += 1
    return text2, n


def _changed_text(staged_only: bool) -> dict[str, str]:
    def git(*a):
        return subprocess.run(["git", "-C", str(REPO), *a], capture_output=True, text=True, encoding="utf-8",
                              errors="replace").stdout

    diff = git("diff", "--cached", "-U0") if staged_only else git("diff", "HEAD", "-U0")
    texts, cur = {}, None
    for line in diff.splitlines():
        if line.startswith("+++ b/"):
            cur = line[6:]
        elif line.startswith("+") and not line.startswith("+++") and cur:
            texts[cur] = texts.get(cur, "") + line[1:] + "\n"
    if not staged_only:
        for f in git("ls-files", "--others", "--exclude-standard").split("\n"):
            if f:
                try:
                    texts[f] = (REPO / f).read_text("utf-8", errors="replace")
                except OSError:
                    pass
    return texts


def scan(staged_only: bool) -> int:
    vals, files = werte()
    if not files:
        print(f"secrets folder not readable ({GEHEIM}) - NOTHING checked")
        return 2
    texts = _changed_text(staged_only)
    hits = 0
    for name, t in texts.items():
        n = sum(t.count(v) for v in vals) + len(PRIVATE_KEY.findall(t))
        if n:
            print(f"HIT: {name}: {n} secret value(s) or private-key header")
            hits += n
    lines = sum(t.count("\n") for t in texts.values())
    print(f"geheim scan: {files} secret file(s), {len(vals)} value(s) >= {MIN_LEN} chars, {len(texts)} file(s) / "
          f"{lines} added line(s) checked -> {'CLEAN' if not hits else f'{hits} HIT(S)'}")
    return 1 if hits else 0


def logs(redact: bool) -> int:
    vals, files = werte()
    if not files:
        print(f"secrets folder not readable ({GEHEIM}) - NOTHING checked")
        return 2
    total, logs_ = 0, sorted((FIRMWARE / "build" / "konsole").glob("*.log"))
    for p in logs_:
        text = p.read_text("utf-8", errors="replace")
        new, n = schwaerze(text, vals)
        if n:
            total += n
            print(f"{p.name}: {n} secret value(s)" + (" - blacked out" if redact else ""))
            if redact:
                p.write_text(new, "utf-8")
    print(f"geheim logs: {len(logs_)} log file(s) checked, {total} hit(s)" + (", blacked out" if redact and total else ""))
    return 1 if total and not redact else 0


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[:1] == ["scan"]:
        sys.exit(scan("--staged" in a))
    if a[:1] == ["logs"]:
        sys.exit(logs("--schwaerzen" in a))
    print(__doc__)
    sys.exit(2)
