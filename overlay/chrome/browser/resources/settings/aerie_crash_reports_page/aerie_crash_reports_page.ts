// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-aerie-crash-reports-page' is the Crash reports section of
 * Settings (issue #262): the reports Aerie saved on this computer when it
 * crashed, with a Delete action for each, Delete all, and Open folder. Nothing
 * is sent anywhere, and the page shows no part of the contents of a report:
 * only the time, the kind of process and the version. It is a top-level route
 * (/crashReports), wired the way the Aerie Guard page is.
 */

import 'chrome://resources/cr_elements/cr_button/cr_button.js';
import '../i18n_setup.js';
import '../settings_page/settings_section.js';

import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {CrLitElement, css} from 'chrome://resources/lit/v3_0/lit.rollup.js';
import {getCss as getCrSharedStyle} from 'chrome://resources/cr_elements/cr_shared_style_lit.css.js';

import {getSearchManager} from '../search_settings.js';
import type {SettingsPlugin} from '../settings_main/settings_plugin.js';
import {SettingsViewMixinLit} from '../settings_page/settings_view_mixin_lit.js';
import {getCss as getSettingsSharedLit} from '../settings_shared_lit.css.js';

import {AerieCrashReportsBrowserProxyImpl} from './aerie_crash_reports_browser_proxy.js';
import type {AerieCrashReport, AerieCrashReportsBrowserProxy, AerieCrashReportsState} from './aerie_crash_reports_browser_proxy.js';
import {getHtml} from './aerie_crash_reports_page.html.js';

const SettingsAerieCrashReportsPageElementBase =
    SettingsViewMixinLit(CrLitElement);

export class SettingsAerieCrashReportsPageElement extends
    SettingsAerieCrashReportsPageElementBase implements SettingsPlugin {
  static get is() {
    return 'settings-aerie-crash-reports-page';
  }

  static override get styles() {
    return [
      getCrSharedStyle(),
      getSettingsSharedLit(),
      css`
        .report-error {
          color: var(--cr-form-field-error-color, var(--google-red-600));
        }
        cr-button.delete-all {
          margin-inline-start: 8px;
        }
        .report-time {
          font-variant-numeric: tabular-nums;
        }
      `,
    ];
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      reports_: {type: Array},
      limit_: {type: Number},
      error_: {type: String},
    };
  }

  protected accessor reports_: AerieCrashReport[] = [];
  protected accessor limit_: number = 20;
  protected accessor error_: string = '';

  private browserProxy_: AerieCrashReportsBrowserProxy =
      AerieCrashReportsBrowserProxyImpl.getInstance();

  override connectedCallback() {
    super.connectedCallback();
    // The page asks once when it opens. A crash while Settings is open shows
    // on the next visit.
    this.browserProxy_.list().then(state => this.applyState_(state));
  }

  private applyState_(state: AerieCrashReportsState, failure: string = '') {
    this.reports_ = state.reports;
    this.limit_ = state.limit;
    this.error_ = state.ok ? '' : failure;
  }

  protected limitText_(): string {
    return loadTimeData.getStringF('aerieCrashReportsLimit', this.limit_);
  }

  protected timeText_(report: AerieCrashReport): string {
    return new Date(report.time).toLocaleString();
  }

  private processText_(processType: string): string {
    switch (processType) {
      case 'browser':
        return loadTimeData.getString('aerieCrashReportsProcessBrowser');
      case 'renderer':
        return loadTimeData.getString('aerieCrashReportsProcessRenderer');
      case 'gpu-process':
        return loadTimeData.getString('aerieCrashReportsProcessGpu');
      case 'utility':
        return loadTimeData.getString('aerieCrashReportsProcessUtility');
      case '':
        return loadTimeData.getString('aerieCrashReportsProcessUnknown');
      default:
        // A plain token the browser checked. Shown as text.
        return processType;
    }
  }

  protected detailsText_(report: AerieCrashReport): string {
    return loadTimeData.getStringF(
        'aerieCrashReportsDetails', this.processText_(report.processType),
        report.version ||
            loadTimeData.getString('aerieCrashReportsVersionUnknown'));
  }

  protected deleteLabel_(report: AerieCrashReport): string {
    return loadTimeData.getStringF(
        'aerieCrashReportsDeleteAria', this.timeText_(report));
  }

  protected onDeleteClick_(e: Event) {
    const id = (e.currentTarget as HTMLElement).dataset['id'];
    if (!id) {
      return;
    }
    this.browserProxy_.deleteReport(id).then(
        state => this.applyState_(
            state, loadTimeData.getString('aerieCrashReportsErrorDelete')),
        () => {
          this.error_ = loadTimeData.getString('aerieCrashReportsErrorDelete');
        });
  }

  protected onDeleteAllClick_() {
    this.browserProxy_.deleteAll().then(
        state => this.applyState_(
            state, loadTimeData.getString('aerieCrashReportsErrorDelete')),
        () => {
          this.error_ = loadTimeData.getString('aerieCrashReportsErrorDelete');
        });
  }

  protected onOpenFolderClick_() {
    this.browserProxy_.openFolder().then(
        state => this.applyState_(
            state, loadTimeData.getString('aerieCrashReportsErrorOpen')),
        () => {
          this.error_ = loadTimeData.getString('aerieCrashReportsErrorOpen');
        });
  }

  // SettingsPlugin implementation.
  async searchContents(query: string) {
    const searchRequest = await getSearchManager().search(query, this);
    return searchRequest.getSearchResult();
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-aerie-crash-reports-page': SettingsAerieCrashReportsPageElement;
  }
}

customElements.define(
    SettingsAerieCrashReportsPageElement.is,
    SettingsAerieCrashReportsPageElement);
