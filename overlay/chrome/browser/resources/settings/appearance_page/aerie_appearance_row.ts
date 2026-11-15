// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-aerie-appearance-row' is the Theme and Mode rows of the Sidebar
 * section of Settings (issue #185). Theme is a radio group on the profile pref
 * aerie.appearance.theme with a small preview swatch for each of the six
 * themes; Mode is a segmented control on aerie.appearance.mode (System,
 * Light, Dark). The browser watches both prefs, so a change needs no restart
 * (components/aerie_design_tokens/aerie_appearance_prefs.h). Theme is the
 * settings-radio-group of Settings. Mode is a radiogroup of three radio
 * buttons drawn as segments: it has one tab stop (the checked segment), the
 * arrow keys, Home and End move and check, and a screen reader reads the group
 * label and description, the checked item and the position. The swatch
 * colours are strings of the page made from the Design Tokens; the page has no
 * colour of its own. A swatch is hidden from a screen reader: the label of the
 * radio button names the theme.
 */

import '../controls/controlled_radio_button.js';
import '../controls/settings_radio_group.js';
import '../i18n_setup.js';

import {css, CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';
import {PrefService} from '/shared/settings/prefs2/pref_service.js';
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
        /* The live region stays in the page, so a change is announced; with
           no note it takes no room. */
        .aerie-note:empty {
          padding: 0;
        }
        .aerie-segmented {
          background: var(--cr-card-background-color, transparent);
          border: 1px solid var(--cr-separator-color);
          border-radius: 12px;
          display: inline-flex;
          gap: 4px;
          padding: 4px;
        }
        .aerie-segment {
          background: transparent;
          border: 0;
          border-radius: 8px;
          color: var(--cr-primary-text-color);
          cursor: pointer;
          font: inherit;
          min-height: 32px;
          min-width: 88px;
          padding: 4px 20px;
        }
        .aerie-segment:hover {
          background: var(--cr-hover-background-color);
        }
        .aerie-segment[aria-checked='true'] {
          background: var(--color-sys-primary, var(--cr-checked-color));
          color: var(--color-sys-on-primary, #fff);
        }
        .aerie-segment:focus-visible {
          outline: 2px solid var(--cr-focus-outline-color,
              var(--color-sys-state-focus-ring));
          outline-offset: 2px;
        }
        @media (forced-colors: active) {
          .aerie-swatch {
            border-color: CanvasText;
            forced-color-adjust: none;
          }
          .aerie-segmented {
            border-color: CanvasText;
          }
          .aerie-segment[aria-checked='true'] {
            background: Highlight;
            color: HighlightText;
            forced-color-adjust: none;
          }
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
      osDark_: {type: Boolean},
    };
  }

  protected accessor themePref_: chrome.settingsPrivate.PrefObject|undefined;
  protected accessor modePref_: chrome.settingsPrivate.PrefObject|undefined;
  // Whether the operating system (the browser's colour scheme, for Mode
  // "System") is dark: what decides if OLED shows or Clean takes its place.
  protected accessor osDark_: boolean = false;
  private darkQuery_: MediaQueryList|null = null;
  private darkListener_: (() => void)|null = null;

  override connectedCallback() {
    super.connectedCallback();
    this.mirrorPrefs({
      'aerie.appearance.theme': 'themePref_',
      'aerie.appearance.mode': 'modePref_',
    });
    this.darkQuery_ = window.matchMedia('(prefers-color-scheme: dark)');
    this.osDark_ = this.darkQuery_.matches;
    this.darkListener_ = () => {
      this.osDark_ = this.darkQuery_!.matches;
    };
    this.darkQuery_.addEventListener('change', this.darkListener_);
  }

  override disconnectedCallback() {
    super.disconnectedCallback();
    if (this.darkQuery_ && this.darkListener_) {
      this.darkQuery_.removeEventListener('change', this.darkListener_);
    }
    this.darkQuery_ = null;
    this.darkListener_ = null;
  }

  // The mode the browser is drawn in: the choice, or for System the
  // operating system's.
  protected effectiveModeIsDark_(): boolean {
    const mode = this.modePref_?.value;
    if (mode === 'dark') {
      return true;
    }
    if (mode === 'light') {
      return false;
    }
    return this.osDark_;
  }

  protected isModeChecked_(mode: string): boolean {
    return (this.modePref_?.value ?? 'system') === mode;
  }

  protected setMode_(mode: string) {
    PrefService.getInstance().setPrefValue('aerie.appearance.mode', mode);
  }

  protected onModeClick_(e: Event) {
    const mode = (e.currentTarget as HTMLElement).dataset['mode'];
    if (mode) {
      this.setMode_(mode);
    }
  }

  // The arrow keys, Home and End move the choice and check it, as in any
  // radio group. Left and Right swap in a right-to-left page.
  protected onModeKeydown_(e: KeyboardEvent) {
    const modes = ['system', 'light', 'dark'];
    const rtl = getComputedStyle(this).direction === 'rtl';
    const current = Math.max(0, modes.indexOf(this.modePref_?.value ?? 'system'));
    let next = -1;
    switch (e.key) {
      case 'ArrowRight':
        next = current + (rtl ? -1 : 1);
        break;
      case 'ArrowLeft':
        next = current + (rtl ? 1 : -1);
        break;
      case 'ArrowDown':
        next = current + 1;
        break;
      case 'ArrowUp':
        next = current - 1;
        break;
      case 'Home':
        next = 0;
        break;
      case 'End':
        next = modes.length - 1;
        break;
      default:
        return;
    }
    e.preventDefault();
    next = (next + modes.length) % modes.length;
    this.setMode_(modes[next]!);
    const button = this.shadowRoot!.querySelector<HTMLElement>(
        `#aerie-mode-${modes[next]}Button`);
    button?.focus();
  }

  // OLED has only a dark look, so while the browser is light it shows Clean.
  // For System that depends on the operating system, not on the pref.
  protected showOledNote_(): boolean {
    return this.themePref_?.value === 'oled' && !this.effectiveModeIsDark_();
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
