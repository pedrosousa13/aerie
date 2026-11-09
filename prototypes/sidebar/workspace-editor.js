/* Aerie Workspace editor mockup (issue #142). Plain JS; the real editor is a Lit WebUI sheet.
   Theme values are read from tokens.css at runtime (probe()), never copied here.
   The contrast model is a port of check_contrast.py: it rebuilds the frame the way sidebar.css paints it
   (workspace gradient at the theme's tint strength, black shade, white glow), samples it where text sits,
   and requires text and muted >= 4.5:1 (7:1 in High contrast) and accent >= 3:1. */
'use strict';
const $ = (s, r = document) => r.querySelector(s);
const root = document.documentElement;
const params = new URLSearchParams(location.search);

/* ---------- data ---------- */
const THEMES = [['vivid', 'Vivid', 'Bold colour'], ['soft', 'Soft', 'Gentle tint'], ['clean', 'Clean', 'Quiet, colour strip'],
  ['oled', 'OLED', 'True black'], ['high-contrast', 'High contrast', '7:1 text'], ['mono', 'Mono', 'No colour in chrome']];
const THEME_IDS = THEMES.map(t => t[0]);
const THEME_NAME = Object.fromEntries(THEMES.map(t => [t[0], t[1]]));
const PRESETS = ['dusk', 'tide', 'moss', 'ember', 'lilac', 'slate', 'sand', 'mint'];
const cap = s => s[0].toUpperCase() + s.slice(1);
const HARMONY = [
  ['analogous', 'Analogous', [-30, 0, 30]], ['complementary', 'Complementary', [0, 180]], ['triad', 'Triad', [0, 120, 240]],
  ['split', 'Split', [0, 150, 210]], ['single', 'Single', [0]]];
const WORKSPACES = {   /* mirrors the sample data in sidebar.js */
  personal: { name: 'Personal', icon: 'home', colour: 'dusk', theme: 'default', mode: 'default', tabs: 14 },
  work: { name: 'Work', icon: 'briefcase', colour: 'tide', theme: 'clean', mode: 'default', tabs: 22 },
  side: { name: 'Side project', icon: 'spark', colour: 'moss', theme: 'oled', mode: 'dark', tabs: 9 },
};
/* 24 simple line icons on a 24px grid. Drawn for Aerie; no emoji, no brand marks. */
const ICONS = {
  home: 'M3 11l9-7 9 7v9a1 1 0 0 1-1 1h-5v-6H9v6H4a1 1 0 0 1-1-1z',
  briefcase: 'M4 7h16a1 1 0 0 1 1 1v11a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V8a1 1 0 0 1 1-1z M9 7V5a1 1 0 0 1 1-1h4a1 1 0 0 1 1 1v2 M3 13h18',
  spark: 'M12 3l2.2 6.3L21 12l-6.8 2.7L12 21l-2.2-6.3L3 12l6.8-2.7z',
  book: 'M6 3h13v15H6a2 2 0 0 0-2 2V5a2 2 0 0 1 2-2z M4 20a2 2 0 0 0 2 2h13',
  code: 'M8 7l-5 5 5 5 M16 7l5 5-5 5 M14 4l-4 16',
  heart: 'M12 20s-8-5-8-11a4.5 4.5 0 0 1 8-2.8A4.5 4.5 0 0 1 20 9c0 6-8 11-8 11z',
  star: 'M12 3l2.8 5.8 6.2.9-4.5 4.4 1.1 6.2L12 17.3l-5.6 3 1.1-6.2L3 9.7l6.2-.9z',
  bolt: 'M13 3L5 14h6l-1 7 8-11h-6z',
  leaf: 'M5 19c0-9 5-14 15-14 0 10-5 15-14 15 M5 19l8-8',
  camera: 'M4 7h3l1.5-3h7L17 7h3a1 1 0 0 1 1 1v11a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V8a1 1 0 0 1 1-1z M12 10a3.5 3.5 0 1 0 0 7 3.5 3.5 0 0 0 0-7z',
  music: 'M9 18V5l11-2v13 M9 18a3 3 0 1 1-6 0 3 3 0 0 1 6 0z M20 16a3 3 0 1 1-6 0 3 3 0 0 1 6 0z',
  gamepad: 'M7 7h10a5 5 0 0 1 0 10H7A5 5 0 0 1 7 7z M7.5 12h4 M9.5 10v4 M15.5 11.5v.01 M17.5 13.5v.01',
  bag: 'M5 8h14l-1 12H6z M9 8V6a3 3 0 0 1 6 0v2',
  globe: 'M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18z M3 12h18 M12 3c3 3 3 15 0 18 M12 3c-3 3-3 15 0 18',
  cloud: 'M7 18a4 4 0 0 1-.5-8A6 6 0 0 1 18 9.5 4.3 4.3 0 0 1 17.5 18z',
  moon: 'M20 14.5A8 8 0 0 1 9.5 4 8 8 0 1 0 20 14.5z',
  sun: 'M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8z M12 2v3 M12 19v3 M2 12h3 M19 12h3 M5 5l2 2 M17 17l2 2 M19 5l-2 2 M7 17l-2 2',
  flask: 'M9 3h6 M10 3v6l-5 10a1 1 0 0 0 .9 1.5h12.2A1 1 0 0 0 19 19l-5-10V3 M7.5 14h9',
  palette: 'M12 3a9 9 0 1 0 0 18c1.5 0 2-1 1.5-2s0-2 1.5-2h2a3 3 0 0 0 3-3c0-6-4-11-8-11z M8 11v.01 M12 7.5v.01 M16 11v.01',
  plane: 'M21 4L10 14 M21 4l-6 17-4-7-7-4z',
  coffee: 'M4 8h13v6a5 5 0 0 1-5 5H9a5 5 0 0 1-5-5z M17 10h2a2 2 0 0 1 0 4h-2 M8 3v2 M12 3v2',
  wrench: 'M15 4a4.5 4.5 0 0 0-4 6l-7 7 3 3 7-7a4.5 4.5 0 0 0 6-4l-3 3-3-1-1-3z',
  chat: 'M4 5h16v11H9l-5 4z',
  folder: 'M3 6a1 1 0 0 1 1-1h5l2 2h9a1 1 0 0 1 1 1v10a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1z',
};
const ICON_NAMES = Object.keys(ICONS);
const iconSvg = n => `<svg viewBox="0 0 24 24" aria-hidden="true"><path d="${ICONS[n]}"/></svg>`;

/* ---------- colour maths ---------- */
const hex2rgb = h => { h = h.trim().replace('#', ''); return [0, 2, 4].map(i => parseInt(h.slice(i, i + 2), 16)); };
const rgb2hex = c => '#' + c.map(v => Math.round(Math.min(255, Math.max(0, v))).toString(16).padStart(2, '0')).join('').toUpperCase();
const lum = c => { const f = v => (v /= 255) <= .03928 ? v / 12.92 : ((v + .055) / 1.055) ** 2.4; const [r, g, b] = c.map(f); return .2126 * r + .7152 * g + .0722 * b; };
const ratio = (a, b) => { const [x, y] = [lum(a), lum(b)].sort((p, q) => q - p); return (x + .05) / (y + .05); };
const mix = (a, b, t) => a.map((v, i) => v * t + b[i] * (1 - t));   /* t = share of a */
function hsl2rgb(h, s, l) {   /* h deg, s and l 0..1 */
  h = ((h % 360) + 360) % 360; const k = n => (n + h / 30) % 12, a = s * Math.min(l, 1 - l);
  const f = n => l - a * Math.max(-1, Math.min(k(n) - 3, Math.min(9 - k(n), 1)));
  return [f(0) * 255, f(8) * 255, f(4) * 255];
}
function rgb2hsl([r, g, b]) {
  r /= 255; g /= 255; b /= 255; const mx = Math.max(r, g, b), mn = Math.min(r, g, b), l = (mx + mn) / 2, d = mx - mn;
  if (!d) return [0, 0, l];
  const s = d / (1 - Math.abs(2 * l - 1)); let h = mx === r ? ((g - b) / d) % 6 : mx === g ? (b - r) / d + 2 : (r - g) / d + 4;
  return [(h * 60 + 360) % 360, s, l];
}

/* ---------- tokens: read for any theme x mode without touching the page's own look ---------- */
const TOK = {
  frame: '--aerie-color-frame-base', surface: '--aerie-color-content-surface', popover: '--aerie-color-surface-popover',
  sel: '--aerie-color-row-selected', pageBlock: '--aerie-color-page-block',
  tint: '--aerie-theme-tint-strength', text: '--aerie-theme-text', muted: '--aerie-theme-text-muted', shade: '--aerie-theme-shade', glow: '--aerie-theme-glow',
  share: '--aerie-theme-accent-hue-share', base: '--aerie-theme-accent-base', strip: '--aerie-theme-strip-height', card: '--aerie-theme-card-border',
  ring: '--aerie-theme-selected-ring', ringColor: '--aerie-theme-selected-ring-color',
};
const probeCache = {};
function probe(theme, mode) {
  const key = theme + '/' + mode; if (probeCache[key]) return probeCache[key];
  const pt = root.getAttribute('data-theme'), pm = root.getAttribute('data-mode');
  root.setAttribute('data-theme', theme); root.setAttribute('data-mode', mode);
  const cs = getComputedStyle(root), g = n => cs.getPropertyValue(n).trim(), out = {};
  for (const k in TOK) out[k] = g(TOK[k]);
  out.accents = Object.fromEntries(PRESETS.map(p => [p, g(`--aerie-workspace-${p}-accent`)]));
  pt === null ? root.removeAttribute('data-theme') : root.setAttribute('data-theme', pt);
  pm === null ? root.removeAttribute('data-mode') : root.setAttribute('data-mode', pm);
  out.hc = theme === 'high-contrast'; out.tintN = parseFloat(out.tint); out.shadeN = parseFloat(out.shade); out.glowN = parseFloat(out.glow);
  out.shareN = parseFloat(out.share) / 100; out.stripN = parseFloat(out.strip);
  return (probeCache[key] = out);
}
const rootVar = n => getComputedStyle(root).getPropertyValue(n).trim();
const presetStops = p => [1, 2, 3].map(i => rootVar(`--aerie-workspace-${p}-stop-${i}`)).filter(Boolean).map(hex2rgb);
const ANGLE = parseFloat(rootVar('--aerie-tint-angle')) * Math.PI / 180;

/* ---------- contrast model (port of check_contrast.py) ---------- */
const W = 1440, H = 900, SIDEBAR = 260;
const SAMPLES = []; for (let y = 0; y <= H; y += 45) { for (let x = 0; x <= SIDEBAR; x += 26) SAMPLES.push([x, y]); for (let x = W - SIDEBAR; x <= W; x += 26) SAMPLES.push([x, y]); }
function gradientAt(stops, x, y) {
  const L = Math.abs(W * Math.sin(ANGLE)) + Math.abs(H * Math.cos(ANGLE));
  const t = Math.min(1, Math.max(0, ((x - W / 2) * Math.sin(ANGLE) - (y - H / 2) * Math.cos(ANGLE)) / L + .5));
  if (stops.length === 1) return stops[0];
  const s = t * (stops.length - 1), i = Math.min(Math.floor(s), stops.length - 2);
  return mix(stops[i + 1], stops[i], s - i);
}
const glowAlpha = (x, y) => Math.max(0, 1 - Math.hypot(x / (1.2 * W), y / (.8 * H)) / .6);
function worst(stops, P, tint, accent) {
  const base = hex2rgb(P.frame), text = hex2rgb(P.text), muted = hex2rgb(P.muted);
  let t = 99, m = 99, a = 99;
  for (const [x, y] of SAMPLES) {
    let c = mix(gradientAt(stops, x, y), base, tint);
    c = mix([0, 0, 0], c, P.shadeN); c = mix([255, 255, 255], c, P.glowN * glowAlpha(x, y));
    t = Math.min(t, ratio(text, c)); m = Math.min(m, ratio(muted, c)); a = Math.min(a, ratio(accent, c));
  }
  return { text: t, muted: m, accent: a };
}
const accentFrom = (hue, P) => mix(hue, hex2rgb(P.base), P.shareN);

/* ---------- state ---------- */
const GLOBAL = { theme: 'vivid', mode: 'light' };
const pick = (v, list, d) => list.includes(v) ? v : d;
function initGlobal() {
  const pt = params.get('theme'), pm = params.get('mode') || (['light', 'dark'].includes(pt) ? pt : null);
  GLOBAL.theme = pick(pt === 'hc' ? 'high-contrast' : pt, THEME_IDS, 'vivid');
  GLOBAL.mode = pm === 'dark' ? 'dark' : pm === 'light' ? 'light' : (matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light');
}
const st = { key: 'personal', name: '', icon: 'home', colour: 'dusk', H: 330, sats: [.8, .8, .8], harmony: 'triad', light: 60, intensity: 50, grain: 0, theme: 'default', mode: 'default' };
let lastSaved = null, model = null;

function loadWorkspace(key) {
  const w = WORKSPACES[key] || WORKSPACES.personal; st.key = WORKSPACES[key] ? key : 'personal';
  Object.assign(st, { name: w.name, icon: w.icon, colour: w.colour, theme: w.theme, mode: w.mode, light: 60, intensity: 50, grain: 0, harmony: 'triad', sats: [.8, .8, .8] });
  const q = params;
  if (q.get('wstheme')) st.theme = pick(q.get('wstheme'), ['default', ...THEME_IDS], st.theme);
  if (q.get('wsmode')) st.mode = pick(q.get('wsmode'), ['default', 'system', 'light', 'dark'], st.mode);
  if (PRESETS.includes(q.get('colour'))) st.colour = q.get('colour');
  if (q.get('tab') === 'custom') seedCustom(); else if (q.get('tab') === 'preset' && st.colour === 'custom') st.colour = 'dusk';
  const h = pick(q.get('harmony'), HARMONY.map(x => x[0]), null); if (h) st.harmony = h;
  if (q.get('hue') !== null && q.get('hue') !== '') st.H = +q.get('hue');
  if (q.get('sat') !== null && q.get('sat') !== '') st.sats = [+q.get('sat'), +q.get('sat'), +q.get('sat')];
  if (q.get('lightness')) st.light = +q.get('lightness');
  if (q.get('intensity')) st.intensity = +q.get('intensity');
  if (q.get('grain')) st.grain = +q.get('grain');
  lastSaved = JSON.stringify(st);
}
function seedCustom() {
  if (st.colour !== 'custom') {
    const [h, s] = rgb2hsl(presetStops(st.colour)[0]); st.H = Math.round(h); st.sats = [s, s, s];
  }
  st.colour = 'custom';
}
const offs = () => HARMONY.find(h => h[0] === st.harmony)[2];
const dotHue = i => st.H + offs()[i];
const primaryIdx = () => offs().indexOf(0);

/* ---------- effective theme and mode ---------- */
const effTheme = () => st.theme === 'default' ? GLOBAL.theme : st.theme;
function effMode(theme) {
  if (theme === 'oled') return 'dark';
  const m = st.mode === 'default' ? GLOBAL.mode : st.mode;
  return m === 'system' ? (matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light') : m;
}

/* ---------- compute the rendered frame, with auto-adjust for custom colours ---------- */
function compute() {
  const theme = effTheme(), mode = effMode(theme), P = probe(theme, mode), need = theme === 'high-contrast' ? 7 : 4.5;
  const custom = st.colour === 'custom';
  const k = custom ? .5 + st.intensity / 100 : 1;
  const tint = Math.min(1, P.tintN * k);
  let hsl, stops, accHue;
  if (custom) {
    hsl = offs().map((_, i) => [dotHue(i), st.sats[i], st.light / 100]);
    stops = hsl.map(([h, s, l]) => hsl2rgb(h, s, l));
    accHue = null;
  } else { stops = presetStops(st.colour); accHue = hex2rgb(P.accents[st.colour]); }
  const text = hex2rgb(P.text), dir = lum(text) > .4 ? -1 : 1;   /* light text: darken the colour; dark text: lighten it */
  const accentFor = (L) => { const [h, s] = rgb2hsl(stops[Math.max(0, primaryIdx())] || stops[0]); return accentFrom(hsl2rgb(h, Math.min(.9, Math.max(.45, s)), L), P); };
  const base0 = mode === 'dark' ? .62 : .5;
  const first = worst(stops, P, tint, custom ? accentFor(base0) : accentFrom(accHue, P));
  const beforeMin = Math.min(first.text, first.muted);
  let shift = 0, outStops = stops, res = first;
  if (custom && Math.min(res.text, res.muted) < need) {
    for (shift = 1; shift <= 90; shift++) {
      outStops = hsl.map(([h, s, l]) => hsl2rgb(h, s, Math.min(.97, Math.max(.03, l + dir * shift / 100))));
      res = worst(outStops, P, tint, accentFor(base0));
      if (Math.min(res.text, res.muted) >= need) break;
    }
  }
  let accent, accShift = 0;
  if (custom) {
    const fdir = lum(hex2rgb(P.frame)) > .4 && P.tintN * k < .5 ? -1 : (mode === 'dark' ? 1 : -1);
    let L = base0; accent = accentFor(L);
    for (let i = 0; i < 60 && worst(outStops, P, tint, accent).accent < 3.2; i++) { L = Math.min(.95, Math.max(.05, L + fdir * .02)); accent = accentFor(L); accShift = Math.abs(L - base0); }
    if (worst(outStops, P, tint, accent).accent < 3.2) { /* try the other way */
      L = base0; for (let i = 0; i < 60 && worst(outStops, P, tint, accentFor(L)).accent < 3.2; i++) L = Math.min(.95, Math.max(.05, L - fdir * .02));
      accent = accentFor(L); accShift = Math.abs(L - base0);
    }
  } else accent = accentFrom(accHue, P);
  const fin = worst(outStops, P, tint, accent);
  return { theme, mode, P, need, custom, tint, stops: outStops, rawStops: stops, accent, res: fin, beforeMin, shift, accShift,
    adjusted: custom && (shift >= 1 || accShift > .1), min: Math.min(fin.text, fin.muted) };
}

/* ---------- applying the model ---------- */
const css = (el, vars) => { for (const k in vars) el.style.setProperty(k, vars[k]); };
const gradCss = stops => stops.length === 1 ? `linear-gradient(${rgb2hex(stops[0])},${rgb2hex(stops[0])})` : `linear-gradient(${ANGLE * 180 / Math.PI}deg,${stops.map(rgb2hex).join(',')})`;
const stripCss = (P, stops, accent) => P.hc ? rgb2hex(accent) : stops.length === 1 ? rgb2hex(stops[0]) : `linear-gradient(90deg,${stops.map(rgb2hex).join(',')})`;
function previewVars(P, stops, tint, accent, extra = {}) {
  const ring = parseFloat(P.ring) ? `inset 0 0 0 ${P.ring} ${P.ringColor}` : 'none';
  return Object.assign({
    '--p-base': P.frame, '--p-grad': gradCss(stops), '--p-tint': tint, '--p-glow': P.glowN, '--p-shade': P.shadeN, '--p-text': P.text, '--p-muted': P.muted,
    '--p-surface': P.surface, '--p-sel': P.sel, '--p-accent': rgb2hex(accent), '--p-on-accent': lum(accent) > .45 ? '#111' : '#fff', '--p-page-block': P.pageBlock,
    '--p-strip-h': P.stripN + 'px', '--p-strip': stripCss(P, stops, accent), '--p-card-border': P.card, '--p-ring': ring,
  }, extra);
}
const chipIcon = ok => ok ? '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M5 12.5l4.5 4.5L19 7.5"/></svg>'
  : '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 4l9 16H3z M12 10v4 M12 17.5v.01"/></svg>';

function fitOnSurface(c, surf, min, towards) {   /* nudge colour toward text until it reaches `min`:1 on surf */
  let out = c; for (let t = 0; t <= 1 && ratio(out, surf) < min; t += .05) out = mix(towards, c, t); return out;
}

function render(opts = {}) {
  model = compute(); const m = model;
  const pv = $('#pv');
  css(pv, previewVars(m.P, m.stops, m.tint, m.accent, { '--p-grain': (m.theme === 'oled' || m.theme === 'high-contrast') ? 0 : st.grain / 100 * parseFloat(rootVar('--aerie-tint-grain-max')) }));
  $('#pvIcon').innerHTML = iconSvg(st.icon); $('#pvName').textContent = st.name || 'Untitled';
  const cname = st.colour === 'custom' ? 'custom colour' : cap(st.colour);
  pv.setAttribute('aria-label', `Preview of ${st.name || 'Untitled'}: ${cname}, ${THEME_NAME[m.theme]} theme, ${m.mode} mode`);
  $('#pvCap').textContent = `Preview: ${THEME_NAME[m.theme]}, ${m.mode}`;
  $('#shSub').textContent = WORKSPACES[st.key].name !== st.name && st.name ? `was ${WORKSPACES[st.key].name}` : '';

  /* sheet accents follow the colour being edited, nudged so rings keep 3:1 and the Save label keeps 4.5:1 */
  const sheetBg = hex2rgb(probe(GLOBAL.theme, GLOBAL.mode === 'dark' || GLOBAL.theme === 'oled' ? 'dark' : 'light').popover);
  const ink = hex2rgb(probe(GLOBAL.theme, GLOBAL.mode === 'dark' || GLOBAL.theme === 'oled' ? 'dark' : 'light').text);
  const ring = fitOnSurface(m.accent, sheetBg, 3, ink);
  const btn = fitOnSurface(m.accent, [255, 255, 255], 4.5, [0, 0, 0]);
  css($('#sheet'), { '--sheet-accent': rgb2hex(ring), '--sheet-btn': rgb2hex(btn), '--sheet-btn-text': '#fff',
    '--sheet-accent-fill': `color-mix(in srgb,${rgb2hex(ring)} 14%,transparent)` });

  renderChip(); renderSwatches(); renderThemes(); renderModes(); renderCustom(opts);
  updateSave();
}

function renderChip() {
  const m = model, chip = $('#chip'), ex = $('#explain'), r = m.min.toFixed(1), lvl = m.need >= 7 ? 'AAA' : 'AA';
  if (m.min < m.need - .005) {   /* only reachable at the clamp limits */
    chip.className = 'chip warn'; chip.innerHTML = `${chipIcon(false)}Contrast ${r}:1, below ${lvl}`;
    ex.hidden = false; ex.textContent = 'This colour cannot reach readable text in this theme, even after adjusting. Pick a lighter or darker colour.';
  } else if (m.adjusted) {
    chip.className = 'chip warn'; chip.innerHTML = `${chipIcon(false)}Adjusted for contrast ${r}:1`;
    ex.hidden = false;
    const how = m.shift >= 1 ? `${lum(hex2rgb(m.P.text)) > .4 ? 'darkened' : 'lightened'} the colours by ${m.shift}%` : '';
    ex.textContent = `Text would have been ${m.beforeMin.toFixed(1)}:1 on this gradient, below the ${lvl} minimum of ${m.need}:1. Aerie ${how}${how && m.accShift > .1 ? ' and ' : ''}${m.accShift > .1 ? 'tuned the accent' : ''} so it stays readable. Your dots are unchanged.`;
  } else {
    chip.className = 'chip'; chip.innerHTML = `${chipIcon(true)}Contrast ${lvl} ✓ ${r}:1`;
    ex.hidden = true;
  }
  chip.title = `Text ${m.res.text.toFixed(1)}:1, muted text ${m.res.muted.toFixed(1)}:1, accent ${m.res.accent.toFixed(1)}:1 (need ${m.need}:1, ${m.need}:1, 3:1)`;
}

/* ---------- roving radio groups ---------- */
function radioGroup(container, items, { cols = 0, onSelect }) {
  const move = (from, delta) => { const n = items().length; return (from + delta + n) % n; };
  container.addEventListener('keydown', e => {
    const list = items(); const i = list.indexOf(document.activeElement); if (i < 0) return;
    let to = -1;
    if (e.key === 'ArrowRight' || (!cols && e.key === 'ArrowDown')) to = move(i, 1);
    else if (e.key === 'ArrowLeft' || (!cols && e.key === 'ArrowUp')) to = move(i, -1);
    else if (cols && e.key === 'ArrowDown') to = Math.min(list.length - 1, i + cols);
    else if (cols && e.key === 'ArrowUp') to = Math.max(0, i - cols);
    else if (e.key === 'Home') to = 0; else if (e.key === 'End') to = list.length - 1;
    if (to < 0) return;
    e.preventDefault(); let t = to, guard = 0;
    while (list[t].getAttribute('aria-disabled') === 'true' && guard++ < list.length) t = move(t, to >= i ? 1 : -1);
    list[t].focus(); if (list[t].getAttribute('aria-disabled') !== 'true') onSelect(list[t]);
  });
  container.addEventListener('click', e => { const b = e.target.closest('[role=radio]'); if (b && b.getAttribute('aria-disabled') !== 'true') onSelect(b); });
}
function setChecked(list, pred) { let any = false; list.forEach(b => { const on = pred(b); b.setAttribute('aria-checked', on); b.tabIndex = on ? 0 : -1; any = any || on; }); if (!any && list[0]) list[0].tabIndex = 0; }

/* ---------- icons ---------- */
function buildIcons() {
  const c = $('#icons');
  ICON_NAMES.forEach(n => { const b = document.createElement('button'); b.type = 'button'; b.className = 'ico'; b.setAttribute('role', 'radio'); b.dataset.v = n; b.setAttribute('aria-label', cap(n)); b.title = cap(n); b.innerHTML = iconSvg(n); c.appendChild(b); });
  radioGroup(c, () => [...c.children], { cols: 8, onSelect: b => { st.icon = b.dataset.v; syncIcons(); render(); } });
}
const syncIcons = () => setChecked([...$('#icons').children], b => b.dataset.v === st.icon);

/* ---------- swatches ---------- */
function buildSwatches() {
  const c = $('#swatches');
  [...PRESETS, 'custom'].forEach(p => {
    const b = document.createElement('button'); b.type = 'button'; b.className = 'sw'; b.setAttribute('role', 'radio'); b.dataset.v = p; b.setAttribute('aria-label', cap(p) + (p === 'custom' ? ' colour' : ' colour preset'));
    b.innerHTML = `<span class="dot${p === 'custom' ? ' custom' : ''}"></span><span aria-hidden="true">${cap(p)}</span>`;
    if (p !== 'custom') b.firstChild.style.background = gradCss(presetStops(p));
    c.appendChild(b);
  });
  radioGroup(c, () => [...c.children], { onSelect: b => { b.dataset.v === 'custom' ? seedCustom() : (st.colour = b.dataset.v); render({ redraw: true }); } });
}
function renderSwatches() { const list = [...$('#swatches').children]; setChecked(list, b => b.dataset.v === st.colour); const cb = list[list.length - 1].firstChild; if (st.colour === 'custom') cb.style.background = gradCss(model.rawStops); else cb.style.background = ''; }

/* ---------- themes and mode ---------- */
function themeCard(id, label, sub) {
  const b = document.createElement('button'); b.type = 'button'; b.setAttribute('role', 'radio'); b.dataset.v = id; b.className = 'th' + (id === 'default' ? ' default' : '');
  if (id === 'default') { b.innerHTML = `<span class="radio"></span><span><b></b><small>Follows your global default theme</small></span>`; }
  else b.innerHTML = `<div class="tp"><i class="a"></i><i class="b"></i><i class="c"></i><i class="s"></i><div class="sb"><i></i><i class="sel"></i><i></i></div><div class="pg"></div></div>
    <div class="th-top"><b>${label}</b><span class="th-note"></span></div><small>${sub}</small>`;
  return b;
}
function buildThemes() {
  const c = $('#themes'); c.appendChild(themeCard('default'));
  THEMES.forEach(t => c.appendChild(themeCard(...t)));
  radioGroup(c, () => [...c.children], { cols: 3, onSelect: b => { st.theme = b.dataset.v; render(); } });
}
function renderThemes() {
  const list = [...$('#themes').children], m = model;
  list[0].querySelector('b').textContent = `Use default (${THEME_NAME[GLOBAL.theme]})`;
  list.slice(1).forEach(b => {
    const id = b.dataset.v, mode = effMode(id), P = probe(id, mode), custom = st.colour === 'custom';
    const tint = Math.min(1, P.tintN * (custom ? .5 + st.intensity / 100 : 1));
    css($('.tp', b), previewVars(P, m.rawStops, tint, m.accent));
    $('.th-note', b).textContent = id === 'oled' ? 'Dark only' : '';
  });
  setChecked(list, b => b.dataset.v === st.theme);
}
function buildModes() {
  const c = $('#modes');
  [['default', 'Default'], ['system', 'System'], ['light', 'Light'], ['dark', 'Dark']].forEach(([v, l]) => {
    const b = document.createElement('button'); b.type = 'button'; b.setAttribute('role', 'radio'); b.dataset.v = v; b.textContent = l; c.appendChild(b);
  });
  radioGroup(c, () => [...c.children], { onSelect: b => { st.mode = b.dataset.v; render(); } });
}
function renderModes() {
  const list = [...$('#modes').children], m = model, forced = m.theme === 'oled';
  list.forEach(b => b.setAttribute('aria-disabled', forced && b.dataset.v !== 'dark' ? 'true' : 'false'));
  setChecked(list, b => forced ? b.dataset.v === 'dark' : b.dataset.v === st.mode);
  $('#modeNote').textContent = forced ? 'OLED is dark only, so this Workspace uses Dark. Pick another theme to choose a mode again.'
    : st.mode === 'default' ? `Follows your global default (${cap(GLOBAL.mode)}).` : st.mode === 'system' ? 'Follows your system setting.' : `Always ${st.mode} in this Workspace.`;
}

/* ---------- custom colour: wheel, harmony, sliders ---------- */
const R = 84;
function drawWheel() {
  const cv = $('#wheelCv'), dpr = window.devicePixelRatio || 1, n = Math.round(168 * dpr);
  cv.width = cv.height = n; const ctx = cv.getContext('2d'), img = ctx.createImageData(n, n), r = n / 2;
  for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) {
    const dx = x + .5 - r, dy = y + .5 - r, d = Math.hypot(dx, dy), o = (y * n + x) * 4;
    if (d > r) continue;
    const h = (Math.atan2(dy, dx) * 180 / Math.PI + 360) % 360, c = hsl2rgb(h, Math.min(1, d / r), st.light / 100);
    img.data[o] = c[0]; img.data[o + 1] = c[1]; img.data[o + 2] = c[2]; img.data[o + 3] = d > r - 1 ? 255 * (r - d) : 255;
  }
  ctx.putImageData(img, 0, 0);
}
function buildHarmony() {
  const c = $('#harmony');
  HARMONY.forEach(([id, label]) => { const b = document.createElement('button'); b.type = 'button'; b.className = 'hchip'; b.setAttribute('role', 'radio'); b.dataset.v = id; b.textContent = label; c.appendChild(b); });
  radioGroup(c, () => [...c.children], { onSelect: b => { const s = st.sats[primaryIdx()]; st.harmony = b.dataset.v; st.sats = [s, s, s]; render({ dots: true }); } });
}
function renderDots() {
  const w = $('#wheel'); const n = offs().length;
  while (w.querySelectorAll('.wdot').length > n) w.querySelector('.wdot:last-of-type').remove();
  while (w.querySelectorAll('.wdot').length < n) {
    const d = document.createElement('div'); d.className = 'wdot'; d.tabIndex = 0; d.setAttribute('role', 'slider'); d.dataset.i = w.querySelectorAll('.wdot').length;
    w.appendChild(d);
  }
  w.querySelectorAll('.wdot').forEach((d, i) => {
    const h = dotHue(i), a = h * Math.PI / 180, s = st.sats[i];
    d.style.left = (R + Math.cos(a) * s * R) + 'px'; d.style.top = (R + Math.sin(a) * s * R) + 'px';
    d.style.background = rgb2hex(hsl2rgb(h, s, st.light / 100));
    d.classList.toggle('primary', i === primaryIdx());
    const hh = Math.round(((h % 360) + 360) % 360);
    d.setAttribute('aria-label', `Colour dot ${i + 1} of ${n}${i === primaryIdx() ? ', primary' : ''}`);
    d.setAttribute('aria-valuemin', 0); d.setAttribute('aria-valuemax', 360); d.setAttribute('aria-valuenow', hh);
    d.setAttribute('aria-valuetext', `hue ${hh} degrees, saturation ${Math.round(s * 100)} percent. Arrow keys change hue and saturation`);
  });
}
function renderCustom(opts = {}) {
  const custom = st.colour === 'custom', box = $('#custom'); box.hidden = !custom;
  if (!custom) return;
  setChecked([...$('#harmony').children], b => b.dataset.v === st.harmony);
  const m = model, noGrain = m.theme === 'oled' || m.theme === 'high-contrast';
  $('#rLight').value = st.light; $('#oLight').textContent = st.light + '%';
  $('#rInt').value = st.intensity; $('#oInt').textContent = st.intensity + '%';
  const g = $('#rGrain'); g.disabled = noGrain; g.value = noGrain ? 0 : st.grain; $('#oGrain').textContent = noGrain ? 'Off' : st.grain + '%';
  $('#grainNote').hidden = !noGrain;
  const flat = m.P.tintN === 0; const tn = $('#tintNote'); tn.hidden = !flat;
  tn.textContent = flat ? `${THEME_NAME[m.theme]} does not tint the frame, so your colour shows in the accent and the strip. Intensity has no effect.` : '';
  if (opts.redraw !== false) { if (!drawWheel.last || drawWheel.last !== st.light) { drawWheel(); drawWheel.last = st.light; } }
  renderDots();
}
function wheelPoint(e) {
  const r = $('#wheelCv').getBoundingClientRect(), dx = e.clientX - r.left - R, dy = e.clientY - r.top - R;
  return { h: (Math.atan2(dy, dx) * 180 / Math.PI + 360) % 360, s: Math.min(1, Math.hypot(dx, dy) / R) };
}
function setDot(i, h, s) { st.H = h - offs()[i]; st.sats[i] = s; st.H = ((st.H % 360) + 360) % 360; }
function buildWheel() {
  const w = $('#wheel'); let drag = -1;
  w.addEventListener('pointerdown', e => {
    const d = e.target.closest('.wdot'); const p = wheelPoint(e);
    drag = d ? +d.dataset.i : primaryIdx();
    w.setPointerCapture(e.pointerId); if (d) d.focus();
    if (!d) { setDot(drag, p.h, p.s); render({ redraw: false }); }
    e.preventDefault();
  });
  w.addEventListener('pointermove', e => { if (drag < 0) return; const p = wheelPoint(e); setDot(drag, p.h, p.s); render({ redraw: false }); });
  const end = () => { drag = -1; }; w.addEventListener('pointerup', end); w.addEventListener('pointercancel', end);
  w.addEventListener('keydown', e => {
    const d = e.target.closest('.wdot'); if (!d) return; const i = +d.dataset.i, big = e.shiftKey, hs = big ? 15 : 3, ss = big ? .15 : .05;
    let h = dotHue(i), s = st.sats[i];
    if (e.key === 'ArrowRight') h += hs; else if (e.key === 'ArrowLeft') h -= hs;
    else if (e.key === 'ArrowUp') s = Math.min(1, s + ss); else if (e.key === 'ArrowDown') s = Math.max(0, s - ss);
    else return;
    e.preventDefault(); setDot(i, h, s); render({ redraw: false }); w.querySelector(`.wdot[data-i="${i}"]`).focus();
  });
  $('#rLight').addEventListener('input', e => { st.light = +e.target.value; render(); });
  $('#rInt').addEventListener('input', e => { st.intensity = +e.target.value; render({ redraw: false }); });
  $('#rGrain').addEventListener('input', e => { st.grain = +e.target.value; render({ redraw: false }); });
}

/* ---------- name, save, cancel, delete ---------- */
function updateSave() {
  const empty = !st.name.trim(); $('#btnSave').disabled = empty; $('#wsName').setAttribute('aria-invalid', empty ? 'true' : 'false'); $('#nameErr').hidden = !empty;
}
const sheet = $('#sheet');
function closeSheet(msg) {
  sheet.hidden = true; $('#scrim').hidden = true; $('#confirm').hidden = true; $('#shFoot').hidden = false;
  $('#reopen').hidden = false; $('#reopenMsg').textContent = msg; $('#reopenBtn').focus();
}
function openSheet() {
  $('#reopen').hidden = true; sheet.hidden = false; $('#scrim').hidden = false; $('#wsName').focus();
}
function openConfirm() {
  const w = st.name.trim() || WORKSPACES[st.key].name;
  $('#cfTitle').textContent = `Delete "${w}"?`;
  $('#cfText').textContent = `Its ${WORKSPACES[st.key].tabs} tabs, cookies and logins are removed. This can't be undone.`;
  $('#confirm').hidden = false; $('#shFoot').hidden = true; $('#cfKeep').focus();
  $('#confirm').scrollIntoView({ block: 'nearest' });
}
function closeConfirm() { $('#confirm').hidden = true; $('#shFoot').hidden = false; $('#btnDelete').focus(); }

/* Focus trap: Tab and Shift+Tab cycle inside the sheet. Esc cancels (or closes the confirm first). */
function focusables() {
  return [...sheet.querySelectorAll('button,input,[tabindex="0"]')].filter(el => !el.disabled && !el.closest('[hidden]') && !el.closest('fieldset:disabled') && el.offsetParent !== null && el.tabIndex >= 0);
}
document.addEventListener('keydown', e => {
  if (sheet.hidden) return;
  if (e.key === 'Escape') { e.preventDefault(); $('#confirm').hidden ? closeSheet('Cancelled. Nothing was saved.') : closeConfirm(); return; }
  if (e.key !== 'Tab') return;
  const list = focusables(); if (!list.length) return;
  const first = list[0], last = list[list.length - 1], a = document.activeElement;
  if (!sheet.contains(a)) { e.preventDefault(); first.focus(); }
  else if (e.shiftKey && a === first) { e.preventDefault(); last.focus(); }
  else if (!e.shiftKey && a === last) { e.preventDefault(); first.focus(); }
});

/* ---------- mockup controls ---------- */
/* Pages row: links to the other mockup pages, keeping the colour mode. Relative links, so they also work inside a multi-file Artifact. */
function pagesRow(body, here, getMode) {
  const PAGES = [['index.html', 'Sidebar'], ['horizontal.html', 'Horizontal'], ['workspace-editor.html', 'Workspace editor'], ['motion.html', 'Motion spec'], ['tokens.html', 'Token spec']];
  const r = document.createElement('div'); r.className = 'mrow pages'; const l = document.createElement('span'); l.textContent = 'Pages'; r.appendChild(l);
  const w = document.createElement('span'); w.className = 'plinks';
  PAGES.forEach(([h, n]) => { const a = document.createElement('a'); a.textContent = n; a.href = h + '?mode=' + getMode(); if (h === here) a.setAttribute('aria-current', 'page'); a.addEventListener('click', () => { a.href = h + '?mode=' + getMode(); }); w.appendChild(a); });
  r.appendChild(w); body.appendChild(r);
}
function buildControls() {
  const mock = $('#mock'); mock.textContent = '';
  const h = document.createElement('div'); h.className = 'mh'; h.innerHTML = '<span>Mockup controls</span><span id="mockToggle">[-]</span>'; mock.appendChild(h);
  const body = document.createElement('div'); body.className = 'mbody'; mock.appendChild(body); pagesRow(body, 'workspace-editor.html', () => GLOBAL.mode);
  const sel = (id, label, opts, val, fn) => {
    const r = document.createElement('label'); r.className = 'mrow'; r.textContent = label; const s = document.createElement('select'); s.id = id;
    opts.forEach(o => { const op = document.createElement('option'); op.value = o[0]; op.textContent = o[1]; s.appendChild(op); }); s.value = val;
    s.addEventListener('change', () => fn(s.value)); r.appendChild(s); body.appendChild(r);
  };
  sel('m-theme', 'theme (global)', THEMES.map(t => [t[0], t[1]]), GLOBAL.theme, v => { GLOBAL.theme = v; applyGlobal(); });
  sel('m-mode', 'mode (global)', [['light', 'light'], ['dark', 'dark']], GLOBAL.mode, v => { GLOBAL.mode = v; applyGlobal(); });
  sel('m-ws', 'workspace', Object.entries(WORKSPACES).map(([k, w]) => [k, w.name]), st.key, v => { params.delete('tab'); loadWorkspace(v); syncAll(); });
  const sm = document.createElement('small'); sm.textContent = 'Not part of Aerie. The sheet and the frame behind it use the global theme and mode; the preview uses this Workspace\'s.'; body.appendChild(sm);
  h.addEventListener('click', () => { mock.classList.toggle('collapsed'); $('#mockToggle').textContent = mock.classList.contains('collapsed') ? '[+]' : '[-]'; });
}
function applyGlobal() {
  root.setAttribute('data-theme', GLOBAL.theme);
  root.setAttribute('data-mode', GLOBAL.theme === 'oled' ? 'dark' : GLOBAL.mode);   /* OLED is dark only */
  render({ redraw: false });
}

function syncAll() {
  $('#wsName').value = st.name; syncIcons(); drawWheel.last = null; render(); $('#shTitle').textContent = 'Edit Workspace';
}

/* ---------- init ---------- */
function init() {
  initGlobal(); root.setAttribute('data-theme', GLOBAL.theme); root.setAttribute('data-mode', GLOBAL.theme === 'oled' ? 'dark' : GLOBAL.mode);
  if (params.get('reduced') === '1') root.setAttribute('data-reduced-motion', '');
  loadWorkspace(params.get('ws'));
  buildIcons(); buildSwatches(); buildThemes(); buildModes(); buildHarmony(); buildWheel();
  $('#wsName').addEventListener('input', e => { st.name = e.target.value; render({ redraw: false }); });
  $('#btnCancel').addEventListener('click', () => closeSheet('Cancelled. Nothing was saved.'));
  $('#btnSave').addEventListener('click', () => closeSheet(`Saved "${st.name.trim()}". (Mockup: nothing is stored.)`));
  $('#btnDelete').addEventListener('click', openConfirm);
  $('#cfKeep').addEventListener('click', closeConfirm);
  $('#cfDelete').addEventListener('click', () => closeSheet(`Deleted "${st.name.trim() || WORKSPACES[st.key].name}". (Mockup: nothing is stored.)`));
  $('#reopenBtn').addEventListener('click', openSheet);
  if (params.get('controls') !== '0') buildControls(); else $('#mock').hidden = true;
  syncAll();
  if (params.get('confirm') === '1') openConfirm(); else $('#wsName').focus({ preventScroll: true });
  window.__editor = { st, get model() { return model; }, render };   /* test hook */
}
init();
