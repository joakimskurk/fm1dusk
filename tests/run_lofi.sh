#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Dusk's own tests, on a computer: no FM-1, no toolchain (cc, Python 3 with numpy and Pillow).
#   sh tests/run_lofi.sh
# The tables are the generator's, the app builds without a warning for both boards, a tape plays (its level, its
# clock and its swing), every page draws, the keys write clips, the shelf keeps and loads, two runs sound the same.
set -e
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p "$OUT"
P=tests/lofi/check.py
fail=0
run() { echo "== $1"; shift; "$@" || { echo "   FAILED"; fail=1; }; }
app() { "$OUT/fm1dusk" "$OUT/$1" "tests/scripts/lofi_$1.txt" > "$OUT/$1.log"; }

run "the tables are tools/gen_lofi.py's" sh -c "cp firmware/gen/lofi_gen.h $OUT/lofi_gen.was && python3 tools/gen_lofi.py >/dev/null && cmp firmware/gen/lofi_gen.h $OUT/lofi_gen.was"
run "the app builds for the host, no warnings (FM-1, SMK-37)" sh -c "
    cc -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers -Werror -DDINGHY_HOST -Ifirmware/hal -Ifirmware/src -Ifirmware/gen -o $OUT/fm1dusk firmware/src/fm1dusk.c &&
    cc -O2 -Wall -Wno-unused-function -Wno-unused-variable -Werror -DDINGHY_HOST -DBOARD_SMK37=1 -Ifirmware/hal -Ifirmware/src -Ifirmware/gen -o $OUT/fm1dusk-smk37 firmware/src/fm1dusk.c"
run "no float, no double in the firmware's own code" sh -c "! grep -nE '\\b(float|double)\\b' firmware/src/fm1dusk.c firmware/src/lofi_engine.h firmware/src/lofi_draw.h firmware/gen/lofi_gen.h"

rm -f "$OUT"/tour-*.ppm "$OUT"/edit-*.ppm "$OUT"/tapes-*.ppm "$OUT"/stop-*.ppm
run "a tour: the first tape plays, every page is opened" app tour
run "its sound: -16 .. -9 dB, nothing clipped, stereo, no DC" python3 $P sound "$OUT/tour.wav" -16 -9
for t in 2500:6500 6500:9500 9500:12500 12500:15500 15500:18500 18500:20500 20500:22500; do
    run "the pages differ: ${t%:*} ms, ${t#*:} ms" python3 $P differ "$OUT/tour-${t%:*}.ppm" "$OUT/tour-${t#*:}.ppm" 2000
done
for b in "0 56 4000" "56 200 3000" "200 240 800"; do
    set -- $b
    run "HOME: the band $1..$2 is drawn" python3 $P band "$OUT/tour-2500.ppm" $1 $2 $3
done
run "the clock: the kicks of DILLA at 76 BPM, steps 0 7 10, the 7 late by the swing (58 %)" sh -c "
    $OUT/fm1dusk $OUT/kick tests/scripts/lofi_kick.txt > $OUT/kick.log && python3 $P kicks $OUT/kick.wav 76 7.16 2.84 6"
run "stopped again, the pages still draw" app stop
run "after a stop: the keys' page" python3 $P differ "$OUT/stop-2600.ppm" "$OUT/stop-3400.ppm" 5000
run "after a stop: a knob turned shows" python3 $P differ "$OUT/stop-3400.ppm" "$OUT/stop-4000.ppm" 40
run "writing: chords with REC, a groove, a bass line, a topline, the song, a tape kept" app edit
run "the tape was kept in the shelf (blob 0)" grep -q "blob 0 saved" "$OUT/edit.log"
run "what was written is on the screen" python3 $P differ "$OUT/edit-1300.ppm" "$OUT/tour-6500.ppm" 1500
run "the shelf: a tape kept, a factory tape, the kept one again" app tapes
run "the factory tape is another" python3 $P differ "$OUT/tapes-2700.ppm" "$OUT/tapes-5200.ppm" 1500
run "the kept tape comes back as it was" python3 $P same "$OUT/tapes-2700.ppm" "$OUT/tapes-7600.ppm"
run "the tape as it is, is kept by itself (settings), when still" grep -q "settings 0 saved" "$OUT/tapes.log"
run "two runs sound the same" sh -c "cp $OUT/tour.wav $OUT/tour1.wav && $OUT/fm1dusk $OUT/tour tests/scripts/lofi_tour.txt >/dev/null && cmp $OUT/tour.wav $OUT/tour1.wav"
run "the SMK-37 build runs (its own buttons: Dinghy's default script)" sh -c "$OUT/fm1dusk-smk37 $OUT/smk >/dev/null"

# Dusk for the browser (web/app/): the firmware compiled to WebAssembly (where clang and wasm-ld are), run in Node
WASM=-
if sh web/app/build.sh build/web/fm1dusk.wasm >/dev/null 2>&1; then
    WASM=build/web/fm1dusk.wasm
    if command -v node >/dev/null; then
        run "Dusk for the browser, in Node: it plays, its screen, its lights, a key, the shelf kept by the page" \
            node tests/web/app_test.mjs $WASM
    else
        echo "== Dusk for the browser in Node: skipped (no node)"
    fi
else
    echo "== Dusk for the browser: skipped (no clang with wasm-ld: apt install clang lld)"
fi

# the site (GitHub Pages): made from the package (CI builds it before the tests; here, if it is there), its installer
# against a simulated FM-1 and its Try it here in Chromium if Playwright is there
if [ -f build/fm1dusk.fwsc ]; then
    run "the site: the page, its installer, the package and Dusk for the browser (web/make_site.py)" sh -c \
        "python3 web/make_site.py build/fm1dusk.fwsc test build/site $WASM && test -s build/site/index.html && \
         test -s build/site/firmware/fm1dusk-test.fwsc && test -s build/site/screens/pages.png"
    if node -e "require('playwright')" 2>/dev/null; then
        run "the site's installer against a simulated FM-1: both steps, every byte, and an install cut off" \
            node tests/web/install_sim.js build/site
        if [ "$WASM" != - ]; then
            run "the site's Try it here in Chromium: Start, the lights, PLAY, a page, a clip, a key" \
                node tests/web/app_page.js build/site
        fi
    else
        echo "== the site's installer and its Try it here in Chromium: skipped (no Playwright)"
    fi
else
    echo "== the site: skipped (no build/fm1dusk.fwsc: make fw)"
fi

[ $fail = 0 ] && echo "run_lofi: all passed" || { echo "run_lofi: FAILED"; exit 1; }
