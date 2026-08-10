# -*- coding: utf-8 -*-
# ClearBell – Sounderzeugung (additive Synthese, gemeinfrei/original)
# 44100 Hz, Mono, 16-bit WAV. Firmware liest die Rate aus dem Header
# -> kein Flashen noetig, nur WAVs auf die SD kopieren.
#
# Klang: Grundton + wenige Obertoene, ALLE unter der Nyquist-Grenze
# gehalten -> weich UND mit Charakter, aber ohne Rechteck-Grieseln.
#
#   ok.wav        heller "Ka-tsching" (coin-artig, metallisch) - ORIGINAL,
#                 keine 1:1-Kopie eines geschuetzten Sounds
#   fehler.wav    tiefer, absteigender Ablehnton (Zonk-artig)
#   klingel_eg.wav / klingel_og.wav   Tetris-Thema (Korobeiniki, gemeinfrei)
#
# Aufruf:  python -X utf8 erzeuge_sounds.py

import wave, struct, math, os

SR = 44100

N = {
    'D3':146.83, 'E3':164.81, 'F3':174.61, 'G3':196.00, 'A3':220.00,
    'A4':440.00, 'B4':493.88, 'C5':523.25, 'D5':587.33, 'E5':659.25,
    'C6':1046.50,'E6':1318.51,'G6':1567.98,'B6':1975.53,
    'REST':0.0,
}

# Obertonprofile (Vielfaches, Pegel)
SOFT       = [(1,1.0),(2,0.22),(3,0.10)]                                  # warm, dezent
COIN_KA    = [(1,1.0),(2,0.5),(3,0.30)]                                   # kurzes "Ka"
COIN_CHING = [(1,1.0),(2,0.6),(3,0.4),(4,0.28),(5,0.18),(6,0.12),(7,0.08)]# helles "Tsching"

def _env(i, n, atk, rel):
    if i < atk:      return i / atk
    if i > n - rel:  return max(0.0, (n - i) / rel)
    return 1.0

def _osc(freq, t, harm):
    v = 0.0
    for mult, lvl in harm:
        if freq * mult < SR * 0.45:      # bandbegrenzt -> kein Aliasing
            v += lvl * math.sin(2 * math.pi * freq * mult * t)
    return v

def ton(freq, dauer_ms, amp=0.5, harm=SOFT, decay_ms=None, gap_ms=8):
    n = int(SR * dauer_ms / 1000)
    atk = min(int(SR * 0.006), n // 4)
    rel = min(int(SR * 0.045), n // 2)
    norm = sum(l for _, l in harm)
    tau = (SR * decay_ms / 1000) if decay_ms else None
    out = []
    for i in range(n):
        t = i / SR
        base = (_osc(freq, t, harm) / norm) if freq > 0 else 0.0
        e = _env(i, n, atk, rel)
        if tau: e *= math.exp(-i / tau)
        out.append(amp * e * base)
    out += [0.0] * int(SR * gap_ms / 1000)
    return out

def bend(f0, f1, dauer_ms, amp=0.5, harm=SOFT, gap_ms=0):
    n = int(SR * dauer_ms / 1000)
    atk = min(int(SR * 0.006), n // 4)
    rel = min(int(SR * 0.060), n // 2)
    norm = sum(l for _, l in harm)
    out = []; phase = 0.0
    for i in range(n):
        f = f0 + (f1 - f0) * (i / n)
        phase += 2 * math.pi * f / SR
        v = 0.0
        for mult, lvl in harm:
            if f * mult < SR * 0.45:
                v += lvl * math.sin(phase * mult)
        out.append(amp * _env(i, n, atk, rel) * v / norm)
    out += [0.0] * int(SR * gap_ms / 1000)
    return out

def melodie(noten, amp=0.5, harm=SOFT):
    out = []
    for name, d in noten:
        out += ton(N[name], d, amp=amp, harm=harm)
    return out

def schreibe(pfad, samples):
    w = wave.open(pfad, 'wb')
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
    b = bytearray()
    for s in samples:
        v = int(max(-1.0, min(1.0, s)) * 32767)
        b += struct.pack('<h', v)
    w.writeframes(b); w.close()
    print("  %-16s %.2f s" % (os.path.basename(pfad), len(samples) / SR))

hier = os.path.dirname(os.path.abspath(__file__))
def p(name): return os.path.join(hier, name)

# ── ok: heller "Ka-tsching" – zwei klar getrennte Silben ────────────────────
#   Ka  = laenger + kleine Luecke, damit es als eigene Silbe hoerbar ist
#   Tsching = heller, glitzernder Ausklang
ok = (ton(N['E6'], 120, amp=0.48, harm=COIN_KA,    gap_ms=15) +
      ton(N['B6'], 330, amp=0.50, harm=COIN_CHING, decay_ms=170, gap_ms=0))

# ── fehler: absteigende Noten + tiefer Abfall (Zonk) ────────────────────────
fehler = (ton(N['A3'], 230, amp=0.5) +
          ton(N['G3'], 230, amp=0.5) +
          ton(N['F3'], 230, amp=0.5) +
          bend(N['E3'], 110.0, 680, amp=0.5))

# ── klingel: Tetris-Thema (Korobeiniki), erste Phrase ───────────────────────
tetris = [
    ('E5',300), ('B4',150), ('C5',150), ('D5',300), ('C5',150), ('B4',150),
    ('A4',300), ('A4',150), ('C5',150), ('E5',300), ('D5',150), ('C5',150),
    ('B4',450), ('C5',150), ('D5',300), ('E5',300), ('C5',300), ('A4',300),
    ('A4',300), ('REST',150),
]
klingel = melodie(tetris, amp=0.5)

print("Erzeuge Sounds (44100 Hz, Mono, 16-bit, additiv):")
schreibe(p('ok.wav'), ok)
schreibe(p('fehler.wav'), fehler)
schreibe(p('klingel_eg.wav'), klingel)
schreibe(p('klingel_og.wav'), klingel)
print("Fertig. Zum Probehoeren die WAVs im Ordner Sounds doppelklicken.")
