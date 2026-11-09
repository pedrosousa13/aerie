// Runs as a dedicated worker and as a shared worker (issue #113): the same measurement as the page.
importScripts('fingerprint_measure.js');
if (typeof SharedWorkerGlobalScope !== 'undefined' && self instanceof SharedWorkerGlobalScope) {
  onconnect = (e) => {
    try { e.ports[0].postMessage(measureContext(new OffscreenCanvas(1, 1))); }
    catch (err) { e.ports[0].postMessage({error: String(err)}); }
  };
} else {
  try { postMessage(measureContext(new OffscreenCanvas(1, 1))); }
  catch (err) { postMessage({error: String(err)}); }
}
