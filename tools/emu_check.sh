#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Dusk's package in fm1-emu: 8 s of the real firmware. PLAY at 0.5 s (the first tape, its scene A), the drums'
# page at 5 s. Then, from its report, its sound and its screens (build/emu/):
#   - the interrupts' share of the chip: the whole engine with every voice it has going must leave room
#   - it sounds: a level as on the host, nothing clipped
#   - the screen: the cassette, the lanes and the knobs are drawn; the drums' page is another
#   tools/emu_check.sh build/fm1dusk.fwsc        FM1_EMU: the fm1-emu binary (tools/emu_ci.sh builds it)
#   tools/emu_check.sh build/fm1dusk.bin         the app alone, when there is no SDK to make a package with
set -e
PKG="${1:-build/fm1dusk.fwsc}"
EMU="${FM1_EMU:-$HOME/fm1-emu/target/release/fm1-emu}"
OUT=build/emu
P=tests/lofi/check.py
rm -rf "$OUT"
mkdir -p "$OUT/work"
case "$PKG" in
*.bin)                                  # the app alone (no SDK, no package): at its place in the flash's window
    (head -c 288 /dev/zero | tr '\0' '\377'; cat "$PKG") > "$OUT/app.bin"
    SRC="--bin app.bin --entry 0x02000120" ;;
*)
    cp "$PKG" "$OUT/pkg.fwsc"
    SRC="--fwsc pkg.fwsc" ;;
esac
s0=$(date +%s)
(cd "$OUT" && timeout "${EMU_TIMEOUT:-1200}" "$EMU" $SRC --ms 8000 --keys '500:PLAY,5000:SEQ' \
    --wav out.wav --snap 4000,7000 >report.txt 2>&1) || true
echo "emu_check: 8 s of the firmware took $(( $(date +%s) - s0 )) s to emulate"
grep -E "in an ISR|^time " "$OUT/report.txt" | sed 's/^/  emu: /' || true
fail=0
run() { echo "== $1"; shift; "$@" || fail=1; }
run "the chip: the interrupts take under 70 % of it" python3 $P isr "$OUT/report.txt" 70
run "it plays: -30 .. -9 dB, nothing clipped" python3 $P sound "$OUT/out.wav" -30 -9
run "HOME is drawn: the cassette, the lanes, the knobs" python3 $P bmp "$OUT/work/lcd_4000.bmp" 20000
run "the drums' page is another" python3 $P bmpdiffer "$OUT/work/lcd_4000.bmp" "$OUT/work/lcd_7000.bmp" 2000
[ $fail = 0 ] && echo "emu_check: all passed" || { echo "emu_check: FAILED"; exit 1; }
