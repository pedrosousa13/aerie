// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-aerie-links-open-row' is the "Links open" row of the Sidebar
 * section of Settings (issue #141). It writes the profile pref
 * aerie.tabs.links_open: "new-tab" (default) or "in-place"
 * (components/aerie_tab_model/tab_model_prefs.h). It is the value that a
 * folder's own "Links open" inherits. The browser reads the pref each time a
 * page asks for a new tab, so a change needs no restart. The drop-down is the
 * settings-dropdown-menu, a native select, like the other Aerie rows.
 */

import '../controls/settings_dropdown_menu.js';
import '../i18n_setup.js';

import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {DropdownMenuOptionList} from '../controls/settings_dropdown_menu.js';
import {loadTimeData} from '../i18n_setup.js';
import {getCss as getSettingsSharedLit} from '../settings_shared_lit.css.js';

import {getHtml} from './aerie_links_open_row.html.js';

export class SettingsAerieLinksOpenRowElement extends CrLitElement {
  static get is() {
    return 'settings-aerie-links-open-row';
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

  // The values are the pref's two values; the names are strings of the grd.
  protected accessor menuOptions_: DropdownMenuOptionList = [
    {value: 'new-tab', name: loadTimeData.getString('aerieLinksOpenNewTab')},
    {value: 'in-place', name: loadTimeData.getString('aerieLinksOpenInPlace')},
  ];
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-aerie-links-open-row': SettingsAerieLinksOpenRowElement;
  }
}

customElements.define(
    SettingsAerieLinksOpenRowElement.is, SettingsAerieLinksOpenRowElement);
