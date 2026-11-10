// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsAerieGuardPageElement} from './aerie_guard_page.js';

// Every word on the page is a string of the grd (aerie_guard_settings_strings.grdp).
export function getHtml(this: SettingsAerieGuardPageElement) {
  return html`<!--_html_template_start_-->
<settings-section page-title="$i18n{aerieGuardPageTitle}"
    class="cr-centered-card-container">
  <settings-toggle-button id="showCount"
      pref-key="aerie.guard.show_blocked_count"
      label="$i18n{aerieGuardShowCount}"
      sub-label="$i18n{aerieGuardShowCountDesc}">
  </settings-toggle-button>
  <div class="cr-row hr">
    <div class="flex cr-padded-text">
      <div id="defaultLevelLabel">$i18n{aerieGuardDefaultLevel}</div>
      <div class="secondary">$i18n{aerieGuardDefaultLevelDesc}</div>
    </div>
    <select id="defaultLevel" class="md-select"
        aria-labelledby="defaultLevelLabel"
        .value="${this.defaultLevel_}"
        @change="${this.onDefaultLevelChange_}">
      <option value="aggressive"
          ?selected="${this.defaultLevel_ === 'aggressive'}">
        $i18n{aerieGuardLevelAggressive}
      </option>
      <option value="standard"
          ?selected="${this.defaultLevel_ === 'standard'}">
        $i18n{aerieGuardLevelStandard}
      </option>
    </select>
  </div>
  <div class="cr-row hr">
    <div class="flex cr-padded-text">
      <div id="autoUpdateLabel">$i18n{aerieGuardAutoUpdate}</div>
      <div class="secondary">$i18n{aerieGuardAutoUpdateDesc}</div>
    </div>
    <cr-toggle id="autoUpdate" ?checked="${this.autoUpdate_}"
        aria-labelledby="autoUpdateLabel"
        @change="${this.onAutoUpdateChange_}">
    </cr-toggle>
  </div>
  <div class="cr-row hr">
    <div class="flex cr-padded-text">
      <h2 id="listsHeading" class="cr-title-text">
        $i18n{aerieGuardListsHeading}
      </h2>
      <div class="secondary">$i18n{aerieGuardListsDesc}</div>
    </div>
  </div>
  <div id="lists" role="list" aria-labelledby="listsHeading">
    ${this.lists_.map(list => html`
      <div class="cr-row list-item list-row" role="listitem"
          data-list-id="${list.id}">
        <div class="flex cr-padded-text">
          <div class="list-title">${list.title}</div>
          <div class="secondary list-rules">${this.listRulesText_(list)}</div>
          <div class="secondary list-ignored">
            ${this.listIgnoredText_(list)}
          </div>
          ${list.enabled ? html`
            <div class="secondary list-when">${this.listWhenText_(list)}</div>
          ` : html`
            <div class="secondary list-off">$i18n{aerieGuardListOff}</div>
          `}
          ${list.required ? html`
            <div class="secondary list-required">
              $i18n{aerieGuardListRequired}
            </div>
          ` : ''}
          ${list.status !== 'idle' ? html`
            <div class="secondary list-status" data-status="${list.status}"
                role="status">
              ${this.listStatusText_(list)}
            </div>
          ` : ''}
          ${list.errorCode ? html`
            <div class="list-error" role="alert">
              ${this.listFailureText_(list)}
            </div>
          ` : ''}
        </div>
        <cr-button class="list-update" data-id="${list.id}"
            ?disabled="${list.status === 'updating'}"
            aria-label="${this.listUpdateLabel_(list)}"
            @click="${this.onListUpdateClick_}">
          ${this.listUpdateButton_(list)}
        </cr-button>
        <cr-toggle class="list-toggle" data-id="${list.id}"
            ?checked="${list.enabled}" ?disabled="${list.required}"
            aria-label="${this.listToggleLabel_(list)}"
            @change="${this.onListToggleChange_}">
        </cr-toggle>
      </div>`)}
  </div>
  ${this.listError_ ? html`
    <div id="listError" class="cr-row list-error" role="alert">
      ${this.listError_}
    </div>
  ` : ''}
  <div class="cr-row hr">
    <div class="flex cr-padded-text">
      <h2 id="sitesHeading" class="cr-title-text">
        $i18n{aerieGuardSitesHeading}
      </h2>
      <div class="secondary">$i18n{aerieGuardSitesDesc}</div>
    </div>
  </div>
  ${this.exceptions_.length ? html`
    <div id="exceptions" role="list" aria-labelledby="sitesHeading">
      ${this.exceptions_.map(host => html`
        <div class="cr-row list-item" role="listitem">
          <div class="flex site-name">${host}</div>
          <cr-button class="remove-site" data-host="${host}"
              aria-label="${this.removeSiteLabel_(host)}"
              @click="${this.onRemoveSiteClick_}">
            $i18n{aerieGuardRemoveSite}
          </cr-button>
        </div>`)}
    </div>
  ` : html`
    <div id="noExceptions" class="cr-row secondary">
      $i18n{aerieGuardSitesEmpty}
    </div>
  `}
  <div class="cr-row">
    <cr-input id="siteInput" class="flex" autocomplete="off"
        label="$i18n{aerieGuardAddSiteLabel}"
        placeholder="$i18n{aerieGuardAddSitePlaceholder}"
        .value="${this.newSite_}"
        ?invalid="${!!this.siteError_}"
        error-message="${this.siteError_}"
        @value-changed="${this.onNewSiteValueChanged_}"
        @keydown="${this.onNewSiteKeydown_}">
    </cr-input>
    <cr-button id="addSite" class="action-button"
        ?disabled="${!this.canAddSite_()}"
        @click="${this.onAddSiteClick_}">
      $i18n{aerieGuardAddSiteButton}
    </cr-button>
  </div>
</settings-section>
<!--_html_template_end_-->`;
}
