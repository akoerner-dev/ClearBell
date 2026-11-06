"""ClearBell V0.2 - generates the tone files (original sounds, additive synthesis, no samples of anyone).

    python tools/toene/erzeuge_toene.py <output folder>
(normally called by: python tools/cb.py toene <port>, which also packs and flashes them)

Design rules from the bring-up (do not break them when changing a tone):
  * No fundamental in 300-600 Hz: the loudspeaker resonance there makes the amplifier clip and rattle
    (IN-B-02_Klirren_440Hz). The V0.1 tones (melody A4-E5 = 440-660 Hz) sat exactly there.
  * Energy mainly at 1-4 kHz, where the ear is most sensitive and the small speaker is efficient
    (M-28_Klingelton_2-4kHz).
  * Full-scale files (peak 0.9); the firmware scales down to at most 40 % digital (AU-B-02).
  * 44.1 kHz, mono, 16 bit - what the WAV player expects (cb_wav.h).

Files (names fixed in cb_audio_logic.cpp, sound_file()):
  klingel.wav    ring tone indoors: bell-like "ding-dong" (E6 -> C6, inharmonic bell partials), twice
  ok.wav         success: two short rising tones G6 -> C7
  fehler.wav     error: three falling tones E6 -> C6 -> A5
  kein_netz.wav  outdoor without WiFi: three identical short beeps at 1 kHz, clearly NOT a confirmation
  tipp.wav       feedback tick: 35 ms at 2.6 kHz
"""

import math
import struct
import sys
import wave
from pathlib import Path

SR = 44100
PEAK = 0.9


def env(i, n, attack, decay_tau=None):
    """Short linear attack, optional exponential decay, short release at the very end (no clicks)."""
    a = min(1.0, i / attack) if attack else 1.0
    d = math.exp(-i / decay_tau) if decay_tau else 1.0
    rel = min(1.0, (n - i) / (SR * 0.008))
    return a * d * rel


def tone(freq, ms, partials=((1, 1.0), (2, 0.25), (3, 0.1)), decay_ms=None, attack_ms=4):
    n = int(SR * ms / 1000)
    norm = sum(l for _, l in partials)
    tau = SR * decay_ms / 1000 if decay_ms else None
    out = []
    for i in range(n):
        t = i / SR
        v = sum(l * math.sin(2 * math.pi * freq * m * t) for m, l in partials if freq * m < SR * 0.45)
        out.append(env(i, n, SR * attack_ms / 1000, tau) * v / norm)
    return out


def silence(ms):
    return [0.0] * int(SR * ms / 1000)


# Bell partials (ratios of a struck bell, inharmonic -> sounds like a chime, not like a beeper)
BELL = ((1.0, 1.0), (2.0, 0.45), (2.76, 0.30), (5.4, 0.12))


def write(path: Path, samples):
    peak = max(1e-9, max(abs(s) for s in samples))
    k = PEAK / peak
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(b"".join(struct.pack("<h", int(s * k * 32767)) for s in samples))
    print(f"  {path.name:14} {len(samples) / SR:.2f} s")


def main(out: Path):
    out.mkdir(parents=True, exist_ok=True)
    ding = tone(1318.5, 700, BELL, decay_ms=260, attack_ms=2)   # E6
    dong = tone(1046.5, 1000, BELL, decay_ms=380, attack_ms=2)  # C6
    klingel = ding + dong + silence(150) + ding + dong
    ok = tone(1568.0, 110) + silence(25) + tone(2093.0, 200, decay_ms=120)          # G6 -> C7
    fehler = tone(1318.5, 180) + silence(40) + tone(1046.5, 180) + silence(40) + tone(880.0, 380)
    beep = tone(1000.0, 150, ((1, 1.0), (3, 0.2)))
    kein_netz = beep + silence(150) + beep + silence(150) + beep
    tipp = tone(2600.0, 35, ((1, 1.0),), attack_ms=1)
    print(f"tone files -> {out}")
    write(out / "klingel.wav", klingel)
    write(out / "ok.wav", ok)
    write(out / "fehler.wav", fehler)
    write(out / "kein_netz.wav", kein_netz)
    write(out / "tipp.wav", tipp)


if __name__ == "__main__":
    main(Path(sys.argv[1]) if len(sys.argv) > 1 else Path("toene"))
