// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {html, nothing} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsAerieTitleRulesRowElement} from './aerie_title_rules_row.js';

// Every word is a string of the grd (aerie_sidebar_strings.grdp). Page text (a
// title, a rule's name or value) is only ever bound as text or as the value of
// a field, never as HTML.
export function getHtml(this: SettingsAerieTitleRulesRowElement) {
  return html`<!--_html_template_start_-->
<div class="settings-box" id="aerieTidyBox">
  <div class="flex cr-padded-text">
    <div id="aerieTidyLabel">$i18n{aerieTidyTitles}</div>
    <div class="secondary" id="aerieTidyDesc">$i18n{aerieTidyTitlesDesc}</div>
  </div>
  <button class="aerie-switch" id="aerieTidyToggle" type="button"
      role="switch" aria-checked="${this.tidyOn_}"
      aria-labelledby="aerieTidyLabel" aria-describedby="aerieTidyDesc"
      @click="${this.onTidyClick_}"></button>
</div>
<div class="aerie-block" id="aerieTidySites">
  <div id="aerieTidySitesLabel">$i18n{aerieTidyOffSites}</div>
  <div class="aerie-hint" id="aerieTidySitesDesc">$i18n{aerieTidyOffSitesDesc}</div>
  <div class="aerie-inline">
    <input id="aerieTidySiteInput" type="text" autocomplete="off"
        spellcheck="false" maxlength="256" style="max-width: 280px"
        aria-labelledby="aerieTidySitesLabel"
        aria-describedby="aerieTidySitesDesc"
        .value="${this.siteDraft_}" @input="${this.onSiteInput_}"
        @keydown="${this.onSiteKeydown_}">
    <button class="aerie-button" id="aerieTidySiteAdd" type="button"
        ?disabled="${this.siteDraft_.trim() === ''}"
        @click="${this.onAddSiteClick_}">$i18n{aerieTidyOffSitesAdd}</button>
  </div>
  <ul class="aerie-list" id="aerieTidySiteList"
      aria-labelledby="aerieTidySitesLabel">
    ${this.offSites_.map(host => html`
    <li class="aerie-item">
      <span class="aerie-item-text aerie-item-name">${host}</span>
      <button class="aerie-button" type="button" data-host="${host}"
          aria-label="$i18n{aerieTidyOffSitesRemove}: ${host}"
          @click="${this.onRemoveSiteClick_}">$i18n{aerieRuleRemove}</button>
    </li>`)}
  </ul>
</div>

<div class="aerie-heading" id="aerieRulesHeading">$i18n{aerieRulesTitle}</div>
<div class="settings-box" id="aerieRulesBox">
  <div class="flex cr-padded-text">
    <div id="aerieRulesAllLabel">$i18n{aerieRulesAll}</div>
    <div class="secondary" id="aerieRulesAllDesc">$i18n{aerieRulesAllDesc}</div>
  </div>
  <button class="aerie-switch" id="aerieRulesToggle" type="button"
      role="switch" aria-checked="${this.rulesOn_}"
      aria-labelledby="aerieRulesAllLabel" aria-describedby="aerieRulesAllDesc"
      @click="${this.onRulesClick_}"></button>
</div>
<div class="aerie-block">
  <div class="aerie-hint" id="aerieRulesHelp">$i18n{aerieRulesHelp}</div>
  <ul class="aerie-list" id="aerieRuleList" role="listbox"
      aria-labelledby="aerieRulesHeading"
      aria-describedby="aerieRulesHelp"
      @keydown="${this.onListKeydown_}">
    ${this.rules_.map((rule, index) => html`
    <li class="${this.rowClass_(rule, index)}"
        role="option" draggable="true"
        @dragstart="${this.onDragstart_}" @dragover="${this.onDragover_}"
        @drop="${this.onDrop_}" @dragend="${this.onDragend_}"
        data-index="${index}" aria-selected="${index === this.selected_}"
        tabindex="${index === this.selected_ ? 0 : -1}"
        @focus="${this.onOptionFocus_}">
      <button class="aerie-switch" type="button" role="switch"
          aria-checked="${rule.enabled}" tabindex="-1"
          aria-label="$i18n{aerieRuleSwitch}: ${this.displayName_(rule)}"
          @click="${this.onToggleRuleClick_}"></button>
      <span class="aerie-item-text">
        <div class="aerie-item-name">${this.displayName_(rule)}</div>
        <div class="aerie-item-summary">${this.summary_(rule)}</div>
      </span>
      <button class="aerie-button" type="button"
          aria-label="$i18n{aerieRuleMoveUp}: ${this.displayName_(rule)}"
          ?disabled="${index === 0}" @click="${this.onMoveUpClick_}">$i18n{aerieRuleUp}</button>
      <button class="aerie-button" type="button"
          aria-label="$i18n{aerieRuleMoveDown}: ${this.displayName_(rule)}"
          ?disabled="${index === this.rules_.length - 1}"
          @click="${this.onMoveDownClick_}">$i18n{aerieRuleDown}</button>
      <button class="aerie-button" type="button"
          aria-label="$i18n{aerieRuleEdit}: ${this.displayName_(rule)}"
          @click="${this.onEditClick_}">$i18n{aerieRuleEdit}</button>
      <button class="aerie-button" type="button"
          aria-label="$i18n{aerieRuleRemove}: ${this.displayName_(rule)}"
          @click="${this.onRemoveClick_}">$i18n{aerieRuleRemove}</button>
    </li>`)}
  </ul>
  <div class="aerie-hint" id="aerieRulesEmpty" ?hidden="${this.rules_.length > 0}">$i18n{aerieRulesEmpty}</div>
  <div class="aerie-hint aerie-status" id="aerieRulesAnnounce" role="status"
      aria-live="polite">${this.announcement_}</div>
  <button class="aerie-button" id="aerieAddRule" type="button"
      @click="${this.onAddRuleClick_}">$i18n{aerieRuleAdd}</button>
</div>
${this.draft_ ? html`
<form class="aerie-form" id="aerieRuleForm" @keydown="${this.onDraftKeydown_}"
    @submit="${this.onSubmit_}"
    aria-labelledby="aerieRuleFormTitle">
  <div id="aerieRuleFormTitle">${this.draft_.id === null ? html`$i18n{aerieRuleNew}` : html`$i18n{aerieRuleEditing}`}</div>
  <label class="aerie-field">$i18n{aerieRuleName}
    <input id="aerieRuleName" type="text" data-field="name" maxlength="100"
        autocomplete="off" .value="${this.draft_.name}" @input="${this.onDraftInput_}">
  </label>
  <label class="aerie-field">$i18n{aerieRuleMatch}
    <select id="aerieRuleType" data-field="type" @change="${this.onDraftInput_}">
      <option value="domain" ?selected="${this.draft_.type === 'domain'}">$i18n{aerieRuleTypeDomain}</option>
      <option value="url-starts-with" ?selected="${this.draft_.type === 'url-starts-with'}">$i18n{aerieRuleTypeUrlStartsWith}</option>
      <option value="url-contains" ?selected="${this.draft_.type === 'url-contains'}">$i18n{aerieRuleTypeUrlContains}</option>
      <option value="title-contains" ?selected="${this.draft_.type === 'title-contains'}">$i18n{aerieRuleTypeTitleContains}</option>
    </select>
  </label>
  <label class="aerie-field">$i18n{aerieRuleValue}
    <input id="aerieRuleValue" type="text" data-field="value" maxlength="512"
        autocomplete="off" spellcheck="false" .value="${this.draft_.value}"
        @input="${this.onDraftInput_}">
  </label>
  <label class="aerie-field">$i18n{aerieRuleRewrite}
    <select id="aerieRuleMode" data-field="mode" @change="${this.onDraftInput_}">
      <option value="template" ?selected="${this.draft_.mode === 'template'}">$i18n{aerieRuleModeTemplate}</option>
      <option value="remove" ?selected="${this.draft_.mode === 'remove'}">$i18n{aerieRuleModeRemove}</option>
    </select>
  </label>
  <label class="aerie-field">${this.draft_.mode === 'template' ? html`$i18n{aerieRuleTemplate}` : html`$i18n{aerieRuleRemoveText}`}
    <input id="aerieRuleText" type="text" data-field="text" maxlength="512"
        autocomplete="off" spellcheck="false" .value="${this.draft_.text}"
        aria-describedby="aerieRuleTextHint" @input="${this.onDraftInput_}">
  </label>
  <div class="aerie-hint" id="aerieRuleTextHint">${this.draft_.mode === 'template' ? html`$i18n{aerieRuleTemplateHint}` : html`$i18n{aerieRuleRemoveHint}`}</div>
  <div class="aerie-error" id="aerieRuleError" role="alert">${this.errorText_()}</div>
  <div class="aerie-inline">
    <button class="aerie-button" id="aerieRuleSave" type="button"
        @click="${this.onSaveEdit_}">$i18n{aerieRuleSave}</button>
    <button class="aerie-button" id="aerieRuleCancel" type="button"
        @click="${this.onCancelEdit_}">$i18n{aerieRuleCancel}</button>
  </div>
</form>` : nothing}
<div class="aerie-block" id="aerieTry">
  <div id="aerieTryLabel">$i18n{aerieTryTitle}</div>
  <div class="aerie-field">
    <label>$i18n{aerieTryUrl}
      <input id="aerieTryUrl" type="text" data-field="url" maxlength="2048"
          autocomplete="off" spellcheck="false" .value="${this.tryUrl_}"
          @input="${this.onTryInput_}">
    </label>
    <label>$i18n{aerieTryPageTitle}
      <input id="aerieTryPageTitle" type="text" data-field="title"
          maxlength="1024" autocomplete="off" .value="${this.tryTitle_}"
          @input="${this.onTryInput_}">
    </label>
  </div>
  <div class="aerie-result aerie-status" id="aerieTryResult" role="status"
      aria-live="polite">${this.tryResultText_()}</div>
</div>
<!--_html_template_end_-->`;
}
