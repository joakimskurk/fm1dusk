#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# A factory tape's song, as the FM-1 would play it, on this computer: build/host/tape-N.wav.
#   tools/render_tape.sh N [SECONDS]      N = 1..8 (the factory tapes on the shelf), 120 s by default
set -e
cd "$(dirname "$0")/.."
N="${1:-1}"; S="${2:-120}"
mkdir -p build/host
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -Ifirmware/hal -Ifirmware/src -Ifirmware/gen -o build/host/fm1dusk firmware/src/fm1dusk.c
# SAVE (the shelf), SELECT to the tape, SEL (load); GLO (the song), SEL (on), PLAY
printf '100 btn 9\n200 enc 0 %d\n300 btn 3\n500 btn 7\n600 btn 3\n800 btn 12\n%d end\n' $((7 + N)) $((S * 1000 + 800)) > build/host/tape.txt
build/host/fm1dusk "build/host/tape-$N" build/host/tape.txt | tail -1
