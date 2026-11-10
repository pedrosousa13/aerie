// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import {BlockingLevel, FingerprintingMode} from './aerie_guard.mojom-webui.js';
import type {AerieGuardAppElement} from './app.js';

export function getHtml(this: AerieGuardAppElement) {
  // clang-format off
  return html`<!--_html_template_start_-->
${this.state_ ? html`
  <header id="site">
    <img id="favicon" alt="" src="${this.state_.faviconUrl}">
    <span id="host" title="${this.state_.host}">${this.state_.host}</span>
  </header>
  <section id="count" aria-live="polite">
    <div id="number" class="${this.state_.blockingOn ? '' : 'off'}">
      ${this.state_.blockedCount}
    </div>
    <div id="summary">${this.state_.blockedSummary}</div>
    ${this.hasBlockedUrls_() ? html`
      <details id="blockedList">
        <summary>${this.i18n_('blockedList')}</summary>
        <ul>
          ${this.state_.blockedUrls.map(url => html`
            <li class="blocked-url">${url}</li>`)}
        </ul>
        ${this.blockedListCutText_() ? html`
          <p id="blockedListCut">${this.blockedListCutText_()}</p>` : ''}
      </details>` : ''}
  </section>
  <section id="control">
    <div id="texts">
      <div id="label">${this.i18n_('switchLabel')}</div>
      <div id="status">${this.statusText_()}</div>
    </div>
    <cr-toggle id="toggle" ?checked="${this.state_.blockingOn}"
        ?disabled="${this.state_.locked}"
        aria-label="${this.i18n_('switchLabel')}"
        @change="${this.onToggleChange_}">
    </cr-toggle>
  </section>
  <p id="scope">${this.scopeText_()}</p>
  ${this.noteText_() ? html`<p id="note">${this.noteText_()}</p>` : ''}
  <section id="level">
    <div id="levelLabel">${this.i18n_('levelLabel')}</div>
    <div id="levelChoices" role="radiogroup" aria-labelledby="levelLabel">
      <button id="levelAggressive" type="button" role="radio"
          aria-checked="${this.levelChecked_(BlockingLevel.kAggressive)}"
          ?disabled="${this.state_.locked}"
          @click="${this.onLevelAggressive_}">
        ${this.i18n_('levelAggressive')}
      </button>
      <button id="levelStandard" type="button" role="radio"
          aria-checked="${this.levelChecked_(BlockingLevel.kStandard)}"
          ?disabled="${this.state_.locked}"
          @click="${this.onLevelStandard_}">
        ${this.i18n_('levelStandard')}
      </button>
      <button id="levelOff" type="button" role="radio"
          aria-checked="${this.levelOffChecked_()}"
          ?disabled="${this.state_.locked}"
          @click="${this.onLevelOff_}">
        ${this.i18n_('levelOff')}
      </button>
    </div>
    <p id="levelDesc">${this.levelDescText_()}</p>
    <p id="levelReload">${this.i18n_('levelReload')}</p>
  </section>
  <details id="advanced" ?open="${this.advancedOpen_}"
      @toggle="${this.onAdvancedToggle_}">
    <summary>${this.i18n_('advanced')}</summary>
    <div class="row">
      <div class="texts">
        <div id="cosmeticLabel" class="label">${this.i18n_('cosmeticLabel')}</div>
        <div id="cosmeticDesc" class="muted">${this.i18n_('cosmeticDesc')}</div>
        ${this.cosmeticNoteText_() ? html`
          <div id="cosmeticNote" class="muted">${this.cosmeticNoteText_()}</div>
        ` : ''}
      </div>
      <cr-toggle id="cosmeticToggle" ?checked="${this.cosmeticChecked_()}"
          ?disabled="${this.cosmeticDisabled_()}"
          aria-labelledby="cosmeticLabel"
          @change="${this.onCosmeticChange_}">
      </cr-toggle>
    </div>
    <section id="fingerprinting">
      <div id="fpControl">
        <div id="fpTexts">
          <div id="fpLabel">${this.i18n_('fingerprinting')}</div>
          <div id="fpStatus">${this.fingerprintingStatus_()}</div>
        </div>
        ${this.fingerprintingHasSwitch_() ? html`
          <cr-toggle id="fpToggle"
              ?checked="${this.state_.fingerprinting.mode === FingerprintingMode.kOn}"
              aria-label="${this.i18n_('fingerprinting')}"
              @change="${this.onFingerprintingChange_}">
          </cr-toggle>` : ''}
        ${this.state_.fingerprinting.mode === FingerprintingMode.kOffInSettings ? html`
          <cr-button id="fpSettings" class="text-button"
              @click="${this.onNoiseSettingsClick_}">
            ${this.i18n_('fingerprintingSettingsLink')}
          </cr-button>` : ''}
      </div>
      ${this.fingerprintingHasSwitch_() ? html`
        <p id="fpScope">${this.fingerprintingScopeText_()}</p>
        ${this.state_.incognito ? html`
          <p id="fpNote">${this.i18n_('fingerprintingIncognito')}</p>` : ''}` : ''}
      ${this.state_.fingerprinting.needsReload ? html`
        <div id="reloadPrompt" role="status">
          <span>${this.i18n_('reloadToApply')}</span>
          <cr-button id="reload" class="text-button"
              @click="${this.onReloadClick_}">
            ${this.i18n_('reload')}
          </cr-button>
        </div>` : ''}
      <details id="surfaces">
        <summary>${this.i18n_('surfacesHeading')}</summary>
        <ul>
          ${this.state_.fingerprinting.surfaces.map(surface => html`
            <li>${this.surfaceText_(surface)}</li>`)}
        </ul>
      </details>
    </section>
  </details>
` : html`${this.loaded_ ? html`
  <p id="empty">${this.i18n_('noSite')}</p>
` : ''}`}
<footer>
  <cr-button id="guardSettings" class="text-button"
      @click="${this.onGuardSettingsClick_}">
    ${this.i18n_('guardSettings')}
  </cr-button>
</footer>
<!--_html_template_end_-->`;
  // clang-format on
}
