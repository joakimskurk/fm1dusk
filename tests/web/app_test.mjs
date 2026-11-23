// SPDX-License-Identifier: GPL-3.0-only
// Dusk for the browser (web/app/: the firmware compiled to WebAssembly, web/app/build.sh) in Node, as the page's
// worklet runs it: no imports; silent until PLAY, then the tape plays; the screen and the lights; a page's button;
// a key plays the lead; a tape kept on the shelf is in the flash the page keeps, and is there after a restart.
//
//   node tests/web/app_test.mjs build/web/fm1dusk.wasm
import { readFileSync } from 'fs';

const mod = await WebAssembly.compile(readFileSync(process.argv[2] || 'build/web/fm1dusk.wasm'));
let fail = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) fail = 1; };

const imports = WebAssembly.Module.imports(mod);
check(imports.length === 0, `no imports (${imports.map((i) => i.name).join(', ') || 'none'})`);
const boot = async (store) => {
  const { exports: x } = await WebAssembly.instantiate(mod, {});
  if (store) new Uint8Array(x.memory.buffer, x.web_store(), store.length).set(store);
  x.web_init();
  return x;
};
let x = await boot();
const run = (sec) => {                           // the loudest sample of the time
  let p = 0;
  for (let i = 0; i < Math.round((sec * 44100) / 128); i++) {
    x.web_run(128);
    const o = new Int32Array(x.memory.buffer, x.web_out_ptr(), 256);
    for (let k = 0; k < 256; k++) p = Math.max(p, Math.abs(o[k]));
  }
  return p;
};
const tap = (id) => { x.web_btn_down(id); run(0.05); x.web_btn_up(id); run(0.05); };
const lcd = () => new Uint16Array(x.memory.buffer, x.web_lcd(), 240 * 240).slice();
const differ = (a, b) => a.reduce((n, v, i) => n + (v !== b[i]), 0);
const BTN = { FX: 2, SEL: 3, HOME: 8, SAVE: 9, SEQ: 11, PLAY: 12 };

const quiet = run(1);
check(quiet < 1500, `stopped: no more than the record's hiss (${quiet})`);
check(x.web_ms() >= 995 && x.web_ms() <= 1005, `the main loop ran a pass a millisecond: ${x.web_ms()} ms`);
const home = lcd();
check(home.filter((c) => c !== home[0]).length > 30000, 'the screen: HOME');
check(((x.web_leds_lo() >>> BTN.HOME) & 1) === 1, "HOME's light is lit from the start");
check(((x.web_leds_lo() >>> 14) & 1) === 1 && ((x.web_glow_lo() >>> 16) & 1) === 1, "the keys' lights: scene A lit, B dim");

const t0 = performance.now();
tap(BTN.PLAY);
const loud = run(4);
const speed = 4100 / (performance.now() - t0);
check(loud > 8000 && loud <= 32767, `PLAY: the tape plays (its loudest ${loud})`);
check(speed > 4, `${speed.toFixed(0)} x real time`);
tap(BTN.SEQ);
run(0.3);
check(differ(home, lcd()) > 10000 && ((x.web_leds_lo() >>> BTN.SEQ) & 1) === 1,
  `SEQ: the drums' page, its light (${differ(home, lcd())} pixels changed)`);
tap(BTN.PLAY);
run(3);
tap(BTN.FX);                                     // (a page where the keys play the lead)
const before = run(0.3);
x.web_keys(1 << 19);                             // C5
const note = run(0.5);
x.web_keys(0);
check(note > before * 3 && note > 1500, `a key plays the lead (${note} over ${before})`);

tap(BTN.SAVE); tap(BTN.SAVE);                    // the shelf, tape 1: keep it here
run(0.5);
const flash = new Uint8Array(x.memory.buffer, x.web_store(), x.web_store_size()).slice();
check(x.web_store_rev() >= 1 && flash[flash.length / 9] === 1, 'SAVE, SAVE: the tape is in the flash the page keeps');
x = await boot(flash);                           // the page again, with what it kept
tap(BTN.SAVE);
run(0.3);
const shelf = lcd();
x = await boot();
tap(BTN.SAVE);
run(0.3);
check(differ(shelf, lcd()) > 300, 'after a restart the kept tape is on the shelf (and not without the flash)');
console.log(fail ? 'app_test: FAILED' : 'app_test: all passed');
process.exit(fail);
