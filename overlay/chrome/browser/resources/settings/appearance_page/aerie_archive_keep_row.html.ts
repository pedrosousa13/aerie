// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsAerieArchiveKeepRowElement} from './aerie_archive_keep_row.js';

// Every word is a string of the grd (aerie_sidebar_strings.grdp). The label of
// the drop-down is the visible label of the row, so a screen reader reads the
// same words.
export function getHtml(this: SettingsAerieArchiveKeepRowElement) {
  return html`<!--_html_template_start_-->
<div class="settings-box" id="aerieArchiveKeepRow">
  <div class="flex cr-padded-text" id="aerieArchiveKeepText">
    <div id="aerieArchiveKeepLabel">$i18n{aerieArchiveKeep}</div>
    <div class="secondary" id="aerieArchiveKeepDesc">$i18n{aerieArchiveKeepDesc}</div>
  </div>
  <settings-dropdown-menu id="aerieArchiveKeepMenu"
      pref-key="aerie.tabs.archive_keep"
      label="$i18n{aerieArchiveKeep}"
      .menuOptions="${this.menuOptions_}">
  </settings-dropdown-menu>
</div>
<!--_html_template_end_-->`;
}
