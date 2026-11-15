// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {html, nothing} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsAerieAppearanceRowElement} from './aerie_appearance_row.js';

// Every word is a string of the grd (aerie_sidebar_strings.grdp); every swatch
// colour is a string made from the Design Tokens.
export function getHtml(this: SettingsAerieAppearanceRowElement) {
  return html`<!--_html_template_start_-->
<div class="settings-box" id="settings-aerie-theme-box">
  <div class="flex cr-padded-text">
    <div id="settings-aerie-theme-label">$i18n{aerieAppearanceTheme}</div>
    <div class="secondary" id="settings-aerie-theme-desc">$i18n{aerieAppearanceThemeDesc}</div>
  </div>
</div>
<div class="cr-row continuation">
  <settings-radio-group id="settings-aerie-theme-group"
      pref-key="aerie.appearance.theme"
      group-aria-label="$i18n{aerieAppearanceTheme}">
    <controlled-radio-button id="aerie-theme-vividButton"
        label="$i18n{aerieAppearanceThemeVivid}"
        name="vivid"
        pref-key="aerie.appearance.theme">
      <span class="aerie-swatch" aria-hidden="true"
          style="background: $i18n{aerieThemeSwatchVivid}"></span>
    </controlled-radio-button>
    <controlled-radio-button id="aerie-theme-softButton"
        label="$i18n{aerieAppearanceThemeSoft}"
        name="soft"
        pref-key="aerie.appearance.theme">
      <span class="aerie-swatch" aria-hidden="true"
          style="background: $i18n{aerieThemeSwatchSoft}"></span>
    </controlled-radio-button>
    <controlled-radio-button id="aerie-theme-cleanButton"
        label="$i18n{aerieAppearanceThemeClean}"
        name="clean"
        pref-key="aerie.appearance.theme">
      <span class="aerie-swatch" aria-hidden="true"
          style="background: $i18n{aerieThemeSwatchClean}"></span>
    </controlled-radio-button>
    <controlled-radio-button id="aerie-theme-oledButton"
        label="$i18n{aerieAppearanceThemeOled}"
        name="oled"
        pref-key="aerie.appearance.theme">
      <span class="aerie-swatch" aria-hidden="true"
          style="background: $i18n{aerieThemeSwatchOled}"></span>
    </controlled-radio-button>
    <controlled-radio-button id="aerie-theme-high-contrastButton"
        label="$i18n{aerieAppearanceThemeHighContrast}"
        name="high-contrast"
        pref-key="aerie.appearance.theme">
      <span class="aerie-swatch" aria-hidden="true"
          style="background: $i18n{aerieThemeSwatchHighContrast}"></span>
    </controlled-radio-button>
    <controlled-radio-button id="aerie-theme-monoButton"
        label="$i18n{aerieAppearanceThemeMono}"
        name="mono"
        pref-key="aerie.appearance.theme">
      <span class="aerie-swatch" aria-hidden="true"
          style="background: $i18n{aerieThemeSwatchMono}"></span>
    </controlled-radio-button>
  </settings-radio-group>
</div>
${this.showOledNote_() ? html`
  <div class="secondary aerie-note" id="settings-aerie-theme-note-oled"
      role="status">$i18n{aerieAppearanceNoteOledLight}</div>` : nothing}
${this.showNoTintNote_() ? html`
  <div class="secondary aerie-note" id="settings-aerie-theme-note-no-tint"
      role="status">$i18n{aerieAppearanceNoteNoTint}</div>` : nothing}
<div class="settings-box" id="settings-aerie-mode-box">
  <div class="flex cr-padded-text">
    <div id="settings-aerie-mode-label">$i18n{aerieAppearanceMode}</div>
    <div class="secondary" id="settings-aerie-mode-desc">$i18n{aerieAppearanceModeDesc}</div>
  </div>
</div>
<div class="cr-row continuation">
  <settings-radio-group id="settings-aerie-mode-group" is-horizontal
      pref-key="aerie.appearance.mode"
      group-aria-label="$i18n{aerieAppearanceMode}">
    <controlled-radio-button id="aerie-mode-systemButton"
        label="$i18n{aerieAppearanceModeSystem}"
        name="system"
        pref-key="aerie.appearance.mode">
    </controlled-radio-button>
    <controlled-radio-button id="aerie-mode-lightButton"
        label="$i18n{aerieAppearanceModeLight}"
        name="light"
        pref-key="aerie.appearance.mode">
    </controlled-radio-button>
    <controlled-radio-button id="aerie-mode-darkButton"
        label="$i18n{aerieAppearanceModeDark}"
        name="dark"
        pref-key="aerie.appearance.mode">
    </controlled-radio-button>
  </settings-radio-group>
</div>
<!--_html_template_end_-->`;
}
