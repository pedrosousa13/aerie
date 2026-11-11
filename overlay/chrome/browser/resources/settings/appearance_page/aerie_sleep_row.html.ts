// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsAerieSleepRowElement} from './aerie_sleep_row.js';

// Every word is a string of the grd (aerie_sidebar_strings.grdp). The label of
// the drop-down is the visible label of the row, so a screen reader reads the
// same words.
export function getHtml(this: SettingsAerieSleepRowElement) {
  return html`<!--_html_template_start_-->
<div class="settings-box first" id="aerieSleepRow">
  <div class="flex cr-padded-text" id="aerieSleepText">
    <div id="aerieSleepLabel">$i18n{aerieSleepAfter}</div>
    <div class="secondary" id="aerieSleepDesc">$i18n{aerieSleepAfterDesc}</div>
  </div>
  <settings-dropdown-menu id="aerieSleepMenu"
      pref-key="aerie.tabs.sleep_after_minutes"
      label="$i18n{aerieSleepAfter}"
      .menuOptions="${this.menuOptions_}">
  </settings-dropdown-menu>
</div>
<!--_html_template_end_-->`;
}
