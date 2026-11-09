/* Aerie horizontal tabs mockup (spec story 51). Plain JS. Durations, easings, distances and scales come from tokens.css. */
(() => {
'use strict';
const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];
const root = document.documentElement;
const params = new URLSearchParams(location.search);
const win = $('#window'), panes = $('#panes'), tbFavs = $('#tbFavs'), chip = $('#chip'), card = $('#card'), urlIn = $('#url');

/* ---------- tokens ---------- */
const tok = n => getComputedStyle(root).getPropertyValue('--aerie-' + n).trim();
const tnum = n => parseFloat(tok(n));
const isReduced = () => root.hasAttribute('data-reduced-motion') || matchMedia('(prefers-reduced-motion: reduce)').matches;
const durMs = n => n === 'spring' ? tnum('motion-spring-duration') : tnum('motion-duration-' + n);
const ease = n => tok('motion-easing-' + n);
/* Web Animations helper: no-op while S.instant (first paint). Returns the finished promise. */
function anim(node, frames, dur, easing, o = {}) {
  if (S.instant) return Promise.resolve();
  const a = node.animate(frames, { duration: durMs(dur), easing: ease(easing), fill: o.fill || 'none', delay: o.delay || 0 });
  return a.finished.then(() => a, () => a);
}

/* ---------- state ---------- */
const S = { ws: 'personal', gTheme: 'vivid', gMode: 'light', explicitTheme: false, density: 'comfortable', instant: true, tintOn: 'A', switching: false, drag: false, wsOverrides: true, rows: 2 };
const PRESET_STOPS = { dusk: 3, tide: 2, moss: 2, ember: 2, lilac: 2, slate: 2, sand: 2, mint: 2 };
const WS_ORDER = ['personal', 'work', 'side'];
const WS = {
  personal: { name: 'Personal', preset: 'dusk', icon: 'i-ws-ring', theme: 'default', mode: 'default' },
  work: { name: 'Work', preset: 'tide', icon: 'i-ws-case', theme: 'clean', mode: 'default' },
  side: { name: 'Side project', preset: 'moss', icon: 'i-ws-spark', theme: 'oled', mode: 'dark' },
};
const THEMES = [['vivid', 'Vivid'], ['soft', 'Soft'], ['clean', 'Clean'], ['oled', 'OLED'], ['high-contrast', 'High contrast'], ['mono', 'Mono']];
const THEME_IDS = THEMES.map(t => t[0]);
const THEME_ALIAS = { neutral: 'clean', hc: 'high-contrast', highcontrast: 'high-contrast' };
const normTheme = v => { v = THEME_ALIAS[v] || v; return THEME_IDS.includes(v) ? v : null; };
const contrastMore = matchMedia('(prefers-contrast: more)');
const systemDark = () => matchMedia('(prefers-color-scheme: dark)').matches;

/* Effective look = Workspace override ?? global default. A theme or mode picked in the URL/controls beats Workspace overrides.
   OLED is dark only and falls back to Clean in light mode. prefers-contrast: more selects High contrast unless a theme was chosen. */
function effectiveLook(wsId) {
  const w = WS[wsId];
  const useOv = S.wsOverrides && !S.explicitTheme;
  const picked = (contrastMore.matches && !S.explicitTheme) ? 'high-contrast' : (useOv && w.theme !== 'default' ? w.theme : S.gTheme);
  const wm = useOv ? w.mode : 'default';
  const mode = wm === 'system' ? (systemDark() ? 'dark' : 'light') : (wm === 'light' || wm === 'dark') ? wm : S.gMode;
  const fell = picked === 'oled' && mode === 'light';
  return { theme: fell ? 'clean' : picked, mode };
}
function applyLook(wsId = S.ws) {
  const e = effectiveLook(wsId);
  root.dataset.theme = e.theme; root.dataset.mode = e.mode;
  const b = tok('theme-card-border');
  if (b && !b.startsWith('0px')) win.dataset.bordered = ''; else delete win.dataset.bordered;
  return e;
}
/* Whole-chrome crossfade (View Transitions) when the effective theme or mode changes. */
function withCrossfade(wsId, fn) {
  const next = effectiveLook(wsId);
  if (S.instant || !document.startViewTransition || (next.theme === root.dataset.theme && next.mode === root.dataset.mode)) { fn(); return; }
  root.style.setProperty('--vt-dur', durMs('xslow') + 'ms');
  document.startViewTransition(fn);
}

/* ---------- data (same Personal / Work / Side shape as sidebar.js) ---------- */
const tabs = {};
const hash = s => [...s].reduce((a, c) => (a * 31 + c.charCodeAt(0)) >>> 0, 7);
function mk(id, title, domain, path, color, x = {}) {
  tabs[id] = { id, title, domain, path, color, pinnedPath: null, asleep: false, audio: false, blocked: 6 + hash(id) % 47, ...x };
  return id;
}
const T = (id, title, domain, color, x) => ({ t: mk(id, title, domain, x && x.path || '/', color, x) });
const F = (id, name, open, kids) => ({ f: id, name, open, kids });
const W = {
  personal: {
    favorites: [
      mk('p-mail', 'Mail', 'mail.fjord.app', '/inbox', '#4F7CFF'), mk('p-cal', 'Calendar', 'cal.fjord.app', '/week', '#E5484D'),
      mk('p-maps', 'Maps', 'maps.atlas.io', '/', '#30A46C'), mk('p-music', 'Music', 'play.reverb.fm', '/', '#8E4EC6', { audio: true }),
      mk('p-notes', 'Notes', 'notes.pebble.app', '/', '#F5A524'), mk('p-photos', 'Photos', 'photos.fjord.app', '/', '#0091FF'),
      mk('p-bank', 'Bank', 'bank.harbour.eu', '/accounts', '#12A594'), mk('p-news', 'News', 'daily.ledger.news', '/', '#5B6B7F'),
    ],
    pinned: [
      T('p-inbox', 'Re: Weekend plans', 'mail.fjord.app', '#4F7CFF', { path: '/thread/48213', pinnedPath: '/inbox' }),
      F('f-read', 'Reading', true, [
        T('p-essay', 'The quiet web, revisited', 'essays.slowread.net', '#8E6A4C'),
        T('p-digest', 'Weekly digest', 'digest.ledger.news', '#5B6B7F'),
        F('f-recipes', 'Recipes', true, [
          T('p-bread', 'Overnight sourdough, step by step', 'bread.crumb.recipes', '#F76B15'),
          T('p-pasta', 'Weeknight pasta, no fuss', 'noodle.crumb.recipes', '#E5484D'),
        ]),
      ]),
      T('p-home', 'Home dashboard', 'hearth.local', '#12A594', { path: '/' }),
    ],
    today: [
      T('p-trail', 'Trail map: Serra de Sintra', 'trails.atlas.io', '#30A46C', { path: '/sintra', asleep: true }),
      T('p-flights', 'Flights to Lisbon, 12 Dec', 'fly.skyward.travel', '#0091FF', { path: '/results' }),
      T('p-lofi', 'lofi study beats, 24/7', 'play.reverb.fm', '#8E4EC6', { audio: true, path: '/live' }),
      T('p-plant', 'How to repot a monstera', 'leafy.garden.guide', '#30A46C'),
      T('p-bike', 'Winter tyres compared', 'spoke.cycling.review', '#F76B15'),
    ],
    selected: 'p-flights',
  },
  work: {
    favorites: [
      mk('w-cal', 'Calendar', 'cal.northwind.work', '/', '#E5484D'), mk('w-docs', 'Docs', 'docs.northwind.work', '/', '#4F7CFF'),
      mk('w-mail', 'Mail', 'mail.northwind.work', '/', '#12A594'), mk('w-chat', 'Chat', 'chat.northwind.work', '/', '#8E4EC6'),
      mk('w-board', 'Board', 'board.northwind.work', '/', '#F5A524'), mk('w-drive', 'Drive', 'drive.northwind.work', '/', '#5B6B7F'),
    ],
    pinned: [
      T('w-sprint', 'Sprint 42 board', 'board.northwind.work', '#F5A524', { path: '/s/42' }),
      F('f-design', 'Design', true, [
        T('w-ds', 'Design system: tokens', 'ds.northwind.work', '#8E4EC6'),
        T('w-icons', 'Icon library', 'icons.northwind.work', '#0091FF'),
        F('f-handoff', 'Handoff', true, [
          T('w-checkout', 'Checkout redesign', 'figs.northwind.work', '#E5484D'),
          T('w-onb', 'Spec: onboarding v3', 'docs.northwind.work', '#4F7CFF', { path: '/onb-v3' }),
        ]),
      ]),
      T('w-road', 'Roadmap Q4', 'docs.northwind.work', '#30A46C', { path: '/roadmap' }),
    ],
    today: [
      T('w-pr', 'PR #2841: retry budget for sync', 'code.northwind.work', '#5B6B7F', { path: '/pr/2841' }),
      T('w-inc', 'Incident review, 28 Oct', 'meet.northwind.work', '#E5484D', { audio: true, path: '/r/inc-28' }),
      F('f-q4', 'Q4 research', true, [
        T('w-comp', 'Competitor teardown', 'docs.northwind.work', '#F76B15', { path: '/teardown' }),
        T('w-int', 'Interview notes: participant 7', 'docs.northwind.work', '#12A594', { path: '/p7' }),
      ]),
      T('w-hire', 'Hiring pipeline', 'people.northwind.work', '#8E4EC6', { asleep: true }),
      T('w-stand', 'Standup notes', 'docs.northwind.work', '#4F7CFF', { path: '/standup' }),
    ],
    selected: 'w-pr',
  },
  side: {
    favorites: [
      mk('s-repo', 'Repo', 'git.forge.dev', '/trellis', '#5B6B7F'), mk('s-dom', 'Domains', 'names.registry.io', '/', '#F76B15'),
      mk('s-an', 'Analytics', 'stats.plainly.io', '/', '#12A594'), mk('s-docs', 'Docs', 'docs.trellis.dev', '/', '#4F7CFF'),
    ],
    pinned: [
      T('s-main', 'trellis / main', 'git.forge.dev', '#5B6B7F', { path: '/trellis' }),
      T('s-deploy', 'Deploy dashboard', 'deploy.edgeline.dev', '#30A46C', { path: '/trellis' }),
    ],
    today: [
      F('f-ref', 'Reference', false, [
        T('s-rust', 'Rust borrow checker notes', 'notes.rustaceans.dev', '#F76B15'),
        T('s-wal', 'SQLite WAL mode explained', 'sqlite.guide', '#0091FF', { asleep: true }),
      ]),
      T('s-pal', 'Palette generator', 'tints.toolbox.dev', '#D6409F'),
      T('s-88', 'Issue #88: flaky test on CI', 'git.forge.dev', '#5B6B7F', { path: '/trellis/issues/88' }),
      T('s-wal2', 'Write-ahead logging, a primer', 'blog.deepcuts.dev', '#8E4EC6'),
    ],
    selected: 's-main',
  },
};
/* extra Today tabs for the crowded state and the "open tab" control */
const EXTRA = [
  ['Rust async book', 'rust-lang.github.io', '#F76B15'], ['MDN: Web Animations API', 'developer.mozilla.org', '#0091FF'],
  ['Tide tables, Cascais', 'tides.atlas.io', '#12A594'], ['Bread flour, 5 kg', 'shop.millhouse.eu', '#F5A524'],
  ['Train times to Porto', 'rail.comboios.pt', '#4F7CFF'], ['Sourdough starter FAQ', 'bread.crumb.recipes', '#F76B15'],
  ['Kernel mailing list', 'lore.kernel.example', '#5B6B7F'], ['Cat photos, weekly', 'whiskers.photos', '#D6409F'],
  ['Visa checklist', 'consulate.gov.example', '#5B6B7F'], ['Chart.js cookbook', 'charts.toolbox.dev', '#8E4EC6'],
  ['Keyboard review: Frost 65', 'switch.cycling.review', '#E5484D'], ['Local weather, 10 days', 'sky.forecast.app', '#0091FF'],
];
let extraN = 0;
function newTabData(title, domain, color) {
  const id = 'n' + (++extraN);
  const [t, d, c] = title ? [title, domain, color] : EXTRA[(extraN - 1) % EXTRA.length];
  return { t: mk(id, t, d, '/', c) };
}
const cur = () => W[S.ws];
const lists = w => [W[w].pinned, W[w].today];

/* ---------- tree helpers ---------- */
function* walk(list, parent = null, area = null) {
  for (let i = 0; i < list.length; i++) {
    const n = list[i];
    yield { n, list, i, parent, area };
    if (n.f) yield* walk(n.kids, n, area);
  }
}
function locate(id, w = S.ws) {
  for (const [area, list] of [['pinned', W[w].pinned], ['today', W[w].today]])
    for (const r of walk(list, null, area)) if ((r.n.t || r.n.f) === id) return { ...r, area };
  const i = W[w].favorites.indexOf(id);
  return i >= 0 ? { n: { t: id }, list: W[w].favorites, i, area: 'favs', parent: null } : null;
}
function folderHasSel(f, sel) { return f.kids.some(k => k.t ? k.t === sel : folderHasSel(k, sel)); }
const firstFolder = (w = S.ws) => { for (const r of walk(W[w].pinned)) if (r.n.f) return r.n; return null; };

/* ---------- DOM helpers ---------- */
function el(tag, cls, attrs) {
  const e = document.createElement(tag); if (cls) e.className = cls;
  if (attrs) for (const k in attrs) e.setAttribute(k, attrs[k]);
  return e;
}
function icon(id, cls) { const s = document.createElementNS('http://www.w3.org/2000/svg', 'svg'); s.setAttribute('class', 'ic' + (cls ? ' ' + cls : '')); const u = document.createElementNS('http://www.w3.org/2000/svg', 'use'); u.setAttribute('href', '#' + id); s.appendChild(u); return s; }
function favEl(t, badge = true) {
  const w = el('span', 'fav-wrap'), f = el('span', 'fav'); f.style.setProperty('--c', t.color); f.textContent = t.title.replace(/[^A-Za-z0-9]/g, '').charAt(0).toUpperCase(); w.appendChild(f);
  if (badge) {
    if (t.asleep) { const b = el('span', 'badge moon'); b.appendChild(icon('i-moon')); w.appendChild(b); }
    else if (t.audio) { const b = el('span', 'badge audio'); b.appendChild(icon('i-speaker')); w.appendChild(b); }
  }
  return w;
}

/* ---------- tab elements ---------- */
const AREAS = { favs: 'tile', pinned: 'pinned', today: 'today' };
function tabEl(n, area) {
  const isF = !!n.f, id = isF ? n.f : n.t, t = isF ? null : tabs[id];
  const kind = AREAS[area];
  const e = el('div', 'tab ' + kind + (isF ? ' folder' : ''), { role: 'tab', tabindex: -1, 'aria-selected': 'false' });
  e.dataset.id = id; e.dataset.area = area;
  e.appendChild(el('i', 'bg'));
  if (kind === 'tile') e.appendChild(el('i', 'tl'));
  if (isF) {
    e.setAttribute('aria-haspopup', 'menu'); e.setAttribute('aria-expanded', 'false');
    e.setAttribute('aria-label', n.name + ' folder'); e.title = n.name;
    const w = el('span', 'fav-wrap fico'); w.appendChild(icon('i-folder')); e.appendChild(w);
    const ti = el('span', 'title'); ti.textContent = n.name; e.appendChild(ti);
    e.appendChild(icon('i-chevron-down', 'cv'));
  } else {
    e.setAttribute('aria-label', t.title + (t.asleep ? ', asleep' : '') + (t.audio ? ', playing audio' : '')); e.title = t.title;
    if (t.asleep) e.classList.add('asleep');
    e.appendChild(favEl(t));
    if (kind !== 'tile') {
      const ti = el('span', 'title'); ti.textContent = t.title; e.appendChild(ti);
      const c = el('button', 'close', { tabindex: -1, 'aria-label': 'Close ' + t.title }); c.dataset.act = 'close'; c.appendChild(icon('i-x')); e.appendChild(c);
    }
  }
  return e;
}
function buildPane(w) {
  const p = el('div', 'pane'); p.dataset.ws = w;
  const two = S.rows === 2;
  const tl = el('div', 'tablist', { role: 'tablist', 'aria-label': WS[w].name + ' tabs', 'aria-orientation': 'horizontal' });
  tl.style.display = 'contents';
  const gF = el('div', 'grp favs', { role: 'presentation' }), gP = el('div', 'grp pinned', { role: 'presentation' }), gT = el('div', 'grp today', { role: 'presentation' });
  W[w].favorites.forEach(id => gF.appendChild(tabEl({ t: id }, 'favs')));
  W[w].pinned.forEach(n => gP.appendChild(tabEl(n, 'pinned')));
  W[w].today.forEach(n => gT.appendChild(tabEl(n, 'today')));
  if (!two && gF.children.length) tl.appendChild(gF);
  if (gP.children.length) tl.appendChild(gP);
  if ((!two && gF.children.length) || gP.children.length) tl.appendChild(el('div', 'divider', { 'aria-hidden': 'true' }));
  /* Today tabs live in a scroller (fade edges + chevrons when even icon-only tabs do not fit) */
  const wrap = el('div', 'today-wrap'); wrap.dataset.n = gT.children.length; wrap.style.setProperty('--n', gT.children.length);
  const scL = el('button', 'sc l', { tabindex: -1, 'aria-hidden': 'true' }); scL.appendChild(icon('i-chevron', 'flip'));
  const scR = el('button', 'sc r', { tabindex: -1, 'aria-hidden': 'true' }); scR.appendChild(icon('i-chevron'));
  wrap.append(gT, scL, scR); tl.appendChild(wrap);
  scL.addEventListener('click', () => gT.scrollBy({ left: -gT.clientWidth * 0.6, behavior: isReduced() ? 'auto' : 'smooth' }));
  scR.addEventListener('click', () => gT.scrollBy({ left: gT.clientWidth * 0.6, behavior: isReduced() ? 'auto' : 'smooth' }));
  gT.addEventListener('scroll', () => updateOverflow(p), { passive: true });
  wrap.addEventListener('wheel', e => { if (gT.scrollWidth > gT.clientWidth + 1) { gT.scrollLeft += Math.abs(e.deltaX) > Math.abs(e.deltaY) ? e.deltaX : e.deltaY; e.preventDefault(); } }, { passive: false });
  p.appendChild(tl);
  const plus = el('button', 'ibtn plus', { 'aria-label': 'New tab', title: 'New tab (Ctrl+T)' }); plus.appendChild(icon('i-plus')); p.appendChild(plus);
  if (two) { const fp = el('div', 'pane favpane'); fp.dataset.ws = w; const tl2 = el('div', 'tablist', { role: 'tablist', 'aria-label': WS[w].name + ' Favorites', 'aria-orientation': 'horizontal' }); tl2.style.display = 'contents'; tl2.appendChild(gF); fp.appendChild(tl2); p._favs = fp; }
  return p;
}
/* mounts a pane (and, in two-row mode, its Favorites pane in the toolbar) */
function mountPane(p) {
  panes.appendChild(p);
  if (p._favs) { tbFavs.appendChild(p._favs); const n = p._favs.querySelectorAll('.tab').length, t = parseFloat(getComputedStyle(win).getPropertyValue('--h-tile')); tbFavs.style.width = n ? (n * (t + 2) - 2) + 'px' : '0px'; }
}
function dropPane(p) { if (p._favs) p._favs.remove(); p.remove(); }
const plusBtn = () => $('.pane:not(.leaving) .plus', panes);
const basisPx = n => { const cs = getComputedStyle(win); return n * (parseFloat(cs.getPropertyValue('--h-tab-w')) + 2) + 2 * parseFloat(cs.getPropertyValue('--h-flare')) - 2; };
/* Today count drives the group's basis; animating that basis slides "+" together with the tab that opens or closes */
function setN(g, n, animate) {
  const wrap = g.parentElement, old = +wrap.dataset.n; wrap.dataset.n = n; wrap.style.setProperty('--n', n);
  if (animate && !S.instant && !isReduced() && old !== n) wrap.animate([{ flexBasis: basisPx(old) + 'px' }, { flexBasis: basisPx(n) + 'px' }], { duration: durMs('base'), easing: ease(n > old ? 'decelerate' : 'standard') });
}
function updateOverflow(p = curPane()) {
  if (!p) return; const g = $('.grp.today', p); if (!g) return; const wrap = g.parentElement;
  const ovf = g.scrollWidth > g.clientWidth + 1, l = g.scrollLeft > 1, r = g.scrollLeft + g.clientWidth < g.scrollWidth - 1;
  g.classList.toggle('ovf', ovf); wrap.dataset.l = ovf && l ? '1' : '0'; wrap.dataset.r = ovf && r ? '1' : '0';
  g.style.setProperty('--fl', ovf && l ? 'var(--h-fade)' : '0px'); g.style.setProperty('--fr', ovf && r ? 'var(--h-fade)' : '0px');
}
function ensureVisible(n, smooth = true) {
  const g = n && n.closest('.grp.today'); if (!g || g.scrollWidth <= g.clientWidth + 1) return;
  const pad = 40, l = n.offsetLeft, r = l + n.offsetWidth; let t = null;
  if (l - g.scrollLeft < pad) t = l - pad; else if (r - g.scrollLeft > g.clientWidth - pad) t = r - g.clientWidth + pad;
  if (t == null) return;
  g.scrollTo({ left: Math.max(0, t), behavior: smooth && !S.instant && !isReduced() ? 'smooth' : 'auto' });
}
const curPane = () => $('.pane:not(.leaving)', panes);
const allTabs = (p = curPane()) => p ? [...$$('.tab', p), ...(p._favs ? $$('.tab', p._favs) : [])] : [];
const tabNode = (id, p = curPane()) => p && ($(`.tab[data-id="${CSS.escape(id)}"]`, p) || (p._favs && $(`.tab[data-id="${CSS.escape(id)}"]`, p._favs)));

/* ---------- selection ---------- */
function selectableIds(w = S.ws) {
  const out = [];
  for (const r of walk(W[w].pinned)) if (r.n.t) out.push(r.n.t);
  for (const r of walk(W[w].today)) if (r.n.t) out.push(r.n.t);
  return out;
}
function refreshSel(focusFirstOk) {
  const sel = cur().selected, p = curPane(); if (!p) return;
  let rove = null;
  for (const e of allTabs(p)) {
    const isF = e.classList.contains('folder'), id = e.dataset.id;
    const on = !isF && id === sel;
    let has = false;
    if (isF) { const loc = locate(id); has = !!loc && folderHasSel(loc.n, sel); }
    e.classList.toggle('sel', on); e.classList.toggle('has-sel', has);
    e.setAttribute('aria-selected', on || has ? 'true' : 'false');
    const t = tabs[id]; if (t) { e.classList.toggle('asleep', t.asleep); }
    e.tabIndex = -1;
    if (on || (has && !rove)) rove = e;
  }
  (rove || allTabs(p)[0]) && ((rove || allTabs(p)[0]).tabIndex = 0);
  const t = tabs[sel];
  updateOverflow(p);
  urlIn.value = t ? t.domain + (t.path === '/' ? '' : t.path) : '';
  $('#guardN').textContent = t ? t.blocked : 0;
}
let pageSeq = 0;
function renderPage(fade) {
  const t = tabs[cur().selected], pg = $('#page'); if (!t) return;
  pg.replaceChildren();
  const d = el('div', 'dom'); d.textContent = t.domain + (t.path === '/' ? '' : t.path);
  const h = el('h1'); h.textContent = t.title;
  const hero = el('div', 'hero'); const f = el('span', 'fav'); f.style.setProperty('--c', t.color); f.textContent = t.title.charAt(0).toUpperCase(); hero.appendChild(f);
  pg.append(d, h, hero, ...[100, 92, 96, 70].map(w => { const b = el('div', 'blk'); b.style.width = w + '%'; return b; }), el('div', 'cols'));
  $('.cols', pg).append(el('div'), el('div'), el('div'));
  if (fade) anim(pg, [{ opacity: 0.4 }, { opacity: 1 }], 'fast', 'decelerate');
}
function select(id, { focus = false } = {}) {
  const t = tabs[id]; if (!t) return;
  if (t.asleep) t.asleep = false;
  cur().selected = id;
  const n = tabNode(id); if (n) { n.classList.remove('asleep'); $$('.badge.moon', n).forEach(b => b.remove()); }
  refreshSel(); renderPage(true); ensureVisible(n);
  if (focus && n) n.focus();
}

/* ---------- open / close ---------- */
function openTab(data) {
  const w = S.ws, nd = data || newTabData();
  cur().today.push(nd);
  const g = $('.grp.today', curPane()), e = tabEl(nd, 'today'); g.appendChild(e); setN(g, g.children.length, true);
  cur().selected = nd.t; refreshSel(); renderPage(true);
  const W0 = e.getBoundingClientRect().width;
  if (!S.instant) {
    e.style.minWidth = '0';
    if (isReduced()) anim(e, [{ opacity: 0 }, { opacity: 1 }], 'tab-open', 'standard');
    else {
      const a = e.animate([{ maxWidth: '0px', opacity: 0, transform: `scale(${tok('motion-scale-enter')})` }, { maxWidth: W0 + 'px', opacity: 1, transform: 'scale(1)' }],
        { duration: durMs('tab-open'), easing: ease('decelerate') });
      const fin = () => { e.style.minWidth = ''; updateOverflow(); ensureVisible(e); };
      a.finished.then(fin, fin);
    }
  } else updateOverflow();
  if (!S.instant) ensureVisible(e);
  return e;
}
function removeData(id) { const l = locate(id); if (l) l.list.splice(l.i, 1); }
function closeTab(id) {
  const loc = locate(id); if (!loc || loc.area === 'favs') return;
  const p = curPane(), e = tabNode(id, p); if (!e) return;
  if (loc.area === 'pinned' && !loc.n.f) {
    /* pinned: reset to pinned URL, sleep, move on; a second press on an asleep pinned tab closes it */
    const t = tabs[id];
    if (!t.asleep) {
      t.path = t.pinnedPath || t.path; t.asleep = true;
      if (cur().selected === id) { const ids = selectableIds().filter(x => locate(x).area === 'today'); const nx = ids[0] || selectableIds().find(x => x !== id); if (nx) { cur().selected = nx; renderPage(true); } }
      const w = favEl(t); e.replaceChild(w, $('.fav-wrap', e)); e.classList.add('asleep'); refreshSel(); return;
    }
  }
  const ids = selectableIds(), was = cur().selected === id;
  const order = allTabs(p).map(x => x.dataset.id);
  removeData(id);
  if (was) { const rest = selectableIds(); const nx = rest[Math.min(Math.max(ids.indexOf(id), 0), rest.length - 1)]; if (nx) cur().selected = nx; }
  e.classList.add('closing'); { const g = e.parentElement; if (g && g.classList.contains('today')) setN(g, g.children.length - 1, true); }
  const finish = () => { e.remove(); updateOverflow(); refreshSel(); if (was) renderPage(true); };
  if (S.instant) { finish(); return; }
  const w0 = e.getBoundingClientRect().width;
  anim(e, [{ opacity: 1, transform: 'translateY(0)' }, { opacity: 0, transform: `translateY(${tok('motion-distance-close-slide')})` }], 'xfast', 'accelerate', { fill: 'forwards' }).then(() => {
    if (isReduced() || e.classList.contains('pinned') || e.classList.contains('tile')) { finish(); return; }
    e.style.minWidth = '0'; e.style.opacity = '0';
    const a = e.animate([{ maxWidth: w0 + 'px' }, { maxWidth: '0px' }], { duration: durMs('tab-close'), easing: ease('standard') });
    a.finished.then(finish, finish);
  });
  refreshSel(); if (was) renderPage(true);
}

/* ---------- Workspace switch ---------- */
function setPreset(key, animate) {
  const A = $('#tintA'), B = $('#tintB');
  const prev = S.tintOn === 'A' ? A : B, next = S.tintOn === 'A' ? B : A;
  const n = PRESET_STOPS[key], stops = Array.from({ length: n }, (_, i) => `var(--aerie-workspace-${key}-stop-${i + 1})`);
  next.style.background = `linear-gradient(var(--aerie-tint-angle), ${stops.join(', ')})`;
  next.classList.add('on'); prev.classList.remove('on'); S.tintOn = S.tintOn === 'A' ? 'B' : 'A';
  root.style.setProperty('--ws-hue', `var(--aerie-workspace-${key}-accent)`);
  for (let i = 1; i <= 3; i++) root.style.setProperty('--ws-g' + i, `var(--aerie-workspace-${key}-stop-${Math.min(i, n)})`);
  if (animate && !S.instant) {
    const s = tnum('theme-tint-strength');
    anim(next, [{ opacity: 0 }, { opacity: s }], 'xslow', 'standard'); anim(prev, [{ opacity: s }, { opacity: 0 }], 'xslow', 'standard');
  }
}
function renderChip() {
  const w = WS[S.ws]; chip.replaceChildren(icon(w.icon));
  const nm = el('span', 'nm'); nm.textContent = w.name; chip.append(nm, icon('i-chevron-down', 'cv'));
  chip.setAttribute('aria-label', 'Workspace: ' + w.name);
}
function switchWs(id) {
  if (id === S.ws || !WS[id]) return;
  closePops();
  $$('.pane.leaving', panes).forEach(dropPane);
  const old = curPane(), dir = WS_ORDER.indexOf(id) > WS_ORDER.indexOf(S.ws) ? 1 : -1;
  const np = buildPane(id);
  const apply = () => {
    S.ws = id; mountPane(np); applyLook(id); setPreset(WS[id].preset, true); renderChip(); refreshSel(); renderPage(false);
    if (old) {
      const pair = (x) => [x, x._favs].filter(Boolean);
      old.classList.add('leaving'); old.removeAttribute('role'); old.setAttribute('aria-hidden', 'true'); if (old._favs) old._favs.classList.add('leaving');
      /* full-width strip slide with the spring; reduced motion crossfades */
      if (isReduced()) { pair(np).forEach(x => anim(x, [{ opacity: 0 }, { opacity: 1 }], 'workspace-switch', 'standard')); pair(old).forEach(x => anim(x, [{ opacity: 1 }, { opacity: 0 }], 'workspace-switch', 'standard', { fill: 'forwards' }).then(() => dropPane(old))); }
      else {
        pair(np).forEach(x => anim(x, [{ transform: `translateX(${dir * 100}%)` }, { transform: 'translateX(0)' }], 'workspace-switch', 'spring'));
        pair(old).forEach(x => anim(x, [{ transform: 'translateX(0)' }, { transform: `translateX(${-dir * 100}%)` }], 'workspace-switch', 'spring', { fill: 'forwards' }).then(() => dropPane(old)));
      }
      if (S.instant) dropPane(old);
    }
    updateOverflow(np); ensureVisible(tabNode(cur().selected, np), false);
  };
  withCrossfade(id, apply);
}

/* ---------- popups ---------- */
function place(pop, anchor, { align = 'left' } = {}) {
  pop.hidden = false;
  const r = anchor.getBoundingClientRect(), pw = pop.offsetWidth;
  let x = align === 'right' ? r.right - pw : r.left;
  x = Math.max(8, Math.min(x, innerWidth - pw - 8));
  pop.style.left = x + 'px'; pop.style.top = (r.bottom + 6) + 'px';
  pop.style.transformOrigin = (align === 'right' ? 'top right' : 'top left');
}
function popIn(pop) { if (!S.instant) anim(pop, [{ opacity: tok('motion-opacity-cmdbar-start'), transform: `scale(${tok('motion-scale-cmdbar-enter')})` }, { opacity: 1, transform: 'scale(1)' }], 'fast', 'decelerate'); }
let openPop = null, returnFocus = null;
function closePops(restore) {
  $$('#wsMenu,#drop,#popGuard').forEach(p => p.hidden = true);
  chip.setAttribute('aria-expanded', 'false'); $('#guard').setAttribute('aria-expanded', 'false');
  $$('.tab.folder').forEach(f => f.setAttribute('aria-expanded', 'false'));
  const rf = returnFocus; openPop = null; returnFocus = null;
  if (restore && rf && document.contains(rf)) rf.focus();
}
function menuKeys(m, e) {
  const items = $$('.mi', m).filter(i => !i.disabled), i = items.indexOf(document.activeElement);
  if (e.key === 'ArrowDown') { e.preventDefault(); items[(i + 1) % items.length].focus(); }
  else if (e.key === 'ArrowUp') { e.preventDefault(); items[(i - 1 + items.length) % items.length].focus(); }
  else if (e.key === 'Home') { e.preventDefault(); items[0].focus(); }
  else if (e.key === 'End') { e.preventDefault(); items[items.length - 1].focus(); }
  else if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); closePops(true); }
  else if (e.key === 'Tab') { e.preventDefault(); closePops(true); }
}
function openWsMenu(focus = true) {
  closePops(); const m = $('#wsMenu'); m.replaceChildren();
  WS_ORDER.forEach((id, i) => {
    const w = WS[id], b = el('button', 'mi', { role: 'menuitemradio', 'aria-checked': id === S.ws, tabindex: -1 }); b.dataset.ws = id;
    const d = el('span', 'wsdot'); d.style.setProperty('--ws-bg', `var(--aerie-workspace-${w.preset}-accent)`); d.appendChild(icon(w.icon));
    const nm = el('span'); nm.textContent = w.name; b.append(d, nm);
    if (id === S.ws) { const c = icon('i-check', 'ck'); b.appendChild(c); } else { const k = el('span', 'k'); k.textContent = 'Ctrl+Shift+' + (i + 1); b.appendChild(k); }
    m.appendChild(b);
  });
  m.appendChild(el('hr'));
  [['New Workspace…', ''], ['Edit Workspace…', ''], ['Archived tabs', '']].forEach(([t]) => { const b = el('button', 'mi dim', { role: 'menuitem', tabindex: -1 }); b.textContent = t; b.dataset.stub = '1'; m.appendChild(b); });
  place(m, chip); popIn(m); chip.setAttribute('aria-expanded', 'true'); openPop = 'ws'; returnFocus = chip;
  if (focus) ($('.mi[aria-checked=true]', m) || $('.mi', m)).focus();
}
function openFolder(id, focus = true) {
  const loc = locate(id), e = tabNode(id); if (!loc || !e) return;
  closePops(); const m = $('#drop'); m.replaceChildren(); m.setAttribute('aria-label', loc.n.name);
  const render = (f, depth) => {
    f.kids.forEach(k => {
      if (k.f) {
        const h = el('div', 'dh', { role: 'presentation' }); h.style.paddingLeft = (10 + depth * 14) + 'px'; h.appendChild(icon('i-folder')); const s = el('span'); s.textContent = k.name; h.appendChild(s); m.appendChild(h);
        render(k, depth + 1);
      } else {
        const t = tabs[k.t], b = el('button', 'mi', { role: 'menuitemradio', 'aria-checked': cur().selected === t.id, tabindex: -1 }); b.dataset.id = t.id; b.style.paddingLeft = (10 + depth * 14) + 'px';
        b.appendChild(favEl(t)); const s = el('span', 'tt'); s.textContent = t.title; b.appendChild(s); if (t.asleep) b.classList.add('asleep');
        m.appendChild(b);
      }
    });
  };
  const h = el('div', 'dh', { role: 'presentation' }); const hs = el('span'); hs.textContent = loc.n.name + ' · ' + countTabs(loc.n) + ' tabs'; h.appendChild(hs); m.appendChild(h);
  render(loc.n, 0);
  place(m, e); popIn(m); e.setAttribute('aria-expanded', 'true'); openPop = 'folder'; returnFocus = e;
  if (focus) ($('.mi[aria-checked=true]', m) || $('.mi', m)).focus();
}
const countTabs = f => f.kids.reduce((a, k) => a + (k.f ? countTabs(k) : 1), 0);
$('#wsMenu').addEventListener('keydown', e => menuKeys($('#wsMenu'), e));
$('#drop').addEventListener('keydown', e => menuKeys($('#drop'), e));
$('#wsMenu').addEventListener('click', e => { const b = e.target.closest('.mi'); if (!b) return; if (b.dataset.ws) { closePops(); switchWs(b.dataset.ws); chip.focus(); } else closePops(true); });
$('#drop').addEventListener('click', e => { const b = e.target.closest('.mi'); if (!b) return; const rf = returnFocus; closePops(); select(b.dataset.id); if (rf && document.contains(rf)) rf.focus(); });
chip.addEventListener('click', () => { if (openPop === 'ws') closePops(); else openWsMenu(false); });
chip.addEventListener('keydown', e => { if (e.key === 'ArrowDown') { e.preventDefault(); openWsMenu(true); } });
$('#guard').addEventListener('click', () => {
  if (openPop === 'guard') { closePops(); return; }
  closePops(); const p = $('#popGuard'); place(p, $('#guard'), { align: 'right' }); popIn(p); $('#guard').setAttribute('aria-expanded', 'true'); openPop = 'guard'; returnFocus = $('#guard');
});
document.addEventListener('pointerdown', e => { if (openPop && !e.target.closest('.menu,.pop,.chip,.guard,.tab.folder')) closePops(); }, true);

/* ---------- command bar ---------- */
const cmd = { open: false, items: [], sel: 0, prev: null };
function cmdItems(q) {
  q = q.trim().toLowerCase(); const out = [];
  const match = s => !q || s.toLowerCase().includes(q);
  if (q) out.push({ g: 'Go to', icon: 'arrow', t: /\./.test(q) && !/\s/.test(q) ? 'Open ' + q : 'Search the web for “' + q + '”', s: 'Enter', run: () => openFromText(q) });
  const mine = [...selectableIds(), ...W[S.ws].favorites].map(id => tabs[id]).filter(t => match(t.title + ' ' + t.domain));
  mine.slice(0, 6).forEach((t, i) => out.push({ g: i === 0 ? 'Tabs in ' + WS[S.ws].name : null, fav: t, t: t.title, s: t.domain, run: () => select(t.id) }));
  WS_ORDER.filter(id => id !== S.ws && match(WS[id].name)).forEach((id, i) => out.push({ g: i === 0 ? 'Workspaces' : null, icon: WS[id].icon.slice(2), wsIcon: WS[id].icon, t: 'Switch to ' + WS[id].name, s: 'Ctrl+Shift+' + (WS_ORDER.indexOf(id) + 1), run: () => switchWs(id) }));
  if (!q) out.push({ g: 'Suggested', wsIcon: 'i-globe', t: 'Open a new tab', s: '', run: () => openTab() });
  return out;
}
function renderCmd() {
  const l = $('#cmdList'); l.replaceChildren();
  cmd.items = cmdItems($('#cmdInput').value); cmd.sel = Math.min(cmd.sel, Math.max(cmd.items.length - 1, 0));
  cmd.items.forEach((it, i) => {
    if (it.g) { const g = el('div', 'cmd-grp', { role: 'presentation' }); g.textContent = it.g; l.appendChild(g); }
    const r = el('div', 'cmd-item', { role: 'option', id: 'cmd-' + i, 'aria-selected': i === cmd.sel }); r.dataset.i = i;
    if (it.fav) { const f = el('span', 'fav'); f.style.setProperty('--c', it.fav.color); f.textContent = it.fav.title.charAt(0).toUpperCase(); r.appendChild(f); }
    else { const a = el('span', 'cmd-act'); a.appendChild(icon(it.wsIcon || 'i-arrow')); r.appendChild(a); }
    const t = el('span', 't'); t.textContent = it.t; const s = el('span', 's'); s.textContent = it.s || ''; r.append(t, s); l.appendChild(r);
  });
  $('#cmdInput').setAttribute('aria-activedescendant', 'cmd-' + cmd.sel);
}
function openFromText(q) {
  const dom = /\./.test(q) && !/\s/.test(q) ? q.replace(/^https?:\/\//, '').split('/')[0] : null;
  const title = dom ? dom : q;
  openTab(newTabData(title.charAt(0).toUpperCase() + title.slice(1), dom || 'search.aerie.example', '#' + (hash(title) & 0xffffff).toString(16).padStart(6, '0').replace(/^(.)(.)/, (m, a, b) => '3' + b)));
}
function openCmd() {
  if (cmd.open) return; closePops(); cmd.open = true; cmd.prev = document.activeElement;
  const lay = $('#cmdLayer'); lay.hidden = false; $('#cmdInput').value = ''; cmd.sel = 0; renderCmd(); $('#cmdInput').focus();
  anim($('#cmdScrim'), [{ opacity: 0 }, { opacity: 1 }], 'base', 'standard');
  anim($('#cmd'), [{ opacity: tok('motion-opacity-cmdbar-start'), transform: `scale(${tok('motion-scale-cmdbar-enter')})` }, { opacity: 1, transform: 'scale(1)' }], 'base', 'decelerate');
}
function closeCmd(restore = true) {
  if (!cmd.open) return; cmd.open = false;
  const lay = $('#cmdLayer');
  anim($('#cmd'), [{ opacity: 1 }, { opacity: 0 }], 'fast', 'accelerate', { fill: 'forwards' });
  const p = anim($('#cmdScrim'), [{ opacity: 1 }, { opacity: 0 }], 'fast', 'accelerate', { fill: 'forwards' });
  p.then(() => { if (!cmd.open) { lay.hidden = true; $$('#cmd,#cmdScrim').forEach(n => n.getAnimations().forEach(a => a.cancel())); } });
  if (restore && cmd.prev && document.contains(cmd.prev)) cmd.prev.focus();
}

$('#cmdScrim').addEventListener('click', () => closeCmd());
$('#cmdInput').addEventListener('input', () => { cmd.sel = 0; renderCmd(); });
$('#cmdList').addEventListener('click', e => { const r = e.target.closest('.cmd-item'); if (r) { const it = cmd.items[+r.dataset.i]; closeCmd(false); it.run(); (tabNode(cur().selected) || plusBtn()).focus(); } });
$('#cmdInput').addEventListener('keydown', e => {
  if (e.key === 'ArrowDown' || e.key === 'ArrowUp') { e.preventDefault(); const n = cmd.items.length; if (n) { cmd.sel = (cmd.sel + (e.key === 'ArrowDown' ? 1 : n - 1)) % n; renderCmd(); } }
  else if (e.key === 'Enter') { e.preventDefault(); const it = cmd.items[cmd.sel]; closeCmd(false); if (it) it.run(); (tabNode(cur().selected) || plusBtn()).focus(); }
  else if (e.key === 'Escape') { e.preventDefault(); closeCmd(); }
  else if (e.key === 'Tab') { e.preventDefault(); }
});

/* ---------- address pill ---------- */
urlIn.addEventListener('focus', () => urlIn.select());
urlIn.addEventListener('keydown', e => { if (e.key === 'Enter') { const q = urlIn.value.trim(); if (q) { openFromText(q); urlIn.blur(); } } else if (e.key === 'Escape') { refreshSel(); urlIn.blur(); } });

/* ---------- reorder (FLIP: neighbours shift with the standard easing, dragged tab settles with the spring) ---------- */
function moveInGroup(node, newIdx) {
  const g = node.parentElement, sibs = [...g.children].filter(c => c.classList.contains('tab') && c !== node);
  const before = new Map(sibs.map(s => [s, s.getBoundingClientRect().left]));
  sibs.forEach(s => s.getAnimations().forEach(a => a.cancel()));
  g.insertBefore(node, sibs[newIdx] || null);
  if (S.instant || isReduced()) return;
  sibs.forEach(s => { const d = before.get(s) - s.getBoundingClientRect().left; if (Math.abs(d) > .5) s.animate([{ transform: `translateX(${d}px)` }, { transform: 'translateX(0)' }], { duration: durMs('fast'), easing: ease('standard') }); });
}
function commitOrder(g) {
  const area = g.classList.contains('favs') ? 'favs' : g.classList.contains('pinned') ? 'pinned' : 'today';
  const ids = [...g.children].filter(c => c.classList.contains('tab')).map(c => c.dataset.id);
  if (area === 'favs') W[S.ws].favorites = ids;
  else { const list = W[S.ws][area], by = new Map(list.map(n => [n.t || n.f, n])); list.splice(0, list.length, ...ids.map(i => by.get(i))); }
}
let suppressClick = false;
const onHosts = (t, f) => [panes, tbFavs].forEach(h => h.addEventListener(t, f));
onHosts('pointerdown', e => {
  const node = e.target.closest('.tab'); if (!node || e.button !== 0 || e.target.closest('.close') || S.drag) return;
  const startX = e.clientX; let dragging = false, curTx = 0, grab = 0, gr = null;
  const g = node.parentElement;
  const layoutLeft = () => { const t = node.style.transform; node.style.transform = 'none'; const l = node.getBoundingClientRect().left; node.style.transform = t; return l; };
  const mv = ev => {
    if (!dragging) {
      if (Math.abs(ev.clientX - startX) < 4) return;
      dragging = true; S.drag = true; closePops(); grab = startX - node.getBoundingClientRect().left; gr = g.getBoundingClientRect();
      node.getAnimations().forEach(a => a.cancel()); node.classList.add('lifted'); node.setPointerCapture(e.pointerId);
      if (!S.instant) node.animate([{ transform: 'translateX(0) scale(1)' }, { transform: `translateX(0) scale(${tok('motion-scale-lift')})` }], { duration: durMs('xfast'), easing: ease('standard') });
    }
    const w = node.offsetWidth; const left = Math.max(gr.left, Math.min(ev.clientX - grab, gr.right - w));
    curTx = left - layoutLeft(); node.style.transform = `translateX(${curTx}px) scale(${tok('motion-scale-lift')})`;
    const sibs = [...g.children].filter(c => c.classList.contains('tab') && c !== node);
    const center = node.offsetLeft + curTx + w / 2;
    const idx = sibs.filter(s => s.offsetLeft + s.offsetWidth / 2 < center).length;
    const myIdx = [...g.children].filter(c => c.classList.contains('tab')).indexOf(node);
    if (idx !== myIdx) { moveInGroup(node, idx); curTx = left - layoutLeft(); node.style.transform = `translateX(${curTx}px) scale(${tok('motion-scale-lift')})`; }
  };
  const up = () => {
    node.removeEventListener('pointermove', mv); node.removeEventListener('pointerup', up); node.removeEventListener('pointercancel', up);
    if (!dragging) return;
    suppressClick = true; setTimeout(() => suppressClick = false, 0);
    commitOrder(g); S.drag = false;
    const done = () => { node.style.transform = ''; node.classList.remove('lifted'); };
    if (S.instant) { done(); return; }
    node.classList.remove('lifted');
    const a = node.animate([{ transform: `translateX(${curTx}px) scale(${tok('motion-scale-lift')})` }, { transform: 'translateX(0) scale(1)' }], { duration: isReduced() ? durMs('fast') : durMs('spring'), easing: ease('spring-settle') });
    node.style.transform = ''; a.finished.then(() => {}, () => {});
  };
  node.addEventListener('pointermove', mv); node.addEventListener('pointerup', up); node.addEventListener('pointercancel', up);
  node.setPointerCapture(e.pointerId);
});
onHosts('click', e => {
  if (suppressClick) return;
  if (e.target.closest('.plus')) { cmd.open ? closeCmd() : openCmd(); return; }
  const node = e.target.closest('.tab'); if (!node || node.closest('.leaving')) return;
  if (e.target.closest('.close')) { closeTab(node.dataset.id); return; }
  if (node.classList.contains('folder')) { openPop === 'folder' && returnFocus === node ? closePops() : openFolder(node.dataset.id, false); return; }
  closePops(); select(node.dataset.id);
});
onHosts('auxclick', e => { if (e.button === 1) { const n = e.target.closest('.tab'); if (n && !n.classList.contains('folder')) closeTab(n.dataset.id); } });

panes.addEventListener('focusin', e => { const n = e.target.closest && e.target.closest('.tab'); if (n) ensureVisible(n); });
addEventListener('resize', () => updateOverflow());

/* ---------- keyboard ---------- */
onHosts('keydown', e => {
  const node = e.target.closest('.tab'); if (!node) return;
  const list = allTabs(), i = list.indexOf(node);
  const go = j => { e.preventDefault(); list[(j + list.length) % list.length].focus(); };
  if (e.altKey && (e.key === 'ArrowLeft' || e.key === 'ArrowRight')) {
    e.preventDefault(); const g = node.parentElement, sibs = [...g.children].filter(c => c.classList.contains('tab')), k = sibs.indexOf(node) + (e.key === 'ArrowRight' ? 1 : -1);
    if (k >= 0 && k < sibs.length) { moveInGroup(node, k); commitOrder(g); node.focus(); } return;
  }
  if (e.key === 'ArrowRight') go(i + 1); else if (e.key === 'ArrowLeft') go(i - 1);
  else if (e.key === 'Home') go(0); else if (e.key === 'End') go(list.length - 1);
  else if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); if (node.classList.contains('folder')) openFolder(node.dataset.id, true); else select(node.dataset.id); }
  else if (e.key === 'ArrowDown' && node.classList.contains('folder')) { e.preventDefault(); openFolder(node.dataset.id, true); }
  else if (e.key === 'Delete' || e.key === 'Backspace') { if (!node.classList.contains('folder')) { e.preventDefault(); const nx = list[i + 1] || list[i - 1]; closeTab(node.dataset.id); if (nx) nx.focus(); } }
});
function cycle(dir) {
  const ids = selectableIds(); if (!ids.length) return; const i = ids.indexOf(cur().selected);
  select(ids[(i + dir + ids.length) % ids.length], { focus: !!document.activeElement.closest('.tab') });
}
document.addEventListener('keydown', e => {
  const ctrl = e.ctrlKey || e.metaKey;
  if (ctrl && e.shiftKey && /^Digit[1-3]$/.test(e.code)) { e.preventDefault(); switchWs(WS_ORDER[+e.code.slice(5) - 1]); return; }
  if (ctrl && e.key === 'Tab') { e.preventDefault(); cycle(e.shiftKey ? -1 : 1); return; }
  if (ctrl && (e.key === 'PageDown' || e.key === 'PageUp')) { e.preventDefault(); cycle(e.key === 'PageDown' ? 1 : -1); return; }
  if (ctrl && !e.shiftKey && e.key.toLowerCase() === 't') { e.preventDefault(); openCmd(); return; }
  if (ctrl && !e.shiftKey && e.key.toLowerCase() === 'w') { e.preventDefault(); closeTab(cur().selected); return; }
  if (ctrl && !e.shiftKey && e.key.toLowerCase() === 'k') { e.preventDefault(); openCmd(); return; }
  if (e.key === 'Escape' && openPop) closePops(true);
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
function buildMock() {
  const m = $('#mock'); if (params.get('controls') === '0') { m.hidden = true; return; }
  m.innerHTML = '<div class="mh"><span>Mockup controls</span><span id="mockT">hide</span></div><div class="mbody"></div>';
  const body = $('.mbody', m); pagesRow(body, 'horizontal.html', () => S.gMode);
  const row = (label, node) => { const r = el('div', 'mrow'); const l = el('span'); l.textContent = label; r.append(l, node); body.appendChild(r); return node; };
  const sel = (opts, val, fn) => { const s = el('select'); opts.forEach(([v, t]) => { const o = el('option'); o.value = v; o.textContent = t; s.appendChild(o); }); s.value = val; s.addEventListener('change', () => fn(s.value)); return s; };
  row('mode', sel([['light', 'Light'], ['dark', 'Dark']], S.gMode, v => { S.gMode = v; withCrossfade(S.ws, () => applyLook()); }));
  row('theme', sel(THEMES, S.gTheme, v => { S.gTheme = v; S.explicitTheme = true; withCrossfade(S.ws, () => applyLook()); }));
  row('workspace', sel(WS_ORDER.map(i => [i, WS[i].name]), S.ws, v => { switchWs(v); }));
  row('rows', sel([['2', '2 rows (default)'], ['1', '1 row (dense)']], String(S.rows), v => setRows(+v)));
  row('density', sel([['comfortable', 'Comfortable'], ['compact', 'Compact']], S.density, v => { S.density = v; win.dataset.density = v; }));
  const rm = el('input', null, { type: 'checkbox' }); rm.checked = root.hasAttribute('data-reduced-motion'); rm.addEventListener('change', () => rm.checked ? root.setAttribute('data-reduced-motion', '') : root.removeAttribute('data-reduced-motion')); row('reduced motion', rm);
  const bo = el('button'); bo.textContent = 'open tab'; bo.onclick = () => openTab();
  const bc = el('button'); bc.textContent = 'close tab'; bc.onclick = () => closeTab(cur().selected); row('actions', bo); body.lastChild.appendChild(bc);
  const sm = el('small'); sm.textContent = 'Ctrl+Shift+1 to 3 switch Workspace. Drag tabs to reorder. Alt+Left/Right reorders from the keyboard.'; body.appendChild(sm);
  $('.mh', m).addEventListener('click', () => { m.classList.toggle('collapsed'); $('#mockT').textContent = m.classList.contains('collapsed') ? 'show' : 'hide'; });
}

/* ---------- one row or two ---------- */
function placeChrome() {
  const nav = $('.navgrp'), pill = $('#pill'), tb = $('#toolbar');
  if (S.rows === 2) tb.prepend(nav, pill); else { $('#strip').append(nav, pill); }
  if (S.rows === 2) tb.append(tbFavs); else $('#strip').append(tbFavs);
}
function setRows(r) {
  S.rows = r; closePops(); win.dataset.rows = r; placeChrome();
  [...panes.children].forEach(dropPane); tbFavs.replaceChildren(); tbFavs.style.width = '';
  mountPane(buildPane(S.ws)); refreshSel(); ensureVisible(tabNode(cur().selected), false); updateOverflow();
}

/* ---------- init ---------- */
function init() {
  const th = normTheme(params.get('theme')); if (th) { S.gTheme = th; S.explicitTheme = true; }
  const md = params.get('mode'); if (md === 'light' || md === 'dark') S.gMode = md; else if (systemDark()) S.gMode = 'dark';
  const ws = params.get('ws'); if (WS[ws]) S.ws = ws;
  if (params.get('density') === 'compact') { S.density = 'compact'; win.dataset.density = 'compact'; }
  if (params.get('reduced') === '1') root.setAttribute('data-reduced-motion', '');
  if (params.get('wsoverrides') === '0') S.wsOverrides = false;
  const crowd = parseInt(params.get('crowd') || '0', 10);
  for (let i = 0; i < crowd; i++) { const [t, d, c] = EXTRA[i % EXTRA.length]; W[S.ws].today.push(newTabData(i >= EXTRA.length ? t + ' ' + (i + 1) : t, d, c)); }
  if (params.get('rows') === '1') S.rows = 1;
  win.dataset.rows = S.rows; placeChrome();
  applyLook(S.ws); setPreset(WS[S.ws].preset, false); renderChip();
  mountPane(buildPane(S.ws));
  if (params.get('select') === 'last') { const ids = W[S.ws].today.filter(n => n.t).map(n => n.t); cur().selected = ids[ids.length - 1]; }
  refreshSel(); renderPage(false); buildMock(); ensureVisible(tabNode(cur().selected), false); updateOverflow();
  const open = params.get('open');
  if (open === 'cmdbar') openCmd();
  else if (open === 'wsmenu') openWsMenu(false);
  else if (open === 'folder') { const f = firstFolder(); if (f) openFolder(f.f, false); }
  requestAnimationFrame(() => requestAnimationFrame(() => { S.instant = false; }));
}
init();
window.__aerie = { S, W, WS, select, openTab, closeTab, switchWs, openCmd, closeCmd };
})();
