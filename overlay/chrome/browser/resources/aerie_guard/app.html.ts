// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {AerieGuardAppElement} from './app.js';

export function getHtml(this: AerieGuardAppElement) {
  // clang-format off
  return html`<!--_html_template_start_-->
${this.state_ ? html`
  <header id="site">
    <img id="favicon" alt="" src="${this.state_.faviconUrl}">
    <span id="host" title="${this.state_.host}">${this.state_.host}</span>
  </header>
  <section id="count" aria-live="polite">
    <div id="number" class="${this.state_.blockingOn ? '' : 'off'}">
      ${this.state_.blockedCount}
    </div>
    <div id="summary">${this.state_.blockedSummary}</div>
  </section>
  <section id="control">
    <div id="texts">
      <div id="label">${this.i18n_('switchLabel')}</div>
      <div id="status">${this.statusText_()}</div>
    </div>
    <cr-toggle id="toggle" ?checked="${this.state_.blockingOn}"
        ?disabled="${this.state_.locked}"
        aria-label="${this.i18n_('switchLabel')}"
        @change="${this.onToggleChange_}">
    </cr-toggle>
  </section>
  <p id="scope">${this.scopeText_()}</p>
  ${this.noteText_() ? html`<p id="note">${this.noteText_()}</p>` : ''}
  ${this.advancedRows_.length ? html`
    <details id="advanced">
      <summary>${this.i18n_('advanced')}</summary>
      ${this.advancedRows_.map(row => html`<div class="row">${row}</div>`)}
    </details>` : ''}
` : this.loaded_ ? html`
  <p id="empty">${this.i18n_('noSite')}</p>
` : ''}
<footer>
  <cr-button id="filterLists" class="text-button"
      @click="${this.onFilterListsClick_}">
    ${this.i18n_('filterLists')}
  </cr-button>
</footer>
<!--_html_template_end_-->`;
  // clang-format on
}
