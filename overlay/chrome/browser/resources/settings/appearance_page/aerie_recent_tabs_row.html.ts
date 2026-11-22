// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsAerieRecentTabsRowElement} from './aerie_recent_tabs_row.js';

// Every word is a string of the grd (aerie_sidebar_strings.grdp).
export function getHtml(this: SettingsAerieRecentTabsRowElement) {
  return html`<!--_html_template_start_-->
<settings-toggle-button id="aerieRecentTabsToggle"
    pref-key="aerie.sidebar.recent_tab_switcher"
    label="$i18n{aerieRecentTabs}"
    sub-label="$i18n{aerieRecentTabsDesc}">
</settings-toggle-button>
<!--_html_template_end_-->`;
}
