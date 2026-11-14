// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-aerie-archive-keep-row' is the "Keep archived tabs for" row of
 * the Sidebar section of Settings (issue #183). It writes the profile pref
 * aerie.tabs.archive_keep: "7d", "30d" (default), "90d" or "forever"
 * (components/aerie_tab_model/tab_model_prefs.h). The archive reads the pref when it
 * drops old records, so a change needs no restart. The drop-down is the settings-dropdown-menu, a
 * native select, like the other Aerie rows.
 */

import '../controls/settings_dropdown_menu.js';
import '../i18n_setup.js';

import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {DropdownMenuOptionList} from '../controls/settings_dropdown_menu.js';
import {loadTimeData} from '../i18n_setup.js';
import {getCss as getSettingsSharedLit} from '../settings_shared_lit.css.js';

import {getHtml} from './aerie_archive_keep_row.html.js';

export class SettingsAerieArchiveKeepRowElement extends CrLitElement {
  static get is() {
    return 'settings-aerie-archive-keep-row';
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
    {value: '7d', name: loadTimeData.getString('aerieArchiveKeep7Days')},
    {value: '30d', name: loadTimeData.getString('aerieArchiveKeep30Days')},
    {value: '90d', name: loadTimeData.getString('aerieArchiveKeep90Days')},
    {value: 'forever', name: loadTimeData.getString('aerieArchiveKeepForever')},
  ];
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-aerie-archive-keep-row': SettingsAerieArchiveKeepRowElement;
  }
}

customElements.define(
    SettingsAerieArchiveKeepRowElement.is,
    SettingsAerieArchiveKeepRowElement);
