// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-aerie-preview-card-row' is the "Show page previews on tab cards"
 * row of the Sidebar section of Settings (issue #270). It writes the profile
 * pref aerie.sidebar.hover_card_snapshots (components/aerie_sidebar/
 * aerie_sidebar_prefs.h): on by default. When it is off, the card that shows
 * when the user hovers a tab keeps its text and shows no snapshot of the page.
 * The browser reads the pref each time a card opens, so a change needs no
 * restart.
 */

import '../controls/settings_toggle_button.js';
import '../i18n_setup.js';

import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {getCss as getSettingsSharedLit} from '../settings_shared_lit.css.js';

import {getHtml} from './aerie_preview_card_row.html.js';

export class SettingsAeriePreviewCardRowElement extends CrLitElement {
  static get is() {
    return 'settings-aerie-preview-card-row';
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
    'settings-aerie-preview-card-row': SettingsAeriePreviewCardRowElement;
  }
}

customElements.define(
    SettingsAeriePreviewCardRowElement.is, SettingsAeriePreviewCardRowElement);
