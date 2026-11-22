// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsAeriePreviewCardRowElement} from './aerie_preview_card_row.js';

// Every word is a string of the grd (aerie_sidebar_strings.grdp).
export function getHtml(this: SettingsAeriePreviewCardRowElement) {
  return html`<!--_html_template_start_-->
<settings-toggle-button id="aeriePreviewCardToggle"
    pref-key="aerie.sidebar.hover_card_snapshots"
    label="$i18n{aeriePreviewCard}"
    sub-label="$i18n{aeriePreviewCardDesc}">
</settings-toggle-button>
<!--_html_template_end_-->`;
}
