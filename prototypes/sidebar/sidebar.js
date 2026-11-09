/* Aerie sidebar + Workspaces mockup. Plain JS. All durations and easings come from tokens.css. */
(() => {
'use strict';
const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];
const root = document.documentElement;
const win = $('#window'), sidebar = $('#sidebar'), scroll = $('#scroll'), card = $('#card');
const params = new URLSearchParams(location.search);

/* ---------- tokens ---------- */
const tok = n => getComputedStyle(root).getPropertyValue('--aerie-' + n).trim();
const tnum = n => parseFloat(tok(n));
const isReduced = () => root.hasAttribute('data-reduced-motion') || matchMedia('(prefers-reduced-motion: reduce)').matches;
const durMs = n => n === 'spring' ? tnum('motion-spring-duration') : tnum('motion-duration-' + n);

/* ---------- state ---------- */
const S = { ws: 'personal', gTheme: 'vivid', gMode: 'light', explicitTheme: false, look: null, density: 'comfortable', side: 'left', hidden: false, peek: false, sbW: null, instant: true, tintOn: 'A', focus: false, drag: false };
const PRESET_STOPS = { dusk: 3, tide: 2, moss: 2, ember: 2, lilac: 2, slate: 2, sand: 2, mint: 2 };
const PRESET_CYCLE = ['ember', 'lilac', 'slate', 'sand', 'mint'];
const WS_ORDER = ['personal', 'work', 'side'];
const WS = {
  /* theme: 'default' | theme id; mode: 'default' | 'light' | 'dark' | 'system'. Per-Workspace appearance is data (it would live on the Workspace record in the Tab Model), not tokens. */
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
const themeName = id => (THEMES.find(t => t[0] === id) || [0, id])[1];
/* Effective look = Workspace override ?? global default. prefers-contrast: more wins unless the user chose a theme in the URL/controls. OLED is dark only and falls back to Clean in light mode. */
function effectiveLook(wsId, over = {}) {
  const w = { ...WS[wsId], ...over }, gT = over.gTheme || S.gTheme, gM = over.gMode || S.gMode;
  const picked = (contrastMore.matches && !S.explicitTheme) ? 'high-contrast' : (w.theme !== 'default' ? w.theme : gT);
  const mode = w.mode === 'system' ? (systemDark() ? 'dark' : 'light') : (w.mode === 'light' || w.mode === 'dark') ? w.mode : gM;
  const fell = picked === 'oled' && mode === 'light';
  return { theme: fell ? 'clean' : picked, mode, picked, fell };
}
function applyLook() { const e = S.look = effectiveLook(S.ws); root.dataset.theme = e.theme; root.dataset.mode = e.mode; }
/* Whole-chrome crossfade between two complete rendered layers (View Transitions: old snapshot dissolves into the live new one),
   so text is never half-faded against the wrong surface. Duration xslow (token falls to 140ms under reduced motion). */
function crossfade(next, fn, { reducedInstant = false } = {}) {
  if (S.instant || !document.startViewTransition || (reducedInstant && isReduced()) || (next.theme === root.dataset.theme && next.mode === root.dataset.mode)) { fn(); return Promise.resolve(); }
  root.style.setProperty('--vt-dur', durMs('xslow') + 'ms');
  return document.startViewTransition(fn).updateCallbackDone.catch(() => {});
}
function changeLook(over, mut) { crossfade(effectiveLook(S.ws, over), () => { mut(); applyLook(); syncControls(); renderFocusBadges(); }, { reducedInstant: true }); }
const dirOut = () => S.side === 'left' ? -1 : 1;
const sbWidth = () => S.density === 'collapsed' ? tnum('size-rail-width') : (S.sbW || tnum('size-sidebar-width'));

/* ---------- data ---------- */
const tabs = {};
let nextId = 1;
const hash = s => [...s].reduce((a, c) => (a * 31 + c.charCodeAt(0)) >>> 0, 7);
function mk(id, title, domain, path, color, x = {}) {
  tabs[id] = { id, title, domain, path, color, pinnedPath: null, asleep: false, audio: false, blocked: 6 + hash(id) % 47, ...x };
  return id;
}
const T = (id, title, domain, color, x) => ({ t: mk(id, title, domain, x && x.path || '/', color, x) });
const F = (id, name, open, kids) => ({ f: id, name, open, kids });
const favs = (...a) => a;
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
const archive = [
  { id: 'a1', title: 'Visa application checklist', domain: 'consulate.gov.example', color: '#5B6B7F', ws: 'Personal', when: 'Yesterday' },
  { id: 'a2', title: 'Q3 planning doc', domain: 'docs.northwind.work', color: '#4F7CFF', ws: 'Work', when: '2 days ago' },
  { id: 'a3', title: 'Allotment layout ideas', domain: 'leafy.garden.guide', color: '#30A46C', ws: 'Personal', when: 'Last week' },
  { id: 'a4', title: 'Build cache sizing', domain: 'blog.deepcuts.dev', color: '#8E4EC6', ws: 'Side project', when: 'Last week' },
];
const cur = () => W[S.ws];
const curTab = () => tabs[cur().selected];

/* ---------- tree helpers ---------- */
function* walk(list, parent = null, area = null) {
  for (let i = 0; i < list.length; i++) {
    const n = list[i];
    yield { n, list, i, parent, area };
    if (n.f) yield* walk(n.kids, n, area);
  }
}
function locate(id, wsId = null) {
  for (const k of (wsId ? [wsId] : Object.keys(W))) {
    const w = W[k], fi = w.favorites.indexOf(id);
    if (fi >= 0) return { ws: k, area: 'fav', list: w.favorites, i: fi, parent: null, n: id };
    for (const a of ['pinned', 'today']) for (const e of walk(w[a], null, a)) if ((e.n.t === id) || (e.n.f === id)) return { ws: k, ...e, area: a };
  }
  return null;
}
const isDesc = (folder, id) => folder.f === id || folder.kids.some(k => k.f ? isDesc(k, id) : false);

/* ---------- DOM helpers ---------- */
function el(tag, cls, attrs) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (attrs) for (const k in attrs) e.setAttribute(k, attrs[k]);
  return e;
}
const icon = (id, cls = '') => { const s = document.createElementNS('http://www.w3.org/2000/svg', 'svg'); s.setAttribute('class', 'ic ' + cls); const u = document.createElementNS('http://www.w3.org/2000/svg', 'use'); u.setAttribute('href', '#' + id); s.appendChild(u); return s; };
function favEl(t, asleepBadge) {
  const w = el('span', 'fav-wrap'); const f = el('span', 'fav'); f.style.setProperty('--c', t.color); f.textContent = t.title[0].toUpperCase();
  w.appendChild(f);
  if (t.asleep && asleepBadge !== false) { const m = el('span', 'moon'); m.appendChild(icon('i-moon')); w.appendChild(m); w.dataset.tip = 'Asleep — frees memory'; }
  return w;
}

/* ---------- motion ---------- */
function fx(target, from, to, dur, ease, o = {}) {
  const done = Promise.resolve(null);
  if (S.instant || !target) return done;
  const red = isReduced();
  let f = { ...from }, t = { ...to };
  if (red) {
    if (o.role === 'move') return done;
    const out = o.role === 'out';
    f = { o: from.o !== undefined ? from.o : (out ? 1 : 0) }; t = { o: to.o !== undefined ? to.o : (out ? 0 : 1) };
    if (f.o === t.o) { f.o = out ? 1 : 0; t.o = out ? 0 : 1; }
  }
  const hasO = f.o !== undefined || t.o !== undefined;
  const kf = v => {
    const k = {};
    if (!red) k.transform = `translate(${v.x || 0}px,${v.y || 0}px) scale(${v.s === undefined ? 1 : v.s})${v.r ? ` rotate(${v.r}deg)` : ''}`;
    if (hasO) k.opacity = v.o === undefined ? 1 : v.o;
    return k;
  };
  const a = target.animate([kf(f), kf(t)], { duration: durMs(dur), easing: tok('motion-easing-' + ease), delay: o.delay || 0, fill: o.fill || 'none' });
  const p = a.finished.then(() => a, () => a); p.anim = a; return p;
}
function heightFx(node, from, to, dur, ease, o = {}) {
  if (S.instant) return Promise.resolve();
  const a = node.animate([{ height: from + 'px' }, { height: to + 'px' }], { duration: durMs(dur), easing: tok('motion-easing-' + ease), fill: o.fill || 'none' });
  node.style.overflow = 'hidden';
  return a.finished.then(() => { node.style.overflow = ''; return a; }, () => a);
}
const raf = () => new Promise(r => requestAnimationFrame(r));

/* ---------- workspace tint ---------- */
const gradientOf = key => {
  const n = PRESET_STOPS[key] || 2, stops = Array.from({ length: n }, (_, i) => `var(--aerie-workspace-${key}-stop-${i + 1})`);
  return `linear-gradient(var(--aerie-tint-angle), ${stops.join(', ')})`;
};
function setPreset(key, animate) {
  const A = $('#tintA'), B = $('#tintB');
  const prev = S.tintOn === 'A' ? A : B, next = S.tintOn === 'A' ? B : A;
  next.style.background = gradientOf(key);
  next.classList.add('on'); prev.classList.remove('on'); S.tintOn = S.tintOn === 'A' ? 'B' : 'A';
  const n = PRESET_STOPS[key], strength = tnum('theme-tint-strength');
  root.style.setProperty('--ws-hue', `var(--aerie-workspace-${key}-accent)`);
  root.style.setProperty('--ws-gradient', gradientOf(key));
  for (let i = 1; i <= 3; i++) root.style.setProperty('--ws-g' + i, i <= n ? `var(--aerie-workspace-${key}-stop-${i})` : `var(--aerie-workspace-${key}-stop-${n})`);
  if (animate) { fx(next, { o: 0 }, { o: strength }, 'xslow', 'standard', { role: 'in' }); fx(prev, { o: strength }, { o: 0 }, 'xslow', 'standard', { role: 'out' }); }
}

/* ---------- building the sidebar pane ---------- */
const cols = () => S.density === 'collapsed' ? 1 : tnum('size-favorites-columns');
function tabRow(id, lvl, area) {
  const t = tabs[id], sel = cur().selected === id;
  const node = el('div', 'node'); node.dataset.id = 't:' + id;
  const row = el('div', 'row' + (sel ? ' sel' : '') + (t.asleep ? ' asleep' : ''), { role: 'treeitem', 'aria-level': lvl + 1, 'aria-selected': sel, tabindex: -1, 'aria-label': t.title + (t.asleep ? ', asleep' : '') });
  row.dataset.id = id; row.dataset.area = area; row.dataset.lvl = lvl; if (S.density === 'collapsed') row.dataset.tip = t.title;
  const inn = el('div', 'row-in'); inn.style.setProperty('--lvl', lvl);
  if (area === 'pinned' && t.pinnedPath && t.path !== t.pinnedPath) {
    const b = el('button', 'back-pinned', { tabindex: -1, 'aria-label': 'Back to pinned page', 'data-tip': 'Back to pinned page' }); b.appendChild(icon('i-return')); b.dataset.act = 'back-pinned'; inn.appendChild(b);
  }
  inn.appendChild(favEl(t));
  const title = el('span', 'title'); title.textContent = t.title; inn.appendChild(title);
  const trail = el('span', 'trail');
  if (t.audio) { const a = el('span', 'audio', { 'aria-label': 'Playing audio' }); a.appendChild(icon('i-speaker')); trail.appendChild(a); }
  const cl = area === 'pinned' ? pinnedCloseLabel(id) : 'Close tab'; const c = el('button', 'close', { tabindex: -1, 'aria-label': cl, 'data-tip': cl }); c.dataset.act = 'close'; c.appendChild(icon('i-x')); trail.appendChild(c);
  inn.appendChild(trail); row.appendChild(inn); node.appendChild(row);
  return node;
}
function folderRow(f, lvl, area) {
  const node = el('div', 'node folder-node'); node.dataset.id = 'f:' + f.f;
  const row = el('div', 'row folder' + (f.open ? ' open' : ''), { role: 'treeitem', 'aria-level': lvl + 1, 'aria-expanded': !!f.open, tabindex: -1, 'aria-label': f.name + ' folder', 'aria-owns': 'g-' + f.f });
  row.dataset.id = f.f; row.dataset.area = area; row.dataset.folder = '1'; row.dataset.lvl = lvl; row.dataset.depth = lvl + 1; if (S.density === 'collapsed') row.dataset.tip = f.name;
  const inn = el('div', 'row-in'); inn.style.setProperty('--lvl', lvl);
  const ch = el('span', 'chev'); ch.appendChild(icon('i-chevron')); inn.appendChild(ch);
  const fi = el('span', 'fav-wrap folder-ic'); fi.style.width = 'var(--fav)'; fi.appendChild(icon('i-folder')); inn.appendChild(fi);
  const title = el('span', 'title'); title.textContent = f.name; inn.appendChild(title);
  row.appendChild(inn); node.appendChild(row);
  const kids = el('div', 'children', { role: 'group', id: 'g-' + f.f }); if (!f.open) kids.hidden = true;
  f.kids.forEach(k => kids.appendChild(nodeEl(k, lvl + 1, area)));
  node.appendChild(kids);
  return node;
}
const nodeEl = (n, lvl, area) => n.f ? folderRow(n, lvl, area) : tabRow(n.t, lvl, area);

function secHead(label, action) {
  const h = el('div', 'sec-head' + (label === 'Pinned' ? ' plain' : ''));
  if (label) { const l = el('span', 'sec-label'); l.textContent = label; h.appendChild(l); }
  h.appendChild(el('span', 'sec-line'));
  if (action) { const b = el('button', 'sec-act', { 'aria-label': 'Clear unpinned tabs' }); b.textContent = action; b.dataset.act = 'clear'; h.appendChild(b); }
  return h;
}
function buildPane(wsId) {
  const w = W[wsId], pane = el('div', 'pane'); pane.dataset.ws = wsId;
  const fv = el('div', 'favs', { role: 'grid', 'aria-label': 'Favorites' });
  const c = cols();
  for (let i = 0; i < w.favorites.length; i += c) {
    const r = el('div', 'favrow', { role: 'row' }); if (c === 1) r.style.gridTemplateColumns = '1fr';
    w.favorites.slice(i, i + c).forEach(id => {
      const t = tabs[id], cell = el('div', '', { role: 'gridcell', style: 'display:contents' });
      const b = el('button', 'tile' + (w.selected === id ? ' sel' : '') + (t.asleep ? ' asleep' : ''), { tabindex: -1, 'aria-label': t.title, 'data-tip': t.title });
      b.dataset.id = id; b.dataset.area = 'fav'; b.appendChild(favEl(t)); cell.appendChild(b); r.appendChild(cell);
    });
    fv.appendChild(r);
  }
  pane.appendChild(fv);
  pane.appendChild(secHead('Pinned'));
  const pt = el('div', 'tree', { role: 'tree', 'aria-label': 'Pinned tabs' }); pt.dataset.area = 'pinned';
  w.pinned.forEach(n => pt.appendChild(nodeEl(n, 0, 'pinned')));
  pane.appendChild(pt);
  const nt = el('button', 'row newtab', { id: 'newTab', 'aria-label': 'New Tab', 'data-tip': 'New Tab' }); nt.dataset.keys = 'Ctrl+T';
  const ni = el('div', 'row-in'); const pl = el('span', 'plus'); pl.appendChild(icon('i-plus')); ni.appendChild(pl);
  const nl = el('span', 'title'); nl.textContent = 'New Tab'; ni.appendChild(nl); nt.appendChild(ni);
  pane.appendChild(nt);
  pane.appendChild(secHead('Today', 'Clear'));
  const tt = el('div', 'tree', { role: 'tree', 'aria-label': 'Today' }); tt.dataset.area = 'today';
  w.today.forEach(n => tt.appendChild(nodeEl(n, 0, 'today')));
  pane.appendChild(tt);
  setRoving(pane, w.selected);
  return pane;
}
function setRoving(pane, selId) {
  $$('.tree', pane).forEach(tree => {
    const items = $$('[role=treeitem]', tree);
    items.forEach(i => i.tabIndex = -1);
    const first = items.find(i => i.dataset.id === selId) || items[0];
    if (first) first.tabIndex = 0;
  });
  const tiles = $$('.tile', pane); tiles.forEach(t => t.tabIndex = -1);
  const ft = tiles.find(t => t.dataset.id === selId) || tiles[0]; if (ft) ft.tabIndex = 0;
}
const pane = () => $('.pane:not(.leaving)', scroll);

function render() {
  const old = pane(), active = document.activeElement, keep = old && old.contains(active) ? (active.dataset.id || null) : null, keepNew = active && active.id === 'newTab';
  const st = scroll.scrollTop, np = buildPane(S.ws);
  if (old) old.replaceWith(np); else scroll.appendChild(np);
  scroll.scrollTop = st;
  if (keep) { const e = $(`[data-id="${CSS.escape(keep)}"][role=treeitem], .tile[data-id="${CSS.escape(keep)}"]`, np); if (e) { setRove(e); e.focus({ preventScroll: true }); } }
  if (keepNew) $('#newTab').focus();
  updateChrome(); renderFocusBadges();
}
function setRove(e) {
  const tree = e.closest('.tree, .favs'); if (!tree) return;
  $$('[tabindex="0"]', tree).forEach(x => { if (x !== e && x.dataset.id) x.tabIndex = -1; }); e.tabIndex = 0;
}

function renderSwitcher() {
  const sw = $('#wsw'); sw.textContent = '';
  /* One Workspace: no dots, only "+ New workspace" (Zen #4256 shows the switcher with one Space). */
  const single = WS_ORDER.length < 2; sw.hidden = single; $('#bottom').dataset.single = single ? '1' : '0'; $('#btnNewWs').classList.toggle('wide', single);
  WS_ORDER.forEach((id, i) => {
    const w = WS[id], b = el('button', 'ws-btn', { role: 'radio', 'aria-checked': id === S.ws, 'aria-label': w.name, tabindex: id === S.ws ? 0 : -1 });
    b.dataset.ws = id; b.dataset.tip = w.name; b.dataset.keys = i < 9 ? `Ctrl+Shift+${i + 1}` : ''; b.appendChild(icon(w.icon)); sw.appendChild(b);
  });
}
function updateChrome() {
  const t = curTab(); if (!t) return;
  const addr = $('#addr'); if (document.activeElement !== addr) addr.value = t.domain;
  addr.dataset.full = 'https://' + t.domain + t.path;
  $('#guardCount').textContent = t.blocked;
  const g = $('#btnGuard'); g.setAttribute('aria-label', `Guard: ${t.blocked} blocked on this page`); g.dataset.tip = `Guard: ${t.blocked} blocked`;
  const tg = $('#btnToggle'); tg.setAttribute('aria-label', S.hidden ? 'Show sidebar' : 'Hide sidebar'); tg.dataset.tip = S.hidden ? 'Show sidebar' : 'Hide sidebar';
  $$('.ws-btn').forEach(b => { const on = b.dataset.ws === S.ws; b.setAttribute('aria-checked', on); b.tabIndex = on ? 0 : -1; });
}

/* ---------- fake page ---------- */
function renderPage(animate) {
  const t = curTab(); if (!t) { card.textContent = ''; return; }
  const p = el('div', 'page'); p.style.setProperty('--c', t.color);
  const nav = el('div', 'pg-nav'); const lg = el('span', 'pg-logo'); lg.textContent = t.title[0].toUpperCase();
  const site = el('span', 'pg-site'); site.textContent = t.domain; nav.append(lg, site);
  for (let i = 0; i < 3; i++) { const c = el('span', 'pg-chip'); c.style.width = (40 + (hash(t.id + i) % 40)) + 'px'; nav.appendChild(c); }
  const btn = el('span', 'pg-btn'); btn.textContent = 'Open'; nav.appendChild(btn); p.appendChild(nav);
  const hero = el('div', 'pg-hero'); const k = el('div', 'pg-kicker'); k.textContent = t.domain + t.path; hero.appendChild(k);
  const h1 = el('h1'); h1.textContent = t.title; hero.appendChild(h1);
  const lines = el('div', 'pg-lines'); [78, 64, 70, 42].forEach((w, i) => { const l = el('i'); l.style.width = (w - (hash(t.id + i) % 14)) + '%'; l.style.maxWidth = '620px'; lines.appendChild(l); });
  hero.appendChild(lines); p.appendChild(hero);
  const grid = el('div', 'pg-grid');
  for (let i = 0; i < 3; i++) { const c = el('div', 'pg-card'); const th = el('div', 'th'); th.style.setProperty('--k', (10 + hash(t.id + 'k' + i) % 26) + '%'); c.appendChild(th); [90, 70, 50].forEach(w => { const l = el('i'); l.style.width = w + '%'; c.appendChild(l); }); grid.appendChild(c); }
  p.appendChild(grid);
  const rows = el('div', 'pg-rows'); [96, 88, 92, 60].forEach(w => { const l = el('i'); l.style.width = w + '%'; rows.appendChild(l); }); p.appendChild(rows);
  card.textContent = ''; card.appendChild(p);
  if (animate) fx(p, { o: 0.4 }, { o: 1 }, 'fast', 'standard', { role: 'in' });
}

/* ---------- tab selection ---------- */
function selectTab(id, { animate = true, focus = false } = {}) {
  const loc = locate(id); if (!loc) return;
  if (loc.ws !== S.ws) { W[loc.ws].selected = id; switchWs(loc.ws); return; }
  cur().selected = id;
  $$('.row.sel, .tile.sel', scroll).forEach(e => { e.classList.remove('sel'); if (e.matches('.row')) e.setAttribute('aria-selected', false); });
  const e = $(`[data-id="${CSS.escape(id)}"]`, pane());
  if (e) { e.classList.add('sel'); if (e.matches('.row')) e.setAttribute('aria-selected', true); setRove(e); if (focus) e.focus(); }
  if (tabs[id].asleep && false) tabs[id].asleep = false;
  updateChrome(); renderPage(animate);
}

/* ---------- workspace switching ---------- */
/* Maintainer decision: full-pane slide. The outgoing and incoming tab-list panes sit side by side as a strip and both
   translate by one pane width (direction from Workspace order, no wrap-around), spring ~250ms with no bounce. Only the two
   panes involved animate. Reduced motion: a short crossfade, no movement (fx() handles it). The frame/theme crossfade
   (crossfade() below, View Transitions) runs in parallel and is not part of this function.
   This is the ONLY place the switch motion is defined; to change the style, edit this function. */
const paneWidth = () => scroll.clientWidth;
function animateWorkspaceSwitch(old, np, dir, fromX = 0) {
  const w = paneWidth(), dur = 'workspace-switch', ease = 'spring-settle';
  if (old) fx(old, { x: fromX, o: 1 }, { x: -dir * w, o: 1 }, dur, ease, { role: 'out', fill: 'forwards' }).then(a => { if (a) a.cancel(); old.remove(); });
  fx(np, { x: fromX + dir * w, o: 1 }, { x: 0, o: 1 }, dur, ease, { role: 'in', fill: 'backwards' });
}
/* Returns a promise that resolves once the new pane is in the DOM. */
function switchWs(id, { fromOffset = 0, animate = true } = {}) {
  if (id === S.ws || !W[id]) return Promise.resolve();
  $$('.pane.leaving', scroll).forEach(p => p.remove());
  const dir = Math.sign(WS_ORDER.indexOf(id) - WS_ORDER.indexOf(S.ws)) || 1;
  const prevWs = S.ws; S.ws = id;
  const run = () => {
    applyLook();
    const old = $$('.pane:not(.leaving)', scroll).find(p => p.dataset.ws === prevWs) || $('.pane:not(.leaving)', scroll);
    const prevTop = scroll.scrollTop;
    const np = buildPane(id); scroll.appendChild(np);
    if (old) { old.classList.add('leaving'); old.style.top = prevTop + 'px'; old.querySelectorAll('[tabindex]').forEach(e => e.tabIndex = -1); }
    scroll.scrollTop = 0;
    const hadFocus = old && old.contains(document.activeElement);
    setPreset(WS[id].preset, animate);
    renderSwitcher(); updateChrome(); renderPage(animate); renderFocusBadges();
    if (animate && !S.instant) animateWorkspaceSwitch(old, np, dir, fromOffset);
    else if (old) old.remove();
    if (hadFocus) { const t = $('[tabindex="0"][role=treeitem], .tile[tabindex="0"]', np); if (t) t.focus({ preventScroll: true }); }
    $('#wsw .ws-btn[aria-checked=true]')?.setAttribute('tabindex', 0);
    syncControls();
  };
  return crossfade(effectiveLook(id), run);
}

/* Horizontal swipe (trackpad or Shift+wheel). The strip follows the gesture 1:1: the current pane and the neighbouring
   pane (built on demand) move together. Past the first or last Workspace only the current pane moves, with rubber-banding
   x * (1 - |x| / (4.5 * width)) (Zen). On release it commits when the distance passes motion-distance-swipe-commit or the
   speed passes motion-ratio-swipe-velocity, otherwise both panes spring back. Reduced motion: nothing moves while swiping;
   a committed swipe crossfades. Mouse back/forward buttons never reach this code. */
const rubber = (x, w) => x * (1 - Math.min(1, Math.abs(x) / (tnum('motion-ratio-rubber-band') * w)));
const swipe = { on: false, acc: 0, samples: [], nb: null, nbWs: null, timer: null };
function swipeShown(w) { // pane offset for the current accumulated gesture, and its target Workspace
  const idx = WS_ORDER.indexOf(S.ws), sg = Math.sign(swipe.acc), target = WS_ORDER[idx + sg] || null;
  return { sg, target, x: target ? -Math.max(-w, Math.min(w, swipe.acc)) : -rubber(swipe.acc, w) };
}
function swipeRender(p, w) {
  const { sg, target, x } = swipeShown(w);
  p.getAnimations().forEach(a => a.cancel()); p.style.transform = `translateX(${x}px)`;
  if (swipe.nb && swipe.nbWs !== target) { swipe.nb.remove(); swipe.nb = null; swipe.nbWs = null; }
  if (target && !swipe.nb) {
    swipe.nb = buildPane(target); swipe.nbWs = target; swipe.nb.classList.add('leaving', 'preview');
    swipe.nb.setAttribute('aria-hidden', 'true'); swipe.nb.querySelectorAll('[tabindex]').forEach(e => e.tabIndex = -1);
    swipe.nb.style.top = scroll.scrollTop + 'px'; scroll.appendChild(swipe.nb);
  }
  if (swipe.nb) swipe.nb.style.transform = `translateX(${x + sg * w}px)`;
}
function swipeEnd() {
  const p = pane(), w = paneWidth(); swipe.on = false; if (!p) return;
  const { sg, target, x } = swipeShown(w), acc = swipe.acc, sm = swipe.samples;
  const span = Math.max(16, sm.length > 1 ? sm[sm.length - 1].t - sm[0].t : 16), v = sm.length > 1 ? sm.reduce((a, s) => a + Math.abs(s.d), 0) / span : 0; // speed needs at least two samples
  const commit = target && (Math.abs(acc) >= tnum('motion-distance-swipe-commit') || (v >= tnum('motion-ratio-swipe-velocity') && Math.abs(acc) >= 20));
  const nb = swipe.nb; swipe.nb = null; swipe.nbWs = null; swipe.acc = 0; swipe.samples = [];
  const red = isReduced();
  p.style.transform = '';
  if (commit) { if (nb) nb.remove(); switchWs(target, { fromOffset: red ? 0 : x }); return; }
  if (nb) { if (red) nb.remove(); else { nb.style.transform = ''; fx(nb, { x: x + sg * w, o: 1 }, { x: sg * w, o: 1 }, 'workspace-switch', 'spring-settle', { role: 'move', fill: 'forwards' }).then(() => nb.remove()); } }
  if (!red) fx(p, { x, o: 1 }, { x: 0, o: 1 }, 'workspace-switch', 'spring-settle', { role: 'move' });
}
function onWheel(e) {
  if (S.hidden && !S.peek) return;
  const d = e.shiftKey && Math.abs(e.deltaX) < 1 ? e.deltaY : e.deltaX;
  if (Math.abs(d) < Math.abs(e.deltaY) && !e.shiftKey) return;
  if (!d) return;
  e.preventDefault();
  const p = pane(); if (!p) return;
  if (!swipe.on) { swipe.on = true; swipe.acc = 0; swipe.samples = []; $$('.pane.leaving', scroll).forEach(q => q.remove()); }
  swipe.acc += d; swipe.samples.push({ t: performance.now(), d }); swipe.samples = swipe.samples.filter(s => swipe.samples[swipe.samples.length - 1].t - s.t <= 100);
  if (!isReduced()) swipeRender(p, paneWidth());
  clearTimeout(swipe.timer); swipe.timer = setTimeout(swipeEnd, 120);
}

/* ---------- sidebar show / hide ---------- */
function setHidden(h, { animate = true } = {}) {
  if (S.hidden === h && !S.peek) return;
  const was = S.hidden, Wd = sbWidth(), dir = dirOut();
  Peek.reset();
  S.hidden = h; S.peek = false; sidebar.classList.remove('peeking');
  win.dataset.sidebar = h ? 'hidden' : 'shown';
  if (S.density === 'autohide' && !h) S.density = 'comfortable', win.dataset.density = 'comfortable', syncControls();
  sidebar.inert = h;
  if (animate && was !== h) {
    if (h) { sidebar.inert = false; fx(sidebar, { x: 0 }, { x: dir * (Wd + 24) }, 'base', 'accelerate', { role: 'out', fill: 'forwards' }).then(a => { if (a) a.cancel(); sidebar.inert = true; });
      fx(card, { x: -dir * Wd }, { x: 0 }, 'base', 'accelerate', { role: 'move' }); }
    else { fx(sidebar, { x: dir * (Wd + 24) }, { x: 0 }, 'slow', 'decelerate', { role: 'in' }); fx(card, { x: dir * Wd }, { x: 0 }, 'slow', 'decelerate', { role: 'move' }); }
  }
  updateChrome(); renderFocusBadges();
}

/* ---------- Peek (auto-hide) state machine ----------
   One explicit machine, no loose attributes plus timers (Zen's compact mode is a pile of attributes and timers, which is where
   its shaking / stuck-open / early-retract bugs come from: #12856, #8643, #15995). Only this module touches the peek state.

   States
     hidden    sidebar off-screen and inert. Only the edge hot zone listens.
     arming    pointer is inside the hot zone, hover-in delay running (motion-timing-peek-delay).
     showing   slide-in running, spring curve, motion-duration-peek-show (250ms). Leave events are remembered, not acted on.
     shown     fully in. Pointer tracking is active.
     leaving   pointer left; keep-open timer running (motion-timing-peek-leave, 150ms). Re-entering cancels it.
     hiding    retract running, accelerate, motion-duration-peek-hide (150ms). Re-entering reverses it from its current offset.
     pinned    "keep peek open" (Ctrl+Alt+S). No hover logic at all until toggled off or the sidebar is docked with Ctrl+S.

   Transitions
     hidden  -> arming    pointer enters the hot zone
     hidden  -> showing   keyboard (F6) or Ctrl+Alt+S
     arming  -> hidden    pointer leaves the hot zone before the delay ends, or leaves the window
     arming  -> showing   delay elapsed, pointer still in the hot zone
     showing -> shown     slide-in finished (then re-evaluate where the pointer is; this is how a leave during the slide is honoured)
     showing -> pinned    slide-in finished and "keep open" is on
     shown   -> leaving   pointer outside the sidebar footprint and not busy (see below), or left the window beyond the grace distance
     leaving -> shown     pointer re-enters the footprint, or at expiry it is back inside / busy / within the grace distance
     leaving -> hiding    keep-open timer expired, pointer still away, nothing busy
     hiding  -> hidden    retract finished
     hiding  -> showing   pointer re-enters (reverses from the current offset, no jump)
     shown/leaving -> pinned, pinned -> shown   Ctrl+Alt+S

   Never hides while busy: a popover or menu or submenu is open, a tab is being dragged, a rename is active, the command bar is
   open, the sidebar is being resized, the address field has focus, or it was opened with the keyboard (F6) and focus is still inside it. A leave seen while busy is remembered
   (pendingLeave) and re-evaluated by poke() when the thing that made it busy ends.

   Grace: when the pointer leaves the window it stays open while within motion-distance-peek-grace-x (200px) horizontally and
   motion-distance-peek-grace-y (100px) vertically of the sidebar. A browser mockup only sees the exit point (events stop at the
   viewport edge), so the exit point stands in for the OS cursor position and motion-timing-peek-outside-max ends the grace.
   Tests can call window.__peek.simulateOutside(x, y) with coordinates beyond the viewport.

   How the three Zen bugs are avoided
     Shaking on edge hover: the pointer is tested against a LAYOUT footprint (window edge to sidebar width + inset, full height),
       never the animated element's rect, so the sidebar sliding under a stationary pointer cannot flip inside/outside. Hot zone
       (6px) only decides arming; once arming starts, the footprint takes over (hysteresis). The delay and keep-open timers
       swallow boundary jitter, and `leaving` -> `shown` costs nothing visually.
     Stuck open: every timer is owned by the machine and cleared on every transition; "busy" always ends with a poke(); a window
       exit has its own expiry; a pointermove outside while shown re-evaluates every time. Ctrl+S docking resets the machine.
     Early retract: leave during `showing` is deferred to `shown`; the keep-open timer covers the gap between the hot zone,
       the 8px inset and the sidebar; busy states and the grace distance stop it reaching for a close button or popover; a
       reversing `hiding` starts from the current offset. */
const Peek = (() => {
  let state = 'hidden', timer = null, otimer = null, pt = null, outside = null, pendingLeave = false, pin = false, holdKbd = false, t0 = performance.now();
  const log = [];
  const go = (to, why) => { if (to === state) return; log.push({ t: Math.round(performance.now() - t0), from: state, to, why }); state = to; win.dataset.peekState = to; S.peek = !(to === 'hidden' || to === 'arming'); };
  const clear = () => { clearTimeout(timer); timer = null; };
  const after = (ms, fn) => { clear(); timer = setTimeout(fn, ms); };
  const inset = () => parseFloat(getComputedStyle(win).getPropertyValue('--inset')) || 8;
  const box = () => { const r = win.getBoundingClientRect(), w = sbWidth() + inset() * 2, left = S.side === 'left'; return { l: left ? r.left : r.right - w, r: left ? r.left + w : r.right, t: r.top, b: r.bottom }; };
  const hot = () => { const r = win.getBoundingClientRect(), h = tnum('size-hot-zone'), left = S.side === 'left'; return { l: left ? r.left : r.right - h, r: left ? r.left + h : r.right, t: r.top, b: r.bottom }; };
  const inBox = (b, x, y) => x >= b.l && x <= b.r && y >= b.t && y <= b.b;
  const zone = (x, y) => (state === 'hidden' || state === 'arming') ? (inBox(hot(), x, y) ? 'hot' : 'out') : (inBox(box(), x, y) ? 'in' : 'out');
  const inGrace = (x, y) => { const b = box(), dx = Math.max(0, b.l - x, x - b.r), dy = Math.max(0, b.t - y, y - b.b); return dx <= tnum('motion-distance-peek-grace-x') && dy <= tnum('motion-distance-peek-grace-y'); };
  const away = () => outside ? !inGrace(outside.x, outside.y) : (pt ? zone(pt.x, pt.y) === 'out' : true);
  const busy = () => !!($$('.pop:not([hidden]), .menu:not([hidden])').length || S.drag || $('.rename', sidebar) || cmdOpen || $('#resizer').classList.contains('active') || holdKbd || document.activeElement === $('#addr'));
  const offX = () => dirOut() * (sbWidth() + 24);

  function startShow(why) {
    let from = offX();
    if (state === 'hiding') { try { from = new DOMMatrix(getComputedStyle(sidebar).transform).m41; } catch (e) { /* keep default */ } }
    sidebar.getAnimations().forEach(a => a.cancel());
    sidebar.inert = false; sidebar.classList.add('peeking'); go('showing', why);
    fx(sidebar, { x: from }, { x: 0 }, 'peek-show', 'spring', { role: 'in' });
    after(S.instant ? 0 : durMs('peek-show'), settle);
  }
  function settle() { go(pin ? 'pinned' : 'shown', 'settled'); evaluate(); renderFocusBadges(); }
  function evaluate() {
    if (state !== 'shown') return;
    clearTimeout(otimer);
    if (!away()) { pendingLeave = false; if (outside) otimer = setTimeout(() => { if (state === 'shown' && outside) beginLeave('outside-timeout'); }, tnum('motion-timing-peek-outside-max')); return; }
    if (busy()) { pendingLeave = true; return; }
    beginLeave('pointer-away');
  }
  function beginLeave(why) {
    pendingLeave = false; go('leaving', why);
    after(tnum('motion-timing-peek-leave'), () => {
      if (busy()) { go('shown', 'busy'); pendingLeave = true; return; }
      if (!away()) { go('shown', 'returned'); evaluate(); return; }
      startHide();
    });
  }
  function startHide() {
    go('hiding', 'keep-open-expired');
    fx(sidebar, { x: 0 }, { x: offX() }, 'peek-hide', 'accelerate', { role: 'out', fill: 'forwards' });
    after(S.instant ? 0 : durMs('peek-hide'), () => { sidebar.getAnimations().forEach(a => a.cancel()); sidebar.classList.remove('peeking'); sidebar.inert = true; go('hidden', 'retracted'); renderFocusBadges(); });
  }
  function onPointer(x, y) {
    pt = { x, y }; outside = null; clearTimeout(otimer);
    const z = zone(x, y); if (z === 'in') holdKbd = false;
    switch (state) {
      case 'hidden': if (z === 'hot' && S.hidden) { go('arming', 'enter-hot-zone'); after(tnum('motion-timing-peek-delay'), () => { if (pt && zone(pt.x, pt.y) === 'hot') startShow('dwell'); else go('hidden', 'left-before-delay'); }); } break;
      case 'arming': if (z !== 'hot') { clear(); go('hidden', 'left-hot-zone'); } break;
      case 'shown': if (z === 'out') evaluate(); else pendingLeave = false; break;
      case 'leaving': if (z !== 'out') { clear(); go('shown', 're-enter'); } break;
      case 'hiding': if (z !== 'out') startShow('re-enter'); break;
      default: break; // showing: remembered in pt, honoured at settle(); pinned: no hover logic
    }
  }
  function onWindowLeave(x, y) {
    outside = { x, y };
    if (state === 'arming') { clear(); go('hidden', 'left-window'); } else if (state === 'shown') evaluate();
  }
  function togglePin() {
    if (!S.hidden) { toast('Sidebar is always visible in this density'); return; }
    if (pin) {
      pin = false; toast('Sidebar auto-hides again');
      if (state === 'pinned') { go('shown', 'unpin'); evaluate(); }
    } else {
      pin = true; toast('Sidebar kept open (Ctrl+Alt+S to undo)');
      if (state === 'hidden' || state === 'arming' || state === 'hiding') { clear(); startShow('pin'); }
      else if (state === 'shown' || state === 'leaving') { clear(); go('pinned', 'pin'); }
    }
  }
  function reset() { clear(); clearTimeout(otimer); pin = false; holdKbd = false; pendingLeave = false; outside = null; if (state !== 'hidden') go('hidden', 'reset'); S.peek = false; }
  function forceShown() { sidebar.inert = false; sidebar.classList.add('peeking'); go('shown', 'init'); }
  document.addEventListener('pointermove', e => onPointer(e.clientX, e.clientY));
  document.addEventListener('pointerdown', e => onPointer(e.clientX, e.clientY));
  root.addEventListener('mouseleave', e => onWindowLeave(e.clientX, e.clientY));
  sidebar.addEventListener('focusout', () => { holdKbd = false; setTimeout(evaluate, 0); });
  window.__peek = { log, get state() { return state; }, simulateOutside: onWindowLeave, simulatePointer: onPointer };
  return { show: why => { if (why === 'keyboard') holdKbd = true; if (state === 'hidden' || state === 'arming' || state === 'hiding') { clear(); startShow(why); } }, togglePin, reset, forceShown, poke: evaluate, get state() { return state; }, get pinned() { return pin; } };
})();

/* toast: small status message, top centre */
let toastSeq = 0;
function toast(msg) {
  const t = $('#toast'), m = $('span', t), my = ++toastSeq; m.textContent = msg; t.hidden = false;
  m.getAnimations().forEach(a => a.cancel()); fx(m, { y: 6, o: 0 }, { y: 0, o: 1 }, 'fast', 'decelerate', { role: 'in' });
  setTimeout(() => { if (my !== toastSeq) return; fx(m, { o: 1 }, { o: 0 }, 'fast', 'accelerate', { role: 'out', fill: 'forwards' }).then(a => { if (my !== toastSeq) return; if (a) a.cancel(); t.hidden = true; }); if (S.instant) t.hidden = true; }, tnum('motion-timing-toast'));
}
const toggleSidebar = () => { if (S.hidden) setHidden(false); else setHidden(true); };

/* resizer */
$('#resizer').addEventListener('pointerdown', e => {
  if (S.density === 'collapsed') return;
  const r = $('#resizer'), x0 = e.clientX, w0 = sbWidth(), sgn = S.side === 'left' ? 1 : -1;
  r.setPointerCapture(e.pointerId); r.classList.add('active');
  const mv = ev => { S.sbW = Math.max(tnum('size-sidebar-min'), Math.min(tnum('size-sidebar-max'), w0 + sgn * (ev.clientX - x0))); win.style.setProperty('--sb-w', S.sbW + 'px'); };
  const up = () => { r.classList.remove('active'); Peek.poke(); r.removeEventListener('pointermove', mv); r.removeEventListener('pointerup', up); renderFocusBadges(); };
  r.addEventListener('pointermove', mv); r.addEventListener('pointerup', up);
});
$('#resizer').addEventListener('dblclick', () => { S.sbW = null; win.style.removeProperty('--sb-w'); });

/* ---------- tab operations ---------- */
function newTabData(text) {
  const id = 'n' + (nextId++), isUrl = /^[\w-]+(\.[\w-]+)+(\/.*)?$/.test(text.trim());
  const colors = ['#E5484D', '#F76B15', '#30A46C', '#12A594', '#0091FF', '#6E56CF', '#D6409F'];
  const c = colors[hash(text || id) % colors.length];
  if (!text.trim()) mk(id, 'New Tab', 'aerie://newtab', '', c);
  else if (isUrl) { const [d, ...p] = text.trim().split('/'); mk(id, d.replace(/^www\./, ''), d, '/' + p.join('/'), c); }
  else mk(id, text.trim()[0].toUpperCase() + text.trim().slice(1), 'search.aerie.example', '/?q=' + encodeURIComponent(text.trim()), c);
  return id;
}
async function openTab(text = '') {
  const id = newTabData(text);
  cur().today.unshift({ t: id }); cur().selected = id;
  render(); updateChrome(); renderPage(true);
  const node = $(`.node[data-id="t:${id}"]`, pane()); if (!node) return id;
  const h = node.getBoundingClientRect().height;
  heightFx(node, 0, h, 'base', 'standard');
  fx($('.row-in', node), { s: tnum('motion-scale-enter'), o: 0 }, { s: 1, o: 1 }, 'fast', 'decelerate', { role: 'in', delay: tnum('motion-timing-enter-delay'), fill: 'backwards' });
  return id;
}
function neighbourOf(id) {
  const items = $$('[role=treeitem]:not([data-folder])', pane()).filter(e => !e.closest('.children[hidden]'));
  const i = items.findIndex(e => e.dataset.id === id);
  const n = items[i + 1] || items[i - 1]; return n ? n.dataset.id : null;
}
/* Pinned tab close (Ctrl+W or x). Mockup control `pinnedclose` (a future setting):
   reset-sleep-switch (default) resets to the pinned URL, unloads (asleep) and selects the next tab;
   sleep unloads and selects the next tab but keeps the current URL; close just closes it (to the archive).
   In the first two modes a second press on a pinned tab that is already asleep (and, in reset mode, at its pinned URL) unpins and closes it. */
let pinnedCloseMode = ['reset-sleep-switch', 'sleep', 'close'].includes(params.get('pinnedclose')) ? params.get('pinnedclose') : 'reset-sleep-switch';
function pinnedCloseAction(id) {
  const t = tabs[id];
  if (pinnedCloseMode === 'close') return 'close';
  if (t.asleep && (pinnedCloseMode === 'sleep' || !t.pinnedPath || t.path === t.pinnedPath)) return 'close';
  return pinnedCloseMode === 'sleep' ? 'sleep' : 'reset-sleep';
}
const pinnedCloseLabel = id => ({ close: 'Close', sleep: 'Sleep', 'reset-sleep': 'Reset and sleep' })[pinnedCloseAction(id)];
async function closeTab(id, { toWorkspace = null } = {}) {
  const loc = locate(id, S.ws); if (!loc) return;
  const t = tabs[id];
  if (loc.area === 'pinned' && !toWorkspace && pinnedCloseAction(id) !== 'close') {
    const act = pinnedCloseAction(id), wasSel0 = cur().selected === id, nb0 = wasSel0 ? neighbourOf(id) : null;
    t.asleep = true; t.audio = false; if (act === 'reset-sleep' && t.pinnedPath) t.path = t.pinnedPath;
    if (nb0) { cur().selected = nb0; tabs[nb0].asleep = false; }
    render(); if (nb0) { updateChrome(); renderPage(true); }
    return;
  }
  const node = $(`.node[data-id="t:${id}"]`, pane()) || $(`.tile[data-id="${CSS.escape(id)}"]`, pane());
  const wasSel = cur().selected === id, nb = wasSel ? neighbourOf(id) : null;
  if (node && !S.instant && loc.area !== 'fav') {
    const inn = $('.row-in', node), h = node.getBoundingClientRect().height;
    await fx(inn, { x: 0, o: 1 }, { x: dirOut() * tnum('motion-distance-close-slide'), o: 0 }, 'fast', 'accelerate', { role: 'out', fill: 'forwards' });
    await heightFx(node, h, 0, 'base', 'standard', { fill: 'forwards' });
  }
  loc.list.splice(loc.list.indexOf(loc.list.find(n => n.t === id || n === id)), 1);
  if (toWorkspace) W[toWorkspace].today.unshift({ t: id }); else archive.unshift({ id, title: t.title, domain: t.domain, color: t.color, ws: WS[S.ws].name, when: 'Just now' });
  if (wasSel) cur().selected = nb || (cur().today[0] && cur().today[0].t) || null;
  render(); if (wasSel) renderPage(true);
}
function mutate(fn, movedIds = [], { spring = true } = {}) {
  const before = new Map($$('.node', pane()).map(n => [n.dataset.id, n.getBoundingClientRect()]));
  const tilesBefore = new Map($$('.tile', pane()).map(n => [n.dataset.id, n.getBoundingClientRect()]));
  fn(); render();
  if (S.instant) return;
  const cb = isReduced();
  const doFlip = (e, b, mv) => {
    const a = e.getBoundingClientRect(), dx = b.left - a.left, dy = b.top - a.top; if (!dx && !dy) return;
    const tgt = e.matches('.node') ? ($('.row-in', e) || e) : e;
    fx(tgt, { x: dx, y: dy }, { x: 0, y: 0 }, mv ? 'spring' : 'base', mv ? 'spring' : 'standard', { role: 'move' });
  };
  $$('.node', pane()).forEach(n => { const b = before.get(n.dataset.id); if (b) { if (n.dataset.id.startsWith('t:') ) doFlip(n, b, movedIds.includes(n.dataset.id)); else if (n.dataset.id) { /* folders: skip */ } } });
  $$('.tile', pane()).forEach(n => { const b = tilesBefore.get(n.dataset.id); if (b) doFlip(n, b, false); });
  if (cb) return;
  movedIds.forEach(id => { const n = $(`.node[data-id="${CSS.escape(id)}"]`, pane()); const b = before.get(id); if (n && b) { /* moved nodes already flipped with spring */ } });
}
function pinTab(id) {
  const loc = locate(id, S.ws); if (!loc) return;
  mutate(() => {
    const [n] = loc.list.splice(loc.list.findIndex(x => x.t === id || x === id), 1);
    const node = typeof n === 'string' ? { t: n } : n;
    if (loc.area === 'pinned') cur().today.unshift(node);
    else { const t = tabs[id]; if (!t.pinnedPath) t.pinnedPath = t.path; cur().pinned.push(node); }
  }, ['t:' + id]);
}
function addFavorite(id) {
  const loc = locate(id, S.ws); if (!loc || loc.area === 'fav') return;
  mutate(() => { loc.list.splice(loc.list.findIndex(x => x.t === id), 1); cur().favorites.push(id); });
}
function removeFavorite(id) {
  mutate(() => { const l = cur().favorites; l.splice(l.indexOf(id), 1); cur().today.unshift({ t: id }); });
}
function sleepNow(id) { tabs[id].asleep = true; if (tabs[id].audio) tabs[id].audio = false; render(); }
function startRename(id) {
  const row = $(`[role=treeitem][data-id="${CSS.escape(id)}"]`, pane()); if (!row) return;
  const title = $('.title', row), isF = !!row.dataset.folder, f = isF ? locate(id).n : null, old = isF ? f.name : tabs[id].title;
  const inp = el('input', 'rename', { 'aria-label': 'Rename', value: old }); inp.value = old; title.replaceWith(inp); inp.focus(); inp.select();
  let done = false;
  const fin = ok => { if (done) return; done = true; setTimeout(() => Peek.poke(), 0); if (ok && inp.value.trim()) { if (isF) f.name = inp.value.trim(); else tabs[id].title = inp.value.trim(); } render(); const r = $(`[role=treeitem][data-id="${CSS.escape(id)}"]`, pane()); if (r) r.focus(); if (!isF && cur().selected === id) renderPage(false); };
  inp.addEventListener('keydown', e => { e.stopPropagation(); if (e.key === 'Enter') fin(true); if (e.key === 'Escape') fin(false); });
  inp.addEventListener('blur', () => fin(true));
}
async function clearToday() {
  const ids = cur().today.filter(n => n.t && n.t !== cur().selected).map(n => n.t);
  for (let i = 0; i < ids.length; i++) { closeTab(ids[i]); await new Promise(r => setTimeout(r, S.instant ? 0 : 60)); }
}
/* Folder collapse: opacity + height, motion-duration-folder-collapse (180ms) with motion-easing-in-out (Zen). Reveal: folder-reveal (200ms).
   Reduced motion: opacity only, height snaps. */
function folderFx(node, from, to, open) {
  node.getAnimations().forEach(a => a.cancel());
  if (S.instant) return Promise.resolve(null);
  const kf = isReduced() ? [{ opacity: open ? 0 : 1 }, { opacity: open ? 1 : 0 }] : [{ height: from + 'px', opacity: open ? 0 : 1 }, { height: to + 'px', opacity: open ? 1 : 0 }];
  const a = node.animate(kf, { duration: durMs(open ? 'folder-reveal' : 'folder-collapse'), easing: tok('motion-easing-in-out'), fill: open ? 'none' : 'forwards' });
  return a.finished.then(() => a, () => a);
}
function toggleFolder(id, open) {
  const loc = locate(id, S.ws); if (!loc) return; const f = loc.n;
  open = open === undefined ? !f.open : open; if (open === f.open) return; f.open = open;
  const row = $(`.row[data-id="${CSS.escape(id)}"]`, pane()), node = row.parentElement, kids = $('.children', node);
  row.classList.toggle('open', open); row.setAttribute('aria-expanded', open);
  fx($('.chev .ic', row), { r: open ? 0 : 90 }, { r: open ? 90 : 0 }, 'fast', 'standard', { role: 'move' });
  if (open) { const h0 = kids.hidden ? 0 : kids.getBoundingClientRect().height; kids.hidden = false; folderFx(kids, h0, kids.scrollHeight, true); }
  else {
    const h = kids.getBoundingClientRect().height;
    if (S.instant) kids.hidden = true; else folderFx(kids, h, 0, false).then(a => { if (!f.open) kids.hidden = true; if (a) a.cancel(); });
  }
}

/* ---------- click / key handling in the sidebar ---------- */
let suppressClick = false;
sidebar.addEventListener('click', e => {
  if (suppressClick) { suppressClick = false; return; }
  const act = e.target.closest('[data-act]');
  const row = e.target.closest('[role=treeitem]'), tile = e.target.closest('.tile');
  if (act && row) {
    e.stopPropagation();
    if (act.dataset.act === 'close') closeTab(row.dataset.id);
    if (act.dataset.act === 'back-pinned') { const t = tabs[row.dataset.id]; t.path = t.pinnedPath; render(); if (cur().selected === row.dataset.id) { updateChrome(); renderPage(true); } }
    return;
  }
  if (act && act.dataset.act === 'clear') return clearToday();
  if (row) { if (row.dataset.folder) toggleFolder(row.dataset.id); else { wake(row.dataset.id); selectTab(row.dataset.id); } return; }
  if (tile) { wake(tile.dataset.id); selectTab(tile.dataset.id); return; }
  if (e.target.closest('#newTab')) return openCmd();
  const ws = e.target.closest('.ws-btn'); if (ws) return switchWs(ws.dataset.ws);
});
function wake(id) { const t = tabs[id]; if (t && t.asleep) { t.asleep = false; render(); } }
sidebar.addEventListener('auxclick', e => {
  if (e.button !== 1) return; const row = e.target.closest('[role=treeitem]:not([data-folder])'), tile = e.target.closest('.tile');
  if (row) { e.preventDefault(); closeTab(row.dataset.id); } else if (tile) e.preventDefault();
});
sidebar.addEventListener('mousedown', e => { if (e.button === 1) e.preventDefault(); });
sidebar.addEventListener('contextmenu', e => {
  const r = e.target.closest('[role=treeitem]:not([data-folder]), .tile'); if (!r) return;
  e.preventDefault(); openMenu(r.dataset.id, e.clientX, e.clientY);
});
sidebar.addEventListener('wheel', onWheel, { passive: false });
$('#btnNewWs').addEventListener('click', () => {
  const n = Object.keys(WS).length + 1, id = 'ws' + n, preset = PRESET_CYCLE[(((n - 4) % PRESET_CYCLE.length) + PRESET_CYCLE.length) % PRESET_CYCLE.length];
  WS[id] = { name: 'Workspace ' + n, preset, icon: 'i-ws-wave' }; W[id] = { favorites: [], pinned: [], today: [], selected: null };
  WS_ORDER.push(id); renderSwitcher(); switchWs(id);
});
$('#btnToggle').addEventListener('click', toggleSidebar);
/* Mouse back/forward buttons (3 and 4) and Alt+Left/Right are page back/forward. They never switch Workspace (Zen #12491). */
function pageNav(dir) { const b = $(dir < 0 ? '#btnBack' : '#btnFwd'); b.classList.add('kick'); setTimeout(() => b.classList.remove('kick'), tnum('motion-duration-fast')); win.dataset.lastNav = dir < 0 ? 'back' : 'forward'; }
$('#btnBack').addEventListener('click', () => pageNav(-1)); $('#btnFwd').addEventListener('click', () => pageNav(1));
document.addEventListener('mousedown', e => { if (e.button === 3 || e.button === 4) e.preventDefault(); });
document.addEventListener('mouseup', e => { if (e.button === 3 || e.button === 4) { e.preventDefault(); pageNav(e.button === 3 ? -1 : 1); } });
document.addEventListener('auxclick', e => { if (e.button === 3 || e.button === 4) e.preventDefault(); });
$('#btnReload').addEventListener('click', () => { const p = $('.page', card); if (p) fx(p, { o: 0.3 }, { o: 1 }, 'base', 'standard', { role: 'in' }); });
$('#wsw').addEventListener('keydown', e => {
  const i = WS_ORDER.indexOf(S.ws); let n = i;
  if (e.key === 'ArrowRight' || e.key === 'ArrowDown') n = Math.min(WS_ORDER.length - 1, i + 1);
  else if (e.key === 'ArrowLeft' || e.key === 'ArrowUp') n = Math.max(0, i - 1);
  else return;
  e.preventDefault(); switchWs(WS_ORDER[n]); $(`.ws-btn[data-ws="${WS_ORDER[n]}"]`).focus();
});

/* tree keyboard navigation */
const visibleItems = tree => $$('[role=treeitem]', tree).filter(e => !e.closest('.children[hidden]'));
scroll.addEventListener('keydown', e => {
  const t = e.target;
  if (t.matches('.tile')) return gridKey(e, t);
  const row = t.closest('[role=treeitem]'); if (!row || t !== row) return;
  const tree = row.closest('.tree'), items = visibleItems(tree), i = items.indexOf(row);
  const go = n => { if (n) { e.preventDefault(); setRove(n); n.focus(); } };
  const isF = !!row.dataset.folder;
  switch (e.key) {
    case 'ArrowDown': go(items[i + 1]); break;
    case 'ArrowUp': go(items[i - 1]); break;
    case 'Home': go(items[0]); break;
    case 'End': go(items[items.length - 1]); break;
    case 'ArrowRight': if (isF) { e.preventDefault(); if (row.getAttribute('aria-expanded') === 'false') toggleFolder(row.dataset.id, true); else go(items[i + 1]); } break;
    case 'ArrowLeft': if (isF && row.getAttribute('aria-expanded') === 'true') { e.preventDefault(); toggleFolder(row.dataset.id, false); } else { const p = row.closest('.children')?.parentElement.querySelector(':scope > [role=treeitem]'); go(p); } break;
    case 'Enter': case ' ': e.preventDefault(); if (isF) toggleFolder(row.dataset.id); else { wake(row.dataset.id); selectTab(row.dataset.id); } break;
    case 'F2': e.preventDefault(); startRename(row.dataset.id); break;
    case 'Delete': if (!isF) { e.preventDefault(); const nb = items[i + 1] || items[i - 1]; closeTab(row.dataset.id).then(() => { if (nb) { const n = $(`[role=treeitem][data-id="${CSS.escape(nb.dataset.id)}"]`, pane()); if (n) { setRove(n); n.focus(); } } }); } break;
    case 'ContextMenu': if (!isF) { e.preventDefault(); const r = row.getBoundingClientRect(); openMenu(row.dataset.id, r.left + 40, r.bottom - 4, true); } break;
  }
});
function gridKey(e, t) {
  const tiles = $$('.tile', pane()), i = tiles.indexOf(t), c = cols();
  const map = { ArrowRight: i + 1, ArrowLeft: i - 1, ArrowDown: i + c, ArrowUp: i - c, Home: 0, End: tiles.length - 1 };
  if (e.key in map) { e.preventDefault(); const n = tiles[map[e.key]]; if (n) { setRove(n); n.focus(); } }
  else if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); wake(t.dataset.id); selectTab(t.dataset.id); }
  else if (e.key === 'ContextMenu') { e.preventDefault(); const r = t.getBoundingClientRect(); openMenu(t.dataset.id, r.left + 20, r.bottom - 4, true); }
}

/* ---------- address pill ---------- */
const addr = $('#addr');
addr.addEventListener('focus', () => { addr.value = addr.dataset.full || ''; setTimeout(() => addr.select(), 0); });
addr.addEventListener('blur', () => { updateChrome(); setTimeout(() => Peek.poke(), 0); });
addr.addEventListener('keydown', e => {
  if (e.key === 'Enter') {
    const v = addr.value.trim(); if (!v) return; const t = curTab(); const m = v.replace(/^https?:\/\//, '').split('/');
    t.domain = m[0] || t.domain; t.path = '/' + m.slice(1).join('/'); addr.blur(); render(); renderPage(true);
  } else if (e.key === 'Escape') addr.blur();
});

/* ---------- popovers ---------- */
function placeNear(pop, rect, prefer = 'below') {
  pop.hidden = false; const pw = pop.offsetWidth, ph = pop.offsetHeight, vw = innerWidth, vh = innerHeight;
  let x, y;
  if (prefer === 'below') { x = rect.left + rect.width / 2 - pw / 2; y = rect.bottom + 8; }
  else if (prefer === 'above') { x = rect.left; y = rect.top - ph - 8; }
  else { x = S.side === 'left' ? rect.right + 8 : rect.left - pw - 8; y = rect.top; }
  pop.style.left = Math.max(8, Math.min(vw - pw - 8, x)) + 'px'; pop.style.top = Math.max(8, Math.min(vh - ph - 8, y)) + 'px';
}
function openPop(pop, btn, prefer, hostRect) {
  closePops(pop);
  placeNear(pop, hostRect || btn.getBoundingClientRect(), prefer); btn.setAttribute('aria-expanded', true);
  fx(pop, { y: -4, o: 0 }, { y: 0, o: 1 }, 'fast', 'decelerate', { role: 'in' });
}
function closePops(except) {
  [['#popGuard', '#btnGuard'], ['#popArchive', '#btnArchive']].forEach(([p, b]) => { const pe = $(p); if (pe !== except && !pe.hidden) { pe.hidden = true; $(b).setAttribute('aria-expanded', false); } });
  Peek.poke();
}
function openGuard() { const b = $('#btnGuard'); openPop($('#popGuard'), b, 'below', $('#pill').getBoundingClientRect()); }
function openArchive() {
  const l = $('#archiveList'); l.textContent = '';
  archive.slice(0, 6).forEach(a => { const r = el('div', 'it'); const f = el('span', 'fav'); f.style.setProperty('--c', a.color); f.textContent = a.title[0].toUpperCase();
    const t = el('span', 't'); t.textContent = a.title; const s = el('span', 's'); s.textContent = a.ws + ' · ' + a.when; r.append(f, t, s); l.appendChild(r); });
  const b = $('#btnArchive'), r = b.getBoundingClientRect();
  openPop($('#popArchive'), b, S.density === 'collapsed' ? 'side' : 'above', S.density === 'collapsed' ? r : r);
}
$('#btnGuard').addEventListener('click', e => { e.stopPropagation(); $('#popGuard').hidden ? openGuard() : closePops(); });
$('#btnArchive').addEventListener('click', e => { e.stopPropagation(); $('#popArchive').hidden ? openArchive() : closePops(); });
document.addEventListener('pointerdown', e => {
  if (!e.target.closest('.pop, #btnGuard, #btnArchive')) closePops();
  if (!e.target.closest('.menu')) closeMenu();
});

/* ---------- context menu ---------- */
let menuFor = null;
function openMenu(id, x, y, kbd = false) {
  closeMenu(); closePops(); menuFor = id;
  const loc = locate(id, S.ws), m = $('#menu'); m.textContent = '';
  const isFav = loc.area === 'fav', isPin = loc.area === 'pinned';
  const items = [
    [isPin ? 'Unpin' : 'Pin', () => isFav ? removeFavorite(id) : pinTab(id), 'Ctrl+Shift+P'],
    ['Rename', () => setTimeout(() => startRename(id), 0), 'F2'],
    [isFav ? 'Remove from Favorites' : 'Add to Favorites', () => isFav ? removeFavorite(id) : addFavorite(id)],
    ...(WS_ORDER.length > 1 ? [['Move to workspace', null, null, true]] : []),
    ['Sleep now', () => sleepNow(id)],
    ['-'],
    [isPin ? pinnedCloseLabel(id) : 'Close', () => closeTab(id), 'Ctrl+W', false, true],
  ];
  items.forEach(it => {
    if (it[0] === '-') return m.appendChild(el('div', 'msep'));
    const b = el('button', 'mi' + (it[4] ? ' danger' : ''), { role: 'menuitem', tabindex: -1 }); b.textContent = it[0];
    if (it[2]) { const k = el('span', 'k'); k.textContent = it[2]; b.appendChild(k); }
    if (it[3]) { b.appendChild(icon('i-chevron')); b.dataset.sub = '1'; b.setAttribute('aria-haspopup', 'menu'); }
    b.addEventListener('click', () => { if (it[3]) return openSub(b); closeMenu(); it[1] && it[1](); });
    b.addEventListener('pointerenter', () => { if (it[3]) openSub(b); else closeSub(); });
    m.appendChild(b);
  });
  m.hidden = false; const w = m.offsetWidth, h = m.offsetHeight;
  m.style.left = Math.min(x, innerWidth - w - 8) + 'px'; m.style.top = Math.min(y, innerHeight - h - 8) + 'px';
  fx(m, { s: .97, o: 0 }, { s: 1, o: 1 }, 'xfast', 'decelerate', { role: 'in' });
  if (kbd) $('.mi', m).focus();
}
function openSub(btn) {
  const s = $('#submenu'); s.textContent = ''; const id = menuFor;
  WS_ORDER.filter(w => w !== S.ws).forEach(w => {
    const b = el('button', 'mi', { role: 'menuitem', tabindex: -1 }); b.textContent = WS[w].name;
    b.addEventListener('click', () => { closeMenu(); closeTab(id, { toWorkspace: w }); }); s.appendChild(b);
  });
  s.hidden = false; const r = btn.getBoundingClientRect(), w = s.offsetWidth;
  s.style.left = (r.right + w + 12 < innerWidth ? r.right + 4 : r.left - w - 4) + 'px'; s.style.top = (r.top - 6) + 'px';
}
function closeSub() { $('#submenu').hidden = true; }
function closeMenu() { $('#menu').hidden = true; closeSub(); menuFor = null; Peek.poke(); }
$('#menu').addEventListener('keydown', e => {
  const its = $$('.mi', $('#menu')), i = its.indexOf(document.activeElement);
  if (e.key === 'ArrowDown') { e.preventDefault(); its[(i + 1) % its.length].focus(); }
  else if (e.key === 'ArrowUp') { e.preventDefault(); its[(i - 1 + its.length) % its.length].focus(); }
  else if (e.key === 'ArrowRight' && document.activeElement.dataset.sub) { openSub(document.activeElement); $('.mi', $('#submenu')).focus(); }
});
$('#submenu').addEventListener('keydown', e => { if (e.key === 'ArrowLeft') { closeSub(); $('[data-sub]', $('#menu')).focus(); } if (e.key === 'ArrowDown') { e.preventDefault(); const its = $$('.mi', $('#submenu')); its[(its.indexOf(document.activeElement) + 1) % its.length].focus(); } });

/* ---------- command bar ---------- */
let cmdItems = [], cmdSel = 0, cmdOpen = false, cmdReturn = null;
function cmdBuild(q) {
  q = q.trim().toLowerCase(); const out = [];
  const lab = k => WS[k].name + ' workspace';
  const fav = (t) => { const f = el('span', 'fav'); f.style.setProperty('--c', t.color); f.textContent = t.title[0].toUpperCase(); return f; };
  out.push({ g: 'Go', kind: 'new', title: q ? `Open "${q}"` : 'New tab', sub: q ? 'New tab' : 'Open a blank tab', act: true, hint: 'Enter', run: () => openTab(q ? $('#cmdInput').value.trim() : '') });
  WS_ORDER.filter(w => w !== S.ws).forEach(w => { const t = `Switch to ${WS[w].name} workspace`; if (!q || t.toLowerCase().includes(q)) out.push({ g: 'Workspaces', kind: 'sw', title: t, sub: 'Workspace', act: true, hint: 'Switch', run: () => switchWs(w) }); });
  const all = []; WS_ORDER.forEach(k => { const w = W[k]; [...w.favorites.map(id => tabs[id]), ...[...walk(w.pinned), ...walk(w.today)].filter(e => e.n.t).map(e => tabs[e.n.t])].forEach(t => all.push({ t, k })); });
  let tabList = all.filter(({ t }) => !q || (t.title + ' ' + t.domain).toLowerCase().includes(q));
  if (!q) tabList = tabList.filter(({ t, k }) => ['p-flights', 'w-pr', 'w-road', 's-88', 'w-inc'].includes(t.id));
  tabList.slice(0, 5).forEach(({ t, k }) => out.push({ g: 'Open tabs', kind: 'tab', title: t.title, sub: WS[k].name + ' · ' + t.domain, tab: t, hint: 'Switch to tab', run: () => selectTab(t.id) }));
  archive.filter(a => !q || a.title.toLowerCase().includes(q)).slice(0, q ? 3 : 1).forEach(a => out.push({ g: 'Archived', kind: 'arch', title: a.title, sub: a.ws + ' · archived ' + a.when.toLowerCase(), tab: { title: a.title, color: a.color }, hint: 'Restore', run: () => { archive.splice(archive.indexOf(a), 1); const id = mk('r' + nextId++, a.title, a.domain, '/', a.color); cur().today.unshift({ t: id }); cur().selected = id; render(); updateChrome(); renderPage(true); } }));
  return out;
}
function cmdRender() {
  const l = $('#cmdList'); l.textContent = ''; let lastG = null;
  cmdItems.forEach((it, i) => {
    if (it.g !== lastG) { const g = el('div', 'cmd-grp'); g.textContent = it.g; g.setAttribute('role', 'presentation'); l.appendChild(g); lastG = it.g; }
    const r = el('div', 'cmd-item', { role: 'option', 'aria-selected': i === cmdSel, id: 'cmd-' + i });
    if (it.act) { const a = el('span', 'cmd-act'); a.appendChild(icon(it.kind === 'new' ? 'i-plus' : 'i-arrow')); r.appendChild(a); } else r.appendChild(favEl(it.tab, false).firstChild);
    const t = el('span', 't'); t.textContent = it.title; const s = el('span', 's'); s.textContent = it.sub; const h = el('span', 'hint'); h.textContent = it.hint;
    r.append(t, s, h);
    r.addEventListener('pointermove', () => { if (cmdSel !== i) { cmdSel = i; cmdMark(); } });
    r.addEventListener('click', () => { cmdSel = i; cmdRun(); });
    l.appendChild(r);
  });
  $('#cmdInput').setAttribute('aria-activedescendant', 'cmd-' + cmdSel);
}
function cmdMark() { $$('.cmd-item', $('#cmdList')).forEach((r, i) => { r.setAttribute('aria-selected', i === cmdSel); if (i === cmdSel) { r.scrollIntoView({ block: 'nearest' }); $('#cmdInput').setAttribute('aria-activedescendant', r.id); } }); }
function cmdRun() { const it = cmdItems[cmdSel]; closeCmd(true); if (it) setTimeout(() => it.run(), 0); }
function openCmd(prefill = '') {
  if (cmdOpen) return; cmdOpen = true; closeMenu(); closePops(); cmdReturn = document.activeElement;
  const layer = $('#cmdLayer'); layer.hidden = false; const inp = $('#cmdInput'); inp.value = prefill;
  cmdItems = cmdBuild(prefill); cmdSel = 0; cmdRender();
  fx($('#scrim'), { o: 0 }, { o: 1 }, 'fast', 'standard', { role: 'in' });
  fx($('#cmd'), { o: tnum('motion-opacity-cmdbar-start'), s: tnum('motion-scale-cmdbar-enter') }, { o: 1, s: 1 }, 'fast', 'decelerate', { role: 'in' });
  inp.focus();
}
function closeCmd(noRestore) {
  if (!cmdOpen) return; cmdOpen = false;
  const layer = $('#cmdLayer');
  const p = fx($('#cmd'), { o: 1, s: 1 }, { o: 0, s: tnum('motion-scale-cmdbar-enter') }, 'xfast', 'accelerate', { role: 'out', fill: 'forwards' });
  fx($('#scrim'), { o: 1 }, { o: 0 }, 'xfast', 'accelerate', { role: 'out', fill: 'forwards' });
  const fin = () => { if (!cmdOpen) { layer.hidden = true; Peek.poke(); $('#cmd').getAnimations().forEach(a => a.cancel()); $('#scrim').getAnimations().forEach(a => a.cancel()); } };
  S.instant ? fin() : p.then(fin);
  if (!noRestore && cmdReturn && cmdReturn.focus) cmdReturn.focus();
}
$('#cmdInput').addEventListener('input', e => { cmdItems = cmdBuild(e.target.value); cmdSel = 0; cmdRender(); });
$('#cmdInput').addEventListener('keydown', e => {
  if (e.key === 'ArrowDown') { e.preventDefault(); cmdSel = Math.min(cmdItems.length - 1, cmdSel + 1); cmdMark(); }
  else if (e.key === 'ArrowUp') { e.preventDefault(); cmdSel = Math.max(0, cmdSel - 1); cmdMark(); }
  else if (e.key === 'Enter') { e.preventDefault(); cmdRun(); }
});
$('#scrim').addEventListener('pointerdown', () => closeCmd());

/* ---------- global keys ---------- */
document.addEventListener('keydown', e => {
  const ctrl = e.ctrlKey || e.metaKey;
  if (e.key === 'Escape') { if (cmdOpen) { closeCmd(); e.preventDefault(); } else if (!$('#menu').hidden) { closeMenu(); } else if ($$('.pop:not([hidden])').length) closePops(); return; }
  if (e.ctrlKey && e.altKey && !e.shiftKey && e.code === 'KeyS') { e.preventDefault(); Peek.togglePin(); return; }
  if (e.altKey && !e.ctrlKey && !e.metaKey && (e.key === 'ArrowLeft' || e.key === 'ArrowRight')) { e.preventDefault(); pageNav(e.key === 'ArrowLeft' ? -1 : 1); return; }
  if (ctrl && !e.shiftKey && !e.altKey && (e.key === 's' || e.key === 'S')) { e.preventDefault(); toggleSidebar(); return; }
  if (ctrl && !e.shiftKey && (['t', 'l', 'k'].includes(e.key.toLowerCase()))) { e.preventDefault(); cmdOpen ? $('#cmdInput').focus() : openCmd(); return; }
  if (ctrl && e.shiftKey && /^Digit[1-9]$/.test(e.code)) { e.preventDefault(); const w = WS_ORDER[+e.code.slice(5) - 1]; if (w) switchWs(w); return; }
  if (ctrl && e.shiftKey && (e.code === 'BracketLeft' || e.code === 'BracketRight')) { e.preventDefault(); const n = WS_ORDER[WS_ORDER.indexOf(S.ws) + (e.code === 'BracketRight' ? 1 : -1)]; if (n) switchWs(n); return; }
  if (ctrl && e.key.toLowerCase() === 'w' && !e.shiftKey) { e.preventDefault(); if (cur().selected) closeTab(cur().selected); return; }
  if (e.key === 'F6') {
    e.preventDefault();
    if (card.contains(document.activeElement) || document.activeElement === card) { if (S.hidden && !S.peek) Peek.show('keyboard'); const t = $('[tabindex="0"][role=treeitem], .tile[tabindex="0"]', pane()); (t || $('#btnToggle')).focus(); }
    else card.focus();
  }
});

/* ---------- drag to reorder / move ---------- */
const MAX_FOLDER_DEPTH = 5; // folders nest up to 5 levels (Zen: zen.folders.max-subfolders)
const folderHeight = n => n.f ? 1 + Math.max(0, ...n.kids.filter(k => k.f).map(folderHeight)) : 0;
const firstTab = wsId => { const w = W[wsId]; for (const a of ['today', 'pinned']) for (const e of walk(w[a])) if (e.n.t) return e.n.t; return w.favorites[0] || null; };
sidebar.addEventListener('pointerdown', e => {
  if (e.button !== 0 || S.density === 'collapsed') return;
  if (e.target.closest('[data-act], input, .sec-head, #newTab')) return;
  const src = e.target.closest('[role=treeitem], .tile'); if (!src) return;
  const x0 = e.clientX, y0 = e.clientY; let started = false, d = null;
  const mv = ev => {
    if (!started) { if (Math.hypot(ev.clientX - x0, ev.clientY - y0) < 5) return; started = true; d = dragBegin(src, ev); }
    dragMove(d, ev, x0, y0);
  };
  const up = ev => {
    removeEventListener('pointermove', mv); removeEventListener('pointerup', up);
    if (started) { suppressClick = true; setTimeout(() => suppressClick = false, 0); dragEnd(d, ev); }
  };
  addEventListener('pointermove', mv); addEventListener('pointerup', up);
});
function dragBegin(src, ev) {
  S.drag = true; const p = pane(); p.classList.add('drag-on'); sidebar.classList.add('drag-on');
  const isTile = src.matches('.tile'), unit = isTile ? src : src.parentElement; // node for rows
  const id = src.dataset.id, rowRect = (isTile ? src : src.querySelector('.row-in')).getBoundingClientRect();
  const lifted = isTile ? src : src.closest('.node');
  unit.classList.add('lifted');
  const H = rowRect.height;
  const allRows = $$('.row', p).filter(r => !r.closest('.children[hidden]'));
  const order = allRows.filter(r => !lifted.contains(r));
  let before = 0; for (const r of order) { if (lifted.compareDocumentPosition(r) & Node.DOCUMENT_POSITION_FOLLOWING) break; before++; }
  fx($('.row-in', lifted) || lifted, { s: 1 }, { s: tnum('motion-scale-lift') }, 'fast', 'standard', { role: 'move' });
  const loc = locate(id, S.ws), node = loc.area === 'fav' ? { t: id } : loc.list[loc.i];
  return { src, id, isTile, lifted, rows: order, srcPos: isTile ? null : before, H, rect: rowRect, target: null, favs: $('.favs', p), p,
    fromWs: S.ws, node, h: folderHeight(node), hoverWs: null, wsTimer: null, ghost: null, cross: false, lastX: ev.clientX, lastY: ev.clientY };
}
/* Drop target under the pointer. Folder rows: the top and bottom motion-ratio-folder-edge (20%, Zen) of the row mean before / after,
   the middle means "into the folder". A drop that would nest a folder deeper than MAX_FOLDER_DEPTH is returned with denied:true. */
function resolveTarget(d, x, y) {
  const fr = d.favs.getBoundingClientRect();
  if (d.favs.children.length === 0 ? false : (y >= fr.top - 6 && y <= fr.bottom + 6 && x >= fr.left && x <= fr.right)) {
    const tiles = $$('.tile', d.favs).filter(t => t !== d.lifted); let idx = tiles.length;
    for (let i = 0; i < tiles.length; i++) { const r = tiles[i].getBoundingClientRect(); if (y < r.bottom && x < r.left + r.width / 2 || y < r.top) { idx = i; break; } }
    return { type: 'fav', idx };
  }
  const edge = tnum('motion-ratio-folder-edge');
  const check = t => { const depth = (t.type === 'into' || t.type === 'into-first') ? +t.row.dataset.depth : +t.row.dataset.lvl; if (d.h && depth + d.h > MAX_FOLDER_DEPTH) t.denied = true; return t; };
  for (let i = 0; i < d.rows.length; i++) {
    const r = d.rows[i], rect = r.getBoundingClientRect(); if (y < rect.top || y > rect.bottom) continue;
    const loc = locate(r.dataset.id, S.ws), rel = (y - rect.top) / rect.height;
    if (r.dataset.folder) {
      if (rel > edge && rel < 1 - edge) return check({ type: 'into', row: r, loc, refPos: nextAfterSubtree(d, r) });
      if (rel <= edge) return check({ type: 'before', row: r, loc, refPos: i });
      return check(r.getAttribute('aria-expanded') === 'true' ? { type: 'into-first', row: r, loc, refPos: i + 1 } : { type: 'after', row: r, loc, refPos: nextAfterSubtree(d, r) });
    }
    if (rel < .5) return check({ type: 'before', row: r, loc, refPos: i });
    return check({ type: 'after', row: r, loc, refPos: nextAfterSubtree(d, r) });
  }
  const tg = e => { const b = e.getBoundingClientRect(); return y >= b.top && y <= b.bottom && x >= b.left && x <= b.right; };
  const pt = $('.tree[data-area=pinned]', d.p), tt = $('.tree[data-area=today]', d.p);
  const lastIn = a => { const rs = d.rows.filter(r => r.dataset.area === a); return rs.length ? d.rows.indexOf(rs[rs.length - 1]) + 1 : null; };
  if (tg(tt) || y > tt.getBoundingClientRect().top) return { type: 'end', area: 'today', refPos: d.rows.length };
  if (tg(pt) || y > pt.getBoundingClientRect().top - 10) return { type: 'end', area: 'pinned', refPos: lastIn('pinned') ?? firstOfToday(d) };
  return null;
}
function firstOfToday(d) { const i = d.rows.findIndex(r => r.dataset.area === 'today'); return i < 0 ? d.rows.length : i; }
function nextAfterSubtree(d, r) { const node = r.parentElement; let i = d.rows.indexOf(r) + 1; while (i < d.rows.length && node.contains(d.rows[i])) i++; return i; }

/* Dragging a tab onto a Workspace icon: the icon highlights; after motion-timing-ws-drag-hover (500ms) the Workspace switches
   with the normal switch motion. The dragged row becomes a floating ghost that keeps following the pointer, and the drag
   carries on in the new Workspace's pane. Dropping on an icon moves the tab to the end of that Workspace's Today without
   switching. The tab then belongs to the Workspace it lands in; Workspaces have separate cookies, so the ghost says so. */
function dragWsHover(d, btn) {
  const id = btn && btn.dataset.ws !== S.ws ? btn.dataset.ws : null;
  if (d.hoverWs === id) return;
  clearTimeout(d.wsTimer); $$('.ws-btn.drop-ws').forEach(b => b.classList.remove('drop-ws'));
  d.hoverWs = id; if (!id) return;
  btn.classList.add('drop-ws'); btn.style.setProperty('--hint-c', `var(--aerie-workspace-${WS[id].preset}-accent)`); btn.style.setProperty('--dwell', tnum('motion-timing-ws-drag-hover') + 'ms');
  d.wsTimer = setTimeout(() => dragSwitchWs(d, id), tnum('motion-timing-ws-drag-hover'));
}
function dragMakeGhost(d) {
  if (d.ghost) return;
  const srcEl = d.isTile ? d.lifted : $('.row', d.lifted), r = (d.isTile ? d.lifted : $('.row-in', d.lifted)).getBoundingClientRect();
  const g = srcEl.cloneNode(true); g.classList.add('drag-ghost'); g.removeAttribute('id'); g.querySelectorAll('.children').forEach(c => c.remove());
  g.querySelectorAll('.row-in').forEach(n => { n.style.cssText = ''; });
  g.style.cssText = `position:fixed;left:${r.left}px;top:${r.top}px;width:${r.width}px;height:${r.height}px;margin:0;pointer-events:none;z-index:60`;
  sidebar.appendChild(g); d.ghost = g; d.gpx = d.lastX; d.gpy = d.lastY;
  d.lifted.style.visibility = 'hidden';
}
async function dragSwitchWs(d, id) {
  if (!S.drag || d.switching || id === S.ws) return; d.switching = true;
  dragMakeGhost(d);
  await switchWs(id);
  if (!S.drag) return;
  const p = pane(); p.classList.add('drag-on');
  Object.assign(d, { cross: true, p, lifted: d.lifted, srcPos: null, rows: $$('.row', p).filter(r => !r.closest('.children[hidden]')), favs: $('.favs', p), target: null, hoverWs: null, switching: false });
  d.lifted = null;
  dragMove(d, { clientX: d.lastX, clientY: d.lastY });
}
function dragHintText(d, t) {
  if (t && t.denied) return `Can't nest deeper than ${MAX_FOLDER_DEPTH} levels`;
  const dest = d.hoverWs || (S.ws !== d.fromWs ? S.ws : null);
  if (dest) { const n = WS[dest].name; return `Moves to ${n} — uses ${n}'s logins`; }
  return '';
}
function dragMove(d, ev, x0, y0) {
  d.lastX = ev.clientX; d.lastY = ev.clientY;
  const sc = tnum('motion-scale-lift');
  if (d.ghost) d.ghost.style.transform = `translate(${ev.clientX - d.gpx}px,${ev.clientY - d.gpy}px) scale(${sc})`;
  else {
    const dx = ev.clientX - x0, dy = ev.clientY - y0, tgt = (d.isTile ? d.lifted : $('.row-in', d.lifted));
    tgt.style.transform = `translate(${dx}px,${dy}px) scale(${sc})`;
    tgt.style.transition = 'none'; tgt.style.boxShadow = 'var(--aerie-shadow-lift)'; tgt.style.background = 'var(--aerie-color-surface-popover)';
    d.lifted.style.pointerEvents = 'none';
  }
  const under = document.elementFromPoint(ev.clientX, ev.clientY);
  dragWsHover(d, under && under.closest('.ws-btn'));
  const t = d.hoverWs ? { type: 'ws', ws: d.hoverWs } : resolveTarget(d, ev.clientX, ev.clientY); d.target = t;
  $$('.drop-into, .drop-denied', d.p).forEach(r => r.classList.remove('drop-into', 'drop-denied'));
  d.favs.classList.toggle('drop-target', !!t && t.type === 'fav');
  sidebar.classList.toggle('drag-denied', !!(t && t.denied));
  if (t && t.type === 'into') t.row.classList.add(t.denied ? 'drop-denied' : 'drop-into');
  else if (t && t.denied && t.row) t.row.classList.add('drop-denied');
  const insertPos = t && t.type !== 'fav' && t.type !== 'ws' && !t.denied ? t.refPos : null;
  d.rows.forEach((r, i) => {
    let s = 0; if (insertPos !== null && i >= insertPos) s += d.H; if (d.srcPos !== null && i >= d.srcPos) s -= d.H;
    const ri = $('.row-in', r); ri.style.transform = s ? `translateY(${s}px)` : '';
  });
  const h = $('#dragHint'), txt = dragHintText(d, t);
  h.hidden = !txt; if (txt) { h.textContent = txt; h.classList.toggle('denied', !!(t && t.denied)); const w = h.offsetWidth; h.style.left = Math.max(6, Math.min(innerWidth - w - 6, ev.clientX + 16)) + 'px'; h.style.top = Math.max(6, ev.clientY - 40) + 'px'; }
}
function dragEnd(d, ev) {
  clearTimeout(d.wsTimer);
  let t = d.target; const id = d.id;
  if (t && t.denied) t = null;
  const gone = d.ghost || (d.isTile ? d.lifted : $('.row-in', d.lifted)), from = gone.getBoundingClientRect();
  const tgt = d.isTile ? d.lifted : (d.lifted && $('.row-in', d.lifted));
  const clean = () => { $$('.row-in', d.p).forEach(r => { r.style.transform = ''; r.style.transition = ''; }); d.p.classList.remove('drag-on'); sidebar.classList.remove('drag-on', 'drag-denied'); S.drag = false; $$('.drop-into, .drop-denied', d.p).forEach(r => r.classList.remove('drop-into', 'drop-denied')); $$('.ws-btn.drop-ws').forEach(b => b.classList.remove('drop-ws')); d.favs.classList.remove('drop-target'); $('#dragHint').hidden = true; if (d.ghost) d.ghost.remove(); Peek.poke(); };
  if (!t && d.cross) t = { type: 'end', area: 'today' }; // cross-Workspace drag released over nothing: land at the end of Today
  if (!t) { // spring back
    clean(); d.lifted.classList.remove('lifted'); tgt.style.cssText = '';
    fx(tgt, { x: from.left - d.rect.left, y: from.top - d.rect.top, s: tnum('motion-scale-lift') }, { x: 0, y: 0, s: 1 }, 'spring', 'spring', { role: 'move' }); return;
  }
  clean();
  const srcWs = d.fromWs, loc = locate(id, srcWs), node = d.node, isF = !!node.f;
  const unlift = () => { if (d.lifted) { d.lifted.classList.remove('lifted'); d.lifted.style.visibility = ''; } if (tgt) tgt.style.cssText = ''; };
  if (isF && t.loc && (t.type === 'into' || t.type === 'into-first') && isDesc(node, t.loc.n.f)) { unlift(); return; }
  if (isF && t.type === 'fav') { unlift(); return; }
  const wasSel = W[srcWs].selected === id;
  loc.list.splice(loc.i, 1); // remove from source
  const dest = t.type === 'ws' ? t.ws : S.ws, w = W[dest];
  const insert = (list, i) => list.splice(i, 0, node);
  if (t.type === 'ws') w.today.push(node);
  else if (t.type === 'fav') w.favorites.splice(t.idx, 0, id);
  else if (t.type === 'end') insert(w[t.area], w[t.area].length);
  else if (t.type === 'into') { t.loc.n.open = true; insert(t.loc.n.kids, t.loc.n.kids.length); }
  else if (t.type === 'into-first') { t.loc.n.open = true; insert(t.loc.n.kids, 0); }
  else { const l2 = locate(t.loc.n.t || t.loc.n.f, S.ws); insert(l2.list, l2.i + (t.type === 'after' ? 1 : 0)); }
  if (dest !== srcWs) {
    if (!locate(W[srcWs].selected, srcWs)) W[srcWs].selected = firstTab(srcWs);
    if (wasSel && dest === S.ws) w.selected = id; else if (!w.selected || !locate(w.selected, dest)) w.selected = firstTab(dest);
  }
  unlift();
  render();
  if (dest !== srcWs || (wasSel && dest !== S.ws)) { updateChrome(); renderPage(true); }
  if (t.type === 'ws') return;
  const key = isF ? 'f:' + id : 't:' + id;
  const ne = t.type === 'fav' ? $(`.tile[data-id="${CSS.escape(id)}"]`, pane()) : $(`.node[data-id="${CSS.escape(key)}"]`, pane());
  if (ne) { const target = ne.matches('.tile') ? ne : ($('.row-in', ne) || ne); const to = target.getBoundingClientRect();
    fx(target, { x: from.left - to.left, y: from.top - to.top, s: tnum('motion-scale-lift') }, { x: 0, y: 0, s: 1 }, 'spring', 'spring', { role: 'move' }); }
}
/* keep native drag from interfering */
sidebar.addEventListener('dragstart', e => e.preventDefault());

/* ---------- tooltips ---------- */
let tipTimer = null; const tip = $('#tooltip');
function showTip(t) {
  tip.textContent = t.dataset.tip; if (t.dataset.keys) { const k = el('kbd'); k.textContent = t.dataset.keys; tip.appendChild(k); }
  tip.hidden = false; const r = t.getBoundingClientRect(), w = tip.offsetWidth, h = tip.offsetHeight;
  let x, y;
  const collapsed = S.density === 'collapsed' && t.closest('.sidebar') && !t.closest('.bottom') || t.matches('.ws-btn') && S.density === 'collapsed';
  if (collapsed) { x = S.side === 'left' ? r.right + 8 : r.left - w - 8; y = r.top + r.height / 2 - h / 2; }
  else { x = r.left + r.width / 2 - w / 2; y = t.closest('.bottom') ? r.top - h - 8 : r.bottom + 8; }
  tip.style.left = Math.max(6, Math.min(innerWidth - w - 6, x)) + 'px'; tip.style.top = Math.max(6, Math.min(innerHeight - h - 6, y)) + 'px';
}
document.addEventListener('pointerover', e => { const t = e.target.closest('[data-tip]'); clearTimeout(tipTimer); tip.hidden = true; if (t && !S.drag) tipTimer = setTimeout(() => showTip(t), tnum('motion-timing-tooltip-delay')); });
document.addEventListener('pointerdown', () => { clearTimeout(tipTimer); tip.hidden = true; });

/* ---------- focus order badges ---------- */
function renderFocusBadges() {
  const host = $('#focusBadges'); host.textContent = ''; if (!S.focus) return;
  const targets = ['#toprow', '#pill', '#btnGuard', '.favs', '.tree[data-area=pinned]', '#newTab', '.tree[data-area=today]', '#btnArchive', '#wsw', '#btnNewWs'];
  targets.forEach((sel, i) => {
    const t = $(sel); if (!t) return; const r = t.getBoundingClientRect(); if (!r.width) return;
    const b = el('div', 'fo'); b.textContent = i + 1;
    const right = S.side === 'right' && sel !== '#btnGuard';
    b.style.left = (sel === '#btnGuard' ? r.left - 8 : right ? r.right - 12 : r.left - 6) + 'px'; b.style.top = (r.top - 7) + 'px'; host.appendChild(b);
  });
}
addEventListener('resize', renderFocusBadges);

/* ---------- mockup controls ---------- */
const mock = $('#mock');
function buildControls() {
  mock.textContent = '';
  const h = el('div', 'mh'); h.innerHTML = '<span>Mockup controls</span><span id="mockToggle">[-]</span>'; mock.appendChild(h);
  const body = el('div', 'mbody'); mock.appendChild(body);
  const sel = (key, label, opts, val, fn) => { const r = el('label', 'mrow'); r.textContent = label; const s = el('select'); opts.forEach(o => { const op = el('option'); op.value = o[0]; op.textContent = o[1]; s.appendChild(op); }); s.value = val; s.id = 'm-' + key; s.addEventListener('change', () => { fn(s.value); }); r.appendChild(s); body.appendChild(r); };
  const chk = (key, label, val, fn) => { const r = el('label', 'mrow'); r.textContent = label; const c = el('input'); c.type = 'checkbox'; c.checked = val; c.id = 'm-' + key; c.addEventListener('change', () => fn(c.checked)); r.appendChild(c); body.appendChild(r); };
  sel('theme', 'theme (global)', THEMES, S.gTheme, v => changeLook({ gTheme: v }, () => { S.gTheme = v; S.explicitTheme = true; }));
  sel('mode', 'mode (global)', [['light', 'light'], ['dark', 'dark']], S.gMode, v => changeLook({ gMode: v }, () => { S.gMode = v; }));
  sel('ws', 'workspace', WS_ORDER.map(w => [w, WS[w].name]), S.ws, v => switchWs(v));
  sel('density', 'density', [['comfortable', 'comfortable'], ['compact', 'compact'], ['collapsed', 'collapsed rail'], ['autohide', 'auto-hide']], S.density, v => setDensity(v));
  sel('side', 'side', [['left', 'left'], ['right', 'right']], S.side, v => setSide(v));
  sel('wstheme', 'workspace theme', [['default', 'default'], ...THEMES], WS[S.ws].theme, v => changeLook({ theme: v }, () => { WS[S.ws].theme = v; }));
  sel('wsmode', 'workspace mode', [['default', 'default'], ['light', 'light'], ['dark', 'dark'], ['system', 'system']], WS[S.ws].mode, v => changeLook({ mode: v }, () => { WS[S.ws].mode = v; }));
  sel('wscount', 'workspaces', [['n', '3 (all)'], ['1', '1 (single)']], WS_ORDER.length < 2 ? '1' : 'n', v => setWsCount(v === '1' ? 1 : 3));
  sel('pinnedclose', 'pinned tab close', [['reset-sleep-switch', 'reset, sleep, next'], ['sleep', 'sleep, next'], ['close', 'close']], pinnedCloseMode, v => { pinnedCloseMode = v; render(); });
  const note = el('small'); note.id = 'themeNote'; note.className = 'theme-note'; body.appendChild(note);
  chk('reduced', 'reduced motion', root.hasAttribute('data-reduced-motion'), v => { v ? root.setAttribute('data-reduced-motion', '') : root.removeAttribute('data-reduced-motion'); });
  chk('focus', 'show focus order', S.focus, v => { S.focus = v; renderFocusBadges(); });
  const sm = el('small'); sm.textContent = 'Not part of Aerie. Ctrl+S sidebar, Ctrl+T command bar, F6 focus.'; body.appendChild(sm);
  h.addEventListener('click', () => { mock.classList.toggle('collapsed'); $('#mockToggle').textContent = mock.classList.contains('collapsed') ? '[+]' : '[-]'; });
}
function syncControls() { const m = (k, v) => { const e = $('#m-' + k); if (e) e.value = v; }; m('theme', S.gTheme); m('mode', S.gMode); m('wstheme', WS[S.ws].theme); m('wsmode', WS[S.ws].mode); const tn = $('#themeNote'); if (tn && S.look) tn.textContent = 'showing: ' + themeName(S.look.theme) + ' ' + S.look.mode + (S.look.fell ? ' (OLED is dark only, so light mode shows Clean)' : '') + (contrastMore.matches && !S.explicitTheme ? ' (system asks for more contrast)' : ''); m('ws', S.ws); m('wscount', WS_ORDER.length < 2 ? '1' : 'n'); m('density', S.density); m('side', S.side); mock.classList.toggle('left', S.side === 'right'); }
/* Demo control and `wscount=1` URL parameter. With one Workspace the dots are hidden and only "+ New workspace" shows. */
let wsStash = [];
function setWsCount(n) {
  if (n === 1 && WS_ORDER.length > 1) { wsStash = WS_ORDER.filter(x => x !== S.ws); WS_ORDER.splice(0, WS_ORDER.length, S.ws); }
  else if (n !== 1 && wsStash.length) { const keep = new Set([...WS_ORDER, ...wsStash]); WS_ORDER.splice(0, WS_ORDER.length, ...Object.keys(WS).filter(k => keep.has(k))); wsStash = []; }
  renderSwitcher(); updateChrome(); renderFocusBadges();
  if ($('#m-wscount')) { const open = !mock.classList.contains('collapsed'); buildControls(); syncControls(); if (!open) mock.classList.add('collapsed'); }
}
function setSide(s) { S.side = s; win.dataset.side = s; mock.classList.toggle('left', s === 'right'); sidebar.getAnimations().forEach(a => a.cancel()); renderFocusBadges(); }
function setDensity(d) {
  S.density = d;
  if (d === 'autohide') { win.dataset.density = 'comfortable'; win.style.removeProperty('--sb-w'); if (S.sbW) win.style.setProperty('--sb-w', S.sbW + 'px'); setHidden(true, { animate: false }); }
  else {
    win.dataset.density = d;
    if (d === 'collapsed') win.style.removeProperty('--sb-w'); else if (S.sbW) win.style.setProperty('--sb-w', S.sbW + 'px');
    if (S.hidden) setHidden(false, { animate: false });
  }
  render(); renderFocusBadges();
}

/* ---------- init ---------- */
function init() {
  const dark = matchMedia('(prefers-color-scheme: dark)').matches;
  const pt = params.get('theme'), pl = params.get('look'), pm = params.get('mode') || (['light', 'dark'].includes(pt) ? pt : null);
  const tp = normTheme(pt) || normTheme(pl);   // look= is the old name for theme=; theme=light|dark is the old name for mode=
  S.explicitTheme = !!tp; S.gTheme = tp || 'vivid';
  S.gMode = pm === 'system' || !pm ? (dark ? 'dark' : 'light') : (pm === 'dark' ? 'dark' : 'light');
  const wt = params.get('wstheme'), wm = params.get('wsmode');
  if (wt === 'default' || normTheme(wt)) WS[params.get('ws') in WS ? params.get('ws') : 'personal'].theme = wt === 'default' ? wt : normTheme(wt);
  if (['default', 'light', 'dark', 'system'].includes(wm)) WS[params.get('ws') in WS ? params.get('ws') : 'personal'].mode = wm;
  if (params.get('reduced') === '1') root.setAttribute('data-reduced-motion', '');
  S.side = params.get('side') === 'right' ? 'right' : 'left'; win.dataset.side = S.side;
  S.ws = WS_ORDER.includes(params.get('ws')) ? params.get('ws') : 'personal';
  S.focus = params.get('focus') === '1';
  if (params.get('wscount') === '1') { wsStash = WS_ORDER.filter(x => x !== S.ws); WS_ORDER.splice(0, WS_ORDER.length, S.ws); }
  const dens = params.get('density'); S.density = ['compact', 'collapsed', 'autohide'].includes(dens) ? dens : 'comfortable';
  win.dataset.density = S.density === 'autohide' ? 'comfortable' : S.density;
  if (S.density === 'autohide') { S.hidden = true; win.dataset.sidebar = 'hidden'; sidebar.inert = true; }
  const bg = el('div', 'sb-tint'); sidebar.insertBefore(bg, sidebar.firstChild);
  applyLook(); setPreset(WS[S.ws].preset, false);
  contrastMore.addEventListener('change', () => { if (!S.explicitTheme) changeLook({}, () => {}); });
  renderSwitcher(); render(); renderPage(false);
  const open = params.get('open');
  if (open === 'peek') { S.hidden = true; win.dataset.sidebar = 'hidden'; sidebar.inert = false; if (S.density !== 'autohide') S.density = S.density; Peek.forceShown(); }
  if (open === 'cmdbar') openCmd();
  if (open === 'archive') openArchive();
  if (open === 'guard') openGuard();
  if (open === 'contextmenu') { const r = $('.node[data-id="t:' + (cur().today.find(n => n.t)?.t) + '"] .row-in', pane()); const b = r ? r.getBoundingClientRect() : { left: 60, top: 300 }; openMenu(cur().today.find(n => n.t).t, b.left + 120, b.top + 20); $('.mi', $('#menu')).classList.add('hot'); }
  if (params.get('controls') !== '0') { buildControls(); syncControls(); } else mock.hidden = true;
  renderFocusBadges();
  requestAnimationFrame(() => requestAnimationFrame(() => { S.instant = false; document.body.dataset.ready = '1'; }));
}
init();
window.__aerie = { S, W, WS, WS_ORDER, tabs, render, swipe }; // test hook for the mockup's Playwright script
})();
