// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-aerie-arc-import-row' is the "Import from Arc" row next to the
 * import entry of the People page (issue #171, docs/arc-import.md). A click
 * asks AerieArcImportHandler for the system file chooser (.json); the handler
 * reads and checks the file out of the UI thread and answers with the Spaces it
 * holds. The dialog then lets the user pick one Space (name, icon, counts), and
 * shows what the import added and what it left out. Nothing from the file is
 * ever put in as markup: names and icons are text bindings.
 */

import 'chrome://resources/cr_elements/cr_button/cr_button.js';
import 'chrome://resources/cr_elements/cr_dialog/cr_dialog.js';
import 'chrome://resources/cr_elements/cr_link_row/cr_link_row.js';
import '../i18n_setup.js';

import type {CrDialogElement} from 'chrome://resources/cr_elements/cr_dialog/cr_dialog.js';
import {css, CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';
import type {PropertyValues} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {loadTimeData} from '../i18n_setup.js';
import {getCss as getSettingsSharedLit} from '../settings_shared_lit.css.js';

import type {AerieArcImportResult, AerieArcSpace} from './aerie_arc_import_browser_proxy.js';
import {AerieArcImportBrowserProxyImpl} from './aerie_arc_import_browser_proxy.js';
import {getHtml} from './aerie_arc_import_row.html.js';

type Step = 'pick'|'done'|'error';
type DoneResult = Extract<AerieArcImportResult, {status: 'ok'}>;

// The reason names the handler sends, and the grd string of each.
const SKIP_STRINGS: {[reason: string]: string} = {
  unsupported_scheme: 'aerieArcImportSkipUnsupportedScheme',
  no_usable_address: 'aerieArcImportSkipNoUsableAddress',
  arc_only_item: 'aerieArcImportSkipArcOnlyItem',
  too_many: 'aerieArcImportSkipTooMany',
  too_deep: 'aerieArcImportSkipTooDeep',
  folder_merged: 'aerieArcImportSkipFolderMerged',
  favorites_full: 'aerieArcImportSkipFavoritesFull',
  little_arc: 'aerieArcImportSkipLittleArc',
  over_cap: 'aerieArcImportSkipOverCap',
  already_there: 'aerieArcImportSkipAlreadyThere',
};

// The error names the handler sends, and the grd string of each.
const ERROR_STRINGS: {[error: string]: string} = {
  too_large: 'aerieArcImportErrorTooLarge',
  unreadable: 'aerieArcImportErrorUnreadable',
  not_a_file: 'aerieArcImportErrorNotAFile',
  invalid_json: 'aerieArcImportErrorInvalidJson',
  not_arc: 'aerieArcImportErrorNotArc',
  no_spaces: 'aerieArcImportErrorNoSpaces',
  not_ready: 'aerieArcImportErrorNotReady',
  apply_failed: 'aerieArcImportErrorApplyFailed',
};

export class SettingsAerieArcImportRowElement extends CrLitElement {
  static get is() {
    return 'settings-aerie-arc-import-row';
  }

  static override get styles() {
    return [
      getSettingsSharedLit(),
      css`
        .aerie-arc-heading {
          font-weight: 500;
          padding-block: 12px 4px;
        }
        .aerie-arc-spaces {
          display: flex;
          flex-direction: column;
          gap: 4px;
          margin-block-start: 12px;
          max-height: 280px;
          overflow-y: auto;
        }
        .aerie-arc-space {
          align-items: center;
          border: 1px solid var(--cr-separator-color);
          border-radius: 8px;
          cursor: pointer;
          display: flex;
          gap: 12px;
          min-height: 48px;
          padding: 6px 12px;
        }
        .aerie-arc-space:has(input:checked) {
          background: var(--cr-hover-background-color);
          border-color: var(--cr-focus-outline-color,
              var(--color-sys-primary));
        }
        .aerie-arc-space:has(input:focus-visible) {
          outline: 2px solid var(--cr-focus-outline-color,
              var(--color-sys-state-focus-ring));
          outline-offset: 2px;
        }
        .aerie-arc-icon {
          font-size: 20px;
          line-height: 1;
          text-align: center;
          width: 28px;
        }
        .aerie-arc-text {
          display: flex;
          flex-direction: column;
          min-width: 0;
        }
        .aerie-arc-name {
          overflow: hidden;
          text-overflow: ellipsis;
          white-space: nowrap;
        }
        .aerie-arc-lines {
          margin: 0;
          padding-inline-start: 20px;
        }
        .aerie-arc-error {
          color: var(--cr-form-field-error-color, var(--color-sys-error));
          padding-block-end: 8px;
        }
      `,
    ];
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      dialogOpen_: {type: Boolean},
      busy_: {type: Boolean},
      step_: {type: String},
      spaces_: {type: Array},
      selected_: {type: Number},
      result_: {type: Object},
      errorKey_: {type: String},
    };
  }

  protected accessor dialogOpen_: boolean = false;
  // A chooser is open, a file is being read, or an import is running.
  protected accessor busy_: boolean = false;
  protected accessor step_: Step = 'pick';
  protected accessor spaces_: AerieArcSpace[] = [];
  protected accessor selected_: number = 0;
  protected accessor result_: DoneResult|null = null;
  protected accessor errorKey_: string = '';

  private readonly proxy_ = AerieArcImportBrowserProxyImpl.getInstance();

  override updated(changedProperties: PropertyValues<this>) {
    super.updated(changedProperties);
    const changed = changedProperties as Map<PropertyKey, unknown>;
    if (changed.has('dialogOpen_') && this.dialogOpen_) {
      const dialog = this.shadowRoot.querySelector<CrDialogElement>(
          '#aerieArcImportDialog');
      if (dialog && !dialog.open) {
        dialog.showModal();
      }
    }
  }

  protected async onTriggerClick_() {
    if (this.busy_) {
      return;
    }
    this.busy_ = true;
    try {
      const answer = await this.proxy_.choose();
      if (answer.status === 'cancelled') {
        return;
      }
      this.result_ = null;
      if (answer.status === 'error') {
        this.showError_(answer.error);
        return;
      }
      this.spaces_ = answer.spaces;
      this.selected_ = answer.spaces.length ? answer.spaces[0].index : 0;
      this.errorKey_ = '';
      this.step_ = 'pick';
      this.dialogOpen_ = true;
    } catch {
      this.showError_('generic');
    } finally {
      this.busy_ = false;
    }
  }

  private showError_(error: string) {
    this.errorKey_ = error;
    this.step_ = 'error';
    this.dialogOpen_ = true;
  }

  protected onSpaceChange_(e: Event) {
    this.selected_ = Number((e.target as HTMLInputElement).value);
  }

  protected async onImportClick_() {
    if (this.busy_ || this.step_ !== 'pick') {
      return;
    }
    this.busy_ = true;
    try {
      const answer = await this.proxy_.importSpace(this.selected_);
      if (answer.status === 'error') {
        this.showError_(answer.error);
        return;
      }
      this.result_ = answer;
      this.errorKey_ = '';
      this.step_ = 'done';
    } catch {
      this.showError_('generic');
    } finally {
      this.busy_ = false;
    }
  }

  protected onDialogCloseClick_() {
    this.shadowRoot.querySelector<CrDialogElement>('#aerieArcImportDialog')
        ?.close();
  }

  protected onDialogClose_() {
    this.dialogOpen_ = false;
    this.spaces_ = [];
    this.result_ = null;
    this.errorKey_ = '';
    this.proxy_.reset();
  }

  protected errorText_(): string {
    return loadTimeData.getString(
        ERROR_STRINGS[this.errorKey_] || 'aerieArcImportErrorGeneric');
  }

  // A Space that Arc left without a name is "Space <number in the file>".
  protected spaceName_(space: AerieArcSpace): string {
    return space.name ||
        loadTimeData.getStringF('aerieArcImportSpaceUnnamed', space.index + 1);
  }

  protected spaceCounts_(space: AerieArcSpace): string {
    return loadTimeData.getStringF(
        'aerieArcImportSpaceCounts', space.pinnedTabs, space.folders,
        space.favorites);
  }

  protected resultLine_(name: string, count: number): string {
    return loadTimeData.getStringF(name, count);
  }

  // One line per reason that has a count; the reasons are the handler's.
  protected skippedLines_(): string[] {
    const skipped = this.result_ ? this.result_.skipped : [];
    return skipped.filter(entry => entry.count > 0 && SKIP_STRINGS[entry.reason])
        .map(
            entry => loadTimeData.getStringF(
                SKIP_STRINGS[entry.reason], entry.count));
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-aerie-arc-import-row': SettingsAerieArcImportRowElement;
  }
}

customElements.define(
    SettingsAerieArcImportRowElement.is, SettingsAerieArcImportRowElement);
