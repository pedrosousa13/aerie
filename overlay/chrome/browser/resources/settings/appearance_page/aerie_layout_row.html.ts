// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsAerieLayoutRowElement} from './aerie_layout_row.js';

// Every word is a string of the grd (aerie_sidebar_strings.grdp).
export function getHtml(this: SettingsAerieLayoutRowElement) {
  return html`<!--_html_template_start_-->
<div class="settings-box" id="settings-aerie-layout-row-box">
  <div class="flex cr-padded-text">
    <div id="settings-aerie-layout-row-label">$i18n{aerieSidebarLayout}</div>
    <div class="secondary" id="settings-aerie-layout-row-desc">$i18n{aerieSidebarLayoutDesc}</div>
  </div>
</div>
<div class="cr-row continuation">
  <settings-radio-group id="settings-aerie-layout-row-group"
      pref-key="aerie.sidebar.layout"
      group-aria-label="$i18n{aerieSidebarLayout}">
    <controlled-radio-button id="aerie-layoutSidebarButton"
        label="$i18n{aerieSidebarLayoutSidebar}"
        name="sidebar"
        pref-key="aerie.sidebar.layout">
    </controlled-radio-button>
    <controlled-radio-button id="aerie-layoutTwoRowsButton"
        label="$i18n{aerieSidebarLayoutTwoRows}"
        name="horizontal"
        pref-key="aerie.sidebar.layout">
    </controlled-radio-button>
    <controlled-radio-button id="aerie-layoutOneRowButton"
        label="$i18n{aerieSidebarLayoutOneRow}"
        name="horizontal-one-row"
        pref-key="aerie.sidebar.layout">
    </controlled-radio-button>
  </settings-radio-group>
</div>
<!--_html_template_end_-->`;
}
