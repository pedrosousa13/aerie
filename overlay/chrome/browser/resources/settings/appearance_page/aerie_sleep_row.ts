// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-aerie-sleep-row' is the "Put inactive tabs to sleep after" row of
 * the Sidebar section of Settings (issue #179). It writes the profile pref
 * aerie.tabs.sleep_after_minutes: 15, 30, 60 (default), 120, 240 or 480, or 0
 * for never (components/aerie_tab_model/tab_model_prefs.h). The sleep
 * controller watches the pref, so a change needs no restart. The drop-down is
 * the settings-dropdown-menu, a native select, like the Reduce motion row.
 */

import '../controls/settings_dropdown_menu.js';
import '../i18n_setup.js';

import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {DropdownMenuOptionList} from '../controls/settings_dropdown_menu.js';
import {loadTimeData} from '../i18n_setup.js';
import {getCss as getSettingsSharedLit} from '../settings_shared_lit.css.js';

import {getHtml} from './aerie_sleep_row.html.js';

export class SettingsAerieSleepRowElement extends CrLitElement {
  static get is() {
    return 'settings-aerie-sleep-row';
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

  // The values are minutes, as the pref holds them (0 is never); the names are
  // strings of the grd.
  protected accessor menuOptions_: DropdownMenuOptionList = [
    {value: 15, name: loadTimeData.getString('aerieSleepAfter15Min')},
    {value: 30, name: loadTimeData.getString('aerieSleepAfter30Min')},
    {value: 60, name: loadTimeData.getString('aerieSleepAfter1Hour')},
    {value: 120, name: loadTimeData.getString('aerieSleepAfter2Hours')},
    {value: 240, name: loadTimeData.getString('aerieSleepAfter4Hours')},
    {value: 480, name: loadTimeData.getString('aerieSleepAfter8Hours')},
    {value: 0, name: loadTimeData.getString('aerieSleepAfterNever')},
  ];
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-aerie-sleep-row': SettingsAerieSleepRowElement;
  }
}

customElements.define(
    SettingsAerieSleepRowElement.is, SettingsAerieSleepRowElement);
