// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-aerie-folder-rules-row' is the "Folder rules everywhere" row of the
 * Sidebar section of Settings (issue #139). It writes the profile pref
 * aerie.folders.rules_enabled (components/aerie_tab_model/tab_model_prefs.h):
 * on by default, and when it is off no folder files a new tab by its rules. The
 * switch in the footer of the folder editor sheet writes the same pref. The
 * browser reads it when a new tab has a page, so a change needs no restart.
 */

import '../controls/settings_toggle_button.js';
import '../i18n_setup.js';

import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {getCss as getSettingsSharedLit} from '../settings_shared_lit.css.js';

import {getHtml} from './aerie_folder_rules_row.html.js';

export class SettingsAerieFolderRulesRowElement extends CrLitElement {
  static get is() {
    return 'settings-aerie-folder-rules-row';
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
    'settings-aerie-folder-rules-row': SettingsAerieFolderRulesRowElement;
  }
}

customElements.define(
    SettingsAerieFolderRulesRowElement.is, SettingsAerieFolderRulesRowElement);
