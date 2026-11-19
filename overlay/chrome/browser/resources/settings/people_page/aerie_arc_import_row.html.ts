// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {html, nothing} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsAerieArcImportRowElement} from './aerie_arc_import_row.js';

// Every word is a string of the grd (aerie_arc_import_strings.grdp). Every
// piece of text from the file (a Space's name, its icon) goes in as a text
// binding, which Lit escapes: it never becomes markup.
export function getHtml(this: SettingsAerieArcImportRowElement) {
  return html`<!--_html_template_start_-->
<cr-link-row id="aerieArcImportTrigger"
    label="$i18n{aerieArcImport}"
    sub-label="$i18n{aerieArcImportDesc}"
    ?disabled="${this.busy_}"
    @click="${this.onTriggerClick_}">
</cr-link-row>
${this.dialogOpen_ ? html`
<cr-dialog id="aerieArcImportDialog" close-text="$i18n{close}"
    ignore-popstate @close="${this.onDialogClose_}">
  <div slot="title">$i18n{aerieArcImportDialogTitle}</div>
  <div slot="body">
    ${this.errorKey_ ? html`
      <div id="aerieArcImportError" class="aerie-arc-error" role="alert">
        ${this.errorText_()}
      </div>` : nothing}
    ${this.step_ === 'pick' ? html`
      <div class="aerie-arc-heading">$i18n{aerieArcImportPickerHeading}</div>
      <div class="secondary">$i18n{aerieArcImportPickerDesc}</div>
      <div id="aerieArcImportSpaces" class="aerie-arc-spaces" role="radiogroup"
          aria-label="$i18n{aerieArcImportPickerAria}">
        ${this.spaces_.map(space => html`
          <label class="aerie-arc-space">
            <input type="radio" name="aerieArcSpace"
                .value="${String(space.index)}"
                ?checked="${space.index === this.selected_}"
                @change="${this.onSpaceChange_}">
            <span class="aerie-arc-icon" aria-hidden="true">${
                space.icon || '▣'}</span>
            <span class="aerie-arc-text">
              <span class="aerie-arc-name">${this.spaceName_(space)}</span>
              <span class="secondary">${this.spaceCounts_(space)}</span>
            </span>
          </label>`)}
      </div>` : nothing}
    ${this.step_ === 'done' && this.result_ ? html`
      <div id="aerieArcImportResult">
        <div class="aerie-arc-heading">$i18n{aerieArcImportResultHeading}</div>
        <ul class="aerie-arc-lines">
          <li>${this.resultLine_('aerieArcImportResultPinned',
                                 this.result_.pinnedTabs)}</li>
          <li>${this.resultLine_('aerieArcImportResultFolders',
                                 this.result_.folders)}</li>
          <li>${this.resultLine_('aerieArcImportResultFavorites',
                                 this.result_.favorites)}</li>
          <li>${this.resultLine_('aerieArcImportResultNames',
                                 this.result_.customNames)}</li>
        </ul>
        ${this.skippedLines_().length ? html`
          <div class="aerie-arc-heading">$i18n{aerieArcImportSkippedHeading}</div>
          <ul class="aerie-arc-lines" id="aerieArcImportSkipped">
            ${this.skippedLines_().map(line => html`<li>${line}</li>`)}
          </ul>` : nothing}
        <div class="secondary">$i18n{aerieArcImportNotImportedFeatures}</div>
        <div class="secondary">$i18n{aerieArcImportNoLogins}</div>
      </div>` : nothing}
  </div>
  <div slot="button-container">
    ${this.step_ === 'done' || this.step_ === 'error' ? html`
      <cr-button class="action-button" id="aerieArcImportDone"
          @click="${this.onDialogCloseClick_}">
        $i18n{aerieArcImportDone}
      </cr-button>` : html`
      <cr-button class="cancel-button" id="aerieArcImportCancel"
          @click="${this.onDialogCloseClick_}">
        $i18n{aerieArcImportCancel}
      </cr-button>
      <cr-button class="action-button" id="aerieArcImportConfirm"
          ?disabled="${this.busy_ || this.step_ !== 'pick'}"
          @click="${this.onImportClick_}">
        $i18n{aerieArcImportImport}
      </cr-button>`}
  </div>
</cr-dialog>` : nothing}
<!--_html_template_end_-->`;
}
