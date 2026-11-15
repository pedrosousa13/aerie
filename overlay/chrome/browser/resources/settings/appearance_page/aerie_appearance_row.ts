// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-aerie-appearance-row' is the Theme and Mode rows of the Sidebar
 * section of Settings (issue #185). Theme is a radio group on the profile pref
 * aerie.appearance.theme with a small preview swatch for each of the six
 * themes; Mode is a segmented radio group on aerie.appearance.mode (System,
 * Light, Dark). The browser watches both prefs, so a change needs no restart
 * (components/aerie_design_tokens/aerie_appearance_prefs.h). Both groups are
 * the settings-radio-group of Settings: arrow keys move the choice and a screen
 * reader reads the group label, the checked item and the position. The swatch
 * colours are strings of the page made from the Design Tokens; the page has no
 * colour of its own. A swatch is hidden from a screen reader: the label of the
 * radio button names the theme.
 */

import '../controls/controlled_radio_button.js';
import '../controls/settings_radio_group.js';
import '../i18n_setup.js';

import {css, CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';
import {PrefServiceObserverMixinLit} from '/shared/settings/prefs2/pref_service_observer_mixin_lit.js';

import {getCss as getSettingsSharedLit} from '../settings_shared_lit.css.js';

import {getHtml} from './aerie_appearance_row.html.js';

const SettingsAerieAppearanceRowElementBase =
    PrefServiceObserverMixinLit(CrLitElement);

export class SettingsAerieAppearanceRowElement extends
    SettingsAerieAppearanceRowElementBase {
  static get is() {
    return 'settings-aerie-appearance-row';
  }

  static override get styles() {
    return [
      getSettingsSharedLit(),
      css`
        .aerie-swatch {
          border: 1px solid var(--cr-separator-color);
          border-radius: 4px;
          box-sizing: border-box;
          display: inline-block;
          height: 16px;
          margin-inline-end: 8px;
          vertical-align: middle;
          width: 28px;
        }
        .aerie-note {
          padding-block: 4px 12px;
          padding-inline: var(--cr-section-padding, 20px);
        }
      `,
    ];
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      themePref_: {type: Object},
      modePref_: {type: Object},
    };
  }

  protected accessor themePref_: chrome.settingsPrivate.PrefObject|undefined;
  protected accessor modePref_: chrome.settingsPrivate.PrefObject|undefined;

  override connectedCallback() {
    super.connectedCallback();
    this.mirrorPrefs({
      'aerie.appearance.theme': 'themePref_',
      'aerie.appearance.mode': 'modePref_',
    });
  }

  // OLED has only a dark look, so in any other mode the browser shows Clean.
  protected showOledNote_(): boolean {
    return this.themePref_?.value === 'oled' && this.modePref_?.value !== 'dark';
  }

  // High contrast and Mono draw no colour gradient.
  protected showNoTintNote_(): boolean {
    const theme = this.themePref_?.value;
    return theme === 'high-contrast' || theme === 'mono';
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-aerie-appearance-row': SettingsAerieAppearanceRowElement;
  }
}

customElements.define(
    SettingsAerieAppearanceRowElement.is, SettingsAerieAppearanceRowElement);
