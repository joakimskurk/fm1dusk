# PLATFORM

Dusk runs on **Dinghy 0.5**, imported as source.

- upstream: https://github.com/zvenson/dinghy
- commit: `ea1b6bf` (2026-11, after "Dinghy 0.5" `1675934`; the release workflow's file pattern only)
- Dinghy's docs stay in `docs/` (`API.md`, `RULES.md`, `MAKE-IT-YOURS.md`, `SMK37.md`). Its README is
  `docs/DINGHY_README.md` (the toolchain, the SDK, the host mode).

## What differs from upstream

Kept small, so a later Dinghy can be merged by diffing these files.

| file | change |
| --- | --- |
| `firmware/src/dinghy.c` | replaced by `fm1dusk.c`: the app; `APP_NAME "DUSK"`, `STORE_MAGIC` "DUSK", `CV_MAX` 240 x 72; with it `lofi_engine.h`, `lofi_draw.h` and `firmware/gen/lofi_gen.h` (made by `tools/gen_lofi.py`) |
| `tools/build.py` | compiles `fm1dusk.c`; outputs `fm1dusk.*` and `fm1dusk.syms`; one identity for every build and release, `FM-1_963` (`SMK-37 Pro_963`, ...), so `--release X.Y` only sets the version text; loader screen "DUSK" in peach `0xF4AB`; data family `DUSK` |
| `build.sh` | a comment |
| `tests/run_host.sh` | builds `fm1dusk.c` as `build/host/fm1dusk` |
| `tests/run_tests.sh` | `fm1dusk.*` names; the SMK-37 packages' identities `_963` |
| `tests/install_test.py` | `fm1dusk.*` names |
| `README.md` | moved to `docs/DINGHY_README.md`; Dusk's README in its place |
| `.github/workflows/release.yml` | replaced by `build.yml` (zvenson/fm1-build) |
| `web/` (Dinghy has none) | `fm1pkg.js` and `fm1ota.js` from SLOOP (`bac9c05`, unchanged: Dinghy's `tests/install_test.py` checks `fm1pkg.js` against `tools/fm1pkg_make.py`). No page yet. |

Dusk's own, beside those: `tools/gen_lofi.py`, `tools/render_tape.sh`, `tools/emu_check.sh`, `tools/emu_ci.sh`,
`tests/run_lofi.sh`, `tests/lofi/`, `tests/scripts/lofi_*.txt`, `docs/screens/`.

Nothing in `firmware/hal/`, `firmware/loader/`, `firmware/app.ld` or `tests/host.c` is changed. `platform.c` has one
change: with `DINGHY_WEB` (and `DINGHY_HOST`) it includes `web/app/web_host.c` (a browser's FM-1) instead of
`tests/host.c`; Dusk has no `web/app/` yet, so do not define `DINGHY_WEB`.

## Updating Dinghy

1. Diff the new upstream against the commit above for the imported paths.
2. Apply upstream changes; re-apply the table above.
3. `make test`, CI's build, a boot in fm1-emu.
4. Update the commit here.
