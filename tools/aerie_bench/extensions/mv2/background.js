// Manifest V2 capability that matters for ad blockers: cancel a request before it goes out.
// The extension records every cancel, so the Bench can ask the extension itself (not the
// network log) whether it was the one that blocked the request.
var aerieCancelled = [];
chrome.webRequest.onBeforeRequest.addListener(
  function (details) { aerieCancelled.push(details.url); return { cancel: true }; },
  { urls: ["http://127.0.0.1/*/ext-block-me.js*"] },
  ["blocking"]);
