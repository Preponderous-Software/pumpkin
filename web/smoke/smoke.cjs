// Smoke test for the browser build in headless Chromium (Playwright).
//
//   node web/smoke/smoke.cjs <url> <out-dir> [--phone]
//
// Plays the toy the way a person does, with real clicks, taps and drags (on the
// phone profile, touch events): two eyes and a mouth from the tray, a piece
// made bigger, a freehand loop cut in the cheek (which pops out as a hole),
// undo, the lights out (the holes then glow: warm pixels on screen), the link
// (#p=...) opened in a fresh page showing the same pumpkin, the porch (the
// pumpkin stands on it, kept in localStorage, and a tap picks it), a PNG
// download of the lit pumpkin, and start over (undone again). On the phone,
// every control is on screen, at least 44 CSS px, and none overlaps another.
// Fails on any page error. Writes PNGs to <out-dir>.
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const { chromium, devices } = require('playwright');

const [url, outDir] = process.argv.slice(2);
const phone = process.argv.includes('--phone');
if (!url || !outDir) {
  console.error('usage: smoke.cjs <url> <out-dir> [--phone]');
  process.exit(2);
}
fs.mkdirSync(outDir, { recursive: true });

// Minimal PNG decoder (8-bit RGB/RGBA, non-interlaced: what Chromium writes).
function decodePng(buf) {
  let pos = 8, width = 0, height = 0, channels = 0;
  const idat = [];
  while (pos < buf.length) {
    const len = buf.readUInt32BE(pos);
    const type = buf.toString('ascii', pos + 4, pos + 8);
    const data = buf.subarray(pos + 8, pos + 8 + len);
    if (type === 'IHDR') {
      width = data.readUInt32BE(0);
      height = data.readUInt32BE(4);
      channels = { 2: 3, 6: 4 }[data[9]];
      if (data[8] !== 8 || !channels || data[12] !== 0) throw new Error('unsupported PNG');
    } else if (type === 'IDAT') idat.push(data);
    pos += 12 + len;
  }
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = width * channels;
  const out = Buffer.alloc(height * stride);
  for (let y = 0; y < height; y++) {
    const f = raw[y * (stride + 1)];
    for (let x = 0; x < stride; x++) {
      const v = raw[y * (stride + 1) + 1 + x];
      const a = x >= channels ? out[y * stride + x - channels] : 0;
      const b = y > 0 ? out[(y - 1) * stride + x] : 0;
      const c = x >= channels && y > 0 ? out[(y - 1) * stride + x - channels] : 0;
      let p;
      if (f === 0) p = 0;
      else if (f === 1) p = a;
      else if (f === 2) p = b;
      else if (f === 3) p = (a + b) >> 1;
      else {
        const pa = Math.abs(b - c), pb = Math.abs(a - c), pc = Math.abs(a + b - 2 * c);
        p = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
      }
      out[y * stride + x] = (v + p) & 255;
    }
  }
  return { width, height, channels, data: out };
}
function colourCount(img) {
  const seen = new Set();
  for (let i = 0; i < img.data.length; i += img.channels)
    seen.add(((img.data[i] >> 4) << 8) | ((img.data[i + 1] >> 4) << 4) | (img.data[i + 2] >> 4));
  return seen.size;
}
// Candlelight: bright, warm pixels.
function warmCount(img) {
  let n = 0;
  for (let i = 0; i < img.data.length; i += img.channels) {
    const r = img.data[i], g = img.data[i + 1], b = img.data[i + 2];
    if (r > 200 && g > 110 && b < 150 && r > b + 80) n++;
  }
  return n;
}

(async () => {
  const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const base = phone ? devices['iPhone 13'] : { viewport: { width: 1280, height: 800 } };
  // "headless" in the UA keeps arcade from counting a smoke run as a play.
  const context = await browser.newContext({ ...base, acceptDownloads: true, userAgent: (base.userAgent || 'Mozilla/5.0') + ' pumpkin-smoke headless' });
  const errors = [];
  const watch = (page) => {
    page.on('pageerror', (e) => errors.push(String(e)));
    page.on('console', (m) => { if (m.type() === 'error') errors.push('console: ' + m.text()); });
  };
  let page = await context.newPage();
  watch(page);
  const fail = async (why) => {
    console.error('FAIL: ' + why + (errors.length ? ' | errors: ' + errors.join(' | ') : ''));
    await page.screenshot({ path: path.join(outDir, 'failure.png') }).catch(() => {});
    await browser.close();
    process.exit(1);
  };
  const open = async (p, u) => {
    await p.goto(u, { waitUntil: 'commit' });
    try {
      await p.waitForFunction(() => window.pumpkin && window.pumpkinStatus, null, { timeout: 120000 });
    } catch (e) {
      await fail('the toy never became ready');
    }
    await p.waitForTimeout(1500);
  };
  const status = async (p = page) => JSON.parse(await p.evaluate(() => window.pumpkin.status()));
  const until = async (pred, ms = 20000, p = page) => {
    const end = Date.now() + ms;
    let st = await status(p);
    while (!pred(st) && Date.now() < end) {
      await p.waitForTimeout(150);
      st = await status(p);
    }
    return st;
  };
  const shot = async (name) => {
    // CDP capture: a software-drawn page may never settle for page.screenshot.
    const cdp = await context.newCDPSession(page);
    const r = await cdp.send('Page.captureScreenshot', { format: 'png' });
    await cdp.detach();
    const buf = Buffer.from(r.data, 'base64');
    fs.writeFileSync(path.join(outDir, name + '.png'), buf);
    return decodePng(buf);
  };
  // A press on a button: a tap on the phone, a click on a desktop.
  const press = async (selector) => {
    const el = page.locator(selector).first();
    if (phone) await el.tap(); else await el.click();
    await page.waitForTimeout(200);
  };
  // Carve point -> CSS pixel on the page.
  const screenOf = async (x, y) => {
    const r = await page.evaluate(([x, y]) => {
      const s = window.pumpkin.screenOf(x, y);
      if (!s) return null;
      const [px, py] = s.split(',').map(Number);
      const c = document.getElementById('canvas');
      const b = c.getBoundingClientRect();
      return [b.left + px * b.width / c.width, b.top + py * b.height / c.height];
    }, [x, y]);
    if (!r) await fail(`carve point (${x}, ${y}) is off the pumpkin`);
    return r;
  };
  // Drag a finger (or the mouse) through carve points.
  const drag = async (points) => {
    const px = [];
    for (const [x, y] of points) px.push(await screenOf(x, y));
    if (phone) {
      const cdp = await context.newCDPSession(page);
      const tp = (p) => [{ x: p[0], y: p[1], id: 1, radiusX: 4, radiusY: 4, force: 1 }];
      await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: tp(px[0]) });
      for (const p of px.slice(1)) {
        await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: tp(p) });
        await page.waitForTimeout(16);
      }
      await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
      await cdp.detach();
    } else {
      await page.mouse.move(px[0][0], px[0][1]);
      await page.mouse.down();
      for (const p of px.slice(1)) { await page.mouse.move(p[0], p[1]); await page.waitForTimeout(16); }
      await page.mouse.up();
    }
    await page.waitForTimeout(300);
  };
  const loop = (cx, cy, r, n = 24) => {
    const pts = [];
    for (let i = 0; i <= n; i++) {
      const a = 2 * Math.PI * i / n;
      pts.push([cx + r * Math.cos(a), cy + r * Math.sin(a)]);
    }
    return pts;
  };

  // On a phone, every visible control is on screen, big enough, and none overlaps another.
  const checkLayout = async (when) => {
    if (!phone) return;
    const bad = await page.evaluate(() => {
      const vw = innerWidth, vh = innerHeight, out = [];
      const vis = [...document.querySelectorAll('button')].filter((b) => b.offsetParent && getComputedStyle(b).visibility !== 'hidden')
        .filter((b) => !b.closest('.dialog') || b.closest('.dialog').style.display === 'block');
      const boxes = vis.map((b) => ({ b, r: b.getBoundingClientRect() }));
      for (const { b, r } of boxes) {
        const name = b.getAttribute('aria-label') || b.textContent;
        const inRow = b.closest('.row');
        const rowBox = inRow ? inRow.getBoundingClientRect() : null;
        const clipped = rowBox && (r.right > rowBox.right + 1 || r.left < rowBox.left - 1);  // scrolled out of a row: fine
        if (!clipped && (r.left < -1 || r.top < -1 || r.right > vw + 1 || r.bottom > vh + 1)) out.push('off screen: ' + name);
        if (r.width < 43.5 || r.height < 43.5) out.push(`too small (${r.width.toFixed(0)}x${r.height.toFixed(0)}): ${name}`);
      }
      for (let i = 0; i < boxes.length; i++)
        for (let j = i + 1; j < boxes.length; j++) {
          const a = boxes[i].r, c = boxes[j].r;
          if (a.left < c.right - 1 && c.left < a.right - 1 && a.top < c.bottom - 1 && c.top < a.bottom - 1)
            out.push('overlap: ' + (boxes[i].b.getAttribute('aria-label') || '?') + ' / ' + (boxes[j].b.getAttribute('aria-label') || '?'));
        }
      return out;
    });
    if (bad.length) await fail(when + ': ' + bad.join('; '));
  };

  await open(page, url);
  let img = await shot('01-start');
  if (colourCount(img) < 40) await fail('the canvas looks blank (' + colourCount(img) + ' colours)');
  let st = await status();
  if (st.ops !== 0 || st.mode !== 'carve') await fail('a fresh page should be an uncarved pumpkin: ' + JSON.stringify(st));
  if (!(await page.locator('#hint').isVisible())) await fail('the first-time hint is not shown');
  await checkLayout('start');

  // Two eyes (the second mirrors the first), then a grin.
  await press('#tray button[data-shape="0"]');
  st = await until((s) => s.stamps === 1 && s.selected === 0);
  if (st.stamps !== 1) await fail('the tray did not place an eye: ' + JSON.stringify(st));
  await checkLayout('a piece chosen');
  await press('#doneBtn');
  await press('#tray button[data-shape="0"]');
  await press('#doneBtn');
  await press('#tray button[data-shape="7"]');
  const before = await page.evaluate(() => window.pumpkin.code());
  await press('#edits button[data-edit="0"]');  // bigger
  const after = await page.evaluate(() => window.pumpkin.code());
  if (before === after) await fail('Bigger did not change the mouth');
  await press('#doneBtn');
  st = await until((s) => s.stamps === 3 && s.selected === -1);
  if (st.stamps !== 3 || st.selected !== -1) await fail('expected three pieces, none chosen: ' + JSON.stringify(st));

  // Dragging a piece moves it.
  const eyeBefore = await page.evaluate(() => window.pumpkin.code());
  await drag([[-0.36, 0.24], [-0.33, 0.27], [-0.30, 0.30], [-0.28, 0.32]]);
  st = await status();
  if (st.stamps !== 3) await fail('dragging an eye changed the pieces: ' + JSON.stringify(st));
  if ((await page.evaluate(() => window.pumpkin.code())) === eyeBefore) await fail('dragging the eye did not move it');
  await press('#undoBtn');
  if ((await page.evaluate(() => window.pumpkin.code())) !== eyeBefore) await fail('undo did not put the eye back');
  st = await status();
  if (st.selected >= 0) { await press('#doneBtn'); }

  // A freehand loop in the cheek pops out as a hole.
  await drag(loop(0.62, 0.0, 0.11));
  st = await until((s) => s.cuts === 1);
  if (st.cuts !== 1) await fail('the freehand loop did not cut: ' + JSON.stringify(st));
  await press('#undoBtn');
  st = await until((s) => s.cuts === 0);
  if (st.cuts !== 0) await fail('undo did not take the cut back');
  await drag(loop(0.62, 0.0, 0.11));
  st = await until((s) => s.cuts === 1);
  if (st.cuts !== 1 || st.ops !== 4) await fail('the second loop did not cut: ' + JSON.stringify(st));
  await shot('02-carved');

  // Lights out: the holes glow.
  await press('#lightBtn');
  st = await until((s) => s.dark);
  if (!st.dark) await fail('the lights did not go out');
  await page.waitForTimeout(800);
  img = await shot('03-lit');
  const warm = warmCount(img);
  console.log('warm pixels with the candle lit: ' + warm);
  if (warm < 300) await fail('the carved face does not glow (' + warm + ' warm pixels)');

  // The link opens the same pumpkin, lit.
  const code = await page.evaluate(() => window.pumpkin.code());
  const hash = await page.evaluate(() => location.hash);
  if (hash !== '#p=' + code) await fail('the address does not carry the carving: ' + hash);
  console.log('link: #p=' + code + ' (' + code.length + ' characters)');
  {
    const other = await context.newPage();
    watch(other);
    await open(other, url.split('#')[0] + '#p=' + code);
    const os = await until((s) => s.ops === 4, 20000, other);
    const oc = await other.evaluate(() => window.pumpkin.code());
    if (os.ops !== 4 || oc !== code || !os.dark) {
      page = other;
      await fail('the link opened a different pumpkin: ' + JSON.stringify(os));
    }
    await other.close();
  }

  // The porch: put it there, it stays (localStorage), and a tap picks it.
  await press('#porchBtn');
  st = await until((s) => s.mode === 'porch' && s.porch === 1);
  if (st.mode !== 'porch' || st.porch !== 1) await fail('the pumpkin is not on the porch: ' + JSON.stringify(st));
  const kept = await page.evaluate(() => JSON.parse(localStorage.getItem('pumpkin-porch-v1') || '[]'));
  if (kept.length !== 1 || kept[0] !== code) await fail('the porch was not kept in this browser: ' + JSON.stringify(kept));
  await page.waitForTimeout(800);
  img = await shot('04-porch');
  if (warmCount(img) < 50) await fail('the porch pumpkin is not lit');
  await checkLayout('porch');
  await press('#backBtn');
  await press('#porchBtn');  // the same pumpkin again: still one on the porch
  st = await until((s) => s.mode === 'porch');
  if (st.porch !== 1) await fail('putting the same pumpkin out twice made two: ' + st.porch);
  {
    const c = await page.evaluate(() => { const b = document.getElementById('canvas').getBoundingClientRect(); return [b.left + b.width / 2, b.top + b.height / 2]; });
    if (phone) await page.touchscreen.tap(c[0], c[1]); else await page.mouse.click(c[0], c[1]);
    await page.waitForTimeout(500);
    if (await page.locator('#openBtn').isDisabled()) await fail('tapping the porch pumpkin did not pick it');
    await press('#openBtn');
    st = await until((s) => s.mode === 'carve');
    if (st.mode !== 'carve' || (await page.evaluate(() => window.pumpkin.code())) !== code) await fail('Open did not bring the pumpkin back');
  }

  // A picture of the lit pumpkin.
  {
    const dl = page.waitForEvent('download', { timeout: 30000 });
    await press('#photoBtn');
    const d = await dl.catch(() => null);
    if (!d) await fail('Save a picture did not download anything');
    const file = path.join(outDir, 'pumpkin.png');
    await d.saveAs(file);
    const png = fs.readFileSync(file);
    if (png.toString('ascii', 1, 4) !== 'PNG') await fail('the picture is not a PNG');
    const pic = decodePng(png);
    console.log(`picture: ${pic.width}x${pic.height}, ${warmCount(pic)} warm pixels`);
    if (pic.width < 100 || warmCount(pic) < 200) await fail('the picture does not show a lit pumpkin');
  }

  // Start over asks first, and can be undone.
  await press('#clearBtn');
  await checkLayout('start over?');
  await press('#clearYes');
  st = await until((s) => s.ops === 0);
  if (st.ops !== 0) await fail('start over did not clear the pumpkin');
  await press('#undoBtn');
  st = await until((s) => s.ops === 4);
  if (st.ops !== 4) await fail('undo did not bring the carving back after start over');

  if (errors.length) await fail('page errors');
  await browser.close();
  console.log('PASS (' + (phone ? 'phone' : 'desktop') + ')');
})().catch((e) => { console.error('FAIL: ' + (e && e.stack || e)); process.exit(1); });
