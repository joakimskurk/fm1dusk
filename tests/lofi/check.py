#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Dusk's host tests: what a run of the app on a computer (tests/host.c) sounded and looked like.

  check.py sound WAV LO HI          its level between LO and HI dB (RMS), nothing clipped, both channels alive
  check.py kicks WAV BPM A B C ..   the kicks (what is under 110 Hz) come A, B, C .. 16ths apart, round and round
  check.py differ A.ppm B.ppm N     two screens differ in at least N pixels
  check.py same A.ppm B.ppm         two screens are the same
  check.py band A.ppm Y0 Y1 N       at least N pixels of rows Y0..Y1 are not the background
  check.py isr REPORT MAX           fm1-emu's report: the interrupts take at most MAX % of the chip
  check.py bmp A.bmp N              fm1-emu's screen: at least N pixels are not the background
  check.py bmpdiffer A.bmp B.bmp N  two of its screens differ in at least N pixels
"""
import re
import sys
import numpy as np


def wav(path):
    raw = open(path, "rb").read()
    assert raw[:4] == b"RIFF" and raw[8:12] == b"WAVE", path
    return np.frombuffer(raw[44:], dtype="<i2").reshape(-1, 2) / 32768.0


def ppm(path):
    raw = open(path, "rb").read()
    assert raw.startswith(b"P6 240 240 255\n"), path
    return np.frombuffer(raw[15:], dtype=np.uint8).reshape(240, 240, 3)


def main():
    cmd, a = sys.argv[1], sys.argv[2:]
    if cmd == "sound":
        x = wav(a[0])[44100:]
        rms = 20 * np.log10(np.sqrt((x ** 2).mean()) + 1e-12)
        peak = np.abs(x).max()
        side = np.sqrt(((x[:, 0] - x[:, 1]) ** 2).mean())
        print(f"   rms {rms:.1f} dB, peak {20 * np.log10(peak + 1e-12):.1f} dB, L-R {20 * np.log10(side + 1e-12):.1f} dB, "
              f"DC {x.mean():.5f}")
        ok = float(a[1]) <= rms <= float(a[2]) and peak < 0.97 and side > 1e-4 and abs(x.mean()) < 0.002
    elif cmd == "kicks":
        x = wav(a[0]).mean(axis=1)
        bpm, want = float(a[1]), [float(v) for v in a[2:]]
        # under 110 Hz: a moving average twice over
        k = np.ones(200) / 200
        low = np.convolve(np.convolve(x, k, "same"), k, "same")
        c = np.cumsum(np.concatenate([[0.0], low ** 2]))        # its level over 30 ms (longer than a cycle), every 5 ms
        env = np.sqrt((c[1323::220] - c[:-1323:220]) / 1323)
        thr = env.max() * 0.35
        on = [i for i in range(8, len(env)) if env[i] > thr and env[i] > 1.6 * env[i - 8]]   # up, and fast
        keep = []
        for t in on:                                            # (one kick: not again within 0.15 s)
            if not keep or t - keep[-1] > 30:
                keep.append(t)
        on = keep
        gaps = np.diff(on) * 220 / 44100 / (15 / bpm)           # in 16ths
        print("   kicks, 16ths apart:", " ".join(f"{g:.2f}" for g in gaps[:12]))
        ok = len(gaps) >= 2 * len(want) and all(abs(g - want[i % len(want)]) < 0.12 for i, g in enumerate(gaps))
    elif cmd in ("differ", "same"):
        n = int((ppm(a[0]) != ppm(a[1])).any(axis=2).sum())
        print(f"   {n} pixels differ")
        ok = n >= int(a[2]) if cmd == "differ" else n == 0
    elif cmd == "band":
        img = ppm(a[0])[int(a[1]):int(a[2])]
        n = int((img != img[0, 0]).any(axis=2).sum())
        print(f"   {n} pixels drawn")
        ok = n >= int(a[3])
    elif cmd == "isr":
        m = re.search(r"cpu0: ([\d.]+)% in an ISR, ([\d.]+)% nested", open(a[0], errors="replace").read())
        tot = float(m[1]) + float(m[2]) if m else 0.0
        print(f"   the interrupts take {tot:.1f} % of the chip" if m else "   no ISR line in the report")
        ok = bool(m) and 1.0 < tot <= float(a[1])
    elif cmd in ("bmp", "bmpdiffer"):
        from PIL import Image
        img = np.asarray(Image.open(a[0]).convert("RGB")).astype(int)
        if cmd == "bmp":
            n = int((np.abs(img - img[120, 2]).sum(axis=2) > 24).sum())
        else:
            n = int((np.abs(img - np.asarray(Image.open(a[1]).convert("RGB")).astype(int)).sum(axis=2) > 24).sum())
        print(f"   {n} pixels")
        ok = n >= int(a[-1])
    else:
        raise SystemExit(__doc__)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
