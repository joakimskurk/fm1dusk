// SPDX-License-Identifier: GPL-3.0-only
// Dusk on its own page: the firmware runs in the audio thread (worklet.js, fm1dusk.wasm); here its screen, the FM-1's
// panel as buttons, its keys and their lights. Nothing is loaded before Start (a browser plays sound after a click).
// What the firmware keeps in flash (the tape as it is, the shelf) is kept in this browser.
const $ = (id) => document.getElementById(id);
const app = $('app');
if (app) {
  const lcd = $('lcd'), g = lcd.getContext('2d');
  const small = document.createElement('canvas');
  small.width = small.height = 240;
  const gs = small.getContext('2d');
  const img = gs.createImageData(240, 240);
  const px32 = new Uint32Array(img.data.buffer);
  const status = $('appstatus');
  const STORE = 'fm1dusk-flash-1';
  let ac = null, node = null, gain = null, booting = false, held = 0;

  // the 27 keys (F3..G5), LED id 14 + key; black where platform.c's pattern says
  const isBlack = (k) => (0x54A >> ((53 + k) % 12)) & 1;
  const keysEl = $('keys');
  const keyEls = [];
  const nWhite = [...Array(27).keys()].filter((k) => !isBlack(k)).length;
  let wi = 0;
  for (let k = 0; k < 27; k++) {
    const el = document.createElement('span');
    if (isBlack(k)) {
      el.className = 'kb';
      el.style.left = `calc(${(100 * wi) / nWhite}% - ${(100 * 0.32) / nWhite}%)`;
      el.style.width = `${(100 * 0.64) / nWhite}%`;
    } else {
      el.className = 'kw';
      el.style.left = `${(100 * wi) / nWhite}%`;
      el.style.width = `${100 / nWhite}%`;
      wi++;
    }
    el.dataset.key = k;
    keysEl.appendChild(el);
    keyEls.push(el);
  }

  const send = (m) => node && node.port.postMessage(m);
  const say = (t) => { status.textContent = t; };
  const b64 = (buf) => { let s = ''; for (const b of new Uint8Array(buf)) s += String.fromCharCode(b); return btoa(s); };
  const unb64 = (s) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0)).buffer;
  const kept = () => { try { const s = localStorage.getItem(STORE); return s ? unb64(s) : null; } catch (e) { return null; } };
  const keep = (buf) => { try { localStorage.setItem(STORE, b64(buf)); } catch (e) { /* (a private window: not kept) */ } };

  function draw(p) {                             // RGB565 -> the canvas, twice its size, sharp
    for (let i = 0; i < 57600; i++) {
      const c = p[i];
      const r = ((c >> 11) * 527 + 23) >> 6, gg = (((c >> 5) & 63) * 259 + 33) >> 6, b = ((c & 31) * 527 + 23) >> 6;
      px32[i] = 0xFF000000 | (b << 16) | (gg << 8) | r;
    }
    gs.putImageData(img, 0, 0);
    g.imageSmoothingEnabled = false;
    g.drawImage(small, 0, 0, lcd.width, lcd.height);
  }
  const bit = (w, id) => (id < 32 ? w[0] & (1 << id) : w[1] & (1 << (id - 32)));
  function lights(leds, glow) {
    for (const b of app.querySelectorAll('[data-btn]')) b.classList.toggle('lit', !!bit(leds, +b.dataset.btn));
    for (let k = 0; k < 27; k++) {
      keyEls[k].classList.toggle('on', !!bit(leds, 14 + k));
      keyEls[k].classList.toggle('glow', !bit(leds, 14 + k) && !!bit(glow, 14 + k));
    }
  }

  async function start() {
    if (booting) return;
    if (ac) {
      if (ac.state === 'running') { await ac.suspend(); $('start').hidden = false; $('start').textContent = '▶ Go on'; say('Paused.'); }
      else { await ac.resume(); $('start').hidden = true; say('Running. PLAY starts the tape.'); }
      return;
    }
    booting = true;
    say('Loading the firmware…');
    try {
      ac = new AudioContext({ sampleRate: 44100, latencyHint: 'interactive' });
      const [wasm] = await Promise.all([
        fetch('app/fm1dusk.wasm').then((r) => { if (!r.ok) throw new Error(`fm1dusk.wasm: ${r.status}`); return r.arrayBuffer(); }),
        ac.audioWorklet.addModule('app/worklet.js'),
      ]);
      node = new AudioWorkletNode(ac, 'fm1-dusk', { numberOfInputs: 0, outputChannelCount: [2] });
      gain = ac.createGain();
      gain.gain.value = vol();
      node.connect(gain).connect(ac.destination);
      node.port.onmessage = (e) => {
        const m = e.data;
        if (m.type === 'frame') { if (m.lcd) draw(m.lcd); lights(m.leds, m.glow); if (m.store) keep(m.store); }
        else if (m.type === 'ready') { $('start').hidden = true; app.classList.add('on'); say('Running. PLAY starts the tape.'); }
        else if (m.type === 'error') say(`It did not start: ${m.message}`);
      };
      node.port.postMessage({ type: 'boot', wasm, store: kept() }, [wasm]);
      await ac.resume();
    } catch (err) {
      say(`It did not start here: ${err.message || err}. It needs a browser with Web Audio worklets (any recent one).`);
      ac = null;
    }
    booting = false;
  }

  const vol = () => { const v = $('vol').value / 100; return v * v; };
  $('vol').addEventListener('input', () => { if (gain) gain.gain.value = vol(); });
  $('start').addEventListener('click', start);
  $('b-pause').addEventListener('click', () => { if (ac) start(); });
  $('b-reset').addEventListener('click', () => {
    if (!confirm('Forget the tapes kept in this browser and start again from the factory tape?')) return;
    try { localStorage.removeItem(STORE); } catch (e) { /* nothing kept */ }
    location.reload();
  });

  // a button: down while it is held (SEL held empties a clip), up when let go, wherever the pointer went
  for (const b of app.querySelectorAll('[data-btn]')) {
    const id = +b.dataset.btn;
    let down = false;
    const up = () => { if (down) { down = false; send({ type: 'up', id }); } };
    b.addEventListener('pointerdown', (e) => { e.preventDefault(); down = true; send({ type: 'down', id }); });
    b.addEventListener('pointerup', up);
    b.addEventListener('pointerleave', up);
    b.addEventListener('pointercancel', up);
    b.addEventListener('keydown', (e) => { if ((e.key === ' ' || e.key === 'Enter') && !e.repeat) { down = true; send({ type: 'down', id }); } });
    b.addEventListener('keyup', (e) => { if (e.key === ' ' || e.key === 'Enter') up(); });
  }
  // an encoder: its two buttons, or the wheel over it
  for (const b of app.querySelectorAll('[data-enc]'))
    b.addEventListener('click', () => send({ type: 'enc', id: +b.dataset.enc, n: +b.dataset.n }));
  for (const w of app.querySelectorAll('[data-wheel]'))
    w.addEventListener('wheel', (e) => { e.preventDefault(); send({ type: 'enc', id: +w.dataset.wheel, n: e.deltaY < 0 ? 1 : -1 }); }, { passive: false });

  // the keys: the pointer (a slide from key to key plays them), and the computer's keyboard
  const setKey = (k, on) => {
    const m = on ? held | (1 << k) : held & ~(1 << k);
    if (m !== held) { held = m; send({ type: 'keys', mask: held }); keyEls[k].classList.toggle('down', on); }
  };
  const ptr = new Map();
  const keyAt = (e) => { const el = document.elementFromPoint(e.clientX, e.clientY); return el && el.dataset.key !== undefined && keysEl.contains(el) ? +el.dataset.key : -1; };
  const move = (e) => {
    if (!ptr.has(e.pointerId)) return;
    const k = keyAt(e), was = ptr.get(e.pointerId);
    if (k === was) return;
    if (was >= 0) setKey(was, false);
    if (k >= 0) setKey(k, true);
    ptr.set(e.pointerId, k);
  };
  const lift = (e) => { const was = ptr.get(e.pointerId); if (was >= 0) setKey(was, false); ptr.delete(e.pointerId); };
  keysEl.addEventListener('pointerdown', (e) => { e.preventDefault(); keysEl.setPointerCapture(e.pointerId); ptr.set(e.pointerId, -1); move(e); });
  keysEl.addEventListener('pointermove', move);
  keysEl.addEventListener('pointerup', lift);
  keysEl.addEventListener('pointercancel', lift);
  // the computer's keys: two rows of a piano from C4 (key 7): A W S E D F T G Y H U J K O L P
  const ROW = 'awsedftgyhujkolp';
  const typing = (e) => /^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName);
  document.addEventListener('keydown', (e) => {
    if (!node || e.repeat || e.ctrlKey || e.metaKey || e.altKey || typing(e)) return;
    const i = ROW.indexOf(e.key.toLowerCase());
    if (i >= 0) setKey(7 + i, true);
  });
  document.addEventListener('keyup', (e) => { const i = ROW.indexOf(e.key.toLowerCase()); if (i >= 0) setKey(7 + i, false); });
  window.addEventListener('blur', () => { if (held) { held = 0; send({ type: 'keys', mask: 0 }); keyEls.forEach((el) => el.classList.remove('down')); } });
}
