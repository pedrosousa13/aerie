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

import {Link} from './aerie_guard.mojom-webui.js';
import type {PanelState} from './aerie_guard.mojom-webui.js';
import {getCss} from './app.css.js';
import {getHtml} from './app.html.js';
import {BrowserProxy} from './browser_proxy.js';

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
      advancedRows_: {type: Array},
    };
  }

  // What the browser says about the captured site. Null: no site.
  protected accessor state_: PanelState|null = null;
  protected accessor loaded_: boolean = false;
  // The rows of the Advanced section. Empty in this slice: the cosmetic row
  // (#101) and the fingerprinting row (#102) join it. The section is not drawn
  // while it has no rows, and is collapsed when it has.
  protected accessor advancedRows_: string[] = [];

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
    const toggle = this.shadowRoot!.querySelector<CrToggleElement>('cr-toggle');
    if (toggle && this.state_) {
      toggle.checked = this.state_.blockingOn;
    }
    // Rendered: the browser may show the bubble now.
    if (this.loaded_ && !this.shown_) {
      this.shown_ = true;
      this.proxy_.handler.showUi();
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
