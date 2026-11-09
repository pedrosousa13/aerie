// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import '/strings.m.js';
import 'chrome://resources/cr_elements/cr_button/cr_button.js';
import 'chrome://resources/cr_elements/cr_toggle/cr_toggle.js';

import type {CrToggleElement} from 'chrome://resources/cr_elements/cr_toggle/cr_toggle.js';
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';
import type {PropertyValues} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {FingerprintingMode, Link, ProtectedSurface} from './aerie_guard.mojom-webui.js';
import type {PanelState} from './aerie_guard.mojom-webui.js';
import {getCss} from './app.css.js';
import {getHtml} from './app.html.js';
import {BrowserProxy} from './browser_proxy.js';

const ADVANCED_OPEN_KEY = 'advancedOpen';

function readAdvancedOpen(): boolean {
  try {
    return window.localStorage.getItem(ADVANCED_OPEN_KEY) === '1';
  } catch (e) {
    return false;  // No storage: the section stays collapsed.
  }
}

function writeAdvancedOpen(open: boolean) {
  try {
    window.localStorage.setItem(ADVANCED_OPEN_KEY, open ? '1' : '0');
  } catch (e) {
    // Not remembered. The panel still works.
  }
}

export class AerieGuardAppElement extends CrLitElement {
  static get is() {
    return 'aerie-guard-app';
  }

  static override get styles() {
    return getCss();
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      state_: {type: Object},
      loaded_: {type: Boolean},
      advancedOpen_: {type: Boolean},
    };
  }

  // What the browser says about the captured site. Null: no site.
  protected accessor state_: PanelState|null = null;
  protected accessor loaded_: boolean = false;
  // Whether the Advanced section is open. Collapsed until the user opens it,
  // then remembered (this panel's own storage; nothing about a site).
  protected accessor advancedOpen_: boolean = readAdvancedOpen();

  private proxy_: BrowserProxy = BrowserProxy.getInstance();
  private listenerId_: number|null = null;
  private shown_: boolean = false;

  override connectedCallback() {
    super.connectedCallback();
    this.listenerId_ = this.proxy_.callbackRouter.onStateChanged.addListener(
        (state: PanelState|null) => {
          this.state_ = state;
        });
    this.proxy_.handler.getState().then(({state}) => {
      this.state_ = state;
      this.loaded_ = true;
    });
  }

  override disconnectedCallback() {
    super.disconnectedCallback();
    if (this.listenerId_ !== null) {
      this.proxy_.callbackRouter.removeListener(this.listenerId_);
      this.listenerId_ = null;
    }
  }

  override updated(changedProperties: PropertyValues<this>) {
    super.updated(changedProperties);
    // The browser's answer is the truth: a switch it refused snaps back.
    const toggle = this.shadowRoot.querySelector<CrToggleElement>('#toggle');
    if (toggle && this.state_) {
      toggle.checked = this.state_.blockingOn;
    }
    const cosmetic =
        this.shadowRoot.querySelector<CrToggleElement>('#cosmeticToggle');
    if (cosmetic && this.state_) {
      cosmetic.checked = this.cosmeticChecked_();
    }
    const fpToggle =
        this.shadowRoot.querySelector<CrToggleElement>('#fpToggle');
    if (fpToggle && this.state_) {
      fpToggle.checked =
          this.state_.fingerprinting.mode === FingerprintingMode.kOn;
    }
    // Rendered: the browser may show the bubble now.
    if (this.loaded_ && !this.shown_) {
      this.shown_ = true;
      this.proxy_.handler.showUI();
    }
  }

  protected i18n_(key: string): string {
    return loadTimeData.getString(key);
  }

  protected statusText_(): string {
    return this.i18n_(this.state_?.blockingOn ? 'statusOn' : 'statusOff');
  }

  protected scopeText_(): string {
    return loadTimeData.getStringF('scope', this.state_?.host ?? '');
  }

  // The note under the switch: why it is disabled, or that a private change is
  // not saved. Empty when there is nothing to add.
  protected noteText_(): string {
    if (this.state_?.locked) {
      return this.i18n_('locked');
    }
    return this.state_?.incognito ? this.i18n_('incognito') : '';
  }

  // The row shows what is in force: with blocking off there is no hiding
  // either, so it reads off and cannot be changed.
  protected cosmeticChecked_(): boolean {
    return !!this.state_ && this.state_.blockingOn &&
        this.state_.cosmeticHidingOn;
  }

  protected cosmeticDisabled_(): boolean {
    return !this.state_ || !this.state_.blockingOn ||
        this.state_.cosmeticLocked;
  }

  protected cosmeticNoteText_(): string {
    return this.state_?.cosmeticLocked ? this.i18n_('cosmeticLocked') : '';
  }

  protected onCosmeticChange_(e: CustomEvent<boolean>) {
    this.proxy_.handler.setCosmeticHidingOn(e.detail);
  }

  protected onAdvancedToggle_(e: Event) {
    this.advancedOpen_ = (e.target as HTMLDetailsElement).open;
    writeAdvancedOpen(this.advancedOpen_);
  }

  protected fingerprintingHasSwitch_(): boolean {
    const mode = this.state_?.fingerprinting.mode;
    return mode === FingerprintingMode.kOn ||
        mode === FingerprintingMode.kOffForSite;
  }

  protected fingerprintingStatus_(): string {
    switch (this.state_?.fingerprinting.mode) {
      case FingerprintingMode.kOn:
        return this.i18n_('fingerprintingOn');
      case FingerprintingMode.kOffForSite:
        return this.i18n_('fingerprintingOff');
      case FingerprintingMode.kOffInSettings:
        return this.i18n_('fingerprintingOffInSettings');
      default:
        return this.i18n_('fingerprintingUnavailable');
    }
  }

  // What the switch covers.
  protected fingerprintingScopeText_(): string {
    return loadTimeData.getStringF(
        'fingerprintingScope', this.state_?.host ?? '');
  }

  protected surfaceText_(surface: ProtectedSurface): string {
    switch (surface) {
      case ProtectedSurface.kCanvas:
        return this.i18n_('surfaceCanvas');
      case ProtectedSurface.kAudio:
        return this.i18n_('surfaceAudio');
      case ProtectedSurface.kCpuCores:
        return this.i18n_('surfaceCpuCores');
      case ProtectedSurface.kDeviceMemory:
        return this.i18n_('surfaceDeviceMemory');
      case ProtectedSurface.kGraphicsCard:
        return this.i18n_('surfaceGraphicsCard');
      case ProtectedSurface.kScreen:
        return this.i18n_('surfaceScreen');
    }
  }

  protected onFingerprintingChange_(e: CustomEvent<boolean>) {
    this.proxy_.handler.setFingerprintingOn(e.detail);
  }

  protected onReloadClick_() {
    this.proxy_.handler.reload();
  }

  protected onNoiseSettingsClick_() {
    this.proxy_.handler.openLink(Link.kNoiseSettings);
  }

  protected onToggleChange_(e: CustomEvent<boolean>) {
    this.proxy_.handler.setGuardOn(e.detail);
  }

  protected onFilterListsClick_() {
    this.proxy_.handler.openLink(Link.kFilterLists);
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'aerie-guard-app': AerieGuardAppElement;
  }
}

customElements.define(AerieGuardAppElement.is, AerieGuardAppElement);
