// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-aerie-guard-page' is the Aerie Guard section of Settings: whether
 * the Guard icon draws the number of blocked items, the automatic update of
 * the filter lists, and the sites where blocking is off. It is a top-level
 * route (/aerieGuard) and wired the way Aerie's Security hardening page is.
 * The lists, the catalogue, custom filters and subscriptions get their own rows
 * from later issues (#104 to #108).
 */

import 'chrome://resources/cr_elements/cr_button/cr_button.js';
import 'chrome://resources/cr_elements/cr_input/cr_input.js';
import 'chrome://resources/cr_elements/cr_toggle/cr_toggle.js';
import '../controls/settings_toggle_button.js';
import '../i18n_setup.js';
import '../settings_page/settings_section.js';

import type {CrToggleElement} from 'chrome://resources/cr_elements/cr_toggle/cr_toggle.js';
import {addWebUiListener, removeWebUiListener} from 'chrome://resources/js/cr.js';
import type {WebUiListener} from 'chrome://resources/js/cr.js';
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {getSearchManager} from '../search_settings.js';
import type {SettingsPlugin} from '../settings_main/settings_plugin.js';
import {SettingsViewMixinLit} from '../settings_page/settings_view_mixin_lit.js';
import {getCss as getSettingsSharedLit} from '../settings_shared_lit.css.js';
import {getCss as getCrSharedStyle} from 'chrome://resources/cr_elements/cr_shared_style_lit.css.js';

import {AerieGuardBrowserProxyImpl} from './aerie_guard_browser_proxy.js';
import type {AerieGuardBrowserProxy, AerieGuardState} from './aerie_guard_browser_proxy.js';
import {getHtml} from './aerie_guard_page.html.js';

const SettingsAerieGuardPageElementBase = SettingsViewMixinLit(CrLitElement);

export class SettingsAerieGuardPageElement extends
    SettingsAerieGuardPageElementBase implements SettingsPlugin {
  static get is() {
    return 'settings-aerie-guard-page';
  }

  static override get styles() {
    return [getCrSharedStyle(), getSettingsSharedLit()];
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      autoUpdate_: {type: Boolean},
      exceptions_: {type: Array},
      newSite_: {type: String},
      siteError_: {type: String},
    };
  }

  protected accessor autoUpdate_: boolean = true;
  protected accessor exceptions_: string[] = [];
  protected accessor newSite_: string = '';
  protected accessor siteError_: string = '';

  private browserProxy_: AerieGuardBrowserProxy =
      AerieGuardBrowserProxyImpl.getInstance();
  private listener_: WebUiListener|null = null;

  override connectedCallback() {
    super.connectedCallback();
    // The lists and exceptions change from other places too (the Guard icon,
    // a list update), so the browser tells the page and the page also asks
    // once when it opens.
    this.listener_ = addWebUiListener(
        'aerie-guard-state-changed',
        (state: AerieGuardState) => this.applyState_(state));
    this.browserProxy_.getState().then(state => this.applyState_(state));
  }

  override disconnectedCallback() {
    super.disconnectedCallback();
    if (this.listener_) {
      removeWebUiListener(this.listener_);
      this.listener_ = null;
    }
  }

  private applyState_(state: AerieGuardState) {
    this.autoUpdate_ = state.autoUpdate;
    this.exceptions_ = state.exceptions;
  }

  protected removeSiteLabel_(host: string): string {
    return loadTimeData.getStringF('aerieGuardRemoveSiteAria', host);
  }

  protected canAddSite_(): boolean {
    return this.newSite_.trim().length > 0;
  }

  protected onAutoUpdateChange_(e: Event) {
    const checked = (e.target as CrToggleElement).checked;
    this.browserProxy_.setAutoUpdate(checked).then(
        state => this.applyState_(state));
  }

  protected onNewSiteChanged_(e: CustomEvent<{value: string}>) {
    this.newSite_ = e.detail.value;
    this.siteError_ = '';
  }

  protected onNewSiteKeydown_(e: KeyboardEvent) {
    if (e.key === 'Enter' && this.canAddSite_()) {
      this.onAddSiteClick_();
    }
  }

  protected onAddSiteClick_() {
    if (!this.canAddSite_()) {
      return;
    }
    this.setSiteBlockingOff_(this.newSite_.trim(), true);
  }

  protected onRemoveSiteClick_(e: Event) {
    const host = (e.currentTarget as HTMLElement).dataset['host'];
    if (host) {
      this.setSiteBlockingOff_(host, false);
    }
  }

  private setSiteBlockingOff_(host: string, off: boolean) {
    this.browserProxy_.setException(host, off).then(
        state => {
          this.applyState_(state);
          this.siteError_ = this.errorText_(state.exceptionError);
          if (!state.exceptionError && off) {
            this.newSite_ = '';
          }
        },
        // The browser refused the argument. Nothing changed.
        () => {
          this.siteError_ = loadTimeData.getString('aerieGuardErrorGeneric');
        });
  }

  private errorText_(code: string): string {
    switch (code) {
      case '':
        return '';
      case 'invalid_host':
        return loadTimeData.getString('aerieGuardErrorInvalidSite');
      case 'table_full':
        return loadTimeData.getString('aerieGuardErrorTooManySites');
      default:
        return loadTimeData.getString('aerieGuardErrorGeneric');
    }
  }

  // SettingsPlugin implementation.
  async searchContents(query: string) {
    const searchRequest = await getSearchManager().search(query, this);
    return searchRequest.getSearchResult();
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-aerie-guard-page': SettingsAerieGuardPageElement;
  }
}

customElements.define(
    SettingsAerieGuardPageElement.is, SettingsAerieGuardPageElement);
