// Workspace test site: the page the Bench drives in every Workspace (issue #73).
// Every function is async, takes and returns plain JSON, and reads or writes ONE kind of state.
// The Python side (tools/aerie_bench/checks/workspace_isolation.py) says what each should show.
(() => {
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const idb = () => new Promise((resolve, reject) => {
    const req = indexedDB.open('ws-probe', 1);
    req.onupgradeneeded = () => req.result.createObjectStore('kv');
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
  const idbDo = async (mode, fn) => {
    const db = await idb();
    return new Promise((resolve, reject) => {
      const tx = db.transaction('kv', mode);
      const req = fn(tx.objectStore('kv'));
      tx.oncomplete = () => { db.close(); resolve(req.result); };
      tx.onerror = () => reject(tx.error);
    });
  };
  const opfs = async () => (await navigator.storage.getDirectory());
  const channels = {};
  const locks = {};

  const surfaces = {
    // The server's view: Set-Cookie on login, then what the server got back.
    cookie: {
      put: async (t) => { await fetch('/ws/login?user=' + encodeURIComponent(t), {credentials: 'same-origin'}); },
      get: async () => (await (await fetch('/ws/whoami', {credentials: 'same-origin', cache: 'no-store'})).json()).user,
    },
    localStorage: {
      put: async (t) => localStorage.setItem('user', t),
      get: async () => localStorage.getItem('user') || '',
    },
    sessionStorage: {
      put: async (t) => sessionStorage.setItem('user', t),
      get: async () => sessionStorage.getItem('user') || '',
    },
    indexedDB: {
      put: async (t) => { await idbDo('readwrite', (s) => s.put(t, 'user')); },
      get: async () => (await idbDo('readonly', (s) => s.get('user'))) || '',
    },
    cacheStorage: {
      put: async (t) => { const c = await caches.open('ws-probe'); await c.put('/ws/cache-entry', new Response(t)); },
      get: async () => {
        const c = await caches.open('ws-probe');
        const r = await c.match('/ws/cache-entry');
        return r ? await r.text() : '';
      },
    },
    opfs: {
      put: async (t) => {
        const f = await (await opfs()).getFileHandle('user.txt', {create: true});
        const w = await f.createWritable();
        await w.write(t);
        await w.close();
      },
      get: async () => {
        try {
          const f = await (await opfs()).getFileHandle('user.txt');
          return await (await f.getFile()).text();
        } catch (e) { return ''; }
      },
    },
  };

  window.wsProbe = {
    put: (surface, token) => surfaces[surface].put(token),
    get: (surface) => surfaces[surface].get(),
    whoami: async () => ({
      cookie: await surfaces.cookie.get(),
      localStorage: await surfaces.localStorage.get(),
      sessionStorage: await surfaces.sessionStorage.get(),
    }),
    hasOpener: async () => window.opener !== null,

    bcListen: async (name) => {
      channels[name] = {got: [], ch: new BroadcastChannel(name)};
      channels[name].ch.onmessage = (e) => channels[name].got.push(e.data);
      return true;
    },
    bcPost: async (name, token) => { const c = new BroadcastChannel(name); c.postMessage(token); c.close(); return true; },
    bcReceived: async (name) => { await sleep(400); return (channels[name] || {got: []}).got; },

    lockHold: async (name) => new Promise((resolve) => {
      navigator.locks.request(name, () => { resolve(true); return new Promise(() => {}); });
    }),
    lockTry: async (name) => navigator.locks.request(name, {ifAvailable: true}, (lock) => lock !== null),

    sharedCounter: (name) => new Promise((resolve) => {
      const w = new SharedWorker('/ws/shared.js', name);
      w.port.onmessage = (e) => resolve(e.data);
      w.port.start();
    }),

    blobMake: async (token) => URL.createObjectURL(new Blob([token], {type: 'text/plain'})),
    blobRead: async (url) => { try { return await (await fetch(url)).text(); } catch (e) { return ''; } },

    // Scope paths must sit under /ws/ (the worker script's directory). The page itself is not
    // controlled by them, so wait for the worker to activate instead of for navigator.serviceWorker.ready.
    swRegister: async (scopePath) => {
      const reg = await navigator.serviceWorker.register('/ws/sw.js', {scope: scopePath});
      for (let i = 0; i < 100 && !reg.active; i++) await sleep(100);
      return reg.scope;
    },
    swScopes: async () => (await navigator.serviceWorker.getRegistrations()).map((r) => r.scope),

    // The standard cache probe: how long a fetch takes, and whether it can be answered from cache only.
    fetchCached: async (id, delayMs) => {
      const t0 = performance.now();
      const text = await (await fetch('/ws/cached?id=' + encodeURIComponent(id) + '&delay_ms=' + delayMs)).text();
      return {ms: performance.now() - t0, text};
    },
    fetchOnlyIfCached: async (id, delayMs) => {
      try {
        const r = await fetch('/ws/cached?id=' + encodeURIComponent(id) + '&delay_ms=' + delayMs,
                              {cache: 'only-if-cached', mode: 'same-origin'});
        return r.ok ? 'hit' : 'miss';
      } catch (e) { return 'miss'; }
    },

    authProbe: async (tag) => (await (await fetch('/ws/auth/' + tag + '/probe', {cache: 'no-store'})).json()).sent,
    ping: async (who) => (await fetch('/ws/ping?w=' + encodeURIComponent(who))).status,
    hstsSet: async (url) => { await fetch(url, {mode: 'no-cors', cache: 'no-store'}); return true; },
    permission: async () => Notification.permission,
    // Issue #121: the canvas, audio and core values a tracker reads. Same drawing and graph as
    // fixtures/hardening/fingerprint.html, so the two Bench checks measure the same surfaces.
    fingerprint: async () => {
      const fnv = (str) => {
        let h = 0x811c9dc5;
        for (let i = 0; i < str.length; i++) { h ^= str.charCodeAt(i); h = Math.imul(h, 0x01000193) >>> 0; }
        return h.toString(16);
      };
      const c = document.createElement('canvas');
      c.width = 240; c.height = 80;
      const x = c.getContext('2d');
      x.textBaseline = 'top'; x.font = '18px Arial';
      x.fillStyle = '#f60'; x.fillRect(10, 10, 100, 40);
      x.fillStyle = '#069'; x.fillText('Aerie fingerprint 1234 \u00e9\u00fc', 4, 20);
      x.strokeStyle = 'rgba(102, 204, 0, 0.7)';
      x.beginPath(); x.arc(150, 40, 25, 0, Math.PI * 2); x.stroke();
      const ctx = new OfflineAudioContext(1, 44100, 44100);
      const osc = ctx.createOscillator();
      osc.type = 'triangle'; osc.frequency.value = 10000;
      const comp = ctx.createDynamicsCompressor();
      comp.threshold.value = -50; comp.knee.value = 40; comp.ratio.value = 12;
      comp.attack.value = 0; comp.release.value = 0.25;
      osc.connect(comp); comp.connect(ctx.destination); osc.start(0);
      const data = (await ctx.startRendering()).getChannelData(0);
      let s = '';
      for (let i = 4500; i < 5000; i++) s += data[i].toString() + ',';
      return {canvas: fnv(c.toDataURL()), audio: fnv(s), cores: navigator.hardwareConcurrency};
    },
    loadScript: () => new Promise((resolve, reject) => {
      const s = document.createElement('script');
      s.src = '/ws/code.js';
      s.onload = () => resolve(true);
      s.onerror = () => reject(new Error('script'));
      document.head.appendChild(s);
    }),
    // Where the link on link.html sits, for the Bench's screenshot.
    linkRect: async () => { const r = document.getElementById('t').getBoundingClientRect(); return {x: r.x, y: r.y, width: r.width, height: r.height}; },
  };
})();
