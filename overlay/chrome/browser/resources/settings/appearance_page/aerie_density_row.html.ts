// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsAerieDensityRowElement} from './aerie_density_row.js';

// Every word is a string of the grd (aerie_sidebar_strings.grdp).
export function getHtml(this: SettingsAerieDensityRowElement) {
  return html`<!--_html_template_start_-->
<div class="settings-box" id="settings-aerie-density-row-box">
  <div class="flex cr-padded-text">
    <div id="settings-aerie-density-row-label">$i18n{aerieSidebarDensity}</div>
    <div class="secondary" id="settings-aerie-density-row-desc">$i18n{aerieSidebarDensityDesc}</div>
  </div>
</div>
<div class="cr-row continuation">
  <settings-radio-group id="settings-aerie-density-row-group"
      pref-key="aerie.sidebar.density"
      group-aria-label="$i18n{aerieSidebarDensity}">
    <controlled-radio-button id="aerie-densityComfortableButton"
        label="$i18n{aerieSidebarDensityComfortable}"
        name="comfortable"
        pref-key="aerie.sidebar.density">
    </controlled-radio-button>
    <controlled-radio-button id="aerie-densityCompactButton"
        label="$i18n{aerieSidebarDensityCompact}"
        name="compact"
        pref-key="aerie.sidebar.density">
    </controlled-radio-button>
    <controlled-radio-button id="aerie-densityRailButton"
        label="$i18n{aerieSidebarDensityRail}"
        name="rail"
        pref-key="aerie.sidebar.density">
    </controlled-radio-button>
    <controlled-radio-button id="aerie-densityAutoHideButton"
        label="$i18n{aerieSidebarDensityAutoHide}"
        name="auto-hide"
        pref-key="aerie.sidebar.density">
    </controlled-radio-button>
  </settings-radio-group>
</div>
<!--_html_template_end_-->`;
}
