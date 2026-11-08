// Workspace test site: a script big enough to be worth a code cache entry.
window.wsCodeLoaded = (window.wsCodeLoaded || 0) + 1;
function wsBusy(n) { let t = 0; for (let i = 0; i < n; i++) { t += i % 7; } return t; }
window.wsBusyResult = wsBusy(1000);
