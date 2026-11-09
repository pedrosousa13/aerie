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
  // chrome.contentSettings.cookies: allowing is refused (it would let third-party cookies through),
  // blocking is the control and must still work.
  const setCookies = (setting) => new Promise((resolve) => {
    try {
      chrome.contentSettings.cookies.set(
          {primaryPattern: 'http://127.0.0.1/*', setting},
          () => resolve(chrome.runtime.lastError ? chrome.runtime.lastError.message : null));
    } catch (e) {
      resolve('threw: ' + e);
    }
  });
  const allowError = await setCookies('allow');
  out.push({name: 'contentSettings.cookies.allow', role: 'content_setting_refused', set_error: allowError,
            level: null, value: null});
  const blockError = await setCookies('block');
  out.push({name: 'contentSettings.cookies.block', role: 'content_setting_control', set_error: blockError,
            level: null, value: null});
  await new Promise((resolve) => chrome.contentSettings.cookies.clear({}, resolve));
  return out;
};
