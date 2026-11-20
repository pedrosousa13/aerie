// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {SettingsAerieCrashReportsPageElement} from './aerie_crash_reports_page.js';

// Every word on the page is a string of the grd (aerie_crash_reports_strings.grdp).
export function getHtml(this: SettingsAerieCrashReportsPageElement) {
  return html`<!--_html_template_start_-->
<settings-section page-title="$i18n{aerieCrashReportsTitle}"
    class="cr-centered-card-container">
  <div class="cr-row first">
    <div class="flex cr-padded-text">
      <div id="intro">$i18n{aerieCrashReportsIntro}</div>
      <div id="privacy" class="secondary">
        $i18n{aerieCrashReportsPrivacy}
      </div>
      <div id="limit" class="secondary">${this.limitText_()}</div>
    </div>
  </div>
  <div class="cr-row">
    <div class="flex cr-padded-text">
      <h2 id="reportsHeading" class="cr-title-text">
        $i18n{aerieCrashReportsHeading}
      </h2>
    </div>
    <cr-button id="openFolder" @click="${this.onOpenFolderClick_}">
      $i18n{aerieCrashReportsOpenFolder}
    </cr-button>
    <cr-button id="deleteAll" class="delete-all"
        ?disabled="${!this.reports_.length}"
        @click="${this.onDeleteAllClick_}">
      $i18n{aerieCrashReportsDeleteAll}
    </cr-button>
  </div>
  ${this.reports_.length ? html`
    <div id="reports" role="list" aria-labelledby="reportsHeading">
      ${this.reports_.map(report => html`
        <div class="cr-row report-row" role="listitem"
            data-report-id="${report.id}">
          <div class="flex cr-padded-text">
            <div class="report-time">${this.timeText_(report)}</div>
            <div class="secondary report-details">
              ${this.detailsText_(report)}
            </div>
          </div>
          <cr-button class="report-delete" data-id="${report.id}"
              aria-label="${this.deleteLabel_(report)}"
              @click="${this.onDeleteClick_}">
            $i18n{aerieCrashReportsDelete}
          </cr-button>
        </div>`)}
    </div>
  ` : html`
    <div id="empty" class="cr-row secondary">
      $i18n{aerieCrashReportsEmpty}
    </div>
  `}
  ${this.error_ ? html`
    <div id="error" class="cr-row report-error" role="alert">
      ${this.error_}
    </div>
  ` : ''}
</settings-section>
<!--_html_template_end_-->`;
}
