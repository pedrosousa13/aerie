// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-aerie-archive-after-row' is the "Archive Today tabs after" row of
 * the Sidebar section of Settings (issue #183). It writes the profile pref
 * aerie.tabs.archive_after: "12h" (default), "24h", "7d" or "never"
 * (components/aerie_tab_model/tab_model_prefs.h). It is the same pref the
 * header menu of the Archive panel writes. The archiver watches the pref, so a
 * change needs no restart. The drop-down is the settings-dropdown-menu, a
 * native select, like the other Aerie rows.
 */

import '../controls/settings_dropdown_menu.js';
import '../i18n_setup.js';

import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {DropdownMenuOptionList} from '../controls/settings_dropdown_menu.js';
import {loadTimeData} from '../i18n_setup.js';
import {getCss as getSettingsSharedLit} from '../settings_shared_lit.css.js';

import {getHtml} from './aerie_archive_after_row.html.js';

export class SettingsAerieArchiveAfterRowElement extends CrLitElement {
  static get is() {
    return 'settings-aerie-archive-after-row';
  }

  static override get styles() {
    return getSettingsSharedLit();
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      menuOptions_: {type: Array},
    };
  }

  // The values are the pref's four values; the names are strings of the grd.
  protected accessor menuOptions_: DropdownMenuOptionList = [
    {value: '12h', name: loadTimeData.getString('aerieArchiveAfter12Hours')},
    {value: '24h', name: loadTimeData.getString('aerieArchiveAfter24Hours')},
    {value: '7d', name: loadTimeData.getString('aerieArchiveAfter7Days')},
    {value: 'never', name: loadTimeData.getString('aerieArchiveAfterNever')},
  ];
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-aerie-archive-after-row': SettingsAerieArchiveAfterRowElement;
  }
}

customElements.define(
    SettingsAerieArchiveAfterRowElement.is,
    SettingsAerieArchiveAfterRowElement);
