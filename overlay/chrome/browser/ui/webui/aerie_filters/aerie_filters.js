// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// The filter list settings page. The browser calls aerieFilters.setState(state)
// now and after every change; the page keeps the last state in
// aerieFilters.state (the Bench reads it). Text from lists and sites is only
// ever put in with textContent, never as HTML.
'use strict';

window.aerieFilters = {
  state: {ready: false, autoUpdate: true, exceptions: [], lists: [],
          addError: '', busy: false},

  setState(state) {
    this.state = state;
    render(state);
  },
};

function send(name, args) {
  chrome.send('aerieFilters.' + name, args || []);
}

function el(tag, text, className) {
  const node = document.createElement(tag);
  if (text !== undefined) {
    node.textContent = text;
  }
  if (className) {
    node.className = className;
  }
  return node;
}

function button(label, onclick) {
  const b = el('button', label);
  b.type = 'button';
  b.addEventListener('click', onclick);
  return b;
}

function formatUpdated(ms) {
  return ms === null ? 'Shipped with Aerie' : new Date(ms).toLocaleString();
}

function renderLists(lists) {
  const body = document.getElementById('lists');
  body.replaceChildren();
  for (const list of lists) {
    const row = el('tr');
    const name = el('td');
    name.append(el('div', list.title || list.url));
    name.append(el('div', list.url, 'url'));
    if (list.error) {
      name.append(el('div', list.error, 'error'));
    }
    row.append(name);
    row.append(el('td', list.rules ? String(list.rules) : '-'));
    row.append(el('td', formatUpdated(list.updated)));
    const actions = el('td');
    actions.append(button('Update', () => send('updateList', [list.id])));
    if (!list.builtin) {
      actions.append(' ');
      actions.append(button('Remove', () => send('removeList', [list.id])));
    }
    row.append(actions);
    body.append(row);
  }
}

function renderExceptions(hosts) {
  const ul = document.getElementById('exceptions');
  ul.replaceChildren();
  if (hosts.length === 0) {
    ul.append(el('li', 'None', 'muted'));
  }
  for (const host of hosts) {
    const li = el('li');
    li.append(el('span', host));
    li.append(button('Turn blocking on', () => send('setException', [host, false])));
    ul.append(li);
  }
}

function render(state) {
  renderLists(state.lists);
  renderExceptions(state.exceptions);
  document.getElementById('auto-update').checked = state.autoUpdate;
  document.getElementById('add-error').textContent = state.addError;
  document.getElementById('busy').textContent = state.busy ? 'Working...' : '';
}

document.getElementById('add-list').addEventListener('submit', (event) => {
  event.preventDefault();
  const input = document.getElementById('list-url');
  send('addList', [input.value.trim()]);
  input.value = '';
});
document.getElementById('add-site').addEventListener('submit', (event) => {
  event.preventDefault();
  const input = document.getElementById('site-host');
  send('setException', [input.value.trim(), true]);
  input.value = '';
});
document.getElementById('update-all').addEventListener('click', () => send('updateAll'));
document.getElementById('auto-update').addEventListener('change', (event) => {
  send('setAutoUpdate', [event.target.checked]);
});

send('getState');
