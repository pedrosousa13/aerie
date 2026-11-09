// Shared by the page and by the dedicated and shared workers of fingerprint_worker.html (issue #113).
// Measures one execution context: a hash of a canvas read-back and the core count. The drawing has no
// text (so the window and a worker cannot differ in fonts) and the read-back is getImageData, which is
// noised in a window and, with #113, in a worker too.
function fnv(bytes) {
  let h = 0x811c9dc5;
  for (let i = 0; i < bytes.length; i++) { h ^= bytes[i]; h = Math.imul(h, 0x01000193) >>> 0; }
  return h.toString(16);
}
function measureContext(canvas) {
  canvas.width = 120; canvas.height = 60;
  const x = canvas.getContext('2d');
  x.fillStyle = '#f60'; x.fillRect(10, 10, 60, 30);
  x.fillStyle = '#069'; x.fillRect(40, 25, 60, 25);
  const g = x.createLinearGradient(0, 0, 120, 0);
  g.addColorStop(0, 'rgba(102, 204, 0, 0.8)'); g.addColorStop(1, 'rgba(204, 0, 102, 0.8)');
  x.fillStyle = g; x.fillRect(0, 45, 120, 12);
  x.strokeStyle = 'rgba(0, 0, 0, 0.6)'; x.beginPath(); x.arc(80, 25, 18, 0, Math.PI * 2); x.stroke();
  return {canvas: fnv(x.getImageData(0, 0, 120, 60).data), cores: navigator.hardwareConcurrency};
}
