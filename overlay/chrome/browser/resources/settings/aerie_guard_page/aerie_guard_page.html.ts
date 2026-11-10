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
      <div class="cr-row list-row" role="listitem"
          data-list-id="${list.id}">
        <div class="flex cr-padded-text">
          <div class="list-title">${this.listTitle_(list)}</div>
          <div class="secondary list-rules">${this.listRulesText_(list)}</div>
          <div class="secondary list-ignored">
            ${this.listIgnoredText_(list)}
          </div>
          ${list.enabled ? html`
            ${list.url ? html`
              <div class="secondary list-when">${this.listWhenText_(list)}</div>
            ` : ''}
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
        ${list.url ? html`
          <cr-button class="list-update" data-id="${list.id}"
              ?disabled="${list.status === 'updating'}"
              aria-label="${this.listUpdateLabel_(list)}"
              @click="${this.onListUpdateClick_}">
            ${this.listUpdateButton_(list)}
          </cr-button>
        ` : ''}
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
      <h2 id="customHeading" class="cr-title-text">
        $i18n{aerieGuardCustomHeading}
      </h2>
      <div class="secondary">$i18n{aerieGuardCustomDesc}</div>
    </div>
  </div>
  <div class="cr-row custom-rules-row">
    <!-- The text is set as a property, so it is text and never markup. -->
    <textarea id="customRules" class="flex" rows="8" spellcheck="false"
        autocomplete="off" autocapitalize="off"
        aria-label="$i18n{aerieGuardCustomLabel}"
        placeholder="$i18n{aerieGuardCustomPlaceholder}"
        .value="${this.customDraft_}"
        @input="${this.onCustomInput_}"></textarea>
  </div>
  <div class="cr-row">
    <div class="flex secondary" id="customSummary">
      ${this.customSummaryText_()}
    </div>
    <cr-button id="saveCustomRules" class="action-button"
        ?disabled="${!this.canSaveCustom_()}"
        @click="${this.onCustomSaveClick_}">
      $i18n{aerieGuardCustomSave}
    </cr-button>
  </div>
  ${this.customError_ ? html`
    <div id="customError" class="cr-row list-error" role="alert">
      ${this.customError_}
    </div>
  ` : ''}
  ${this.customSaved_ ? html`
    <div id="customSaved" class="cr-row secondary" role="status">
      $i18n{aerieGuardCustomSaved}
    </div>
  ` : ''}
  ${this.customRules_.issueCount > 0 ? html`
    <div class="cr-row">
      <h3 id="customIssuesHeading" class="cr-title-text">
        $i18n{aerieGuardCustomIssuesHeading}
      </h3>
    </div>
    <div id="customIssues" role="list" aria-labelledby="customIssuesHeading">
      ${this.customRules_.issues.map(issue => html`
        <div class="cr-row custom-issue" role="listitem"
            data-reason="${issue.reason}">
          <div class="flex cr-padded-text">
            <div class="secondary">${this.issueLineText_(issue)}</div>
            <code class="issue-rule">${issue.rule}</code>
            <div class="secondary issue-reason">
              ${this.issueReasonText_(issue)}
            </div>
          </div>
        </div>`)}
    </div>
    ${this.customRules_.issueCount > this.customRules_.issues.length ? html`
      <div id="customIssuesMore" class="cr-row secondary">
        ${this.issuesMoreText_()}
      </div>
    ` : ''}
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
        <div class="cr-row site-row" role="listitem">
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
