// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// aerie://setup, the first-run setup page (issue #46). It replaces Helium's
// onboarding page and talks to the same browser handlers that Helium's page
// controller registers (chrome/browser/ui/webui/onboarding):
//
//   import    initializeImportDialog, importData, 'import-data-status-changed'
//   search    getSearchEnginesList, setDefaultSearchEngine
//   default   requestDefaultBrowserState, setAsDefaultBrowser,
//             'browser-default-state-changed'
//   finish    setPref('completed_onboarding', true), acceptLatestSchema
//   theme     aerieSetup.setColorScheme, aerieSetup.setThemeColor
//             (chrome/browser/ui/webui/aerie_onboarding)
//
// The page never sets helium.services.user_consented, so Helium services stay
// off (issue #6). Text from the browser (profile and engine names) only ever
// goes in with textContent; the page builds every node itself and never
// assigns HTML.
'use strict';

/* ---------- WebUI glue: responses, events, strings ---------- */

const pending = new Map();
const listeners = new Map();
let nextCallId = 0;
window.cr = window.cr || {};
window.cr.webUIResponse = (id, isSuccess, response) => {
  const call = pending.get(id);
  if (!call) {
    return;
  }
  pending.delete(id);
  (isSuccess ? call.resolve : call.reject)(response);
};
window.cr.webUIListenerCallback = (event, ...args) => {
  for (const fn of listeners.get(event) || []) {
    fn(...args);
  }
};
function sendWithPromise(name, ...args) {
  const id = `${name}_${nextCallId++}`;
  return new Promise((resolve, reject) => {
    pending.set(id, {resolve, reject});
    chrome.send(name, [id, ...args]);
  });
}
function onBrowserEvent(event, fn) {
  if (!listeners.has(event)) {
    listeners.set(event, []);
  }
  listeners.get(event).push(fn);
}

// The page controller serves the strings as /strings.js, which fills
// loadTimeData.data.
window.loadTimeData = window.loadTimeData || {data: {}};
function t(key, ...subs) {
  let text = window.loadTimeData.data[key];
  if (typeof text !== 'string') {
    console.error(`aerie setup: missing string ${key}`);
    return '';
  }
  subs.forEach((s, i) => { text = text.replace(`$${i + 1}`, String(s)); });
  return text;
}
const internal = host => `${location.protocol}//${host}/`;

/* ---------- state ---------- */

const COLORS = {
  // Neutral is the browser's plain default theme. It follows the appearance,
  // so its swatch and preview use NEUTRAL_DARK when the page is dark.
  neutral: ['#FFFFFF', '#E4E5EA', '#B9BBC4', 'colorNeutral'],
  alpenglow: ['#FFA463', '#F0466F', '#7B3FE4', 'colorAlpenglow'],
  sky: ['#62D6F5', '#2F6BFF', '#22309A', 'colorSky'],
  moss: ['#3FD19B', '#138476', '#0F3F4D', 'colorMoss'],
  dusk: ['#2B2A8F', '#8E4FE8', '#FF9E7A', 'colorDusk'],
  sand: ['#F6D7A7', '#D99A6C', '#8C5A44', 'colorSand'],
  graphite: ['#5A596D', '#2A2936', '#101016', 'colorGraphite'],
};
const NEUTRAL_DARK = ['#3A3B42', '#202127', '#0E0F12'];
const MODES = [['light', 'lookLight'], ['dark', 'lookDark'], ['system', 'lookSystem']];
const IMPORT_ITEMS = [
  ['bookmarks', 'importBookmarks', 'favorites', 'import_dialog_bookmarks'],
  ['passwords', 'importPasswords', 'passwords', 'import_dialog_saved_passwords'],
  ['history', 'importHistory', 'history', 'import_dialog_history'],
  ['engines', 'importSearch', 'search', 'import_dialog_search_engine'],
];
const ENGINE_LOOKS = [
  // keyword part, tile colour, description string
  ['duckduckgo', '#DE5833', 'searchDescDuckDuckGo'],
  ['brave', '#FB542B', 'searchDescBrave'],
  ['kagi', '#FFB319', 'searchDescKagi'],
  ['startpage', '#6573FF', 'searchDescStartpage'],
];
const PRIVACY = [
  ['privacyHttps', 'privacyHttpsDesc'], ['privacyCookies', 'privacyCookiesDesc'],
  ['privacyFingerprint', 'privacyFingerprintDesc'], ['privacyWebrtc', 'privacyWebrtcDesc'],
  ['privacyPreload', 'privacyPreloadDesc'], ['privacyTelemetry', 'privacyTelemetryDesc'],
];
const BROWSER_TILES = ['#4285F4', '#FB542B', '#FF7139', '#5B6B73', '#0A84FF', '#8E4FE8'];

const S = {
  steps: ['welcome', 'import', 'look', 'block', 'search', 'privacy', 'default', 'done'],
  step: 0,
  profiles: null,            // null while loading, [] when none found
  from: 'none',              // profile index as a string, or 'none'
  items: new Set(['bookmarks', 'passwords', 'history']),
  importing: false, importError: false, imported: null,
  mode: 'system', color: 'neutral',
  engines: [], engine: null,
  isDefault: false, canBeDefault: true,
};
window.aerieSetup = {state: S};  // read by the Bench

/* ---------- DOM helpers ---------- */

const $ = sel => document.querySelector(sel);
const $$ = sel => [...document.querySelectorAll(sel)];
const SVG = 'http://www.w3.org/2000/svg';

function h(tag, props, ...children) {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(props || {})) {
    if (v === null || v === undefined || v === false) {
      continue;
    }
    if (k === 'class') {
      node.className = v;
    } else if (k === 'style') {
      for (const [p, val] of Object.entries(v)) {
        node.style.setProperty(p, val);
      }
    } else if (k.startsWith('on')) {
      node.addEventListener(k.slice(2), v);
    } else {
      node.setAttribute(k, v === true ? '' : v);
    }
  }
  for (const c of children.flat()) {
    if (c === null || c === undefined || c === false) {
      continue;
    }
    node.append(c instanceof Node ? c : document.createTextNode(String(c)));
  }
  return node;
}
function s(tag, attrs, ...children) {
  const node = document.createElementNS(SVG, tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    node.setAttribute(k, v);
  }
  children.forEach(c => node.append(c));
  return node;
}
const tickIcon = () => s('svg', {width: 12, height: 12, viewBox: '0 0 12 12', fill: 'none', 'aria-hidden': 'true'},
    s('path', {d: 'm2.5 6.2 2.3 2.3 4.7-5', stroke: 'currentColor', 'stroke-width': 2, 'stroke-linecap': 'round', 'stroke-linejoin': 'round'}));
const arrowIcon = () => s('svg', {class: 'arrow', width: 16, height: 16, viewBox: '0 0 16 16', fill: 'none', 'aria-hidden': 'true'},
    s('path', {d: 'M3 8h10M9 4l4 4-4 4', stroke: 'currentColor', 'stroke-width': 2, 'stroke-linecap': 'round', 'stroke-linejoin': 'round'}));
const shieldIcon = (size = 10) => s('svg', {width: size, height: size, viewBox: '0 0 18 18', 'aria-hidden': 'true'},
    s('path', {d: 'M9 1.8 3 4v4.6c0 3.6 2.6 6.4 6 7.6 3.4-1.2 6-4 6-7.6V4L9 1.8Z', fill: 'currentColor'}));

const WING = 'M48 182 C88 160 110 124 128 66 C146 124 168 160 208 182';
function gradientDefs(id) {
  return s('defs', {}, s('linearGradient', {id, x1: 0, y1: 0, x2: 0.6, y2: 1},
      s('stop', {offset: 0, 'stop-color': '#FFA463'}), s('stop', {offset: 0.5, 'stop-color': '#F0466F'}),
      s('stop', {offset: 1, 'stop-color': '#7B3FE4'})));
}
let gradientCount = 0;
function appIcon(cls, size) {
  const id = `alp${gradientCount++}`;
  return s('svg', {class: cls, viewBox: '0 0 256 256', width: size, height: size, 'aria-hidden': 'true'}, gradientDefs(id),
      s('rect', {width: 256, height: 256, rx: 58, fill: `url(#${id})`}),
      s('path', {d: WING, fill: 'none', stroke: '#fff', 'stroke-width': 30, 'stroke-linecap': 'round', 'stroke-linejoin': 'round'}),
      s('circle', {cx: 128, cy: 160, r: 19, fill: '#fff'}));
}
function heroMark() {
  const id = `alp${gradientCount++}`;
  return h('span', {class: 'hero-wrap'}, s('svg', {class: 'hero-mark', viewBox: '0 0 256 256', width: 148, height: 148, 'aria-hidden': 'true'},
      gradientDefs(id), s('rect', {class: 'sq', width: 256, height: 256, rx: 58, fill: `url(#${id})`}),
      s('path', {class: 'wing', d: WING, fill: 'none', stroke: '#fff', 'stroke-width': 30, 'stroke-linecap': 'round', 'stroke-linejoin': 'round'}),
      s('circle', {class: 'sunpt', cx: 128, cy: 160, r: 19, fill: '#fff'})));
}

// Entering elements rise in one after another; --i sets the order.
const E = (i, node) => { node.classList.add('enter'); node.style.setProperty('--i', i); return node; };
function heading(key, i) {
  const words = t(key).split(' ');
  const el = h('h1', {style: {'--i': i}});
  words.forEach((w, wi) => {
    el.append(h('span', {class: 'w', style: {'--wi': wi}}, w));
    if (wi < words.length - 1) {
      el.append(' ');
    }
  });
  return el;
}
const eyebrow = i => {
  const steps = S.steps.length - 2;
  return E(i, h('div', {class: 'eyebrow'}, t('stepOf', S.step, steps)));
};
const primary = (label, onclick, id = 'next') =>
  h('button', {class: 'btn primary', type: 'button', id, onclick}, label, arrowIcon());

function radioGroup(name, items, current, body) {
  return h('div', {class: 'options', role: 'radiogroup'}, items.map(it =>
    h('button', {class: 'opt', type: 'button', role: 'radio', id: `${name}-${it.id}`, 'data-group': name, 'data-val': it.id,
                 'aria-checked': String(it.id === current), onclick: () => choose(name, it.id)},
      body(it), h('span', {class: 'tick'}, tickIcon()))));
}
const optBody = it => [h('span', {class: 'logo', style: {background: it.tile}}, it.letter),
  h('span', {class: 'tx'}, h('span', {class: 't'}, it.name), it.desc ? h('span', {class: 'd'}, it.desc) : null)];
function retrigger(el, cls) {
  if (!el) {
    return;
  }
  el.classList.remove(cls);
  void el.offsetWidth;
  el.classList.add(cls);
}
const isDark = () => S.mode === 'dark' || (S.mode === 'system' && matchMedia('(prefers-color-scheme: dark)').matches);
const palette = id => (id === 'neutral' && isDark() ? NEUTRAL_DARK : COLORS[id].slice(0, 3));
const swatchBackground = id => { const [a, b, c] = palette(id); return `linear-gradient(140deg, ${a}, ${b} 55%, ${c})`; };

/* ---------- preview window ---------- */

function preview(i, {counter = false} = {}) {
  const [a1, a2, a3] = palette(S.color);
  const engine = S.engines.find(e => e.id === S.engine);
  return E(i, h('div', {class: 'preview', 'aria-hidden': 'true'},
    h('div', {class: `win${isDark() ? ' dark' : ''}`, id: 'win', style: {'--a1': a1, '--a2': a2, '--a3': a3}},
      h('div', {class: 'strip'}, h('div', {class: 'lights'}, h('i'), h('i'), h('i')),
        h('div', {class: 'wtab on'}, h('b'), t('previewTab')), h('div', {class: 'wtab'}, h('b'), t('previewTab2'))),
      h('div', {class: 'bar'}, h('div', {class: 'url', id: 'pv-url'}, engine ? engine.name : '')),
      h('div', {class: 'page'},
        counter ? h('div', {class: 'counter'}, shieldIcon(), t('blockCounter', 23)) : null,
        h('div', {class: 'h'}), h('div', {class: 'l', style: {width: '92%'}}), h('div', {class: 'l', style: {width: '80%'}}),
        h('div', {class: 'card'}, h('div', {class: 'l', style: {width: '60%', opacity: '.3'}}),
          h('div', {class: 'l', style: {width: '88%'}}), h('div', {class: 'l', style: {width: '70%'}})),
        h('div', {class: 'l', style: {width: '86%'}}), h('div', {class: 'l', style: {width: '64%'}})))));
}
function updatePreview() {
  const win = $('#win');
  if (!win) {
    return;
  }
  const [a1, a2, a3] = palette(S.color);
  win.style.setProperty('--a1', a1);
  win.style.setProperty('--a2', a2);
  win.style.setProperty('--a3', a3);
  win.classList.toggle('dark', isDark());
  $$('.sw').forEach(b => { b.style.background = swatchBackground(b.dataset.val); });
  const engine = S.engines.find(e => e.id === S.engine);
  const url = $('#pv-url');
  if (engine && url.textContent !== engine.name) {
    url.textContent = engine.name;
    retrigger(url, 'swap');
  }
}

/* ---------- steps ---------- */

const VIEWS = {
  welcome() {
    return h('section', {class: 'step solo'}, h('div', {class: 'copy'},
      E(0, h('div', {}, heroMark())), heading('welcomeTitle', 1),
      E(3, h('p', {class: 'lede'}, t('welcomeBody'))),
      E(4, h('div', {class: 'actions'}, primary(t('welcomeStart'), () => go(1))))));
  },

  import() {
    const profiles = S.profiles || [];
    const options = profiles.map((p, n) => ({
      id: String(p.index), name: p.name, desc: p.profileName || null,
      tile: BROWSER_TILES[n % BROWSER_TILES.length], letter: (p.name.trim()[0] || '?').toUpperCase(),
    }));
    options.push({id: 'none', name: t('importFresh'), desc: t('importFreshDesc'), tile: 'rgba(255,255,255,.25)', letter: '+'});
    const status = S.profiles === null ? t('importLoading') : profiles.length === 0 ? t('importNoneFound') : null;
    return h('section', {class: 'step'},
      h('div', {class: 'copy'}, eyebrow(0), heading('importTitle', 1),
        status ? E(2, h('p', {class: 'note', id: 'import-status'}, status)) : null,
        profiles.length ? E(2, radioGroup('from', options, S.from, optBody)) : null,
        E(3, h('div', {class: `collapse${S.from === 'none' ? ' shut' : ''}`, id: 'what'}, h('div', {},
          h('div', {class: 'label'}, t('importWhat')),
          h('div', {class: 'checks', role: 'group', id: 'import-items'}, importPills())))),
        E(3, h('p', {class: 'note error', id: 'import-error', hidden: !S.importError}, t('importFailed'))),
        E(4, h('div', {class: 'actions'},
          primary(S.from === 'none' ? t('buttonContinue') : t('importButton'), runImport),
          h('button', {class: 'link', type: 'button', id: 'skip', hidden: S.from === 'none', onclick: () => go(1)}, t('importSkip'))))),
      E(3, h('div', {class: 'preview', 'aria-hidden': 'true', style: {display: 'grid', 'place-items': 'center'}},
        h('div', {class: 'handoff'}, h('div', {class: 'row'},
          h('span', {class: 'logo big', id: 'ho-logo'}),
          h('div', {class: 'flow idle', id: 'ho-flow'}, h('i'), h('i'), h('i'), h('i')),
          appIcon('mk', 76)),
        h('p', {id: 'ho-text'})))));
  },

  look() {
    return h('section', {class: 'step'},
      h('div', {class: 'copy'}, eyebrow(0), heading('lookTitle', 1), E(2, h('p', {class: 'lede'}, t('lookBody'))),
        E(3, h('div', {}, h('div', {class: 'label'}, t('lookAppearance')),
          h('div', {class: 'seg', role: 'radiogroup', 'aria-label': t('lookAppearance'), style: {'--pos': MODES.findIndex(m => m[0] === S.mode)}},
            h('span', {class: 'thumb'}),
            MODES.map(([id, key]) => h('button', {type: 'button', role: 'radio', id: `mode-${id}`, 'data-group': 'mode', 'data-val': id,
                                                  'aria-checked': String(S.mode === id), onclick: () => choose('mode', id)}, t(key)))))),
        E(4, h('div', {}, h('div', {class: 'label'}, `${t('lookColour')} · `, h('b', {id: 'color-name'}, t(COLORS[S.color][3]))),
          h('div', {class: 'swatches', role: 'radiogroup', 'aria-label': t('lookColour')},
            Object.entries(COLORS).map(([id, [,,, key]]) => h('button', {class: 'sw', type: 'button', role: 'radio', id: `color-${id}`,
              'data-group': 'color', 'data-val': id, 'aria-checked': String(S.color === id), 'aria-label': t(key),
              style: {background: swatchBackground(id)}, onclick: () => choose('color', id)}))))),
        E(5, h('div', {class: 'actions'}, primary(t('buttonContinue'), () => { applyLook(); go(1); })))),
      preview(3));
  },

  block() {
    const items = ['blockAds', 'blockTrackers', 'blockBoxes'];
    return h('section', {class: 'step'},
      h('div', {class: 'copy'}, eyebrow(0), heading('blockTitle', 1), E(2, h('p', {class: 'lede'}, t('blockBody'))),
        E(3, h('ul', {class: 'shield-list'}, items.map((key, n) =>
          h('li', {}, h('span', {class: 'ok', style: {'--i': n + 1}}, tickIcon()), h('div', {}, h('b', {}, t(key))))))),
        E(4, h('p', {class: 'note'}, t('blockPerSite'), ' ',
          h('a', {class: 'manage', href: internal('filters'), target: '_blank', id: 'manage-filters'}, t('blockManage')))),
        E(5, h('div', {class: 'actions'}, primary(t('buttonContinue'), () => go(1))))),
      preview(3, {counter: true}));
  },

  search() {
    const options = S.engines.map(e => {
      const look = ENGINE_LOOKS.find(([part]) => e.keyword.toLowerCase().includes(part) || e.name.toLowerCase().includes(part));
      return {id: e.id, name: e.name, desc: look ? t(look[2]) : null, tile: look ? look[1] : 'rgba(255,255,255,.25)',
              letter: (e.name.trim()[0] || '?').toUpperCase()};
    });
    return h('section', {class: 'step'},
      h('div', {class: 'copy'}, eyebrow(0), heading('searchTitle', 1),
        E(2, radioGroup('engine', options, S.engine, optBody)),
        E(3, h('p', {class: 'note'}, t('searchSuggest'))),
        E(4, h('div', {class: 'actions'}, primary(t('buttonContinue'), () => go(1))))),
      preview(3));
  },

  privacy() {
    return h('section', {class: 'step'},
      h('div', {class: 'copy'}, eyebrow(0), heading('privacyTitle', 1), E(2, h('p', {class: 'lede'}, t('privacyBody'))),
        E(3, h('div', {class: 'actions'}, primary(t('privacyOk'), () => go(1))))),
      h('ul', {class: 'shield-list preview'}, PRIVACY.map(([title, desc], n) =>
        E(n + 1, h('li', {}, h('span', {class: 'ok', style: {'--i': n + 1}}, tickIcon()),
          h('div', {}, h('b', {}, t(title)), h('span', {}, t(desc))))))));
  },

  default() {
    return h('section', {class: 'step solo'}, h('div', {class: 'copy'}, eyebrow(0), heading('defaultTitle', 1),
      E(2, h('p', {class: 'lede'}, t('defaultBody'))),
      E(3, h('div', {class: 'actions'},
        primary(t('defaultYes'), () => { chrome.send('setAsDefaultBrowser', [false]); go(1); }, 'make-default'),
        h('button', {class: 'btn ghost', type: 'button', id: 'not-now', onclick: () => go(1)}, t('defaultNo'))))));
  },

  done() {
    const engine = S.engines.find(e => e.id === S.engine);
    const rows = [
      ['summaryImported', S.imported || t('summaryNothing')],
      ['summaryLook', `${t(COLORS[S.color][3])}, ${t(MODES.find(m => m[0] === S.mode)[1])}`],
      ['summarySearch', engine ? engine.name : ''],
      ['summaryDefault', S.isDefault ? t('summaryIsDefault') : t('summaryNotDefault')],
    ];
    return h('section', {class: 'step solo'}, h('div', {class: 'copy'},
      E(0, h('div', {}, heroMark())), heading('doneTitle', 1),
      h('dl', {class: 'summary'}, rows.map(([k, v], j) => h('div', {style: {'--j': j}}, h('dt', {}, t(k)), h('dd', {}, v)))),
      E(6, h('div', {class: 'actions'}, primary(t('doneStart'), finish, 'start')))));
  },
};

/* ---------- import ---------- */

function selectedProfile() {
  return (S.profiles || []).find(p => String(p.index) === S.from) || null;
}
function importPills() {
  const p = selectedProfile();
  return IMPORT_ITEMS.filter(([, , flag]) => !p || p[flag]).map(([id, key]) =>
    h('button', {class: 'pill', type: 'button', role: 'checkbox', id: `item-${id}`, 'aria-checked': String(S.items.has(id)),
                 onclick: e => {
                   S.items.has(id) ? S.items.delete(id) : S.items.add(id);
                   e.currentTarget.setAttribute('aria-checked', String(S.items.has(id)));
                   updateImport();
                 }},
      h('span', {class: 'box'}, tickIcon()), t(key)));
}
function chosenItems() {
  const p = selectedProfile();
  return IMPORT_ITEMS.filter(([id, , flag]) => S.items.has(id) && p && p[flag]);
}
function updateImport(changedFrom) {
  const logo = $('#ho-logo');
  if (!logo) {
    return;
  }
  const p = selectedProfile();
  const n = (S.profiles || []).indexOf(p);
  logo.style.background = p ? BROWSER_TILES[n % BROWSER_TILES.length] : 'rgba(255,255,255,.25)';
  logo.textContent = p ? (p.name.trim()[0] || '?').toUpperCase() : '+';
  if (changedFrom) {
    retrigger(logo, 'flip');
    const items = $('#import-items');
    items.replaceChildren(...importPills());
  }
  $('#ho-flow').classList.toggle('idle', !p || chosenItems().length === 0);
  $('#ho-flow').classList.toggle('fast', S.importing);
  $('#what').classList.toggle('shut', !p);
  $('#skip').hidden = !p;
  const next = $('#next');
  next.firstChild.textContent = p ? t('importButton') : t('buttonContinue');
  next.disabled = S.importing;
  const text = S.importing ? t('importRunning') : !p ? t('importFreshDesc')
      : chosenItems().length === 0 ? t('importPickOne') : t('importLocal');
  const ho = $('#ho-text');
  if (ho.textContent !== text) {
    ho.textContent = text;
    retrigger(ho, 'swap');
  }
}
function runImport() {
  const p = selectedProfile();
  const items = chosenItems();
  if (!p || items.length === 0) {
    go(1);
    return;
  }
  const types = {
    import_dialog_autofill_form_data: false, import_dialog_bookmarks: false, import_dialog_history: false,
    import_dialog_saved_passwords: false, import_dialog_search_engine: false, import_dialog_extensions: false,
  };
  items.forEach(([, , , type]) => { types[type] = true; });
  S.importing = true;
  S.importingName = p.name;
  S.importError = false;
  $('#import-error').hidden = true;
  updateImport();
  chrome.send('importData', [p.index, types]);
}
onBrowserEvent('import-data-status-changed', status => {
  if (!S.importing || (status !== 'succeeded' && status !== 'failed')) {
    return;
  }
  S.importing = false;
  if (status === 'succeeded') {
    S.imported = S.importingName;
    if (S.steps[S.step] === 'import') {
      go(1);
    }
    return;
  }
  S.importError = true;
  if (S.steps[S.step] === 'import') {
    $('#import-error').hidden = false;
    updateImport();
  }
});

/* ---------- choices ---------- */

// Continuing from the colour step applies what it shows, even untouched, so
// the summary and the browser agree.
function applyLook() {
  chrome.send('aerieSetup.setColorScheme', [S.mode]);
  chrome.send('aerieSetup.setThemeColor', [S.color]);
}

function choose(group, val) {
  S[group] = val;  // engine ids are strings such as "db:3"; never coerce them
  $$(`[data-group="${group}"]`).forEach(b => b.setAttribute('aria-checked', String(b.dataset.val === String(val))));
  if (group === 'from') {
    updateImport(true);
  } else if (group === 'mode') {
    $('.seg').style.setProperty('--pos', MODES.findIndex(m => m[0] === S.mode));
    chrome.send('aerieSetup.setColorScheme', [S.mode]);
  } else if (group === 'color') {
    const name = $('#color-name');
    name.textContent = t(COLORS[S.color][3]);
    retrigger(name, 'swap');
    chrome.send('aerieSetup.setThemeColor', [S.color]);
  } else if (group === 'engine') {
    chrome.send('setDefaultSearchEngine', [S.engine, /* ChoiceMadeLocation.OTHER */ 0, null]);
  }
  updatePreview();
}

/* ---------- navigation ---------- */

const SUN = [96, 86, 74, 62, 50, 38, 26, 12];
let busy = false;

function render(dir = 1) {
  const name = S.steps[S.step];
  const stage = $('#stage');
  stage.classList.toggle('back-dir', dir < 0);
  stage.replaceChildren(VIEWS[name]());
  const sun = SUN[Math.round(S.step * (SUN.length - 1) / (S.steps.length - 1))];
  document.documentElement.style.setProperty('--sun-y', `${sun}%`);
  $('#back').classList.toggle('off', S.step === 0 || name === 'done' || S.importing);
  $('#dots').replaceChildren(...S.steps.slice(1, -1).map((_, i) =>
    h('i', {class: i + 1 === S.step ? 'on' : i + 1 < S.step ? 'done' : ''})));
  if (name === 'import') {
    updateImport();
  }
  const focus = stage.querySelector('[aria-checked="true"], #next, #make-default, #start');
  if (focus) {
    setTimeout(() => focus.focus({preventScroll: true}), 400);
  }
}
function go(d) {
  const target = Math.max(0, Math.min(S.steps.length - 1, S.step + d));
  if (busy || target === S.step || S.importing) {
    return;
  }
  busy = true;
  const cur = $('#stage .step');
  if (cur) {
    cur.classList.add('leaving');
    if (d < 0) {
      cur.classList.add('to-back');
    }
  }
  const reduce = matchMedia('(prefers-reduced-motion: reduce)').matches;
  setTimeout(() => { S.step = target; render(d); busy = false; }, reduce ? 0 : 300);
}
async function finish() {
  const start = $('#start');
  if (start) {
    start.disabled = true;
  }
  try {
    await sendWithPromise('setPref', 'completed_onboarding', true);
  } catch (e) {
    console.error('aerie setup: could not save completion', e);
  }
  chrome.send('acceptLatestSchema');
  $('#launch').classList.add('go');
  const reduce = matchMedia('(prefers-reduced-motion: reduce)').matches;
  setTimeout(() => { location.href = internal('newtab'); }, reduce ? 0 : 900);
}

document.addEventListener('keydown', e => {
  const name = S.steps[S.step];
  if (e.key === 'Escape' && S.step > 0 && name !== 'done') {
    e.preventDefault();
    go(-1);
  } else if (e.key === 'Enter' && !(e.target.closest && e.target.closest('button, a'))) {
    e.preventDefault();
    const main = $('#stage #next, #stage #make-default, #stage #start');
    if (main && !main.disabled) {
      main.click();
    }
  }
});

/* ---------- start ---------- */

async function loadBrowserState() {
  sendWithPromise('initializeImportDialog').then(profiles => {
    // Chromium lists "Bookmarks HTML file" as a source; setup imports browsers only.
    S.profiles = (profiles || []).filter(p => !/html/i.test(p.name));
    if (S.profiles.length) {
      S.from = String(S.profiles[0].index);
    }
    if (S.steps[S.step] === 'import') {
      render(0);
    }
  }, () => { S.profiles = []; });

  // The list can be empty while the search engine service loads; the browser
  // then fires 'search-engines-changed' with the real list.
  const loadEngines = info => {
    if (!info) {
      return;
    }
    S.engines = (info.defaults || []).filter(e => e.canBeDefault !== false || e.default);
    const current = S.engines.find(e => e.default);
    const keep = S.engines.find(e => e.id === S.engine);
    S.engine = keep ? keep.id : current ? current.id : (S.engines[0] ? S.engines[0].id : null);
    if (S.steps[S.step] === 'search') {
      render(0);
    }
  };
  onBrowserEvent('search-engines-changed', loadEngines);
  sendWithPromise('getSearchEnginesList').then(loadEngines, () => {});

  onBrowserEvent('browser-default-state-changed', state => { S.isDefault = !!state.isDefault; });
  try {
    const state = await sendWithPromise('requestDefaultBrowserState');
    S.isDefault = !!state.isDefault;
    S.canBeDefault = state.canBeDefault && !state.isDisabledByPolicy && !state.isUnknownError;
  } catch (e) {
    S.canBeDefault = false;
  }
  if (S.isDefault || !S.canBeDefault) {
    S.steps = S.steps.filter(x => x !== 'default');
  }
}

async function start() {
  try {
    await import('/strings.js');
  } catch (e) {
    console.error('aerie setup: strings did not load', e);
  }
  document.title = t('pageTitle');
  $('#back').setAttribute('aria-label', t('buttonBack'));
  $('#back').addEventListener('click', () => go(-1));
  $('#foot').textContent = t('keyboardHint');
  // Only the default-browser check is awaited (it decides how many steps
  // there are); profiles and engines arrive in the background.
  await loadBrowserState();
  render(1);
}
start();
