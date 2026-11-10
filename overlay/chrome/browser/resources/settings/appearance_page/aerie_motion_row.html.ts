// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsAerieMotionRowElement} from './aerie_motion_row.js';

// Every word is a string of the grd (aerie_motion_strings.grdp). The label of
// the drop-down is the visible label of the row, so a screen reader reads the
// same words.
export function getHtml(this: SettingsAerieMotionRowElement) {
  return html`<!--_html_template_start_-->
<div class="settings-row" id="aerieMotionRow">
  <div class="flex cr-padded-text" id="aerieMotionText">
    <div id="aerieMotionLabel">$i18n{aerieMotionReduce}</div>
    <div class="secondary" id="aerieMotionDesc">$i18n{aerieMotionReduceDesc}</div>
  </div>
  <settings-dropdown-menu id="aerieMotionMenu"
      pref-key="aerie.motion.reduce"
      label="$i18n{aerieMotionReduce}"
      .menuOptions="${this.menuOptions_}">
  </settings-dropdown-menu>
</div>
<!--_html_template_end_-->`;
}
