/* Spec pages (motion.html, tokens.html). Shared chrome, token loading, curve plotting and the contrast maths.
   Everything is read from tokens.json at runtime. No network. body[data-page] picks the page; body.dataset.ready = '1' when rendered. */
(() => {
'use strict';
const qs = new URLSearchParams(location.search);
const root = document.documentElement;
const $ = (s, r = document) => r.querySelector(s);
const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
let T = null;

/* ---------- mode, chrome ---------- */
function initMode() {
  let m = qs.get('mode');
  if (m !== 'light' && m !== 'dark') m = matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
  root.dataset.mode = m;
}
function keepQuery() {
  const q = new URLSearchParams(location.search); q.set('mode', root.dataset.mode);
  document.querySelectorAll('.topbar nav a').forEach(a => { a.href = a.getAttribute('data-page') + '?mode=' + root.dataset.mode; });
  history.replaceState(null, '', location.pathname + '?' + q.toString());
}
function chrome(active, extra) {
  const bar = document.createElement('header'); bar.className = 'topbar';
  const pages = [['index.html', 'Sidebar'], ['horizontal.html', 'Horizontal'], ['workspace-editor.html', 'Workspace editor'], ['motion.html', 'Motion spec'], ['tokens.html', 'Token spec']];
  bar.innerHTML = `<span class="brand">Aerie<small>Sidebar design, issue #142</small></span>
    <nav aria-label="Spec pages">${pages.map(([h, n]) => `<a data-page="${h}" href="${h}"${h === active ? ' aria-current="page"' : ''}>${n}</a>`).join('')}</nav>
    <span class="spacer"></span>${extra || ''}
    <button class="btn" id="modeBtn" type="button" aria-label="Toggle light and dark"></button>`;
  document.body.prepend(bar);
  const btn = $('#modeBtn', bar), sync = () => { btn.textContent = root.dataset.mode === 'dark' ? 'Dark' : 'Light'; };
  btn.addEventListener('click', () => { root.dataset.mode = root.dataset.mode === 'dark' ? 'light' : 'dark'; sync(); keepQuery(); });
  sync(); keepQuery();
}

/* ---------- tokens ---------- */
/* tokens.json is inlined as <script type="application/json" id="tokens-json"> in the Artifact bundle; fetch is the fallback for local use. */
async function load() { const inl = document.getElementById('tokens-json'); T = inl ? JSON.parse(inl.textContent) : await (await fetch('tokens.json', { cache: 'no-store' })).json(); return T; }
const groups = () => Object.keys(T).filter(g => !g.startsWith('_'));
function tok(key) {   // 'motion-duration-slow' -> { group, name, t }
  for (const g of groups()) if (key.startsWith(g + '-')) { const n = key.slice(g.length + 1); if (T[g][n]) return { group: g, name: n, t: T[g][n] }; }
  return null;
}
const unit = t => t.unit || '';
const valueOf = (t, mode) => 'value' in t ? t.value : t[mode];
const fmt = (v, u) => `${v}${u || ''}`;

/* ---------- easing curves ---------- */
function parseEase(s) {
  let m = /^cubic-bezier\(([^)]+)\)$/.exec(s);
  if (m) {
    const [x1, y1, x2, y2] = m[1].split(',').map(Number);
    return t => {
      let lo = 0, hi = 1, u = t;
      for (let i = 0; i < 32; i++) { u = (lo + hi) / 2; const x = 3 * (1 - u) ** 2 * u * x1 + 3 * (1 - u) * u * u * x2 + u ** 3; if (x < t) lo = u; else hi = u; }
      return 3 * (1 - u) ** 2 * u * y1 + 3 * (1 - u) * u * u * y2 + u ** 3;
    };
  }
  m = /^linear\(([^)]+)\)$/.exec(s);
  if (m) {
    const p = m[1].split(',').map(x => parseFloat(x)), n = p.length - 1;
    return t => { const sx = Math.min(1, Math.max(0, t)) * n, i = Math.min(Math.floor(sx), n - 1); return p[i] + (p[i + 1] - p[i]) * (sx - i); };
  }
  return t => t;
}
function plot(fn, { label = '', sub = '', ymin = -0.12, ymax = 1.22, ref } = {}) {
  const W = 118, H = 78, px = 10, py = 9, X = t => px + t * (W - 2 * px), Y = v => H - py - (v - ymin) / (ymax - ymin) * (H - 2 * py);
  let d = '', peak = -9;
  for (let i = 0; i <= 80; i++) { const t = i / 80, v = fn(t); peak = Math.max(peak, v); d += (i ? 'L' : 'M') + X(t).toFixed(1) + ' ' + Y(v).toFixed(1); }
  const refPath = ref ? (() => { let r = ''; for (let i = 0; i <= 40; i++) { const t = i / 40; r += (i ? 'L' : 'M') + X(t).toFixed(1) + ' ' + Y(ref(t)).toFixed(1); } return `<path d="${r}" fill="none" stroke="var(--sp-muted)" stroke-dasharray="3 3" stroke-width="1"/>`; })() : '';
  const note = peak > 1.004 ? `<text x="${W - 6}" y="12" text-anchor="end" font-size="9" fill="var(--sp-muted)">peak ${peak.toFixed(3)}</text>` : '';
  return `<div class="curve"><svg viewBox="0 0 ${W} ${H}" role="img" aria-label="${esc(label)} curve">
    <line x1="${X(0)}" y1="${Y(0)}" x2="${X(1)}" y2="${Y(0)}" stroke="var(--sp-line)"/><line x1="${X(0)}" y1="${Y(1)}" x2="${X(1)}" y2="${Y(1)}" stroke="var(--sp-line)" stroke-dasharray="2 3"/>
    ${refPath}<path d="${d}" fill="none" stroke="var(--sp-accent)" stroke-width="2" stroke-linecap="round"/>${note}</svg><div class="cl">${esc(label)}</div>${sub ? `<div>${esc(sub)}</div>` : ''}</div>`;
}

/* ---------- contrast (port of check_contrast.py: same frame model, samples, thresholds) ---------- */
function contrastMatrix() {
  const D = T, W = 1440, H = 900, SIDEBAR = 260, ANGLE = D.tint.angle.value * Math.PI / 180;
  const rgb = h => { h = h.replace('#', ''); return [0, 2, 4].map(i => parseInt(h.slice(i, i + 2), 16)); };
  const lum = c => { const f = v => { v /= 255; return v <= 0.03928 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4; }; const [r, g, b] = c.map(f); return 0.2126 * r + 0.7152 * g + 0.0722 * b; };
  const ratio = (a, b) => { const la = lum(a), lb = lum(b); return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05); };
  const mix = (a, b, t) => a.map((v, i) => v * t + b[i] * (1 - t));
  const gradAt = (stops, x, y) => {
    const L = Math.abs(W * Math.sin(ANGLE)) + Math.abs(H * Math.cos(ANGLE));
    let t = ((x - W / 2) * Math.sin(ANGLE) - (y - H / 2) * Math.cos(ANGLE)) / L + 0.5; t = Math.min(1, Math.max(0, t));
    if (stops.length === 1) return stops[0];
    const s = t * (stops.length - 1), i = Math.min(Math.floor(s), stops.length - 2);
    return mix(stops[i + 1], stops[i], s - i);
  };
  const glowAlpha = (x, y) => Math.max(0, 1 - Math.hypot(x / (1.2 * W), y / (0.8 * H)) / 0.6);
  const presets = [...new Set(Object.keys(D.workspace).map(k => k.split('-')[0]))].sort();
  const samples = []; for (let y = 0; y <= H; y += 45) { for (let x = 0; x <= SIDEBAR; x += 26) samples.push([x, y]); for (let x = W - SIDEBAR; x <= W; x += 26) samples.push([x, y]); }
  const rows = [];
  for (const [tname, th] of Object.entries(D.theme)) for (const mode of ['light', 'dark']) {
    if (!th._modes.includes(mode)) continue;
    const g = k => 'value' in th[k] ? th[k].value : th[k][mode];
    const frame = rgb((th['color.frame-base'] || D.color['frame-base'])[mode]);
    const text = rgb(g('text')), muted = rgb(g('text-muted')), need = tname === 'high-contrast' ? 7 : 4.5;
    const strength = g('tint-strength'), shade = g('shade'), glow = g('glow'), share = parseFloat(String(g('accent-hue-share'))) / 100;
    const cells = {};
    for (const p of presets) {
      const stops = [1, 2, 3].filter(i => `${p}-stop-${i}` in D.workspace).map(i => rgb(D.workspace[`${p}-stop-${i}`].value));
      const hue = rgb(D.workspace[`${p}-accent`][mode]), acc = mix(hue, rgb(g('accent-base')), share);
      const pm = { text: 99, muted: 99, accent: 99 };
      for (const [x, y] of samples) {
        let c = mix(gradAt(stops, x, y), frame, strength); c = mix([0, 0, 0], c, shade); c = mix([255, 255, 255], c, glow * glowAlpha(x, y));
        pm.text = Math.min(pm.text, ratio(text, c)); pm.muted = Math.min(pm.muted, ratio(muted, c)); pm.accent = Math.min(pm.accent, ratio(acc, c));
      }
      cells[p] = { ...pm, ok: pm.text >= need && pm.muted >= need && pm.accent >= 3 };
    }
    rows.push({ theme: tname, mode, need, cells });
  }
  return { rows, presets };
}

window.Specs = { $, esc, qs, root, load, tok, groups, valueOf, fmt, unit, parseEase, plot, contrastMatrix, chrome, initMode, get T() { return T; } };
initMode();
})();

/* =====================================================================================================================
   MOTION PAGE
   Each card: what moves, token values (read from tokens.json), curve plots, reduced version and a live demo.
   fx() is the same rule set as fx() in sidebar.js: transform and opacity only; under reduced motion 'move' roles do nothing
   and 'in'/'out' roles become a pure opacity fade using the reduced durations and easings from tokens.json.
   ===================================================================================================================== */
(() => {
'use strict';
if (document.body.dataset.page !== 'motion') return;
const { $, esc, tok, fmt, unit, parseEase, plot } = Specs;
const $$ = (s, r = document) => [...r.querySelectorAll(s)];
const sleep = ms => new Promise(r => setTimeout(r, ms));
let T, redToggle, forceRed = false;
const osRed = matchMedia('(prefers-reduced-motion: reduce)');
const isRed = () => forceRed || (redToggle && redToggle.checked) || osRed.matches;

/* ---- token access (reduced-aware) ---- */
const dTok = n => n === 'spring' ? T['motion-spring'].duration : T['motion-duration'][n];
const ms = (n, R) => { const t = dTok(n); return R && 'reduced' in t ? t.reduced : t.value; };
const ez = (n, R) => { const t = T['motion-easing'][n]; return R && t.reduced ? t.reduced : t.value; };
const num = (g, n) => T['motion-' + g][n].value;

function fx(el, from, to, dur, ease, o = {}) {
  if (!el) return Promise.resolve();
  const R = isRed(), out = o.role === 'out';
  let f = { ...from }, t = { ...to };
  if (R) {
    if (o.role === 'move') return Promise.resolve();
    f = { o: from.o !== undefined ? from.o : (out ? 1 : 0) }; t = { o: to.o !== undefined ? to.o : (out ? 0 : 1) };
    if (f.o === t.o) { f.o = out ? 1 : 0; t.o = out ? 0 : 1; }
  }
  const hasO = f.o !== undefined || t.o !== undefined;
  const kf = v => { const k = {}; if (!R) k.transform = `translate(${v.x || 0}px,${v.y || 0}px) scale(${v.s === undefined ? 1 : v.s})${v.r ? ` rotate(${v.r}deg)` : ''}`; if (hasO) k.opacity = v.o === undefined ? 1 : v.o; return k; };
  const a = el.animate([kf(f), kf(t)], { duration: ms(dur, R), easing: ez(ease, R), delay: o.delay || 0, fill: o.fill || 'both' });
  return a.finished.then(() => a, () => a);
}
/* The one allowed layout animation: tab-list row height. Snaps under reduced motion. */
function hfx(node, from, to, dur, ease) {
  const R = isRed(); node.style.overflow = 'hidden';
  if (R) { node.style.height = to + 'px'; return Promise.resolve(); }
  const a = node.animate([{ height: from + 'px' }, { height: to + 'px' }], { duration: ms(dur, R), easing: ez(ease, R), fill: 'both' });
  return a.finished.then(() => a, () => a);
}

/* ---- tiny DOM helpers for the demos ---- */
const h = (tag, cls, html, style) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html !== undefined) e.innerHTML = html; if (style) e.style.cssText = style; return e; };
const COLS = ['#E94D73', '#686BEC', '#149F9E', '#C37F3C', '#576981', '#EA58A5'];
const mrow = (txt, i = 0, sel = false) => h('div', 'mr' + (sel ? ' sel' : ''), `<b style="--c:${COLS[i % COLS.length]}"></b><span>${txt}</span>`);
const node = (txt, i, sel) => { const n = h('div', 'node'); n.appendChild(mrow(txt, i, sel)); return n; };
const win = (stage, cls = '') => { const mw = h('div', 'mw ' + cls); const ms_ = h('div', 'ms'), mc = h('div', 'mc', '<i style="width:70%"></i><i style="width:92%"></i><i style="width:58%"></i><i style="width:80%"></i>'); mw.append(ms_, mc); stage.appendChild(mw); return { mw, ms: ms_, mc }; };
const hint = (stage, t) => { let e = $('.hint', stage); if (!e) { e = h('div', 'hint'); stage.appendChild(e); } e.textContent = t; };
const NAMES = ['Inbox', 'Design doc', 'Reading list', 'Weather', 'Release notes', 'Bench'];

/* ---- demos: setup(stage) builds the idle state and returns async run() ---- */
const demos = {};

demos.sidebar = stage => {
  const { ms: sb, mc } = win(stage); NAMES.slice(0, 5).forEach((n, i) => sb.appendChild(mrow(n, i, i === 1)));
  const W = 104, delta = 98;
  return async () => {
    hint(stage, 'hide');
    mc.classList.add('full');
    await Promise.all([fx(sb, { x: 0 }, { x: -(W + 24) }, 'base', 'accelerate', { role: 'out' }), fx(mc, { x: delta }, { x: 0 }, 'base', 'accelerate', { role: 'move' })]);
    await sleep(450); hint(stage, 'show'); mc.classList.remove('full');
    await Promise.all([fx(sb, { x: -(W + 24) }, { x: 0 }, 'slow', 'decelerate', { role: 'in' }), fx(mc, { x: -delta }, { x: 0 }, 'slow', 'decelerate', { role: 'move' })]);
    hint(stage, 'shown');
  };
};

demos.peek = stage => {
  const { ms: sb, mc } = win(stage); mc.classList.add('full'); NAMES.slice(0, 5).forEach((n, i) => sb.appendChild(mrow(n, i, i === 1)));
  sb.style.cssText = 'background:var(--aerie-color-frame-base);box-shadow:var(--aerie-shadow-peek);border-radius:0 8px 8px 0;z-index:3;transform:translateX(-128px)';
  const hot = h('div', 'hot'); stage.appendChild(hot);
  const ptr = h('div', 'dotbtn on', '', 'position:absolute;width:12px;height:12px;left:60px;top:90px;z-index:9;transition:left 160ms linear'); stage.appendChild(ptr);
  hint(stage, 'hidden. Hot zone is the strip on the left edge');
  return async () => {
    const R = isRed(); sb.style.transform = R ? 'none' : 'translateX(-128px)'; sb.style.opacity = R ? 0 : 1;
    ptr.style.left = '60px'; hint(stage, 'hidden'); await sleep(250);
    ptr.style.left = '14px'; hint(stage, 'arming (peek-delay ' + num('timing', 'peek-delay') + 'ms)'); await sleep(num('timing', 'peek-delay'));
    hint(stage, 'showing (peek-show, spring)');
    await fx(sb, { x: -128 }, { x: 0 }, 'peek-show', 'spring', { role: 'in' });
    hint(stage, 'shown'); await sleep(600); ptr.style.left = '210px'; hint(stage, 'leaving (peek-leave ' + num('timing', 'peek-leave') + 'ms)');
    await sleep(num('timing', 'peek-leave')); hint(stage, 'hiding (peek-hide, accelerate)');
    await fx(sb, { x: 0 }, { x: -128 }, 'peek-hide', 'accelerate', { role: 'out' });
    hint(stage, 'hidden');
  };
};

demos.workspace = stage => {
  const { mw, ms: sb } = win(stage); sb.style.width = '150px'; mw.querySelector('.mc').style.left = '150px';
  const mk = (names, off) => { const p = h('div', 'pane'); names.forEach((n, i) => p.appendChild(mrow(n, i + off, i === 0))); return p; };
  const A = mk(['Inbox', 'Design doc', 'Reading list', 'Weather'], 0), B = mk(['Sprint board', 'PR #412', 'Metrics', 'Docs'], 2);
  B.style.visibility = 'hidden'; sb.style.overflow = 'hidden'; sb.append(A, B);
  const dots = h('div', '', '<span class="dotbtn on"></span> <span class="dotbtn"></span>', 'position:absolute;left:8px;bottom:6px;display:flex;gap:6px;z-index:3'); sb.appendChild(dots);
  return async () => {
    B.style.visibility = 'visible'; const w = 150;
    $$('.dotbtn', dots).forEach((d, i) => d.classList.toggle('on', i === 1));
    hint(stage, 'strip: both panes move one pane width');
    await Promise.all([fx(A, { x: 0, o: 1 }, { x: -w, o: 1 }, 'workspace-switch', 'spring-settle', { role: 'out' }), fx(B, { x: w, o: 1 }, { x: 0, o: 1 }, 'workspace-switch', 'spring-settle', { role: 'in' })]);
  };
};

demos.theme = stage => {
  const { mw, ms: sb, mc } = win(stage);
  const before = mw.querySelector('.mw::before'); void before;
  const mk = cls => { const l = h('div', '', '', 'position:absolute;inset:0;opacity:0;z-index:0'); l.style.background = cls; return l; };
  const A = mk('linear-gradient(135deg,var(--aerie-workspace-dusk-stop-1),var(--aerie-workspace-dusk-stop-2),var(--aerie-workspace-dusk-stop-3))');
  const B = mk('linear-gradient(135deg,var(--aerie-workspace-tide-stop-1),var(--aerie-workspace-tide-stop-2))');
  mw.insertBefore(A, mw.firstChild); mw.insertBefore(B, mw.firstChild); mw.style.setProperty('--x', 0);
  mw.style.background = 'var(--aerie-color-frame-base)'; mw.classList.add('none');
  mw.insertAdjacentHTML('beforeend', '<style>.mw.none::before{display:none}</style>');
  A.style.opacity = .5; ['Personal', 'Work'].forEach((n, i) => sb.appendChild(mrow(n, i, i === 0)));
  const dark = h('div', '', '', 'position:absolute;inset:0;background:#17161B;opacity:0;z-index:4;pointer-events:none;border-radius:8px'); mw.appendChild(dark);
  return async () => {
    hint(stage, 'tint A to B, and chrome snapshot dissolves');
    await Promise.all([fx(A, { o: .5 }, { o: 0 }, 'xslow', 'standard', { role: 'out' }), fx(B, { o: 0 }, { o: .5 }, 'xslow', 'standard', { role: 'in' }), fx(dark, { o: 0 }, { o: .55 }, 'xslow', 'standard', { role: 'in' })]);
    await sleep(300); hint(stage, 'and back');
    await Promise.all([fx(B, { o: .5 }, { o: 0 }, 'xslow', 'standard', { role: 'out' }), fx(A, { o: 0 }, { o: .5 }, 'xslow', 'standard', { role: 'in' }), fx(dark, { o: .55 }, { o: 0 }, 'xslow', 'standard', { role: 'out' })]);
  };
};

demos.swipe = stage => {
  const w = 200, band = num('ratio', 'rubber-band'), commit = num('distance', 'swipe-commit'), vcommit = num('ratio', 'swipe-velocity');
  const rub = x => x * (1 - Math.min(1, Math.abs(x) / (band * w)));
  const box = h('div', '', '', `position:absolute;left:50%;top:22px;width:${w}px;height:140px;margin-left:-${w / 2}px;overflow:hidden;border-radius:8px;background:var(--aerie-color-frame-base);box-shadow:0 0 0 1px var(--sp-line)`);
  const strip = h('div', '', '', `position:absolute;inset:0;width:${w * 3}px;left:0;display:flex;will-change:transform`);
  const mk = (n, i) => { const p = h('div', '', '', `width:${w}px;padding:8px 6px;display:flex;flex-direction:column;gap:4px;flex:none`); [n + ' 1', n + ' 2', n + ' 3'].forEach((t, j) => p.appendChild(mrow(t, i * 2 + j, j === 0))); return p; };
  strip.append(mk('Personal', 0), mk('Work', 1), mk('Side', 2)); box.appendChild(strip); stage.appendChild(box);
  hint(stage, 'drag me. Past the first pane the band resists (x * (1 - |x| / (' + band + ' * w)))');
  let idx = 0, base = 0; const set = x => { strip.style.transform = `translateX(${x}px)`; };
  let sx = 0, st = 0, dragging = false, dxLast = 0;
  box.style.touchAction = 'none'; box.style.cursor = 'grab';
  box.addEventListener('pointerdown', e => { dragging = true; sx = e.clientX; st = performance.now(); strip.getAnimations().forEach(a => a.cancel()); box.setPointerCapture(e.pointerId); });
  const track = dx => { if (isRed()) return; const nxt = idx + (dx < 0 ? 1 : -1); const ok = nxt >= 0 && nxt <= 2; set(-idx * w + (ok ? dx : rub(dx))); };
  box.addEventListener('pointermove', e => { if (!dragging) return; dxLast = e.clientX - sx; track(dxLast); });
  const release = async (dx, v) => {
    const nxt = idx + (dx < 0 ? 1 : -1), ok = nxt >= 0 && nxt <= 2;
    const go = ok && (Math.abs(dx) >= commit || (v >= vcommit && Math.abs(dx) >= 20));
    const cur = -idx * w + (isRed() ? 0 : (ok ? dx : rub(dx))), to = -(go ? nxt : idx) * w;
    if (go) idx = nxt;
    if (isRed()) { await fx(strip, { o: .2 }, { o: 1 }, 'workspace-switch', 'spring-settle', { role: 'in' }); set(to); return; }
    set(to); await fx(strip, { x: cur - to }, { x: 0 }, 'workspace-switch', 'spring-settle', { role: 'move' });
  };
  box.addEventListener('pointerup', async () => { if (!dragging) return; dragging = false; const v = Math.abs(dxLast) / Math.max(1, performance.now() - st); await release(dxLast, v); dxLast = 0; });
  const drag = async (dx, dur = 420) => { const t0 = performance.now(); await new Promise(r => { const f = () => { const k = Math.min(1, (performance.now() - t0) / dur); track(dx * k); k < 1 ? requestAnimationFrame(f) : r(); }; f(); }); };
  return async () => {
    idx = 0; set(0); strip.getAnimations().forEach(a => a.cancel());
    hint(stage, '1. drag right past the first pane: rubber band'); await drag(150); await release(150, 0.1); await sleep(300);
    hint(stage, '2. drag left 40px and release: below ' + commit + 'px, springs back'); await drag(-40, 300); await release(-40, 0.05); await sleep(300);
    hint(stage, '3. drag left ' + (commit + 20) + 'px: commits'); await drag(-(commit + 20), 380); await release(-(commit + 20), 0.2);
    hint(stage, 'also commits below ' + commit + 'px at >' + vcommit + ' px/ms');
  };
};

demos.open = stage => {
  const { ms: sb } = win(stage); ['Design doc', 'Reading list', 'Weather'].forEach((n, i) => sb.appendChild(node(n, i + 1)));
  sb.insertAdjacentHTML('afterbegin', '<div class="mr" style="background:none;color:var(--sp-muted)"><span>+ New tab</span></div>');
  return async () => {
    const n = node('New tab', 0, true); sb.insertBefore(n, sb.children[1]); const hh = n.getBoundingClientRect().height || 22;
    await Promise.all([hfx(n, 0, hh, 'base', 'standard'), fx(n.firstChild, { s: num('scale', 'enter'), o: 0 }, { s: 1, o: 1 }, 'fast', 'decelerate', { role: 'in', delay: num('timing', 'enter-delay') })]);
  };
};

demos.close = stage => {
  const { ms: sb } = win(stage); ['Inbox', 'Design doc', 'Reading list', 'Weather'].forEach((n, i) => sb.appendChild(node(n, i, i === 1)));
  return async () => {
    const n = sb.children[1], hh = n.getBoundingClientRect().height;
    await fx(n.firstChild, { x: 0, o: 1 }, { x: -num('distance', 'close-slide'), o: 0 }, 'fast', 'accelerate', { role: 'out' });
    await hfx(n, hh, 0, 'base', 'standard');
    $$('.mr', sb)[1].classList.add('sel');
  };
};

demos.pinclose = stage => {
  const { ms: sb, mc } = win(stage);
  sb.insertAdjacentHTML('beforeend', '<div class="mr" style="background:none;color:var(--sp-muted);font-size:9px"><span>PINNED</span></div>');
  const p = node('Docs', 1, true), q = node('Mail', 2); sb.append(p, q, h('div', 'mr', '<span style="color:var(--sp-muted);font-size:9px">TODAY</span>', 'background:none'), node('Inbox', 0), node('Weather', 3));
  return async () => {
    hint(stage, 'close pinned: reset URL, sleep, select next');
    const fav = $('b', p);
    await Promise.all([fx(fav, { o: 1 }, { o: num('opacity', 'sleeping') }, 'fast', 'standard', { role: 'out' }),
      fx(mc, { o: .4 }, { o: 1 }, 'fast', 'standard', { role: 'in' })]);
    fav.style.filter = 'saturate(0)'; $$('.mr', sb).forEach(r => r.classList.remove('sel')); $('.mr', q).classList.add('sel');
    hint(stage, 'asleep. A second close removes it');
  };
};

demos.pin = stage => {
  const { ms: sb } = win(stage);
  const pz = h('div', '', '<div class="mr" style="background:none;color:var(--sp-muted);font-size:9px"><span>PINNED</span></div>'), tz = h('div', '', '<div class="mr" style="background:none;color:var(--sp-muted);font-size:9px"><span>TODAY</span></div>');
  [pz, tz].forEach(z => { z.style.cssText = 'display:flex;flex-direction:column;gap:4px'; });
  pz.appendChild(node('Docs', 1)); ['Inbox', 'Design doc', 'Weather'].forEach((n, i) => tz.appendChild(node(n, i + 2, i === 1)));
  sb.append(pz, tz);
  return async () => {
    const all = () => $$('.node', sb), before = new Map(all().map(n => [n.textContent, n.getBoundingClientRect()]));
    const mv = all().find(n => n.textContent === 'Design doc'); pz.appendChild(mv);
    const jobs = all().map(n => { const b = before.get(n.textContent), a = n.getBoundingClientRect(), dx = b.left - a.left, dy = b.top - a.top; if (!dx && !dy) return null; const moved = n === mv; return fx(n.firstChild, { x: dx, y: dy }, { x: 0, y: 0 }, moved ? 'spring' : 'base', moved ? 'spring' : 'standard', { role: 'move' }); });
    await Promise.all(jobs); hint(stage, 'pinned. Unpin runs the reverse, same curves');
  };
};

demos.drag = stage => {
  const { ms: sb } = win(stage); const list = ['Inbox', 'Design doc', 'Reading list', 'Weather', 'Bench'].map((n, i) => node(n, i, i === 0)); list.forEach(n => sb.appendChild(n));
  return async () => {
    const R = isRed(), step = list[1].getBoundingClientRect().top - list[0].getBoundingClientRect().top, d = list[0].firstChild;
    hint(stage, 'lift: scale ' + num('scale', 'lift') + ' + shadow-lift');
    d.style.boxShadow = 'var(--aerie-shadow-lift)'; d.style.background = 'var(--aerie-color-surface-popover)'; d.style.zIndex = 4; d.style.position = 'relative';
    await fx(d, { s: 1 }, { s: num('scale', 'lift') }, 'fast', 'standard', { role: 'move' });
    hint(stage, 'follows the pointer 1:1; rows in its way shift (base, standard)');
    const y = 2 * step, t0 = performance.now(), dur = 700;
    const shifts = [list[1], list[2]].map((n, i) => sleep(150 + i * 250).then(() => fx(n.firstChild, { y: 0 }, { y: -step }, 'base', 'standard', { role: 'move' })));
    await new Promise(r => { const f = () => { const k = Math.min(1, (performance.now() - t0) / dur); d.style.translate = `0 ${y * k + 6 * (1 - k) * 0}px`; k < 1 ? requestAnimationFrame(f) : r(); }; f(); });
    await Promise.all(shifts); hint(stage, 'drop: spring settle, scale back to 1');
    await fx(d, { y: 6, s: num('scale', 'lift') }, { y: 0, s: 1 }, 'spring', 'spring', { role: 'move' });
    d.style.boxShadow = '';
  };
};

demos.wsdrag = stage => {
  const { mw, ms: sb } = win(stage);
  ['Inbox', 'Design doc', 'Weather'].forEach((n, i) => sb.appendChild(mrow(n, i, i === 0)));
  const mk = (top, on) => { const d = h('div', 'dotbtn' + (on ? ' on' : ''), '', `position:absolute;left:122px;top:${top}px;z-index:3`); mw.appendChild(d); return d; };
  const d1 = mk(40, true), d2 = mk(76, false), ring = h('div', 'ring', '', 'left:118px;top:72px;z-index:4'); mw.appendChild(ring);
  const ghost = mrow('Design doc', 1, true); ghost.style.cssText = 'position:absolute;left:6px;top:42px;width:92px;z-index:6;box-shadow:var(--aerie-shadow-lift)'; mw.appendChild(ghost);
  return async () => {
    const R = isRed(), dwell = num('timing', 'ws-drag-hover');
    hint(stage, 'drag the tab to the second Workspace icon');
    ghost.style.transition = 'left 380ms ease, top 380ms ease'; ghost.style.left = '104px'; ghost.style.top = '74px'; await sleep(420);
    hint(stage, 'hover ' + dwell + 'ms: ring grows in (linear token)');
    const dw = R ? ring.animate([{ opacity: num('opacity', 'ws-ring-static') }, { opacity: num('opacity', 'ws-ring-static') }], { duration: dwell, fill: 'both' }) : ring.animate([{ opacity: num('opacity', 'ws-ring-start'), transform: `scale(${num('scale', 'ws-ring-start')})` }, { opacity: 1, transform: 'scale(1)' }], { duration: dwell, easing: ez('linear'), fill: 'both' });
    await dw.finished; hint(stage, 'switches with the normal Workspace motion');
    d1.classList.remove('on'); d2.classList.add('on'); ring.style.opacity = 0; dw.cancel();
    const rows = $$('.mr', sb); await Promise.all(rows.map(r => fx(r, { x: 0, o: 1 }, { x: -20, o: 0 }, 'workspace-switch', 'spring-settle', { role: 'out', fill: 'backwards' }).then(() => fx(r, { x: 20, o: 0 }, { x: 0, o: 1 }, 'workspace-switch', 'spring-settle', { role: 'in' }))));
  };
};

demos.folder = stage => {
  const { ms: sb } = win(stage);
  const f = h('div', 'node'); f.appendChild(h('div', 'mr', '<span class="chev" style="display:inline-block;width:10px">&#9656;</span><span>Research</span>'));
  const kids = h('div', '', '', 'display:flex;flex-direction:column;gap:4px;padding-left:12px'); ['Paper 1', 'Paper 2', 'Paper 3', 'Notes'].forEach((n, i) => kids.appendChild(node(n, i + 2)));
  f.appendChild(kids); sb.append(node('Inbox', 0), f, node('Weather', 4));
  const chev = $('.chev', f); chev.style.transform = 'rotate(90deg)';
  const HH = () => kids.scrollHeight;
  return async () => {
    const R = isRed();
    hint(stage, 'collapse (' + ms('folder-collapse', R) + 'ms)');
    const hh = kids.getBoundingClientRect().height; kids.style.overflow = 'hidden';
    const ca = R ? kids.animate([{ opacity: 1 }, { opacity: 0 }], { duration: ms('folder-collapse', R), easing: ez('in-out', R), fill: 'forwards' }) : kids.animate([{ height: hh + 'px', opacity: 1 }, { height: '0px', opacity: 0 }], { duration: ms('folder-collapse', R), easing: ez('in-out', R), fill: 'forwards' });
    await Promise.all([fx(chev, { r: 90 }, { r: 0 }, 'fast', 'standard', { role: 'move' }), ca.finished]);
    if (R) kids.style.display = 'none';
    await sleep(450); hint(stage, 'reveal (' + ms('folder-reveal', R) + 'ms, children ' + num('timing', 'stagger') + 'ms apart)');
    ca.cancel(); kids.style.display = 'flex'; const full = HH();
    const ra = R ? kids.animate([{ opacity: 0 }, { opacity: 1 }], { duration: ms('folder-reveal', R), easing: ez('in-out', R) }) : kids.animate([{ height: '0px' }, { height: full + 'px' }], { duration: ms('folder-reveal', R), easing: ez('in-out', R) });
    if (!R) [...kids.children].slice(0, num('timing', 'stagger-cap')).forEach((c, i) => c.animate([{ opacity: 0 }, { opacity: 1 }], { duration: ms('folder-reveal', R), easing: ez('in-out', R), delay: i * num('timing', 'stagger'), fill: 'backwards' }));
    await Promise.all([fx(chev, { r: 0 }, { r: 90 }, 'fast', 'standard', { role: 'move' }), ra.finished]);
  };
};

demos.cmd = stage => {
  const { mw, ms: sb } = win(stage); ['Inbox', 'Design doc', 'Weather'].forEach((n, i) => sb.appendChild(mrow(n, i)));
  const scrim = h('div', 'scrim'), cmd = h('div', 'cmd', '<b>Search or enter address</b><div class="muted" style="margin-top:6px">design doc &middot; Reading list &middot; Weather</div>');
  scrim.style.opacity = 0; cmd.style.opacity = 0; mw.append(scrim, cmd);
  return async () => {
    hint(stage, 'appear'); const a = fx(scrim, { o: 0 }, { o: 1 }, 'fast', 'standard', { role: 'in' });
    await Promise.all([a, fx(cmd, { o: num('opacity', 'cmdbar-start'), s: num('scale', 'cmdbar-enter') }, { o: 1, s: 1 }, 'fast', 'decelerate', { role: 'in' })]);
    await sleep(650); hint(stage, 'dismiss (xfast, accelerate)');
    await Promise.all([fx(cmd, { o: 1, s: 1 }, { o: 0, s: num('scale', 'cmdbar-enter') }, 'xfast', 'accelerate', { role: 'out' }), fx(scrim, { o: 1 }, { o: 0 }, 'xfast', 'accelerate', { role: 'out' })]);
  };
};

demos.popover = stage => {
  const { mw, ms: sb } = win(stage); ['Inbox', 'Design doc', 'Weather'].forEach((n, i) => sb.appendChild(mrow(n, i)));
  const pop = h('div', 'pop', '<div>Archive</div><div class="muted">Design doc, 2m ago</div><div class="muted">Weather, 1h ago</div>', 'left:112px;top:30px;width:150px;opacity:0');
  const menu = h('div', 'pop', '<div>Pin tab</div><div>Move to Workspace</div><div>Close tab</div>', 'left:112px;top:30px;width:130px;opacity:0;transform-origin:0 0');
  mw.append(pop, menu);
  return async () => {
    hint(stage, 'popover'); await fx(pop, { y: -4, o: 0 }, { y: 0, o: 1 }, 'fast', 'decelerate', { role: 'in' });
    await sleep(600); pop.style.opacity = 0; pop.getAnimations().forEach(a => a.cancel()); hint(stage, 'menu (no exit animation, closes at once)');
    await sleep(250); await fx(menu, { s: .97, o: 0 }, { s: 1, o: 1 }, 'xfast', 'decelerate', { role: 'in' });
  };
};

demos.toast = stage => {
  const { mw } = win(stage);
  const t = h('div', 'pop', 'Sidebar kept open', 'left:50%;margin-left:-60px;bottom:18px;width:120px;text-align:center;opacity:0'); mw.appendChild(t);
  return async () => {
    await fx(t, { y: 6, o: 0 }, { y: 0, o: 1 }, 'fast', 'decelerate', { role: 'in' });
    await sleep(num('timing', 'toast')); await fx(t, { o: 1 }, { o: 0 }, 'fast', 'accelerate', { role: 'out' });
  };
};

demos.tooltip = stage => {
  const { mw, ms: sb } = win(stage); ['Inbox', 'Design doc', 'Weather'].forEach((n, i) => sb.appendChild(mrow(n, i, i === 1)));
  const tip = h('div', 'tipbox', 'Back to pinned page', 'left:36px;top:60px;opacity:0'); mw.appendChild(tip);
  const bar = h('div', '', '', 'position:absolute;left:10px;bottom:22px;height:3px;width:0;background:var(--sp-accent);z-index:9;border-radius:2px'); stage.appendChild(bar);
  return async () => {
    const d = num('timing', 'tooltip-delay'); tip.style.opacity = 0; hint(stage, 'pointer rests: ' + d + 'ms delay');
    bar.animate([{ width: '0px' }, { width: '120px' }], { duration: d, easing: 'linear', fill: 'both' });
    await sleep(d); await fx(tip, { o: 0 }, { o: 1 }, 'xfast', 'decelerate', { role: 'in' }); tip.style.opacity = 1; hint(stage, 'fades in over xfast; disappears at once');
  };
};

demos.sleep = stage => {
  const { ms: sb } = win(stage); const a = node('Mail', 1), b = node('Docs', 2, true), c = node('Weather', 3); sb.append(a, b, c);
  return async () => {
    const fav = $('b', a); fav.style.filter = ''; hint(stage, 'tab goes to sleep: opacity fade, then desaturate');
    await fx(fav, { o: 1 }, { o: num('opacity', 'sleeping') }, 'fast', 'standard', { role: 'out' });
    fav.style.filter = 'saturate(0)'; a.firstChild.insertAdjacentHTML('beforeend', '<span style="margin-left:auto;font-size:9px;color:var(--sp-muted)">&#9790;</span>');
  };
};

demos.archive = stage => {
  const { mw, ms: sb } = win(stage); ['Inbox', 'Design doc', 'Weather'].forEach((n, i) => sb.appendChild(node(n, i, i === 0)));
  const btn = h('div', 'mr', '<span>Archive</span>', 'position:absolute;left:6px;right:6px;bottom:6px;z-index:3'); sb.appendChild(btn);
  return async () => {
    const R = isRed(), n = sb.children[1], r = n.getBoundingClientRect(), m = mw.getBoundingClientRect(), b = btn.getBoundingClientRect();
    const ghost = n.firstChild.cloneNode(true); ghost.style.cssText = `position:absolute;left:${r.left - m.left}px;top:${r.top - m.top}px;width:${r.width}px;z-index:6;pointer-events:none`;
    mw.appendChild(ghost); n.firstChild.style.opacity = 0; hint(stage, 'copy shrinks toward the Archive button; the row collapses');
    const dx = b.left + b.width / 2 - (r.left + r.width / 2), dy = b.top + b.height / 2 - (r.top + r.height / 2);
    await Promise.all([fx(ghost, { x: 0, y: 0, s: 1, o: 1 }, { x: dx, y: dy, s: num('scale', 'archive-collapse'), o: 0 }, 'archive-collapse', 'accelerate', { role: 'out' }), hfx(n, r.height, 0, 'base', 'standard')]);
    ghost.remove(); hint(stage, 'the button pulses once');
    const flash = 'color-mix(in srgb, var(--sp-accent) 34%, transparent)';
    await btn.animate(R ? [{ background: flash }, { background: 'transparent' }] : [{ transform: 'scale(1)', background: flash }, { transform: `scale(${num('scale', 'archive-pulse')})`, offset: .4 }, { transform: 'scale(1)', background: 'transparent' }], { duration: ms('archive-pulse', R), easing: ez('standard', R) }).finished;
  };
};

demos.restore = stage => {
  const { mw, ms: sb } = win(stage); ['Inbox', 'Weather'].forEach((n, i) => sb.appendChild(node(n, i + 2)));
  const pop = h('div', 'pop', '<div>Archive</div>', 'left:112px;top:20px;width:150px'); const ar = mrow('Design doc', 1); pop.appendChild(ar); mw.appendChild(pop);
  return async () => {
    hint(stage, 'click a row in the Archive panel');
    const m = mw.getBoundingClientRect(), a = ar.getBoundingClientRect(), n = node('Design doc', 1, true); sb.insertBefore(n, sb.firstChild);
    const to = n.firstChild.getBoundingClientRect(), hh = n.getBoundingClientRect().height;
    const ghost = ar.cloneNode(true); ghost.style.cssText = `position:absolute;left:${a.left - m.left}px;top:${a.top - m.top}px;width:${a.width}px;z-index:6;pointer-events:none`; mw.appendChild(ghost); ar.style.visibility = 'hidden';
    await Promise.all([hfx(n, 0, hh, 'base', 'standard'), fx(n.firstChild, { o: 0 }, { o: 1 }, 'archive-restore', 'accelerate', { role: 'in' }), fx(ghost, { x: 0, y: 0, o: 1 }, { x: to.left - a.left, y: to.top - a.top, o: 0 }, 'archive-restore', 'spring', { role: 'out' })]);
    ghost.remove(); hint(stage, 'back at the top of Today');
  };
};

demos.autofile = stage => {
  const { ms: sb } = win(stage);
  const folder = node('Trips', 3); $('.mr', folder).insertAdjacentHTML('beforeend', '<span style="margin-left:auto;color:var(--sp-muted);font-size:9px">rule</span>');
  sb.append(folder, node('Inbox', 0), node('Weather', 4));
  return async () => {
    const R = isRed(), fr = $('.mr', folder); hint(stage, 'the new tab lands at the top of Today');
    const n = node('Flights to Lisbon', 1, true); sb.insertBefore(n, sb.firstChild);
    await Promise.all([hfx(n, 0, n.getBoundingClientRect().height, 'base', 'standard'), fx(n.firstChild, { s: num('scale', 'enter'), o: 0 }, { s: 1, o: 1 }, 'fast', 'decelerate', { role: 'in' })]);
    const d = ms2('autofile-delay'); hint(stage, 'pause ' + d + 'ms, then it files'); await sleep(d);
    const b = n.firstChild.getBoundingClientRect(); n.firstChild.style.paddingLeft = '14px'; folder.after(n); const a = n.firstChild.getBoundingClientRect();
    hint(stage, 'flies into the folder (spring); the folder pulses');
    const flash = 'color-mix(in srgb, var(--sp-accent) 30%, transparent)';
    await Promise.all([R ? fx(n.firstChild, { o: 0 }, { o: 1 }, 'fast', 'standard', { role: 'in' }) : fx(n.firstChild, { x: b.left - a.left, y: b.top - a.top }, { x: 0, y: 0 }, 'spring', 'spring', { role: 'move' }),
      fr.animate([{ background: flash }, { background: 'transparent' }], { duration: ms('autofile-pulse', R), easing: ez('standard', R) }).finished]);
  };
  function ms2(n) { const t = T['motion-timing'][n]; return isRed() && 'reduced' in t ? t.reduced : t.value; }
};

demos.sheet = stage => {
  const { mw, ms: sb } = win(stage); ['Inbox', 'Trips', 'Weather'].forEach((n, i) => sb.appendChild(mrow(n, i)));
  const sh = h('div', 'pop', '<b>Edit folder</b><div class="muted" style="margin-top:6px">Name, kind, rules</div><div class="muted">Behaviours</div>', 'left:112px;top:24px;width:150px;opacity:0'); mw.appendChild(sh);
  return async () => {
    hint(stage, 'enter: slide 10px and fade (sheet-enter, decelerate)'); await fx(sh, { x: -10, o: 0 }, { x: 0, o: 1 }, 'sheet-enter', 'decelerate', { role: 'in' });
    await sleep(650); hint(stage, 'exit: shorter (sheet-exit, accelerate)'); await fx(sh, { x: 0, o: 1 }, { x: -10, o: 0 }, 'sheet-exit', 'accelerate', { role: 'out' });
  };
};

demos.switch = stage => {
  const mkSw = lab => { const r = h('div', 'mr', `<span>${lab}</span>`, 'justify-content:space-between;margin:10px 16px 0;max-width:200px'); const tr = h('i', '', '', 'position:relative;display:inline-block;width:30px;height:18px;border-radius:9px;background:var(--sp-line);flex:none'); const kn = h('b', '', '', 'position:absolute;top:2px;left:2px;width:14px;height:14px;border-radius:50%;background:#fff;box-shadow:0 1px 2px rgba(0,0,0,.35)'); tr.appendChild(kn); r.appendChild(tr); return { r, tr, kn }; };
  const a = mkSw('Auto-archive'), b = mkSw('Folder rules'); stage.append(a.r, b.r);
  return async () => {
    const R = isRed(); hint(stage, 'knob slides, track colour changes (' + ms('switch', R) + 'ms)');
    const go = (s, on) => Promise.all([s.kn.animate([{ transform: `translateX(${on ? 0 : 12}px)` }, { transform: `translateX(${on ? 12 : 0}px)` }], { duration: ms('switch', R), easing: ez('standard', R), fill: 'both' }).finished,
      s.tr.animate([{ background: on ? 'var(--sp-line)' : 'var(--sp-accent)' }, { background: on ? 'var(--sp-accent)' : 'var(--sp-line)' }], { duration: ms('switch', R), easing: ez('standard', R), fill: 'both' }).finished]);
    await go(a, true); await sleep(250); await go(b, true); await sleep(400); await go(a, false);
  };
};

demos.hztab = stage => {
  const strip = h('div', '', '', 'position:absolute;left:10px;right:10px;top:50px;height:34px;display:flex;gap:2px;align-items:flex-end;overflow:hidden;padding:0 6px;background:var(--aerie-color-frame-base);border-radius:8px 8px 0 0;box-shadow:0 0 0 1px var(--sp-line)');
  const tab = (t, i, sel) => { const e = h('div', '', `<b style="--c:${COLS[i % COLS.length]};display:inline-block;width:12px;height:12px;border-radius:3px;background:var(--c);flex:none"></b><span style="overflow:hidden;white-space:nowrap">${t}</span>`, `display:flex;align-items:center;gap:6px;padding:0 8px;height:28px;width:92px;flex:none;border-radius:8px 8px 0 0;font-size:10px;${sel ? 'background:var(--aerie-color-content-surface)' : ''}`); return e; };
  ['Inbox', 'Design doc', 'Weather'].forEach((n, i) => strip.appendChild(tab(n, i, i === 1))); stage.appendChild(strip);
  return async () => {
    const R = isRed(); hint(stage, 'open: width grows, tab scales in (tab-open)');
    const e = tab('New tab', 4, true); strip.appendChild(e);
    if (R) await fx(e, { o: 0 }, { o: 1 }, 'tab-open', 'standard', { role: 'in' });
    else await e.animate([{ maxWidth: '0px', opacity: 0, transform: `scale(${num('scale', 'enter')})` }, { maxWidth: '92px', opacity: 1, transform: 'scale(1)' }], { duration: ms('tab-open', R), easing: ez('decelerate', R) }).finished;
    await sleep(600); hint(stage, 'close: slide 8px down and fade, then the width collapses (tab-close)');
    await fx(e, { y: 0, o: 1 }, { y: num('distance', 'close-slide'), o: 0 }, 'xfast', 'accelerate', { role: 'out', fill: 'forwards' });
    if (!R) await e.animate([{ maxWidth: '92px' }, { maxWidth: '0px' }], { duration: ms('tab-close', R), easing: ez('standard', R), fill: 'forwards' }).finished;
    e.remove();
  };
};

demos.startup = stage => {
  const { ms: sb } = win(stage); ['Inbox', 'Design doc', 'Reading list', 'Weather'].forEach((n, i) => sb.appendChild(node(n, i, i === 1)));
  hint(stage, 'startup and restore: placed at once, ' + num('duration', 'instant') + 'ms');
  return async () => {
    hint(stage, 'startup and restore: placed at once, ' + num('duration', 'instant') + 'ms'); await sleep(800);
    hint(stage, 'window is interactive: user actions animate'); const n = node('New tab', 4, true); sb.insertBefore(n, sb.firstChild);
    await Promise.all([hfx(n, 0, n.getBoundingClientRect().height || 22, 'base', 'standard'), fx(n.firstChild, { s: num('scale', 'enter'), o: 0 }, { s: 1, o: 1 }, 'fast', 'decelerate', { role: 'in' })]);
  };
};

/* ---- the cards ---- */
const P = (...a) => a;
const CARDS = [
  { id: 'sidebar', n: 1, title: 'Sidebar show and hide', trigger: 'Ctrl+S, the sidebar button, or docking the auto-hide sidebar.',
    moves: [P('transform', 'Sidebar slides by its width plus the hide offset toward the outer edge. The page card follows with a transform from its old position while its box snaps to the new size.')],
    tokens: [P('motion-duration-slow', 'show'), P('motion-duration-base', 'hide'), P('motion-easing-decelerate', 'show'), P('motion-easing-accelerate', 'hide'), P('motion-distance-sidebar-hide-offset', 'extra slide past the width')],
    delays: 'None. Sidebar and card start together.', reduced: 'No movement. The sidebar fades (opacity) over 140ms, the card snaps to its new size.', demo: 'sidebar' },
  { id: 'peek', n: 2, title: 'Auto-hide peek', trigger: 'Pointer dwells in the 6px edge hot zone of the hidden sidebar (or F6, Ctrl+Alt+S).',
    moves: [P('transform', 'Sidebar floats over the page, slides in from the edge; shadow-peek is static.')],
    tokens: [P('motion-duration-peek-show', 'show'), P('motion-duration-peek-hide', 'hide'), P('motion-easing-spring', 'show'), P('motion-easing-accelerate', 'hide'), P('motion-timing-peek-delay', 'hover-in dwell'), P('motion-timing-peek-leave', 'keep-open after leave'), P('motion-distance-peek-grace-x', 'grace x'), P('motion-distance-peek-grace-y', 'grace y'), P('motion-timing-peek-outside-max', 'mockup stand-in')],
    delays: 'Dwell before showing, keep-open before hiding. Re-entering while hiding reverses from the current offset. Never hides while a menu, drag, rename or the command bar is open.',
    reduced: 'Slide becomes a 100ms fade (decelerate). All delays and the grace distances stay the same, they are behaviour, not motion.', fsm: true, demo: 'peek' },
  { id: 'workspace', n: 3, title: 'Workspace switch', trigger: 'Click a Workspace icon, Ctrl+Shift+1..9, Ctrl+Shift+[ ], command bar.',
    moves: [P('transform', 'Outgoing and incoming tab-list panes sit side by side as a strip; both translate one pane width. Direction follows Workspace order, no wrap-around. Only the two panes animate.')],
    tokens: [P('motion-duration-workspace-switch', 'slide'), P('motion-easing-spring-settle', 'critically damped, no bounce')],
    delays: 'None. The tint and theme crossfade (next card) runs in parallel.', reduced: 'Crossfade of the two panes, 140ms, no movement.', demo: 'workspace' },
  { id: 'theme', n: 4, title: 'Theme and tint crossfade', trigger: 'Switching to a Workspace with a different colour, theme or mode; changing the theme setting.',
    moves: [P('opacity', 'Two tint layers (old out, new in) at the theme tint strength. Theme or mode changes crossfade the whole chrome with a View Transition so text is never half-faded on the wrong surface.')],
    tokens: [P('motion-duration-xslow', 'crossfade'), P('motion-easing-standard', '')],
    delays: 'Starts with the pane slide.', reduced: 'Same crossfade at 140ms. A theme change from settings applies instantly, no View Transition.', demo: 'theme' },
  { id: 'swipe', n: 5, title: 'Swipe with rubber band', trigger: 'Horizontal trackpad swipe or Shift+wheel on the sidebar.',
    moves: [P('transform', 'The strip follows the fingers 1:1 with the neighbouring pane. On release the springs settle to the nearest pane, or back.')],
    tokens: [P('motion-distance-swipe-commit', 'commit distance'), P('motion-ratio-swipe-velocity', 'commit speed, px/ms'), P('motion-ratio-rubber-band', 'band stiffness'), P('motion-duration-workspace-switch', 'release spring'), P('motion-easing-spring-settle', '')],
    delays: 'None. Past the first or last Workspace only the current pane moves: x * (1 - |x| / (ratio * width)). Commits past the distance, or above the speed when moved at least 20px.',
    reduced: 'Nothing moves while swiping. Release past the thresholds switches with the 140ms crossfade.', plots: [{ label: 'rubber band', sub: 'x finger vs shown', fn: () => { const r = Specs.T['motion-ratio']['rubber-band'].value; return t => t * (1 - Math.min(1, t / r)); }, ref: t => t, ymax: 1.1, ymin: -0.05 }], demo: 'swipe' },
  { id: 'open', n: 6, title: 'Open tab', trigger: 'Ctrl+T, + New tab, link opened in a new tab.',
    moves: [P('transform', 'Row content scales from 0.96 to 1.'), P('opacity', 'Row content fades in.')], exc: 'Row height grows from 0 (layout). This is the one place the tab list lays out per frame.',
    tokens: [P('motion-duration-base', 'row height'), P('motion-easing-standard', 'row height'), P('motion-duration-fast', 'content'), P('motion-easing-decelerate', 'content'), P('motion-scale-enter', 'start scale'), P('motion-timing-enter-delay', 'content delay')],
    delays: 'Content waits 60ms so the row has opened a little before it appears.', reduced: 'Content fades in (90ms, same delay). The row height snaps, no scale.', demo: 'open' },
  { id: 'close', n: 7, title: 'Close tab', trigger: 'The x on a row, middle-click, Ctrl+W, Delete.',
    moves: [P('transform', 'Row content slides 8px toward the outer edge.'), P('opacity', 'Row content fades out.')], exc: 'Then row height collapses to 0 (layout) and the rows below move up.',
    tokens: [P('motion-duration-fast', 'slide and fade'), P('motion-easing-accelerate', 'slide and fade'), P('motion-distance-close-slide', 'slide'), P('motion-duration-base', 'height'), P('motion-easing-standard', 'height')],
    delays: 'Sequential: slide and fade finish, then height collapses.', reduced: 'Fade only (90ms), then the height snaps closed.', demo: 'close' },
  { id: 'pinclose', n: 8, title: 'Pinned tab close (reset and sleep)', trigger: 'Ctrl+W or the x on a pinned tab. Default setting: reset and sleep, then select the next tab.',
    moves: [P('opacity', 'Pinned favicon fades to the sleeping opacity, then desaturates (a static filter, not animated). The next tab takes the selection marker. The page card dissolves in.')],
    tokens: [P('motion-opacity-sleeping', 'asleep favicon'), P('motion-duration-fast', 'fade'), P('motion-easing-standard', 'fade')],
    delays: 'None. The row never moves or leaves the pinned section. A second press on the asleep tab closes it and uses the Close tab motion.', reduced: 'Instant swap, no fades.', demo: 'pinclose' },
  { id: 'pin', n: 9, title: 'Pin and unpin (FLIP)', trigger: 'Pin or Unpin from the context menu, or dropping on the pinned list.',
    moves: [P('transform', 'First, Last, Invert, Play: each row that changed place animates from its old box. The tab you pinned moves on the spring, the rows it leaves behind close the gap on the standard ease.')],
    tokens: [P('motion-spring-duration', 'moved row'), P('motion-easing-spring', 'moved row'), P('motion-spring-stiffness', 'spring definition'), P('motion-spring-damping', 'spring definition'), P('motion-spring-mass', 'spring definition'), P('motion-duration-base', 'other rows'), P('motion-easing-standard', 'other rows')],
    delays: 'None.', reduced: 'Instant. No movement.', demo: 'pin' },
  { id: 'drag', n: 10, title: 'Drag: lift, shift, settle', trigger: 'Press and drag a row to reorder or move between sections and folders.',
    moves: [P('transform', 'Lift: the row scales to 1.02. Shift: rows it passes move one row. Settle: the dropped row springs from the release point.')], exc: 'shadow-lift appears at once (no transition); box-shadow is never animated.',
    tokens: [P('motion-scale-lift', 'lift scale'), P('motion-duration-fast', 'lift'), P('motion-easing-standard', 'lift, shift'), P('motion-duration-base', 'shift'), P('motion-spring-duration', 'settle'), P('motion-easing-spring', 'settle')],
    delays: 'None. The row tracks the pointer without easing.', reduced: 'No scale, no shift and an instant drop. The row still tracks the pointer (direct manipulation).', demo: 'drag' },
  { id: 'wsdrag', n: 11, title: 'Drag onto a Workspace icon', trigger: 'Dragging a tab over a Workspace icon in the switcher.',
    moves: [P('transform', 'A ring around the icon scales from the start scale (1.25) to 1.'), P('opacity', 'Ring fades from the start opacity (0.15) to 1 as the dwell runs. Then the normal Workspace switch.')],
    tokens: [P('motion-timing-ws-drag-hover', 'dwell before switching'), P('motion-easing-linear', 'ring grows on a timer'), P('motion-opacity-ws-ring-start', 'ring start opacity'), P('motion-scale-ws-ring-start', 'ring start scale'), P('motion-opacity-ws-ring-static', 'reduced: static ring')],
    delays: 'The ring runs linear for exactly the dwell, so it shows the time left. It is a CSS keyframe (wsDwell) that reads only these tokens.', reduced: 'Static ring at the static opacity (0.6). The 500ms dwell stays. The switch is the 140ms crossfade.', demo: 'wsdrag' },
  { id: 'folder', n: 12, title: 'Folder reveal and collapse', trigger: 'Click a folder row or Enter on it. Up to 5 levels.',
    moves: [P('opacity', 'Reveal: the first 6 children fade in 15ms apart. Collapse: the whole group fades out.'), P('transform', 'Chevron rotates 90 degrees.')], exc: 'Children container height (layout), clipped. Tab-list row height only.',
    tokens: [P('motion-duration-folder-reveal', 'reveal'), P('motion-duration-folder-collapse', 'collapse'), P('motion-easing-in-out', 'both'), P('motion-duration-fast', 'chevron'), P('motion-easing-standard', 'chevron'), P('motion-timing-stagger', 'child stagger'), P('motion-timing-stagger-cap', 'rows that stagger'), P('motion-ratio-folder-edge', 'drag over a folder row: top and bottom share that mean before or after, the middle means into')],
    delays: 'Reveal only: child n (up to 6) starts n x 15ms late, so a long folder opens as a short cascade rather than all at once. Collapse does not stagger, it must feel immediate.', reduced: 'Opacity only (90ms) on the group, no stagger, height snaps, chevron snaps.', demo: 'folder' },
  { id: 'cmd', n: 13, title: 'Command bar appear and dismiss', trigger: 'Ctrl+T, Ctrl+L, Ctrl+K.',
    moves: [P('opacity', 'Scrim fades; the bar starts at 0.5 so it shows in the first frame.'), P('transform', 'Bar scales from 0.98 to 1.')],
    tokens: [P('motion-duration-fast', 'appear'), P('motion-easing-decelerate', 'appear'), P('motion-duration-xfast', 'dismiss'), P('motion-easing-accelerate', 'dismiss'), P('motion-scale-cmdbar-enter', 'scale'), P('motion-opacity-cmdbar-start', 'start opacity')],
    delays: 'None. Dismiss is quicker than appear.', reduced: 'Opacity only, no scale (90ms in and out).', demo: 'cmd' },
  { id: 'popover', n: 14, title: 'Popover and menu appear', trigger: 'Guard and Archive popovers; tab context menu.',
    moves: [P('transform', 'Popover drops 4px into place. Menu scales from 0.97.'), P('opacity', 'Both fade in.')],
    tokens: [P('motion-duration-fast', 'popover'), P('motion-easing-decelerate', 'popover and menu'), P('motion-duration-xfast', 'menu')],
    delays: 'None. Closing has no animation, so a menu never blocks the next action.', reduced: 'Opacity only (90ms), no offset, no scale.', demo: 'popover' },
  { id: 'toast', n: 15, title: 'Toast', trigger: 'Short confirmations such as "Sidebar kept open".',
    moves: [P('transform', '6px rise into place.'), P('opacity', 'Fade in and out.')],
    tokens: [P('motion-duration-fast', 'in and out'), P('motion-easing-decelerate', 'in'), P('motion-easing-accelerate', 'out'), P('motion-timing-toast', 'visible time'), P('motion-timing-toast-action', 'with actions (Undo): visible time')],
    delays: 'Stays for the toast time.', reduced: 'Opacity only.', demo: 'toast' },
  { id: 'tooltip', n: 16, title: 'Tooltip', trigger: 'Pointer rests on an element with a tooltip. Not while dragging.',
    moves: [P('opacity', 'Fades in over xfast (90ms). It disappears at once, so it never lingers over the next target.')],
    tokens: [P('motion-timing-tooltip-delay', 'rest time before it shows'), P('motion-duration-xfast', 'fade in'), P('motion-easing-decelerate', 'fade in')],
    delays: 'The delay is the whole timing. Any pointer press or move to another target cancels it.', reduced: 'Same: opacity only, already within the 140ms limit.', demo: 'tooltip' },
  { id: 'sleep', n: 17, title: 'Sleeping tab', trigger: 'A tab goes to sleep (idle timeout, close of a pinned tab, "Sleep tab").',
    moves: [P('opacity', 'Favicon fades to the sleeping opacity; then a static desaturate is applied. A moon badge appears.')],
    tokens: [P('motion-opacity-sleeping', 'favicon opacity'), P('motion-duration-fast', 'fade'), P('motion-easing-standard', 'fade')],
    delays: 'None.', reduced: 'Instant. No fade.', demo: 'sleep' },
  { id: 'archive', n: 18, title: 'Archive moment', trigger: 'Auto-archive or "Archive now": a Today tab is archived.',
    moves: [P('transform', 'A copy of the row shrinks to 0.25 and flies toward the Archive button while the real row collapses.'), P('opacity', 'The copy fades out on the way.'), P('transform', 'On arrival the Archive button pulses once (scale 1.25 and back) with a colour flash.')],
    exc: 'The real row collapses its height (layout), the same exception as Close tab.',
    tokens: [P('motion-duration-archive-collapse', 'fly and shrink'), P('motion-easing-accelerate', 'fly and shrink'), P('motion-scale-archive-collapse', 'end scale'), P('motion-duration-archive-pulse', 'button pulse'), P('motion-easing-standard', 'button pulse'), P('motion-scale-archive-pulse', 'pulse peak')],
    delays: 'The pulse starts when the copy lands.', reduced: 'The copy only fades (120ms), no flight. The button flashes colour without scaling (140ms).', demo: 'archive' },
  { id: 'restore', n: 19, title: 'Archive restore', trigger: 'Click a row in the Archive panel.',
    moves: [P('transform', 'A copy of the archived row flies from the panel into the top of Today on the spring.'), P('opacity', 'The new row fades in while the copy fades out.')], exc: 'The new row opens its height from 0 first (layout), as in Open tab.',
    tokens: [P('motion-duration-archive-restore', 'flight and fade'), P('motion-easing-spring', 'flight'), P('motion-easing-accelerate', 'row fade')],
    delays: 'If the tab belongs to another Workspace, the Workspace switch runs first (card 3).', reduced: 'Fades only (120ms), no flight.', demo: 'restore' },
  { id: 'autofile', n: 20, title: 'Auto-file into a folder', trigger: 'A new tab matches an auto-file rule (#139).',
    moves: [P('transform', 'The row lands at the top of Today, then flies into the folder (FLIP on the spring).'), P('opacity', 'The folder row flashes the accent colour once (background tint, not a transform).')],
    tokens: [P('motion-timing-autofile-delay', 'pause before it flies'), P('motion-easing-spring', 'flight'), P('motion-spring-duration', 'flight'), P('motion-duration-autofile-pulse', 'folder pulse'), P('motion-easing-standard', 'folder pulse'), P('motion-timing-toast-action', 'toast with Undo stays')],
    delays: 'The pause lets you see where the tab first landed, so the move is read as a move. The toast with Undo and "Don\'t file this site" follows and stays 6 seconds.', reduced: 'The pause is 120ms, the row fades in place of flying, the pulse is 300ms.', demo: 'autofile' },
  { id: 'sheet', n: 21, title: 'Folder editor sheet', trigger: 'Edit folder... from the folder menu, a double-click on the folder icon, or the rule glyph.',
    moves: [P('transform', 'The sheet slides 10px from the sidebar edge into place.'), P('opacity', 'Fades in; fades out on close.')],
    tokens: [P('motion-duration-sheet-enter', 'enter'), P('motion-easing-decelerate', 'enter'), P('motion-duration-sheet-exit', 'exit'), P('motion-easing-accelerate', 'exit')],
    delays: 'None. Exit is shorter than enter. Moving to another folder while open does not replay the exit.', reduced: 'Opacity only (90ms), no slide.', demo: 'sheet' },
  { id: 'switch', n: 22, title: 'Switch and chip toggles', trigger: 'A switch in a sheet or the mockup controls, a segmented control or a chip.',
    moves: [P('transform', 'The switch knob slides across.'), P('opacity', 'The track colour cross-fades (a colour change, the one allowed colour transition on a 30px control).')],
    tokens: [P('motion-duration-switch', 'knob and track'), P('motion-easing-standard', 'knob')],
    delays: 'None.', reduced: 'Knob and track change in 60ms.', demo: 'switch' },
  { id: 'hztab', n: 23, title: 'Horizontal tab open and close', trigger: 'Open or close a tab in the horizontal strip.',
    moves: [P('transform', 'Open: the tab scales from 0.96 to 1. Close: it slides 8px down.'), P('opacity', 'Fades in or out.')], exc: 'Tab width (layout) grows from 0 on open and collapses to 0 on close, so neighbours slide over. One strip only, clipped.',
    tokens: [P('motion-duration-tab-open', 'open'), P('motion-easing-decelerate', 'open'), P('motion-scale-enter', 'start scale'), P('motion-duration-tab-close', 'width collapse'), P('motion-easing-standard', 'width collapse'), P('motion-duration-xfast', 'slide and fade out'), P('motion-easing-accelerate', 'slide and fade out'), P('motion-distance-close-slide', 'slide')],
    delays: 'Close is sequential: slide and fade, then the width collapses.', reduced: 'Open is a 140ms fade, no width animation. Close fades (90ms) and the tab is removed at once.', demo: 'hztab' },
  { id: 'startup', n: 24, title: 'Startup, restore and snaps', trigger: 'First paint, session restore, the first Workspace render, a theme set from a URL or setting.',
    moves: [P('none', 'Nothing animates. Every animation helper returns at once while the window is not yet interactive (the mockup\'s S.instant).')],
    tokens: [P('motion-duration-instant', 'zero-length: used for snaps and startup')],
    delays: 'Motion turns on after the first interactive frame, so no animation object, layer or curve is built on the startup path.', reduced: 'Same. Reduced motion also snaps the tab-list row height.', demo: 'startup' },
];

function tokRow(key, role) {
  const k = tok(key); if (!k) return `<div class="tok"><div><b>${esc(key)}</b> <span class="chip bad">missing</span></div><div class="r">${esc(role)}</div></div>`;
  const t = k.t, v = fmt(t.value, unit(t)), r = 'reduced' in t && t.reduced !== t.value ? `reduced ${fmt(t.reduced, unit(t))}` : (k.group === 'motion-easing' && t.reduced ? 'reduced cubic-bezier(0.05, 0.7, 0.1, 1)' : '');
  const short = String(v).length > 28 ? String(v).slice(0, 26) + '...' : v;
  return `<div class="tok"><div><b>${esc(key)}</b> <span class="v" title="${esc(v)}">${esc(short)}</span></div><div class="r">${esc(role)}${r ? ' &middot; ' + esc(r) : ''}</div></div>`;
}
function fsmSvg() {
  const pk = n => num('timing', n), pd = n => num('duration', n), gx = num('distance', 'peek-grace-x'), gy = num('distance', 'peek-grace-y');
  const box = (x, y, id, rest) => `<rect class="st${rest ? ' rest' : ''}" x="${x}" y="${y}" width="124" height="40" rx="10"/><text x="${x + 62}" y="${y + 25}" text-anchor="middle">${id}</text>`;
  const e = (d, lab, lx, ly, anchor = 'middle') => `<path class="e" d="${d}" marker-end="url(#ar)"/>${lab ? `<text class="lbl" x="${lx}" y="${ly}" text-anchor="${anchor}">${lab}</text>` : ''}`;
  return `<svg class="fsm" viewBox="0 0 1040 336" role="img" aria-label="Auto-hide peek state machine">
  <defs><marker id="ar" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0L10 5L0 10z"/></marker></defs>
  ${box(30, 60, 'hidden', true)}${box(250, 60, 'arming')}${box(470, 60, 'showing')}${box(690, 60, 'shown', true)}${box(890, 60, 'pinned', true)}
  ${box(690, 230, 'leaving')}${box(380, 230, 'hiding')}
  ${e('M154 80H250', 'enter hot zone', 202, 72)}
  ${e('M374 80H470', 'dwell ' + pk('peek-delay') + 'ms', 422, 72)}
  ${e('M594 80H690', pd('peek-show') + 'ms spring', 642, 72)}
  ${e('M814 70H890', 'Ctrl+Alt+S', 852, 62)}${e('M890 90H814', '', 0, 0)}
  ${e('M312 100V150H92V100', 'leave the zone or window before the dwell ends', 330, 168)}
  ${e('M732 100V230', 'pointer away, not busy, beyond grace', 722, 190, 'end')}
  ${e('M778 230V100', 're-enter, or busy, or within grace', 790, 195, 'start')}
  ${e('M690 250H504', 'keep-open ' + pk('peek-leave') + 'ms expired', 597, 242)}
  ${e('M442 230V100', 're-enter: reverse from current offset', 436, 205, 'end')}
  ${e('M380 250H92V100', pd('peek-hide') + 'ms accelerate, then hidden', 236, 268)}
  ${e('M92 60V24H532V60', 'F6 or Ctrl+Alt+S shows it', 312, 16)}
  ${e('M952 60V24H742V60', '', 0, 0)}
  <text class="lbl" x="20" y="320">Grace: ${gx}px horizontal, ${gy}px vertical when the pointer leaves the window. Ctrl+S docks and resets to hidden.</text>
  <text class="lbl" x="20" y="300">Never hides while busy: popover, menu, drag, rename, command bar, resize, focused address field.</text>
</svg>`;
}
function cardHtml(c) {
  const used = c.tokens.map(([k]) => k), easings = [...new Set(used.filter(k => k.startsWith('motion-easing-')).map(k => k.slice(14)))];
  const curves = easings.map(n => {
    const t = T['motion-easing'][n];
    const sub = n.startsWith('spring') ? `${T['motion-spring'].stiffness.value}/${T['motion-spring'].damping.value}/${T['motion-spring'].mass.value}, ${T['motion-spring'].duration.value}ms` : t.value.replace('cubic-bezier', '');
    return plot(parseEase(t.value), { label: n, sub: sub.length > 26 ? sub.slice(0, 24) + '...' : sub });
  }).join('') + (c.plots || []).map(p => plot(p.fn(), { label: p.label, sub: p.sub, ymin: p.ymin, ymax: p.ymax, ref: p.ref })).join('');
  return `<article class="card" id="m-${c.id}">
    <header><span class="num">${String(c.n).padStart(2, '0')}</span><h3>${esc(c.title)}</h3><span class="trig">${esc(c.trigger)}</span></header>
    <div class="card-body"><div><dl>
      <dt>What moves</dt><dd>${c.moves.map(([p, t]) => `<div style="margin-bottom:4px"><span class="chip ${p === 'none' ? 'ok' : ''}">${esc(p)}</span>${esc(t)}</div>`).join('')}${c.exc ? `<div style="margin-bottom:4px"><span class="chip warn">exception</span>${esc(c.exc)}</div>` : ''}</dd>
      <dt>Tokens</dt><dd>${c.tokens.map(([k, r]) => tokRow(k, r)).join('')}</dd>
      <dt>Delays</dt><dd>${esc(c.delays)}</dd>
      <dt>Reduced</dt><dd>${esc(c.reduced)}</dd>
    </dl></div>
    <div class="stage-col"><div class="stage" data-demo="${c.demo}" id="st-${c.id}"></div>
      <div class="stage-bar"><button class="btn primary" data-play="${c.id}" type="button">Play</button><button class="btn" data-play-red="${c.id}" type="button">Play reduced</button><span class="cap" id="cap-${c.id}"></span></div>
      <div class="curves">${curves}</div></div></div>${c.fsm ? `<div style="padding:0 18px 18px"><h3 style="margin:0 0 8px">State machine</h3>${fsmSvg()}</div>` : ''}</article>`;
}

function summary() {
  const usedKeys = new Set(CARDS.flatMap(c => c.tokens.map(t => t[0])));
  const all = Specs.groups().filter(g => g.startsWith('motion-')).flatMap(g => Object.keys(T[g]).map(n => `${g}-${n}`));
  const unused = all.filter(k => !usedKeys.has(k)), noRed = [];
  for (const k of all) { const t = tok(k).t; if (t.reduced === undefined && !['motion-spring-stiffness', 'motion-spring-damping', 'motion-spring-mass'].includes(k) && /^motion-duration-/.test(k)) noRed.push(k); }
  return { unused, noRed, count: all.length };
}

function perf() {
  return `<ol class="rules">
   <li><b>Compositor layers only.</b> Animate <code>transform</code> and <code>opacity</code>. In native Views that means <code>ui::Layer</code> with a <code>ui::LayerAnimator</code> (transform and opacity elements). In WebUI, Web Animations or CSS on the same two properties. Shadows, filters, blur and colour are never animated; they switch with the state.</li>
   <li><b>No layout per frame, one exception.</b> The tab-list row height (open tab, close tab, folder reveal and collapse) is the only property that lays out during an animation. It affects one list, runs at most 200ms, and is clipped. Nothing else resizes while it plays: the sidebar width, the page card and the toolbar all change as one step (the card follows with a transform).</li>
   <li><b>Nothing animates on a startup path.</b> The first paint, session restore and the first Workspace render run with motion disabled (the mockup's <code>S.instant</code>). Animations only start from a user action after the window is interactive. No animation objects, layers or curves are built before then; the curves are constants.</li>
   <li><b>Reduced motion is honoured everywhere.</b> The OS setting (<code>prefers-reduced-motion</code>, and the platform equivalents in Views) and the in-app setting both apply. Every motion token with a <code>reduced</code> value uses it: movement is removed, state changes become opacity fades of 90 to 140ms, or an instant swap. Delays that are behaviour (hover dwell, keep-open) are unchanged.</li>
   <li><b>One owner per animation.</b> A running animation is cancelled or reversed from its current value when a new one starts on the same element (peek re-entry reverses from the current offset). No stacked timers, no leaked animators.</li>
   <li><b>Bench and test check.</b> The Bench (and a browser test) loads the sidebar with reduced motion forced, runs each interaction and asserts that no transform animation is created on any layer and that every opacity animation is 140ms or less. A second check asserts that no animated property outside <code>transform</code>, <code>opacity</code> and the tab-list row height is created in the normal mode.</li>
  </ol>`;
}

async function main() {
  T = await Specs.load();
  Specs.chrome('motion.html', `<label class="switch"><input type="checkbox" id="redToggle"> Reduced motion</label>`);
  redToggle = $('#redToggle');
  if (Specs.qs.get('reduced') === '1') redToggle.checked = true;
  const sum = summary();
  $('#cards').innerHTML = CARDS.map(cardHtml).join('');
  $('#toc').innerHTML = '<h4>Interactions</h4>' + CARDS.map(c => `<a href="#m-${c.id}">${String(c.n).padStart(2, '0')} ${esc(c.title)}</a>`).join('') + '<h4 style="margin-top:14px">More</h4><a href="#perf">Performance rules</a><a href="#open-points">Open points</a>';
  $('#perf-body').innerHTML = perf();
  const OPEN = [['sleep', 'The desaturate is a static filter that snaps on when the favicon opacity fade ends, because filters are never animated. A smoother option is a second, grey favicon layer crossfaded over the colour one. It costs one extra layer per sleeping tab, so it is only worth it if the snap looks harsh in the native build.']];
  $('#open-body').innerHTML = (OPEN.length ? `<ul>${OPEN.map(([id, t]) => `<li><a href="#m-${id}">${esc(CARDS.find(c => c.id === id).title)}</a>: ${esc(t)}</li>`).join('')}</ul>` : '<p>None.</p>') +
    `<h3>Tokens defined but not used by any card</h3><p>${sum.unused.length ? sum.unused.map(k => `<span class="chip warn">${esc(k)}</span>`).join('') : 'None. Every motion token appears on a card.'}</p>
    <h3>Duration tokens without a reduced value</h3><p>${sum.noRed.length ? sum.noRed.map(k => `<span class="chip warn">${esc(k)}</span>`).join('') : 'None. Every duration token has a reduced value. Only the spring easings need a reduced curve; the cubic-bezier easings are reused as is.'}</p>`;
  const runs = {}, running = {};
  for (const c of CARDS) { const st = $('#st-' + c.id); runs[c.id] = demos[c.demo](st); }
  const play = async (id, red) => {
    if (running[id]) return; running[id] = true; const st = $('#st-' + id), cap = $('#cap-' + id), b = $$(`[data-play="${id}"],[data-play-red="${id}"]`);
    b.forEach(x => x.disabled = true); st.replaceChildren(); runs[id] = demos[CARDS.find(c => c.id === id).demo](st);
    forceRed = !!red; const R = isRed(); cap.textContent = R ? 'reduced motion: opacity only' : 'full motion';
    try { await runs[id](); } catch (e) { console.error('demo ' + id, e); }
    forceRed = false; b.forEach(x => x.disabled = false); running[id] = false;
  };
  document.addEventListener('click', e => {
    const p = e.target.closest('[data-play]'), r = e.target.closest('[data-play-red]');
    if (p) play(p.dataset.play, false); if (r) play(r.dataset.playRed, true);
  });
  window.__motion = { play, CARDS, isRed };
  document.body.dataset.ready = '1';
}
main().catch(e => { console.error(e); });
})();

/* =====================================================================================================================
   TOKEN PAGE
   ===================================================================================================================== */
(() => {
'use strict';
if (document.body.dataset.page !== 'tokens') return;
const { $, esc, fmt, unit, plot, parseEase } = Specs;
const isColor = v => typeof v === 'string' && /^(#[0-9a-f]{3,8}|rgba?\()/i.test(v.trim());
const sw = v => isColor(v) ? `<span class="sw" style="background:${esc(v)}"></span>` : '';
const code = s => `<code>${esc(s)}</code>`;
const val = (v, u) => `${sw(v)}<span class="mono">${esc(fmt(v, u))}</span>`;
const GROUP_NOTE = {
  color: 'Frame, text and surface colours. Text never changes per Workspace.',
  tint: 'The gradient angle shared by every Workspace tint.',
  shadow: 'Elevation. Shadows are static: they switch with state, they are never animated.',
  radius: 'Corner radii.', space: 'Spacing scale in px. Padding and gaps use these steps only.', type: 'Type scale and weights. Family is system-ui so the UI matches the OS.',
  size: 'Fixed dimensions: sidebar, rows, tiles, hit areas, focus ring.',
  'motion-duration': 'Duration tokens. See the motion spec for where each is used.', 'motion-easing': 'Easing curves. Springs are CSS linear() samples of the native spring.',
  'motion-spring': 'Native spring parameters (Views LayerAnimator / gfx::SpringAnimation). The CSS linear() curves approximate them.',
  'motion-distance': 'Distances and thresholds.', 'motion-scale': 'Scales.', 'motion-opacity': 'Opacities.', 'motion-timing': 'Delays and hold times.', 'motion-ratio': 'Ratios and speeds.',
  workspace: 'The 8 Workspace colour presets: gradient stops and the accent for each mode.',
};
const hasRed = g => Object.values(g).some(t => t && typeof t === 'object' && 'reduced' in t);
function groupTable(name) {
  const g = Specs.T[name], red = hasRed(g);
  const rows = Object.entries(g).map(([k, t]) => {
    const u = unit(t), shared = 'value' in t;
    const cells = shared ? `<td colspan="2">${val(t.value, u)}</td>` : `<td>${val(t.light, u)}</td><td>${val(t.dark, u)}</td>`;
    return `<tr><td>${code('--aerie-' + name + '-' + k)}</td>${cells}${red ? `<td>${'reduced' in t ? val(t.reduced, u) : '<span class="muted">-</span>'}</td>` : ''}<td>${esc(t.description || '')}</td></tr>`;
  }).join('');
  return `<div class="tbl-wrap"><table><thead><tr><th>Name</th><th colspan="2">Value (light, dark)</th>${red ? '<th>Reduced motion</th>' : ''}<th>Description</th></tr></thead><tbody>${rows}</tbody></table></div>`;
}
const sec = (id, title, body, n) => `<section class="sec" id="${id}"><h2>${esc(title)}${n ? `<span class="n">${esc(n)}</span>` : ''}</h2>${body}</section>`;

function colorSection() {
  const g = Specs.T.color;
  const sws = Object.entries(g).map(([k, t]) => `<div style="border:1px solid var(--sp-line);border-radius:10px;overflow:hidden"><div style="display:flex;height:44px"><div style="flex:1;background:linear-gradient(${t.light},${t.light}),#fff"></div><div style="flex:1;background:linear-gradient(${t.dark},${t.dark}),#1B1A20"></div></div><div style="padding:6px 10px;font-size:12px"><b>${esc(k)}</b><div class="muted mono" style="font-size:11px">${esc(t.light)} / ${esc(t.dark)}</div></div></div>`).join('');
  return `<p class="muted">${GROUP_NOTE.color} Swatches show light (left) and dark (right).</p><div style="display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px;margin-bottom:16px">${sws}</div>${groupTable('color')}`;
}
function shadowSection() {
  const g = Specs.T.shadow;
  return `<p class="muted">${GROUP_NOTE.shadow}</p><div class="shadow-bed"><div class="shadow-grid">${Object.keys(g).map(k => `<div class="shadow-item" style="box-shadow:var(--aerie-shadow-${k})">${k}</div>`).join('')}</div></div><div style="height:14px"></div>${groupTable('shadow')}`;
}
function radiusSection() {
  const g = Specs.T.radius;
  return `<p class="muted">${GROUP_NOTE.radius} Drawn at 72px so the curve is readable; the value is the real radius.</p><div class="radius-grid">${Object.entries(g).map(([k, t]) => `<div class="radius-item"><div style="border-radius:${t.value}${unit(t)}"></div><b style="color:var(--sp-text)">${k}</b><br>${t.value}${unit(t)}</div>`).join('')}</div><div style="height:14px"></div>${groupTable('radius')}`;
}
function spaceSection() {
  const g = Specs.T.space;
  return `<p class="muted">${GROUP_NOTE.space} Bars are drawn at 1:1 px.</p>${Object.entries(g).map(([k, t]) => `<div class="scale-row"><span>${code('space-' + k)}</span><span class="mono">${t.value}${unit(t)}</span><div class="bar" style="width:${Math.max(1, t.value)}px"></div></div>`).join('')}<div style="height:14px"></div>${groupTable('space')}`;
}
function typeSection() {
  const g = Specs.T.type, steps = ['caption', 'label', 'body', 'title', 'heading'];
  const sample = { caption: 'Caption: Asleep, frees memory', label: 'Label: New workspace', body: 'Body: Design doc, privacy first browser', title: 'Title: Personal workspace', heading: 'Heading: Sidebar and Workspaces' };
  const spec = steps.map(s => `<div class="type-spec"><span class="meta">${s}</span><span style="font-size:${g['size-' + s].value}px;line-height:${g['line-' + s].value}px;font-weight:${s === 'heading' || s === 'title' ? g['weight-semibold'].value : g['weight-regular'].value}">${esc(sample[s])}</span><span class="meta">${g['size-' + s].value}px / ${g['line-' + s].value}px</span></div>`).join('');
  const w = ['regular', 'medium', 'semibold'].map(k => `<div class="type-spec"><span class="meta">weight-${k}</span><span style="font-size:15px;font-weight:${g['weight-' + k].value}">Aerie sidebar, Workspaces and tabs</span><span class="meta">${g['weight-' + k].value}</span></div>`).join('');
  return `<p class="muted">${GROUP_NOTE.type} Family: ${code(g.family.value)}</p><div style="margin-bottom:12px">${spec}${w}</div>${groupTable('type')}`;
}
function sizeSection() {
  const g = Specs.T.size, bars = ['sidebar-width', 'sidebar-min', 'sidebar-max', 'rail-width', 'row-height-comfortable', 'row-height-compact', 'tile-comfortable', 'tile-compact', 'rail-tile', 'hot-zone', 'indent'];
  return `<p class="muted">${GROUP_NOTE.size}</p>${bars.filter(k => g[k]).map(k => `<div class="scale-row" style="grid-template-columns:200px 70px minmax(0,1fr)"><span>${code('size-' + k)}</span><span class="mono">${g[k].value}${unit(g[k])}</span><div class="bar" style="width:${g[k].value}px;height:${k.includes('height') || k.includes('tile') ? Math.min(g[k].value, 44) : 10}px"></div></div>`).join('')}<div style="height:14px"></div>${groupTable('size')}`;
}
function motionSection() {
  const ms = Specs.groups().filter(g => g.startsWith('motion-'));
  const ez = Specs.T['motion-easing'], curves = `<div class="curves" style="margin-bottom:14px">${Object.entries(ez).map(([k, t]) => plot(parseEase(t.value), { label: k, sub: k.startsWith('spring') ? 'linear() sample' : t.value.replace('cubic-bezier', '') })).join('')}</div>`;
  return `<p class="muted">Where each token is used, and how it plays, is on the <a href="motion.html?mode=${Specs.root.dataset.mode}">motion spec</a>. Every duration and easing carries a reduced-motion value.</p>${curves}` +
    ms.map(g => `<h3>${g}</h3><p class="muted">${GROUP_NOTE[g] || ''}</p>${groupTable(g)}`).join('');
}
function presetSection() {
  const g = Specs.T.workspace, names = [...new Set(Object.keys(g).map(k => k.split('-')[0]))];
  const chips = names.map(p => {
    const stops = [1, 2, 3].filter(i => g[`${p}-stop-${i}`]).map(i => g[`${p}-stop-${i}`].value);
    return `<div class="preset"><div class="g" style="background:linear-gradient(${Specs.T.tint.angle.value}deg,${stops.join(',')})"></div><div class="m"><b>${p}</b>${stops.map(s => `<div>${sw(s)}${s}</div>`).join('')}<div>${sw(g[p + '-accent'].light)}light accent ${g[p + '-accent'].light}</div><div>${sw(g[p + '-accent'].dark)}dark accent ${g[p + '-accent'].dark}</div></div></div>`;
  }).join('');
  return `<p class="muted">${GROUP_NOTE.workspace} Gradients drawn at the shared tint angle (${Specs.T.tint.angle.value}deg). The accent is the last stop with its lightness adjusted for 3:1 or better on the frame.</p><div class="preset-grid">${chips}</div><h3>All workspace tokens</h3>${groupTable('workspace')}`;
}
function themeSection() {
  const th = Specs.T.theme, ids = Object.keys(th), keys = [];
  ids.forEach(id => Object.keys(th[id]).forEach(k => { if (!k.startsWith('_') && !keys.includes(k)) keys.push(k); }));
  const cell = (t, id, k) => {
    if (!t) return '<span class="muted">-</span>';
    const u = unit(t); if ('value' in t) return `<div class="pair"><span>${sw(t.value)}${esc(fmt(t.value, u))}</span></div>`;
    const modes = th[id]._modes;
    return `<div class="pair">${['light', 'dark'].map(m => modes.includes(m) ? `<span><span class="muted">${m[0].toUpperCase()}</span> ${sw(t[m])}${esc(fmt(t[m], u))}</span>` : `<span class="muted">${m[0].toUpperCase()} falls back to Clean</span>`).join('')}</div>`;
  };
  const head = ids.map(id => `<th style="min-width:130px">${id.replace('-', ' ')}<div style="text-transform:none;letter-spacing:0;font-weight:400">${th[id]._modes.join(' and ')}</div></th>`).join('');
  const desc = `<tr class="sub"><td class="k">description</td>${ids.map(id => `<td style="font-weight:400">${esc(th[id]._description)}</td>`).join('')}</tr>`;
  const row = k => `<tr><td class="k">${esc(k)}</td>${ids.map(id => `<td>${cell(th[id][k], id, k)}</td>`).join('')}</tr>`;
  const own = keys.filter(k => !k.includes('.')), over = keys.filter(k => k.includes('.'));
  const body = own.map(row).join('') + (over.length ? `<tr class="sub"><td colspan="${ids.length + 1}">Overrides of global tokens while the theme is active ("-" means the global value is used)</td></tr>` + over.map(row).join('') : '');
  const desc2 = keys.map(k => { const d = ids.map(id => th[id][k] && th[id][k].description).find(Boolean); return d ? `<li>${code(k)} ${esc(d)}</li>` : ''; }).join('');
  return `<p class="muted">Theme is how the chrome is drawn; Workspace colour is the hue (separate axes, see <code>_meta.axes</code> in tokens.json). Each Workspace can override the theme and mode. Values are shown light (L) and dark (D). Keys written <code>color.x</code> override that global token while the theme is active.</p>
  <div class="tbl-wrap"><table class="theme-table"><thead><tr><th>Token</th>${head}</tr></thead><tbody>${desc}${body}</tbody></table></div>
  <details style="margin-top:12px"><summary class="muted" style="cursor:pointer">What each theme token means</summary><ul>${desc2}</ul></details>`;
}
function contrastSection() {
  const { rows, presets } = Specs.contrastMatrix();
  let total = 0, fails = 0;
  const f = n => n.toFixed(2);
  const body = rows.map(r => `<tr><td class="rl">${r.theme.replace('-', ' ')} <span class="muted">${r.mode}</span><small class="muted" style="display:block;font-weight:400">need ${r.need} / ${r.need} / 3</small></td>${presets.map(p => {
    const c = r.cells[p]; total++; if (!c.ok) fails++;
    const lo = (v, need) => `<span class="${v < need ? 'low' : ''}">${f(v)}</span>`;
    return `<td class="cell ${c.ok ? 'ok' : 'bad'}">${lo(c.text, r.need)}<br>${lo(c.muted, r.need)}<br>${lo(c.accent, 3)}<small>${c.ok ? 'pass' : 'FAIL'}</small></td>`;
  }).join('')}</tr>`).join('');
  return `<p class="muted">Worst-case contrast ratio over the sidebar column on both sides of a 1440 by 900 window. Each cell lists text, muted text and accent, top to bottom. Text and muted need 4.5 (7 in High contrast); accent and focus ring need 3. Computed in this page from tokens.json.</p>
  <div class="note"><p><b>${total - fails} of ${total} theme, mode and preset combinations pass.</b> This is a JavaScript port of <code>check_contrast.py</code> and uses the same maths: frame base, the Workspace gradient at the theme tint strength, black shade, white radial glow, WCAG relative luminance, the same 462 sample points. If the two ever disagree, <code>check_contrast.py</code> is the source and this port is wrong.</p></div>
  <div class="tbl-wrap" style="margin-top:12px"><table class="matrix"><thead><tr><th>Theme and mode</th>${presets.map(p => `<th>${p}</th>`).join('')}</tr></thead><tbody>${body}</tbody></table></div>`;
}
function pipelineSection() {
  return `<div class="pipeline">
    <div class="pipe-box src"><b>tokens.json</b><small>Single source of truth. Edited by hand, reviewed in the design issue.</small></div><span class="pipe-arrow">&rarr;</span>
    <div class="pipe-box"><b>gen_tokens_css.py</b><small>Emits <code>tokens.css</code> for WebUI (exists). Plain groups become <code>--aerie-&lt;group&gt;-&lt;token&gt;</code>; <code>theme</code> becomes per-theme, per-mode blocks; <code>reduced</code> becomes the reduced-motion override.</small></div>
    <div class="pipe-box planned"><b>C++ generator (planned)</b><small>Emits <code>constexpr</code> constants for native Views, for example <code>kMotionDurationSlow = 280ms</code>, springs as <code>SpringParams</code>, colours as <code>SkColor</code> light/dark pairs. Nothing is hand-copied into C++.</small></div></div>
  <div class="note"><p>Never edit <code>tokens.css</code> by hand and never type a value that has a token. A new value is added to <code>tokens.json</code> with a description first. <code>check_contrast.py</code> gates every theme, mode and preset on each change. Tokens carry either <code>value</code> or <code>light</code> and <code>dark</code>, plus an optional <code>reduced</code>.</p></div>`;
}

async function main() {
  const T = await Specs.load();
  Specs.chrome('tokens.html');
  const S = [
    ['pipeline', 'Token pipeline', pipelineSection()],
    ['color', 'Colour', colorSection(), 'color'], ['shadow', 'Shadow', shadowSection(), 'shadow'], ['radius', 'Radius', radiusSection(), 'radius'], ['space', 'Spacing', spaceSection(), 'space'], ['type', 'Type', typeSection(), 'type'], ['size', 'Size', sizeSection(), 'size'], ['tint', 'Tint', `<p class="muted">${GROUP_NOTE.tint}</p>${groupTable('tint')}`, 'tint'],
    ['motion', 'Motion', motionSection(), 'motion-*'],
    ['presets', 'Workspace presets', presetSection(), 'workspace'],
    ['themes', 'Theme catalogue', themeSection(), 'theme'],
    ['contrast', 'Contrast matrix', contrastSection()],
  ];
  $('#toc').innerHTML = '<h4>Tokens</h4>' + S.map(s => `<a href="#${s[0]}">${esc(s[1])}</a>`).join('');
  $('#sections').innerHTML = S.map(s => sec(s[0], s[1], s[2], s[3])).join('');
  const count = Specs.groups().reduce((n, g) => n + (g === 'theme' ? Object.values(T.theme).reduce((a, t) => a + Object.keys(t).filter(k => !k.startsWith('_')).length, 0) : Object.keys(T[g]).length), 0);
  $('#count').textContent = `${count} tokens in ${Specs.groups().length} groups, ${Object.keys(T.theme).length} themes, 8 Workspace presets.`;
  document.body.dataset.ready = '1';
}
main().catch(e => console.error(e));
})();
