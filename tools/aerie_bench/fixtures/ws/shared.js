// Workspace test site: a shared worker that counts the pages that connected to it.
let connections = 0;
onconnect = (e) => {
  connections += 1;
  e.ports[0].postMessage(connections);
};
