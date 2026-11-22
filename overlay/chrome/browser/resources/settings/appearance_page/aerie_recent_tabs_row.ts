// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-aerie-recent-tabs-row' is the "Ctrl+Tab cycles recent tabs" row of
 * the Sidebar section of Settings (issue #269). It writes the profile pref
 * aerie.sidebar.recent_tab_switcher (components/aerie_sidebar/
 * aerie_sidebar_prefs.h): on by default. When it is on, Ctrl+Tab goes to the
 * tab used before and shows the card overlay; when it is off, Ctrl+Tab goes
 * along the tab order as Chromium does. The browser reads the pref at each
 * Ctrl+Tab, so a change needs no restart.
 */

import '../controls/settings_toggle_button.js';
import '../i18n_setup.js';

import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {getCss as getSettingsSharedLit} from '../settings_shared_lit.css.js';

import {getHtml} from './aerie_recent_tabs_row.html.js';

export class SettingsAerieRecentTabsRowElement extends CrLitElement {
  static get is() {
    return 'settings-aerie-recent-tabs-row';
  }

  static override get styles() {
    return getSettingsSharedLit();
  }

  override render() {
    return getHtml.bind(this)();
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-aerie-recent-tabs-row': SettingsAerieRecentTabsRowElement;
  }
}

customElements.define(
    SettingsAerieRecentTabsRowElement.is, SettingsAerieRecentTabsRowElement);
