#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Dusk's tables: firmware/gen/lofi_gen.h.

Everything the firmware cannot compute without floats, and everything that is music rather than code: the sine, the
pitches, the filters' coefficients, the sounds (seconds and ratios here, envelope coefficients there), the drum kits,
the chord bank, and the clips (progressions, comping rhythms, bass lines, grooves, toplines) and the factory tapes
made of them.   python3 tools/gen_lofi.py   (no dependencies)"""
import math
from pathlib import Path

FS = 44100.0
CH = 16                                 # the engine's control block, in frames
OUT = Path(__file__).resolve().parents[1] / "firmware" / "gen" / "lofi_gen.h"

PC = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def midi(name):
    pc = PC[name[0]]
    rest = name[1:]
    while rest[0] in "#b":
        pc += 1 if rest[0] == "#" else -1
        rest = rest[1:]
    return 12 * (int(rest) + 1) + pc


def kc(tau):                            # an exponential's coefficient a control block, Q16
    return max(1, min(65535, round(65536 * (1 - math.exp(-CH / (tau * FS))))))


def cname(s, n):
    assert len(s) < n, s
    return '"' + s + '"'


# ---- sounds: one FM pair (a modulator on a carrier) and a "tine" partial, for the keys, the bass and the topline
def snd(name, rm=1.0, i0=1.0, i1=0.2, itau=0.3, att=0.004, dtau=1.5, sus=0.0, rtau=0.25, tine=0.0, rt=7.0,
        ttau=0.05, gain=1.0, trem=0.0, trate=4.3, vib=0.0, vrate=5.2):
    return dict(name=name, rm=round(rm * 256), rt=round(rt * 256), i0=round(i0 * 20861), i1=round(i1 * 20861),
                ik=kc(itau), att=max(1, round((1 << 24) / (att * FS / CH))), dk=kc(dtau), sus=round(sus * (1 << 24)),
                rk=kc(rtau), tine=round(tine * 32767), tk=kc(ttau), gain=round(gain * 256),
                trem=round(trem * 32767), trate=round(trate * CH / FS * 2 ** 32), vib=round(vib * 32768),
                vrate=round(vrate * CH / FS * 2 ** 32))


KEYS = [
    snd("RHODES", 1, 1.6, 0.22, 0.30, 0.004, 1.6, 0.0, 0.28, tine=0.12, rt=7.02, ttau=0.05, trem=0.12, trate=4.3),
    snd("WURLI", 1, 2.6, 0.8, 0.45, 0.003, 1.0, 0.0, 0.16, tine=0.08, rt=3.0, ttau=0.04, gain=1.2, trem=0.22, trate=5.6),
    snd("FELT", 2, 0.9, 0.04, 0.10, 0.006, 0.9, 0.0, 0.22, tine=0.05, rt=5.4, ttau=0.02, gain=1.0),
    snd("PAD", 1, 0.3, 0.75, 0.9, 0.30, 3.0, 0.7, 0.7, gain=0.75, trem=0.10, trate=0.8),
    snd("GLASS", 1, 1.0, 0.1, 0.5, 0.003, 2.0, 0.0, 0.35, tine=0.20, rt=14.0, ttau=0.12, gain=0.9, trem=0.08, trate=3.1),
    snd("VIBES", 4, 0.9, 0.08, 0.2, 0.003, 1.8, 0.0, 0.5, gain=1.1, trem=0.38, trate=5.0),
    snd("NYLON", 1, 3.0, 0.2, 0.06, 0.002, 0.5, 0.0, 0.12, tine=0.06, rt=4.0, ttau=0.02, gain=1.3),
    snd("ORGAN", 2, 0.55, 0.5, 1.0, 0.012, 0.6, 0.85, 0.06, tine=0.14, rt=3.0, ttau=6.0, gain=0.6, trem=0.14, trate=6.2),
]
BASS = [
    snd("SUB", 1, 0.5, 0.08, 0.2, 0.010, 1.4, 0.35, 0.08),
    snd("ROUND", 1, 1.3, 0.3, 0.25, 0.008, 1.0, 0.3, 0.09, gain=1.1),
    snd("PLUCK", 1, 2.4, 0.15, 0.08, 0.004, 0.45, 0.0, 0.10, tine=0.08, rt=3.0, ttau=0.03, gain=1.5),
    snd("TAPE", 2, 0.9, 0.5, 0.5, 0.012, 1.2, 0.4, 0.10, gain=0.85),
]
LEAD = [
    snd("WHISTLE", 1, 0.3, 0.12, 0.2, 0.020, 2.0, 0.7, 0.25, vib=0.005, vrate=5.2),
    snd("KALIMBA", 3, 1.5, 0.05, 0.07, 0.002, 0.55, 0.0, 0.30, tine=0.10, rt=6.3, ttau=0.02, gain=1.5),
    snd("MUSICBOX", 1, 0.8, 0.0, 0.10, 0.002, 0.8, 0.0, 0.40, tine=0.30, rt=4.0, ttau=0.20, gain=1.3),
    snd("FLUTE", 1, 0.5, 0.35, 0.08, 0.050, 2.5, 0.8, 0.15, vib=0.006, vrate=4.8, gain=0.9),
    snd("BELL", 3.5, 2.0, 0.2, 0.30, 0.002, 1.2, 0.0, 0.50, gain=1.2),
    snd("PLUCK", 1, 2.5, 0.3, 0.10, 0.002, 0.4, 0.0, 0.20, gain=1.7),
    snd("REED", 1, 1.4, 1.5, 0.4, 0.030, 2.0, 0.8, 0.12, vib=0.007, vrate=5.5, gain=1.3),
    snd("EPIANO", 1, 1.6, 0.22, 0.30, 0.004, 1.6, 0.0, 0.28, tine=0.12, rt=7.02, ttau=0.05, gain=1.3),
]


# ---- drums: a sine with a falling pitch, a noise through a filter, a click
def drum(f0=0, f1=0, ptau=0.03, slvl=0.0, stau=0.1, nlvl=0.0, natt=0.0, ntau=0.05, ftype=0, fc=2000, q=0.7,
         click=0.0, drive=1.0):
    return dict(f0=round(f0 * 2 ** 32 / FS), f1=round(f1 * 2 ** 32 / FS), pk=kc(ptau), slvl=round(slvl * 32767),
                sk=kc(stau), nlvl=round(nlvl * 32767), natt=0 if natt <= 0 else max(1, round(32767 / (natt * FS / CH))),
                nk=kc(ntau), ftype=ftype, ff=round(2 * math.sin(math.pi * min(fc, 9000) / FS) * 32768),
                fq=round(q * 32768), click=round(click * 32767), drive=round(drive * 256))


LP, BP, HP = 0, 1, 2
OPEN = drum(nlvl=0.5, ntau=0.11, ftype=HP, fc=7000)
RIM = drum(1700, 1700, 0.01, 0.5, 0.008, nlvl=0.25, ntau=0.004, ftype=BP, fc=3000, click=0.4)
SHAKER = drum(nlvl=0.45, natt=0.012, ntau=0.04, ftype=HP, fc=6000)
CLAVE = drum(2400, 2400, 0.01, 0.6, 0.03)
TOM = drum(170, 105, 0.06, 0.8, 0.16, click=0.1)
KITS = [
    ("DUSTY", drum(124, 44, 0.035, 1.0, 0.16, click=0.25, drive=2.2),
     drum(245, 185, 0.02, 0.45, 0.06, nlvl=0.9, ntau=0.075, ftype=BP, fc=2300, q=0.9, click=0.3),
     drum(nlvl=0.6, ntau=0.018, ftype=HP, fc=7000), OPEN),
    ("BOOMBAP", drum(150, 50, 0.028, 0.9, 0.20, click=0.5, drive=2.6),
     drum(260, 200, 0.02, 0.5, 0.07, nlvl=1.0, ntau=0.095, ftype=BP, fc=2800, q=0.8, click=0.5),
     drum(nlvl=0.65, ntau=0.026, ftype=HP, fc=7500), RIM),
    ("BLANKET", drum(95, 42, 0.045, 1.0, 0.22, click=0.05, drive=1.6),
     drum(200, 165, 0.02, 0.5, 0.05, nlvl=0.7, ntau=0.05, ftype=BP, fc=1500, q=1.0),
     drum(nlvl=0.5, ntau=0.012, ftype=HP, fc=6000), SHAKER),
    ("RIMSHOT", drum(118, 46, 0.03, 1.0, 0.17, click=0.2, drive=2.0),
     drum(480, 440, 0.01, 0.55, 0.014, nlvl=0.35, ntau=0.006, ftype=BP, fc=3200, q=0.8, click=0.5),
     drum(nlvl=0.55, ntau=0.02, ftype=HP, fc=7000), CLAVE),
    ("BRUSH", drum(105, 46, 0.04, 0.9, 0.18, click=0.1, drive=1.6),
     drum(210, 190, 0.02, 0.2, 0.05, nlvl=0.8, natt=0.012, ntau=0.13, ftype=BP, fc=3400, q=1.2),
     drum(nlvl=0.45, natt=0.004, ntau=0.03, ftype=HP, fc=6500), SHAKER),
    ("808", drum(82, 41, 0.06, 0.8, 0.42, click=0.12, drive=1.8),
     drum(330, 180, 0.012, 0.6, 0.07, nlvl=0.7, ntau=0.11, ftype=HP, fc=2500, q=0.9),
     drum(nlvl=0.55, ntau=0.03, ftype=HP, fc=8000), TOM),
]

# ---- the chord bank: one chord a key. Types: a name, a rootless voicing (four pitch classes above the root) and
# the notes a topline may land on over it (a 12-bit mask above the root)
TYPES = [
    ("M9", [4, 7, 11, 2], [0, 2, 4, 7, 9, 11]),
    ("m9", [3, 7, 10, 2], [0, 2, 3, 5, 7, 10]),
    ("m7", [0, 3, 7, 10], [0, 3, 5, 7, 10]),
    ("13", [10, 2, 4, 9], [0, 2, 4, 7, 9, 10]),
    ("m7b5", [0, 3, 6, 10], [0, 3, 5, 6, 10]),
    ("dim", [0, 3, 6, 9], [0, 2, 3, 5, 6, 8, 9, 11]),
    ("M7", [0, 4, 7, 11], [0, 2, 4, 7, 9, 11]),
    ("9", [4, 7, 10, 2], [0, 2, 4, 7, 9, 10]),
    ("69", [4, 7, 9, 2], [0, 2, 4, 7, 9]),
    ("7b13", [4, 8, 10, 1], [0, 1, 3, 4, 8, 10]),
    ("9sus", [5, 7, 10, 2], [0, 2, 5, 7, 9, 10]),
    ("M7#11", [4, 6, 7, 11], [0, 2, 4, 6, 7, 9, 11]),
]
TI = {t[0]: i for i, t in enumerate(TYPES)}
# the 27 keys, F3 up, in C: the lower octave the key's own ninth chords and the borrowed ones between them, the
# upper octave their restless relatives (the minor fourth, the dominants that lead home, the passing diminished)
BANK = ["M9", "m7b5", "13", "M7", "m9", "13", "m7b5", "M9", "dim", "m9", "M7", "m7",
        "m9", "dim", "9sus", "dim", "7b13", "M7#11", "7b13", "69", "9", "9", "dim", "7b13",
        "69", "7b13", "7b13"]
ROOTS = ["C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
CHORD = {}                              # "Dm9" -> its key + 1
for k, t in enumerate(BANK):
    CHORD.setdefault(ROOTS[(5 + k) % 12] + t, k + 1)


def prog(name, text):
    slots = [0 if w == "." else CHORD[w] for w in text.split()]
    assert len(slots) == 16, name
    return name, slots


PROGS = [
    prog("COFFEE", "Dm9 . G13 . CM9 . A7b13 . FM9 . Fm9 . Em7 . A7b13 ."),
    prog("RAINY", "FM9 . Em7 . Dm9 . G13 . FM9 . Em7 . Am9 . . ."),
    prog("SUNDAY", "CM9 . Am9 . Dm9 . G13 . CM9 . A7b13 . Dm9 . G9sus G7b13"),
    prog("DUSK", "Am9 . FM9 . CM9 . G13 . Am9 . FM9 . Dm9 . E7b13 ."),
    prog("VELVET", "FM9 . Fm9 . Em7 . A7b13 . Dm9 . G13 . CM9 . C69 ."),
    prog("NEON", "CM9 . EbM7 . AbM7 . Db9 . CM9 . EbM7 . Dm9 . Db9 ."),
    prog("BUS", "Dm9 . . . G13 . . . Dm9 . . . G9sus . G7b13 ."),
    prog("PORCH", "CM9 . C69 . FM9 . Fm9 . CM9 . E7b13 . Am9 . D9 ."),
    prog("ATTIC", "Am9 . D9 . FM9 . E7b13 . Am9 . D9 . FM9 G13 CM9 ."),
    prog("LETTER", "FM9 . G13 . Em7 . Am9 . FM9 . G13 . CM9 . . ."),
    prog("MOSS", "Dm9 . Em7 . FM9 . Em7 . Dm9 . Em7 . FM9 . G13 ."),
    prog("LAMP", "CM9 . B7b13 . Em7 . A7b13 . Dm9 . Db9 . CM9 . . ."),
    prog("STEPS", "Dm9 G13 CM9 A7b13 Dm9 G13 CM9 . FM9 Fm9 Em7 A7b13 Dm9 Db9 CM9 ."),
    prog("TIDE", "FM9 Em7 Dm9 Em7 FM9 Em7 Dm9 G13 FM9 Em7 Dm9 Em7 FM9 Em7 G9sus G7b13"),
]

# ---- comping rhythms (the keys): events in a bar of 16 steps: step, length, velocity, kind, argument
BLOCK, DOWN, UPPER, ARP, LOW = range(5)
COMPS = [
    ("HOLD", 1, []),
    ("LAZY", 1, [(0, 9, 100, BLOCK, 0), (10, 6, 70, BLOCK, 0)]),
    ("PUSH", 1, [(0, 6, 100, BLOCK, 0), (6, 4, 70, UPPER, 0), (10, 6, 82, BLOCK, 0)]),
    ("PULSE", 1, [(0, 3, 96, BLOCK, 0), (4, 3, 70, UPPER, 0), (8, 3, 84, BLOCK, 0), (12, 3, 66, UPPER, 0)]),
    ("STABS", 0, [(4, 2, 92, BLOCK, 0), (11, 2, 80, BLOCK, 0)]),
    ("BROKEN", 1, [(0, 4, 96, LOW, 0), (2, 6, 78, UPPER, 0), (8, 4, 84, LOW, 0), (10, 6, 72, UPPER, 0)]),
    ("ARP", 1, [(0, 4, 96, ARP, 0), (2, 4, 78, ARP, 1), (4, 4, 84, ARP, 2), (6, 4, 80, ARP, 3),
                (8, 4, 88, ARP, 2), (10, 4, 76, ARP, 1), (12, 4, 82, ARP, 2), (14, 4, 72, ARP, 3)]),
    ("RIPPLE", 1, [(i, 3, 92 - 6 * (i % 4) - (10 if i % 2 else 0), ARP, [0, 2, 1, 3, 2, 1, 3, 2][i % 8], )
                   for i in range(16)]),
    ("BOSSA", 1, [(0, 3, 96, BLOCK, 0), (3, 3, 74, UPPER, 0), (6, 4, 84, BLOCK, 0), (10, 3, 74, UPPER, 0),
                  (13, 3, 80, BLOCK, 0)]),
    ("STRUM", 1, [(0, 12, 96, BLOCK, 0), (12, 4, 60, DOWN, 0)]),
]
NCEV = 16

# ---- bass lines: a bar of 16 steps: the chord's Root, its 5th, its 8ve, or an Approach to the next chord's root
BT = {".": 0, "R": 1, "5": 2, "8": 3, "A": 4}
BASSES = [
    ("LAZY", "R.........R....."),
    ("WHOLE", "R..............."),
    ("BOUNCE", "R......R..R....."),
    ("OCTAVE", "R.....8...R...5."),
    ("WALK", "R.......R.....A."),
    ("DOTTED", "R.....R.....R..."),
    ("HEART", "R..R......R..R.."),
    ("PUSH", "R.........R..5.."),
]

# ---- grooves: kick, snare, hat, perc; x a hit, o a ghost
GROOVES = [
    ("SIMPLE", "x.........x.....", "....x.......x...", "x.x.x.x.x.x.x.x.", "................"),
    ("BOOMBAP", "x.....x...x.....", "....x.......x...", "x.x.x.x.x.x.x.x.", "..............x."),
    ("DILLA", "x......x..x.....", "....x..o....x...", "x.xox.x.x.xox.x.", "................"),
    ("DUSTY", "x..x......x.....", "....x.......x..o", "x.x.x.x.x.x.x.xx", "................"),
    ("HALF", "x.........x.....", "........x.......", "x...x...x...x...", "..x...x...x...x."),
    ("LAZY", "x.....o...x..x..", "....x.......x...", "..x...x...x...x.", "................"),
    ("SWING", "x..o..x...x.....", "....x..o.o..x...", "x.xxx.xxx.xxx.xx", "................"),
    ("BARE", "x.......x.......", "....x.......x...", "................", "................"),
    ("SHUFFLE", "x.....x..xx.....", "....x.......x...", "xoxoxoxoxoxoxoxo", "................"),
    ("RIMS", "x..x..x...x..x..", "....x.......x...", "x.x.x.x.x.x.x.x.", "..x.....x..x...."),
]
DL = {".": 0, "o": 1, "x": 2}


# ---- toplines: eight bars, in C: "step note length" a bar. Played over other chords, a note that would clash
# moves to the nearest one that fits (the chord types' masks)
def mel(name, bars):
    out = [0] * 128
    assert len(bars) == 8, name
    for b, bar in enumerate(bars):
        for ev in bar.split(","):
            if not ev.strip():
                continue
            st, note, ln = ev.split()
            st, ln = int(st), int(ln)
            assert out[b * 16 + st] == 0, (name, b, st)
            out[b * 16 + st] = midi(note)
            for i in range(1, ln):
                if b * 16 + st + i < 128:
                    out[b * 16 + st + i] = 0x80
    return name, out


MELS = [
    mel("COFFEE", ["2 E5 4, 6 D5 2, 8 A4 6", "2 B4 4, 6 D5 2, 8 E5 6", "0 G5 6, 6 E5 2, 8 D5 2, 10 B4 6",
                   "2 C#5 4, 6 F5 4, 10 E5 4", "0 C5 2, 2 E5 2, 4 G5 6, 10 E5 4", "2 Ab4 4, 6 C5 2, 8 D5 6",
                   "0 B4 4, 4 D5 2, 6 G4 6", "2 C#5 4, 6 E5 2, 8 A4 8"]),
    mel("RAINY", ["2 E4 4, 6 G4 2, 8 A4 6", "2 G4 4, 6 E4 2, 8 D4 6", "0 E4 4, 4 F4 2, 6 A4 6",
                  "2 B4 4, 6 A4 2, 8 E4 6", "0 C5 6, 6 A4 2, 8 G4 2, 10 E4 6", "2 D4 4, 6 G4 4, 10 B4 4",
                  "0 C5 4, 4 B4 2, 6 G4 6", "2 E4 4, 6 D4 2, 8 C4 8"]),
    mel("SLEEPY", ["0 E5 8, 8 D5 8", "0 C5 12", "0 A4 8, 8 C5 8", "0 D5 12", "0 E5 8, 8 G5 8", "0 E5 12",
                   "0 D5 8, 8 C5 8", "0 A4 14"]),
    mel("HOPS", ["0 G4 2, 3 A4 2, 6 C5 4, 12 A4 2", "2 G4 2, 6 E4 6", "0 G4 2, 3 A4 2, 6 C5 2, 8 D5 4",
                 "2 C5 2, 6 A4 6", "0 C5 2, 3 D5 2, 6 E5 4, 12 D5 2", "2 C5 2, 6 A4 6",
                 "0 G4 2, 3 A4 2, 6 C5 2, 8 D5 4, 14 E5 2", "0 C5 10"]),
    mel("CALL", ["0 E5 2, 2 G5 2, 4 A5 4", "8 G5 2, 10 E5 2, 12 D5 4", "0 E5 2, 2 G5 2, 4 A5 4, 12 C6 4",
                 "0 A5 2, 2 G5 2, 4 E5 8", "0 D5 2, 2 E5 2, 4 G5 4", "8 E5 2, 10 D5 2, 12 C5 4",
                 "0 D5 2, 2 E5 2, 4 G5 4, 10 E5 2, 12 D5 2", "0 C5 12"]),
    mel("DRIP", ["4 G5 2, 12 E5 2", "6 D5 2", "4 G5 2, 12 A5 2", "6 E5 4", "4 G5 2, 12 E5 2", "6 D5 2, 10 C5 2",
                 "4 E5 2, 12 D5 2", "4 C5 8"]),
    mel("STAIRS", ["0 C5 2, 2 D5 2, 4 E5 2, 6 G5 6", "0 A5 2, 2 G5 2, 4 E5 2, 6 D5 6",
                   "0 C5 2, 2 D5 2, 4 E5 2, 6 A5 6", "0 G5 4, 4 E5 4, 8 D5 6", "0 C5 2, 2 D5 2, 4 E5 2, 6 G5 6",
                   "0 A5 2, 2 G5 2, 4 E5 2, 6 G5 6", "0 E5 2, 2 D5 2, 4 C5 2, 6 A4 6", "0 C5 12"]),
    mel("HUM", ["0 G4 6, 8 A4 4, 12 G4 4", "0 E4 10", "0 G4 6, 8 A4 4, 12 C5 4", "0 D5 10",
                "0 E5 6, 8 D5 4, 12 C5 4", "0 A4 10", "0 C5 4, 4 A4 4, 8 G4 4, 12 E4 4", "0 G4 12"]),
]

# ---- the factory tapes. A scene: bars, the clips of the keys, the bass, the drums and the topline (1.. = a clip
# above). The song: scenes in order, each with the tracks that play: K B D L
def nm(lst, name):
    return [n[0] if isinstance(n, tuple) else n["name"] for n in lst].index(name)


FX = ["WOBBLE", "VINYL", "TONE", "DRIVE", "SPACE", "PUMP", "CRUSH", "RAIN"]


def tape(name, bpm, key, swing, snds, scenes, song, vol=(24, 24, 24, 22), tone=(18, 12, 22, 20), k3=(1, 26, 8, 12),
         k4=(8, 8, 12, 0), fx=(10, 12, 24, 8, 12, 14, 0, 0)):
    """key: semitones from C, -5..6. k3: the keys' rhythm (a COMPS index), the bass's length, the drums' fill, the
    topline's echo. k4: the keys' strum, the bass's drive, the drums' lazy, the topline's vibrato."""
    sc = []
    for s in scenes:
        bars, p, b, g, m = s
        sc.append((bars, nm(PROGS, p) + 1, nm(BASSES, b) + 1, nm(GROOVES, g) + 1, nm(MELS, m) + 1))
    sc = sc[:3]
    sc.append((min(4, sc[0][0]),) + sc[0][1:])     # D: the first four bars of A, for the way in and the way out
    chain = []
    for letter, tracks in song:
        mute = sum(1 << i for i, t in enumerate("KBDL") if t not in tracks)
        chain.append(("ABCD".index(letter) + 1) | mute << 4)
    assert len(chain) <= 16
    chain += [0] * (16 - len(chain))
    return dict(name=name, bpm=bpm, key=key + 5, swing=swing,
                snd=(nm(KEYS, snds[0]), nm(BASS, snds[1]), [k[0] for k in KITS].index(snds[2]), nm(LEAD, snds[3])),
                vol=vol, tone=tone, k3=(nm(COMPS, k3[0]) if isinstance(k3[0], str) else k3[0],) + tuple(k3[1:]),
                k4=k4, fx=fx, sc=sc, chain=chain)


ARR = [("D", "K"), ("A", "KBD"), ("A", "KBDL"), ("B", "KBDL"), ("C", "KBL"), ("A", "KBDL"), ("B", "KBDL"), ("D", "KB")]
TAPES = [
    tape("MORNING COFFEE", 76, 0, 58, ("RHODES", "SUB", "DUSTY", "WHISTLE"),
         [(8, "COFFEE", "LAZY", "BOOMBAP", "COFFEE"), (8, "COFFEE", "BOUNCE", "DUSTY", "HOPS"),
          (8, "VELVET", "WHOLE", "BARE", "SLEEPY"), (4, "SUNDAY", "LAZY", "SIMPLE", "DRIP")], ARR,
         k3=("LAZY", 26, 8, 12)),
    tape("RAINY WINDOW", 70, 5, 56, ("FELT", "ROUND", "BLANKET", "KALIMBA"),
         [(8, "RAINY", "LAZY", "SIMPLE", "RAINY"), (8, "RAINY", "WHOLE", "HALF", "DRIP"),
          (8, "MOSS", "LAZY", "BARE", "HUM"), (4, "TIDE", "LAZY", "SIMPLE", "SLEEPY")], ARR,
         tone=(15, 11, 9, 18), k3=("LAZY", 26, 4, 16), fx=(20, 16, 20, 8, 14, 14, 0, 14)),
    tape("LATE BUS", 82, -3, 60, ("WURLI", "ROUND", "BOOMBAP", "PLUCK"),
         [(4, "BUS", "BOUNCE", "DILLA", "HOPS"), (8, "STEPS", "OCTAVE", "DILLA", "STAIRS"),
          (4, "BUS", "WHOLE", "BARE", "DRIP"), (8, "SUNDAY", "BOUNCE", "SWING", "CALL")], ARR,
         k3=("PUSH", 22, 12, 10), k4=(4, 12, 16, 0)),
    tape("STUDY HALL", 72, 2, 57, ("GLASS", "SUB", "RIMSHOT", "MUSICBOX"),
         [(8, "LETTER", "LAZY", "RIMS", "SLEEPY"), (8, "LETTER", "DOTTED", "SIMPLE", "CALL"),
          (8, "DUSK", "WHOLE", "BARE", "DRIP"), (8, "PORCH", "LAZY", "RIMS", "HUM")], ARR,
         k3=("PULSE", 26, 6, 14)),
    tape("SUNDAY PORCH", 86, -2, 61, ("NYLON", "PLUCK", "BRUSH", "FLUTE"),
         [(8, "PORCH", "WALK", "SHUFFLE", "HUM"), (8, "SUNDAY", "WALK", "SHUFFLE", "STAIRS"),
          (8, "PORCH", "WHOLE", "BARE", "SLEEPY"), (8, "ATTIC", "HEART", "SWING", "HOPS")], ARR,
         k3=("BOSSA", 20, 10, 8), k4=(14, 6, 10, 10), fx=(8, 8, 26, 6, 12, 8, 0, 0)),
    tape("DUSK", 68, 4, 55, ("PAD", "SUB", "BLANKET", "BELL"),
         [(8, "DUSK", "WHOLE", "HALF", "DRIP"), (8, "DUSK", "LAZY", "SIMPLE", "SLEEPY"),
          (8, "NEON", "WHOLE", "BARE", "DRIP"), (8, "ATTIC", "LAZY", "HALF", "HUM")], ARR,
         tone=(16, 11, 10, 18), k3=("HOLD", 30, 4, 20), fx=(14, 14, 21, 6, 20, 16, 0, 6)),
    tape("POLAROID", 78, -4, 59, ("VIBES", "TAPE", "DUSTY", "REED"),
         [(8, "VELVET", "PUSH", "DUSTY", "COFFEE"), (8, "LAMP", "BOUNCE", "BOOMBAP", "CALL"),
          (8, "VELVET", "WHOLE", "BARE", "HUM"), (8, "NEON", "LAZY", "LAZY", "DRIP")], ARR,
         k3=("BROKEN", 24, 10, 10), fx=(16, 18, 22, 12, 10, 12, 6, 0)),
    tape("NIGHT SHIFT", 64, 1, 54, ("ORGAN", "SUB", "808", "EPIANO"),
         [(8, "NEON", "WHOLE", "HALF", "SLEEPY"), (8, "NEON", "HEART", "LAZY", "HOPS"),
          (8, "MOSS", "WHOLE", "BARE", "DRIP"), (8, "TIDE", "LAZY", "HALF", "RAINY")], ARR,
         tone=(14, 12, 12, 16), k3=("HOLD", 30, 6, 18), fx=(12, 10, 20, 10, 18, 20, 0, 4)),
]

# ---- the 5 x 7 character-generator font (ASCII 32..126; five columns a glyph, bit 0 the top row)
FONT = bytes.fromhex(
    "0000000000" "00005f0000" "0007000700" "147f147f14" "242a7f2a12" "2313086462" "3649552250" "0005030000"
    "001c224100" "0041221c00" "14083e0814" "08083e0808" "0050300000" "0808080808" "0060600000" "2010080402"
    "3e5149453e" "00427f4000" "4261514946" "2141454b31" "1814127f10" "2745454539" "3c4a494930" "0171090503"
    "3649494936" "064949291e" "0036360000" "0056360000" "0814224100" "1414141414" "0041221408" "0201510906"
    "324979413e" "7e1111117e" "7f49494936" "3e41414122" "7f4141221c" "7f49494941" "7f09090901" "3e4149497a"
    "7f0808087f" "00417f4100" "2040413f01" "7f08142241" "7f40404040" "7f020c027f" "7f0408107f" "3e4141413e"
    "7f09090906" "3e4151215e" "7f09192946" "4649494931" "01017f0101" "3f4040403f" "1f2040201f" "3f4038403f"
    "6314081463" "0708700807" "6151494543" "007f414100" "0204081020" "0041417f00" "0402010204" "4040404040"
    "0001020400" "2054545478" "7f48444438" "3844444420" "384444487f" "3854545418" "087e090102" "0c5252523e"
    "7f0804047c" "00447d4000" "2040443d00" "7f10284400" "00417f4000" "7c0418047c" "7c0804047c" "3844444438"
    "7c14141408" "081414187c" "7c08040408" "4854545420" "043f444020" "3c4040207c" "1c2040201c" "3c4030403c"
    "4428102844" "0c5050503c" "4464544c44" "0008364100" "00007f0000" "0041360800" "0804081008")
assert len(FONT) == 95 * 5


def arr(vals, per=16, fmt="{}"):
    vals = list(vals)
    lines = [", ".join(fmt.format(v) for v in vals[i:i + per]) for i in range(0, len(vals), per)]
    return "{\n    " + ",\n    ".join(lines) + "\n}"


def snd_c(s):
    return ("{{{name}, {rm}, {rt}, {i0}, {i1}, {ik}, {att}, {dk}, {sus}, {rk}, {tine}, {tk}, {gain}, {trem}, {trate}u, "
            "{vib}, {vrate}u}}").format(**{**s, "name": cname(s["name"], 9)})


def drum_c(d):
    return ("{{{f0}u, {f1}u, {pk}, {slvl}, {sk}, {nlvl}, {natt}, {nk}, {ftype}, {ff}, {fq}, {click}, {drive}}}"
            .format(**d))


def main():
    o = ["/* generated by tools/gen_lofi.py: do not edit */"]
    o.append("static const int16_t LSIN[4096] = " +
             arr((round(32767 * math.sin(2 * math.pi * i / 4096)) for i in range(4096))) + ";")
    o.append("static const uint32_t NOTE_INC[128] = " +
             arr((round(440 * 2 ** ((m - 69) / 12) * 2 ** 32 / FS) for m in range(128)), 8, "{}u") + ";")
    # a one-pole low-pass's coefficient (Q15) for the TONE knobs, 0..32: 220 Hz .. 16 kHz
    tone = [min(32767, round(32768 * (1 - math.exp(-2 * math.pi * 220 * (16000 / 220) ** (i / 32) / FS))))
            for i in range(33)]
    o.append("static const uint16_t TONE_K[33] = " + arr(tone) + ";")
    o.append("static const uint8_t FONT5[95 * 5] = " + arr(FONT, 20) + ";")

    o.append("typedef struct { char name[9]; uint16_t rm, rt; int32_t i0, i1; uint16_t ik; int32_t att; uint16_t dk; "
             "int32_t sus; uint16_t rk; int16_t tine; uint16_t tk, gain; int16_t trem; uint32_t trate; int16_t vib; "
             "uint32_t vrate; } snd_t;")
    for nme, lst in (("KEYS", KEYS), ("BASS", BASS), ("LEAD", LEAD)):
        o.append(f"#define N{nme} {len(lst)}u")
        o.append(f"static const snd_t SND_{nme}[N{nme}] = {{\n    " + ",\n    ".join(snd_c(s) for s in lst) + "\n};")
    o.append("typedef struct { uint32_t f0, f1; uint16_t pk; int16_t slvl; uint16_t sk; int16_t nlvl; uint16_t natt, nk; "
             "uint8_t ftype; uint16_t ff, fq; int16_t click; uint16_t drive; } drum_t;")
    o.append(f"#define NKIT {len(KITS)}u")
    o.append("typedef struct { char name[9]; drum_t d[4]; } kit_t;")
    o.append("static const kit_t KIT[NKIT] = {\n    " + ",\n    ".join(
        "{" + cname(k[0], 9) + ", {" + ", ".join(drum_c(d) for d in k[1:]) + "}}" for k in KITS) + "\n};")

    o.append(f"#define NTYPE {len(TYPES)}u")
    o.append("typedef struct { char name[6]; uint8_t pc[4]; uint16_t mask; } ctype_t;")
    o.append("static const ctype_t CTYPE[NTYPE] = {\n    " + ",\n    ".join(
        "{%s, {%s}, 0x%03X}" % (cname(t[0], 6), ", ".join(map(str, t[1])), sum(1 << p for p in t[2]))
        for t in TYPES) + "\n};")
    o.append("static const uint8_t BANK[27] = " + arr(TI[t] for t in BANK) + ";")

    def named(nme, n, items, body, width):
        o.append(f"#define N{nme} {len(items)}u")
        o.append(f"static const char {nme}_NAME[N{nme}][9] = {{" + ", ".join(cname(i[0], 9) for i in items) + "};")
        o.append(f"static const uint8_t {nme}[N{nme}][{n}] = {{\n    " +
                 ",\n    ".join("{" + ", ".join(map(str, body(i))) + "}" for i in items) + "\n};")

    named("PROG", 16, PROGS, lambda p: p[1], 16)
    named("BASSP", 16, BASSES, lambda b: [BT[c] for c in b[1]], 16)
    named("GROOVE", 64, GROOVES, lambda g: [DL[c] for row in g[1:] for c in row], 16)
    named("MELC", 128, MELS, lambda m: m[1], 16)
    o.append(f"#define NCOMP {len(COMPS)}u")
    o.append(f"#define NCEV {NCEV}u")
    o.append("typedef struct { char name[9]; uint8_t implicit, n; uint8_t ev[NCEV][5]; } comp_t;")
    rows = []
    for name, imp, evs in COMPS:
        assert len(evs) <= NCEV
        rows.append("{%s, %d, %d, {%s}}" % (cname(name, 9), imp, len(evs),
                                             ", ".join("{%d, %d, %d, %d, %d}" % e for e in evs) or "{0}"))
    o.append("static const comp_t COMP[NCOMP] = {\n    " + ",\n    ".join(rows) + "\n};")

    o.append(f"#define NTAPE {len(TAPES)}u")
    o.append("typedef struct { char name[16]; uint8_t bpm, key, swing, snd[4], vol[4], tone[4], k3[4], k4[4], fx[8], "
             "sc[4][5], chain[16]; } tape_t;")
    rows = []
    for t in TAPES:
        f = lambda v: "{" + ", ".join(map(str, v)) + "}"
        rows.append("{%s, %d, %d, %d, %s, %s, %s, %s, %s, %s, {%s}, %s}" % (
            cname(t["name"], 16), t["bpm"], t["key"], t["swing"], f(t["snd"]), f(t["vol"]), f(t["tone"]), f(t["k3"]),
            f(t["k4"]), f(t["fx"]), ", ".join(f(s) for s in t["sc"]), f(t["chain"])))
    o.append("static const tape_t TAPE[NTAPE] = {\n    " + ",\n    ".join(rows) + "\n};")
    # the reverb: four combs and two all-passes a side, at half the sample rate; ~1.9 s
    combs = [653, 821, 907, 967]
    fb = [round(32768 * 10 ** (-3 * n / (1.9 * FS / 2))) for n in combs]
    o.append("static const uint16_t RV_LEN[4] = " + arr(combs) + ";")
    o.append("static const uint16_t RV_FB[4] = " + arr(fb) + ";")
    OUT.write_text("\n".join(o) + "\n")
    print(f"gen_lofi: {OUT} ({OUT.stat().st_size} B): {len(KEYS)}+{len(BASS)}+{len(LEAD)} sounds, {len(KITS)} kits, "
          f"{len(PROGS)} progressions, {len(COMPS)} rhythms, {len(BASSES)} bass lines, {len(GROOVES)} grooves, "
          f"{len(MELS)} toplines, {len(TAPES)} tapes")


if __name__ == "__main__":
    main()
