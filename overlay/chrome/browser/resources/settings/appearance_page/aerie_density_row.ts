// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-aerie-density-row' is a row of the Sidebar section of Settings (issue #184). It is a
 * radio group on the profile pref aerie.sidebar.density (components/aerie_sidebar/aerie_sidebar_prefs.h).
 * The browser watches the pref, so a change needs no restart. The radio group is
 * the settings-radio-group of Settings: arrow keys move the choice and a screen
 * reader reads the group label first.
 */

import '../controls/controlled_radio_button.js';
import '../controls/settings_radio_group.js';
import '../i18n_setup.js';

import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {getCss as getSettingsSharedLit} from '../settings_shared_lit.css.js';

import {getHtml} from './aerie_density_row.html.js';

export class SettingsAerieDensityRowElement extends CrLitElement {
  static get is() {
    return 'settings-aerie-density-row';
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
    'settings-aerie-density-row': SettingsAerieDensityRowElement;
  }
}

customElements.define(SettingsAerieDensityRowElement.is, SettingsAerieDensityRowElement);
