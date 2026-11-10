// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-aerie-motion-row' is the "Reduce motion" row of the Appearance
 * page (issue #174). It writes the profile pref aerie.motion.reduce, which is
 * "system" (default: follow the operating system) or "on". The Aerie motion
 * helper (components/aerie_design_tokens/aerie_motion.h) reads it, together
 * with the system setting, in one place. The drop-down is the settings-
 * dropdown-menu, a native select, so the keyboard and screen readers work as
 * on the other rows.
 */

import '../controls/settings_dropdown_menu.js';
import '../i18n_setup.js';

import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {DropdownMenuOptionList} from '../controls/settings_dropdown_menu.js';
import {loadTimeData} from '../i18n_setup.js';
import {getCss as getSettingsSharedLit} from '../settings_shared_lit.css.js';

import {getHtml} from './aerie_motion_row.html.js';

export class SettingsAerieMotionRowElement extends CrLitElement {
  static get is() {
    return 'settings-aerie-motion-row';
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
    {value: 'system', name: loadTimeData.getString('aerieMotionReduceSystem')},
    {value: 'on', name: loadTimeData.getString('aerieMotionReduceOn')},
  ];
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-aerie-motion-row': SettingsAerieMotionRowElement;
  }
}

customElements.define(
    SettingsAerieMotionRowElement.is, SettingsAerieMotionRowElement);
