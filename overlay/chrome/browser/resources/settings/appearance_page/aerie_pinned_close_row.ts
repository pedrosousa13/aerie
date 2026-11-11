// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-aerie-pinned-close-row' is the "Close a Pinned tab" row of the
 * Sidebar section of Settings (issue #180). It writes the profile pref
 * aerie.tabs.pinned_close: "reset-sleep-switch" (default), "sleep" or "close"
 * (components/aerie_tab_model/tab_model_prefs.h). The browser reads the pref at
 * every Ctrl+W or click on the close button of a Pinned or Favorites tab, so a
 * change needs no restart. The drop-down is the settings-dropdown-menu, a
 * native select, like the other Aerie rows.
 */

import '../controls/settings_dropdown_menu.js';
import '../i18n_setup.js';

import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {DropdownMenuOptionList} from '../controls/settings_dropdown_menu.js';
import {loadTimeData} from '../i18n_setup.js';
import {getCss as getSettingsSharedLit} from '../settings_shared_lit.css.js';

import {getHtml} from './aerie_pinned_close_row.html.js';

export class SettingsAeriePinnedCloseRowElement extends CrLitElement {
  static get is() {
    return 'settings-aerie-pinned-close-row';
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

  // The values are the pref's three values; the names are strings of the grd.
  protected accessor menuOptions_: DropdownMenuOptionList = [
    {
      value: 'reset-sleep-switch',
      name: loadTimeData.getString('aeriePinnedCloseResetSleepSwitch'),
    },
    {value: 'sleep', name: loadTimeData.getString('aeriePinnedCloseSleep')},
    {value: 'close', name: loadTimeData.getString('aeriePinnedCloseClose')},
  ];
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-aerie-pinned-close-row': SettingsAeriePinnedCloseRowElement;
  }
}

customElements.define(
    SettingsAeriePinnedCloseRowElement.is, SettingsAeriePinnedCloseRowElement);
