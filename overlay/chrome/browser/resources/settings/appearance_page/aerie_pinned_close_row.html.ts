// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsAeriePinnedCloseRowElement} from './aerie_pinned_close_row.js';

// Every word is a string of the grd (aerie_sidebar_strings.grdp). The label of
// the drop-down is the visible label of the row, so a screen reader reads the
// same words.
export function getHtml(this: SettingsAeriePinnedCloseRowElement) {
  return html`<!--_html_template_start_-->
<div class="settings-box" id="aeriePinnedCloseRow">
  <div class="flex cr-padded-text" id="aeriePinnedCloseText">
    <div id="aeriePinnedCloseLabel">$i18n{aeriePinnedClose}</div>
    <div class="secondary" id="aeriePinnedCloseDesc">$i18n{aeriePinnedCloseDesc}</div>
  </div>
  <settings-dropdown-menu id="aeriePinnedCloseMenu"
      pref-key="aerie.tabs.pinned_close"
      label="$i18n{aeriePinnedClose}"
      .menuOptions="${this.menuOptions_}">
  </settings-dropdown-menu>
</div>
<!--_html_template_end_-->`;
}
