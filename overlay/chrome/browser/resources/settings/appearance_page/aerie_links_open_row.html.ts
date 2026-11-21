// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsAerieLinksOpenRowElement} from './aerie_links_open_row.js';

// Every word is a string of the grd (aerie_sidebar_strings.grdp). The label of
// the drop-down is the visible label of the row, so a screen reader reads the
// same words.
export function getHtml(this: SettingsAerieLinksOpenRowElement) {
  return html`<!--_html_template_start_-->
<div class="settings-box" id="aerieLinksOpenRow">
  <div class="flex cr-padded-text" id="aerieLinksOpenText">
    <div id="aerieLinksOpenLabel">$i18n{aerieLinksOpen}</div>
    <div class="secondary" id="aerieLinksOpenDesc">$i18n{aerieLinksOpenDesc}</div>
  </div>
  <settings-dropdown-menu id="aerieLinksOpenMenu"
      pref-key="aerie.tabs.links_open"
      label="$i18n{aerieLinksOpen}"
      .menuOptions="${this.menuOptions_}">
  </settings-dropdown-menu>
</div>
<!--_html_template_end_-->`;
}
