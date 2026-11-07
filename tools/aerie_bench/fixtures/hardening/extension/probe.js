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
  return out;
};
