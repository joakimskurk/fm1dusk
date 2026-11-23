#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Dusk's site (GitHub Pages): its page, its installer in the browser, the firmware.

  index.html                   web/index.html with fm1pkg.js, fm1ota.js (SLOOP's, unchanged) and the metadata
                               inlined: the installer (Web MIDI, Chrome or Edge), the controls, the links
  firmware/fm1dusk-VER.fwsc   the package the page installs (its SHA-256 in the page: a damaged download is
                               never offered)
  screens/                     the pictures and the icon (docs/screens)
  app/                         Dusk itself in the browser: fm1dusk.wasm (web/app/build.sh, the firmware compiled
                               to WebAssembly), worklet.js (it, in the audio thread), dusk.js (its screen, its panel)
  .nojekyll                    (served as it is)

  python3 web/make_site.py build/fm1dusk.fwsc VERSION OUT_DIR [build/web/fm1dusk.wasm | -]

The package must be Dusk's: its identity FM-1_963, its Felucca update loader in it (which the installer
talks to). Made after SLOOP's web/make_site.py (Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton
Instruments; isod89).
"""
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F
SCREENS = ("pages.png", "home.png")           # (the pages; HOME is the icon too)


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def main(pkg, version, out, wasm="build/web/fm1dusk.wasm"):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if product != "FM-1_963":
        raise SystemExit(f"{pkg}: identity {product!r} is not Dusk's (FM-1_963)")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: no Felucca update loader in it: the installer could not write it")
    name = f"fm1dusk-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    meta = json.dumps({"version": version, "product": product, "pkg": "firmware/" + name,
                       "sha256": hashlib.sha256(raw).hexdigest(), "size": len(raw)})
    lib = strip_module((HERE / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1ota.js").read_text(encoding="utf-8"))
    html = (HERE / "index.html").read_text(encoding="utf-8")
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"web/index.html must hold {mark} once")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta)
    if out.exists():
        shutil.rmtree(out)
    (out / "firmware").mkdir(parents=True)
    (out / "screens").mkdir()
    (out / "app").mkdir()
    if wasm == "-":                             # (the tests, where no clang for WebAssembly is: the page, no app)
        print("site: no app/fm1dusk.wasm: the page's Try it here will not start")
        wasm_n = 0
    else:
        wasm = Path(wasm)
        if not wasm.exists():
            raise SystemExit(f"{wasm}: not there (sh web/app/build.sh makes it)")
        shutil.copy(wasm, out / "app" / "fm1dusk.wasm")
        wasm_n = wasm.stat().st_size
    for f in ("dusk.js", "worklet.js"):
        shutil.copy(HERE / "app" / f, out / "app" / f)
    shutil.copy(pkg, out / "firmware" / name)
    for f in SCREENS:
        shutil.copy(ROOT / "docs" / "screens" / f, out / "screens" / f)
    (out / "index.html").write_text(html, encoding="utf-8")
    (out / ".nojekyll").write_text("")
    print(f"site: {out}: index.html ({len(html)} B), firmware/{name} ({len(raw)} B, {product}), "
          f"{len(SCREENS)} pictures, app/ ({wasm_n} B of WebAssembly)")


if __name__ == "__main__":
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    main(*sys.argv[1:])
