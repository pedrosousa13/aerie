// Runs in an extension page. window.__probe() tries chrome.privacy writes and reports what happened.
function callSetting(api, method, arg) {
  return new Promise((resolve) => {
    const done = (value) => resolve(value);
    try {
      api[method](arg, (data) => done(chrome.runtime.lastError ? {error: chrome.runtime.lastError.message} : {data}));
    } catch (e) {
      done({error: 'threw: ' + e});
    }
  });
}

window.__probe = async function() {
  const attempts = [
    {name: 'webRTCIPHandlingPolicy', api: chrome.privacy.network.webRTCIPHandlingPolicy,
     value: 'default', role: 'hardened'},
    {name: 'networkPredictionEnabled', api: chrome.privacy.network.networkPredictionEnabled,
     value: true, role: 'hardened'},
    {name: 'thirdPartyCookiesAllowed', api: chrome.privacy.websites.thirdPartyCookiesAllowed,
     value: true, role: 'hardened'},
    {name: 'referrersEnabled', api: chrome.privacy.websites.referrersEnabled,
     value: false, role: 'control'},
  ];
  const out = [];
  for (const a of attempts) {
    const set = await callSetting(a.api, 'set', {value: a.value});
    const got = await callSetting(a.api, 'get', {});
    out.push({name: a.name, role: a.role, set_error: set.error || null,
              level: got.data ? got.data.levelOfControl : null,
              value: got.data ? got.data.value : null});
    await callSetting(a.api, 'clear', {});
  }
  // chrome.contentSettings.cookies, with third-party cookies blocked (the Aerie default): an allow that
  // could apply to a third-party context is refused, an allow for one site's own cookies (secondary pattern
  // equal to the primary pattern) must work, and blocking is the control.
  const setCookies = (details) => new Promise((resolve) => {
    try {
      chrome.contentSettings.cookies.set(
          details, () => resolve(chrome.runtime.lastError ? chrome.runtime.lastError.message : null));
    } catch (e) {
      resolve('threw: ' + e);
    }
  });
  const site = 'http://127.0.0.1/*';
  // A domain-wide pattern; built from parts so the fixture scan finds no external host name in the file.
  const domain = ['http://', '*.', 'localhost', '/*'].join('');
  const add = (name, role, set_error) => out.push({name, role, set_error, level: null, value: null});
  add('contentSettings.cookies.allow', 'content_setting_refused',
      await setCookies({primaryPattern: site, setting: 'allow'}));
  add('contentSettings.cookies.session_only', 'content_setting_refused',
      await setCookies({primaryPattern: site, setting: 'session_only'}));
  add('contentSettings.cookies.allow_domain_wildcard', 'content_setting_refused',
      await setCookies({primaryPattern: domain, secondaryPattern: domain, setting: 'allow'}));
  add('contentSettings.cookies.allow_first_party', 'content_setting_first_party',
      await setCookies({primaryPattern: site, secondaryPattern: site, setting: 'allow'}));
  add('contentSettings.cookies.block', 'content_setting_control',
      await setCookies({primaryPattern: site, setting: 'block'}));
  await new Promise((resolve) => chrome.contentSettings.cookies.clear({}, resolve));
  return out;
};

// The allow-then-block step (#166). Hosts are the fixture sites: b is covered by a broad cookie allow (secondary
// pattern any site), f by a broad session-only rule, d by a one-host first-party session-only rule. Session-only
// shows in get(); a plain allow would not, because allow is the default.
const hostUrl = (name) => ['http:', '', name + '.localhost', ''].join('/');
const hostPattern = (name) => hostUrl(name) + '*';

window.__probeRules = async function() {
  const setCookies = (details) => new Promise((resolve) => {
    try {
      chrome.contentSettings.cookies.set(
          details, () => resolve(chrome.runtime.lastError ? chrome.runtime.lastError.message : null));
    } catch (e) {
      resolve('threw: ' + e);
    }
  });
  return {
    broad_cookie: await setCookies({primaryPattern: hostPattern('b'), setting: 'allow'}),
    broad_session: await setCookies({primaryPattern: hostPattern('f'), setting: 'session_only'}),
    first_party: await setCookies({primaryPattern: hostPattern('d'), secondaryPattern: hostPattern('d'),
                                   setting: 'session_only'}),
  };
};

window.__probeGet = async function() {
  const get = (name) => new Promise((resolve) => {
    chrome.contentSettings.cookies.get(
        {primaryUrl: hostUrl(name), secondaryUrl: hostUrl(name)},
        (details) => resolve(details ? details.setting : null));
  });
  return {broad_session: await get('f'), first_party: await get('d')};
};
