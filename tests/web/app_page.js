// SPDX-License-Identifier: GPL-3.0-only
// The site's "Try it here" in Chromium (Playwright): the page served over HTTP (a worklet needs it), Start, and Dusk
// runs: its screen drawn, HOME's light, the scenes' keys lit; PLAY (its light on the beat); the drums' page; ALGORITHM
// changes the clip on the screen; a key pressed with the pointer; no error on the page.
//
//   node tests/web/app_page.js build/site          (or a URL: the site as it is published)
const http = require('http');
const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');

const dir = path.resolve(/^https?:/.test(process.argv[2] || '') ? '.' : process.argv[2] || 'build/site');
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.png': 'image/png',
                '.fwsc': 'application/octet-stream' };
const server = http.createServer((req, res) => {
  const f = path.join(dir, decodeURIComponent(req.url.split('?')[0].split('#')[0]).replace(/\/$/, '/index.html'));
  if (!f.startsWith(dir) || !fs.existsSync(f)) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': TYPES[path.extname(f)] || 'application/octet-stream' });
  fs.createReadStream(f).pipe(res);
});

(async () => {
  const live = /^https?:/.test(process.argv[2] || '');
  if (!live) await new Promise((r) => server.listen(0, '127.0.0.1', r));
  const url = live ? process.argv[2] : `http://127.0.0.1:${server.address().port}/`;
  const b = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required'] });
  let fail = 0;
  const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) fail = 1; };
  try {
    const p = await b.newPage({ viewport: { width: 1000, height: 1400 } });
    const errs = [];
    p.on('pageerror', (e) => errs.push(String(e)));
    p.on('console', (m) => { if (m.type() === 'error') errs.push(m.text()); });
    const shot = () => p.$eval('#lcd', (c) => Array.from(c.getContext('2d').getImageData(0, 0, c.width, c.height).data));
    const differ = (x, y) => { let n = 0; for (let i = 0; i < x.length; i += 4) if (x[i] !== y[i] || x[i + 1] !== y[i + 1] || x[i + 2] !== y[i + 2]) n++; return n; };
    const isLit = (id) => p.$eval(`[data-btn="${id}"]`, (e) => e.classList.contains('lit'));
    await p.goto(url, { waitUntil: 'networkidle' });
    await p.click('#start');
    await p.waitForFunction(() => document.getElementById('app').classList.contains('on'), null, { timeout: 15000 });
    await p.waitForTimeout(800);
    const home = await shot();
    const drawn = home.filter((v, i) => i % 4 === 0 && v > 40).length / (home.length / 4);
    check(drawn > 0.2, `Start: HOME on the screen (${(100 * drawn).toFixed(0)} % of it lit)`);
    check(await isLit(8), "HOME's light");
    check((await p.$$('.kbd span.on')).length === 5 && (await p.$$('.kbd span.glow')).length === 3,
      "the keys' lights: scene A and the four tracks lit, B C D dim");
    await p.click('[data-btn="12"]');
    let lit = false;
    for (let i = 0; i < 20 && !lit; i++) { await p.waitForTimeout(60); lit = await isLit(12); }
    check(lit, 'PLAY: its light, on the beat');
    await p.click('[data-btn="11"]');
    await p.waitForTimeout(500);
    const drums = await shot();
    check(differ(home, drums) > 40000 && await isLit(11), "SEQ: the drums' page");
    await p.click('[data-btn="12"]');            // (stop: the screen stands still)
    await p.waitForTimeout(400);
    const still = await shot();
    await p.click('[data-enc="1"][data-n="1"]');
    await p.waitForTimeout(2200);                // (past the note at the bottom that says what changed)
    const other = await shot();
    check(differ(still, other) > 300, `ALGORITHM: another groove on the screen (${differ(still, other)} pixels changed)`);
    const key = (await p.$$('.kbd span'))[19];
    await key.scrollIntoViewIfNeeded();
    const box = await key.boundingBox();
    await p.mouse.move(box.x + box.width / 2, box.y + box.height - 6);
    await p.mouse.down();
    await p.waitForTimeout(150);
    check(await key.evaluate((e) => e.classList.contains('down')), 'a key pressed with the pointer');
    await p.mouse.up();
    check(/Running/.test(await p.textContent('#appstatus')), 'the status: running');
    check(errs.length === 0, `no errors on the page${errs.length ? ': ' + errs.join(' | ') : ''}`);
  } finally {
    await b.close();
    if (!live) server.close();
  }
  console.log(fail ? 'app_page: FAILED' : 'app_page: all passed');
  process.exit(fail);
})();
