// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsAerieSideRowElement} from './aerie_side_row.js';

// Every word is a string of the grd (aerie_sidebar_strings.grdp).
export function getHtml(this: SettingsAerieSideRowElement) {
  return html`<!--_html_template_start_-->
<div class="settings-box" id="settings-aerie-side-row-box">
  <div class="flex cr-padded-text">
    <div id="settings-aerie-side-row-label">$i18n{aerieSidebarSide}</div>
    <div class="secondary" id="settings-aerie-side-row-desc">$i18n{aerieSidebarSideDesc}</div>
  </div>
</div>
<div class="cr-row continuation">
  <settings-radio-group id="settings-aerie-side-row-group"
      pref-key="aerie.sidebar.side"
      group-aria-label="$i18n{aerieSidebarSide}">
    <controlled-radio-button id="aerie-sideLeftButton"
        label="$i18n{aerieSidebarSideLeft}"
        name="left"
        pref-key="aerie.sidebar.side">
    </controlled-radio-button>
    <controlled-radio-button id="aerie-sideRightButton"
        label="$i18n{aerieSidebarSideRight}"
        name="right"
        pref-key="aerie.sidebar.side">
    </controlled-radio-button>
  </settings-radio-group>
</div>
<!--_html_template_end_-->`;
}
