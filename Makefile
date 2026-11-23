# Dusk: the everyday commands. The firmware itself is built by ./build.sh (docs/DINGHY_README.md: the toolchain).
#   make test       the app's own tests on this computer: no FM-1, no toolchain (cc, Python 3 with numpy and Pillow)
#   make host       the app on this computer with a tour of its pages: build/host/fm1dusk.wav and its screens
#   make tape N=2   a factory tape's song as the FM-1 plays it: build/host/tape-2.wav
#   make gen        firmware/gen/lofi_gen.h from tools/gen_lofi.py (the sounds, the chords, the clips, the tapes)
#   make fw         the FM-1 package build/fm1dusk.fwsc (JieLi toolchain in ~/.jieli, SDK in ~/fw-AC79_AIoT_SDK)
#   make emu        the package in fm1-emu: how much of the chip it takes, that it plays
#   make check      what CI runs after the package: the app's tests, Dinghy's platform tests, fm1-emu
PY ?= python3
N ?= 1

.PHONY: test host tape gen fw check emu clean
test:
	sh tests/run_lofi.sh
host:
	sh tests/run_host.sh tests/scripts/lofi_tour.txt
tape:
	sh tools/render_tape.sh $(N)
gen:
	$(PY) tools/gen_lofi.py
fw:
	PYTHON=$(PY) sh build.sh
	sha256sum build/fm1dusk.fwsc
emu:
	sh tools/emu_check.sh build/fm1dusk.fwsc
check: fw
	sh tests/run_lofi.sh
	sh tests/run_tests.sh
	sh tools/emu_ci.sh
clean:
	rm -rf build/host build/gen build/loader build/emu build/fm1dusk*
