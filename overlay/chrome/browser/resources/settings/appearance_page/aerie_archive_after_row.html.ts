// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsAerieArchiveAfterRowElement} from './aerie_archive_after_row.js';

// Every word is a string of the grd (aerie_sidebar_strings.grdp). The label of
// the drop-down is the visible label of the row, so a screen reader reads the
// same words.
export function getHtml(this: SettingsAerieArchiveAfterRowElement) {
  return html`<!--_html_template_start_-->
<div class="settings-box" id="aerieArchiveAfterRow">
  <div class="flex cr-padded-text" id="aerieArchiveAfterText">
    <div id="aerieArchiveAfterLabel">$i18n{aerieArchiveAfter}</div>
    <div class="secondary" id="aerieArchiveAfterDesc">$i18n{aerieArchiveAfterDesc}</div>
  </div>
  <settings-dropdown-menu id="aerieArchiveAfterMenu"
      pref-key="aerie.tabs.archive_after"
      label="$i18n{aerieArchiveAfter}"
      .menuOptions="${this.menuOptions_}">
  </settings-dropdown-menu>
</div>
<!--_html_template_end_-->`;
}
