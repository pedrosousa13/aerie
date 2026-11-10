// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-aerie-guard-page' is the Aerie Guard section of Settings: whether
 * the Guard icon draws the number of blocked items, the default blocking
 * level, the automatic update of the filter lists, and the sites where
 * blocking is off. It is a top-level
 * route (/aerieGuard) and wired the way Aerie's Security hardening page is.
 * It also lists the filter lists with their rule counts, ignored exception
 * rules and update status, and switches a list on or off (#104). The
 * catalogue, custom filters and subscriptions get their own rows from later
 * issues (#105 to #108).
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
import {CrLitElement, css} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {getSearchManager} from '../search_settings.js';
import type {SettingsPlugin} from '../settings_main/settings_plugin.js';
import {SettingsViewMixinLit} from '../settings_page/settings_view_mixin_lit.js';
import {getCss as getSettingsSharedLit} from '../settings_shared_lit.css.js';
import {getCss as getCrSharedStyle} from 'chrome://resources/cr_elements/cr_shared_style_lit.css.js';

import {AerieGuardBrowserProxyImpl} from './aerie_guard_browser_proxy.js';
import type {AerieGuardBrowserProxy, AerieGuardList, AerieGuardState} from './aerie_guard_browser_proxy.js';
import {getHtml} from './aerie_guard_page.html.js';

const SettingsAerieGuardPageElementBase = SettingsViewMixinLit(CrLitElement);

export class SettingsAerieGuardPageElement extends
    SettingsAerieGuardPageElementBase implements SettingsPlugin {
  static get is() {
    return 'settings-aerie-guard-page';
  }

  static override get styles() {
    return [
      getCrSharedStyle(),
      getSettingsSharedLit(),
      css`
        .list-error {
          color: var(--cr-form-field-error-color, var(--google-red-600));
        }
        cr-button.list-update {
          margin-inline-end: 16px;
        }
      `,
    ];
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      autoUpdate_: {type: Boolean},
      defaultLevel_: {type: String},
      exceptions_: {type: Array},
      lists_: {type: Array},
      listsBusy_: {type: Boolean},
      listError_: {type: String},
      newSite_: {type: String},
      siteError_: {type: String},
    };
  }

  protected accessor autoUpdate_: boolean = true;
  protected accessor defaultLevel_: string = 'aggressive';
  protected accessor exceptions_: string[] = [];
  protected accessor lists_: AerieGuardList[] = [];
  protected accessor listsBusy_: boolean = false;
  protected accessor listError_: string = '';
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
    this.defaultLevel_ = state.defaultLevel;
    this.exceptions_ = state.exceptions;
    this.lists_ = state.lists;
    this.listsBusy_ = state.busy;
    if (state.listError) {
      this.listError_ = this.listErrorText_(state.listError);
    }
  }

  protected listRulesText_(list: AerieGuardList): string {
    // The shipped lists are counted when the browser starts; until then the
    // number is not known, and the page says so instead of showing 0.
    if (list.rules === 0 && this.listsBusy_ && list.updated === null) {
      return loadTimeData.getString('aerieGuardListRulesCounting');
    }
    return loadTimeData.getStringF(
        'aerieGuardListRules', list.rules.toLocaleString());
  }

  protected listIgnoredText_(list: AerieGuardList): string {
    return loadTimeData.getStringF(
        'aerieGuardListIgnored', list.ignoredExceptions.toLocaleString());
  }

  protected listWhenText_(list: AerieGuardList): string {
    if (list.updated === null) {
      return loadTimeData.getString('aerieGuardListShipped');
    }
    return loadTimeData.getStringF(
        'aerieGuardListUpdated',
        new Date(list.updated).toLocaleDateString());
  }

  protected listStatusText_(list: AerieGuardList): string {
    switch (list.status) {
      case 'updating':
        return loadTimeData.getString('aerieGuardListStatusUpdating');
      case 'updated':
        return loadTimeData.getString('aerieGuardListStatusUpdated');
      case 'failed':
        return loadTimeData.getString('aerieGuardListStatusFailed');
      default:
        return '';
    }
  }

  // The code is a fixed word of the handler ('too_large'); the page owns the
  // sentence. An unknown code gets the generic one.
  protected listFailureText_(list: AerieGuardList): string {
    if (!list.errorCode) {
      return '';
    }
    const key = 'aerieGuardListError' +
        list.errorCode.split('_')
            .map(w => w.charAt(0).toUpperCase() + w.slice(1))
            .join('');
    return loadTimeData.valueExists(key) ?
        loadTimeData.getString(key) :
        loadTimeData.getString('aerieGuardErrorGeneric');
  }

  private listErrorText_(code: string): string {
    return code === 'required' ?
        loadTimeData.getString('aerieGuardListErrorRequired') :
        loadTimeData.getString('aerieGuardErrorGeneric');
  }

  protected listToggleLabel_(list: AerieGuardList): string {
    return loadTimeData.getStringF('aerieGuardListToggleAria', list.title);
  }

  protected listUpdateLabel_(list: AerieGuardList): string {
    return loadTimeData.getStringF(
        list.status === 'failed' ? 'aerieGuardListRetryAria' :
                                   'aerieGuardListUpdateNowAria',
        list.title);
  }

  protected listUpdateButton_(list: AerieGuardList): string {
    return loadTimeData.getString(
        list.status === 'failed' ? 'aerieGuardListRetry' :
                                   'aerieGuardListUpdateNow');
  }

  protected onListToggleChange_(e: Event) {
    const toggle = e.target as CrToggleElement;
    const id = toggle.dataset['id'];
    if (!id) {
      return;
    }
    this.listError_ = '';
    this.browserProxy_.setListEnabled(id, toggle.checked).then(
        state => {
          this.applyState_(state);
          // A refused change leaves the list as it was: draw it that way.
          toggle.checked =
              state.lists.find(l => l.id === id)?.enabled ?? toggle.checked;
        },
        () => {
          this.listError_ = loadTimeData.getString('aerieGuardErrorGeneric');
        });
  }

  protected onListUpdateClick_(e: Event) {
    const id = (e.currentTarget as HTMLElement).dataset['id'];
    if (id) {
      this.browserProxy_.updateList(id).then(state => this.applyState_(state));
    }
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

  protected onDefaultLevelChange_(e: Event) {
    const level = (e.target as HTMLSelectElement).value;
    this.browserProxy_.setDefaultLevel(level).then(
        state => this.applyState_(state),
        // The browser refused the value. Show what it has.
        () => this.browserProxy_.getState().then(
            state => this.applyState_(state)));
  }

  protected onNewSiteValueChanged_(e: CustomEvent<{value: string}>) {
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
