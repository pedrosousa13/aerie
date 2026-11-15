// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-aerie-title-rules-row' is the tab-title part of the Sidebar section
 * of Settings (issue #146): the switch of the built-in tidy and the sites where
 * it is off, the "All rules" switch, the list of rename rules (add, edit,
 * reorder, switch one off, remove), and a box that tries a title.
 *
 * It writes four profile prefs (components/aerie_tab_model/tab_model_prefs.h):
 * aerie.tabs.tidy_titles, aerie.tabs.tidy_off_sites,
 * aerie.tabs.rename_rules_enabled and aerie.tabs.rename_rules. The sidebar
 * watches them, so a change needs no restart. Whatever the pref holds is
 * untrusted: every field is checked here for its type, and the browser checks
 * the rules again when it reads them.
 *
 * Keyboard: the rule list is a listbox with one tab stop. Up and Down move,
 * Home and End jump, Alt+Up and Alt+Down move the rule, Enter edits it and
 * Space switches it. Each rule also has buttons for the mouse (up, down, edit,
 * remove). A rule's switch has the `switch` role. The result of "Try a title"
 * and the note after a move are in live regions.
 *
 * "Make a rename rule from this tab..." in the sidebar's tab menu opens this
 * page with ?aerieRuleHost=...&aerieRuleTitle=...; the editor then opens with
 * the host and the title filled in. Both are page text: they are only ever
 * put into two fields.
 */

import '../i18n_setup.js';

import {css, CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';
import type {PropertyValues} from 'chrome://resources/lit/v3_0/lit.rollup.js';
import {PrefService} from '/shared/settings/prefs2/pref_service.js';
import {PrefServiceObserverMixinLit} from '/shared/settings/prefs2/pref_service_observer_mixin_lit.js';

import {loadTimeData} from '../i18n_setup.js';
import {getCss as getSettingsSharedLit} from '../settings_shared_lit.css.js';

import {AerieTitleRulesBrowserProxyImpl} from './aerie_title_rules_browser_proxy.js';
import type {AerieTitleTryResult} from './aerie_title_rules_browser_proxy.js';
import {getHtml} from './aerie_title_rules_row.html.js';
import type {AerieRenameRule, AerieRewriteMode, AerieRuleError, AerieRuleType} from './aerie_title_rules_model.js';
import {hostFromInput, moveRule, moveRuleTo, newRuleId, rulesFromPref, validateRule} from './aerie_title_rules_model.js';

export const RULES_PREF = 'aerie.tabs.rename_rules';
export const OFF_SITES_PREF = 'aerie.tabs.tidy_off_sites';
export const TIDY_PREF = 'aerie.tabs.tidy_titles';
export const RULES_ON_PREF = 'aerie.tabs.rename_rules_enabled';

const TRY_DELAY_MS = 250;

const SettingsAerieTitleRulesRowElementBase =
    PrefServiceObserverMixinLit(CrLitElement);

/** The rule being added or edited. `id` is null for a new rule. */
interface Draft {
  id: string|null;
  name: string;
  type: AerieRuleType;
  value: string;
  mode: AerieRewriteMode;
  text: string;
}

export class SettingsAerieTitleRulesRowElement extends
    SettingsAerieTitleRulesRowElementBase {
  static get is() {
    return 'settings-aerie-title-rules-row';
  }

  static override get styles() {
    return [
      getSettingsSharedLit(),
      css`
        .aerie-block {
          padding-block: 4px 12px;
          padding-inline: var(--cr-section-padding, 20px);
        }
        .aerie-heading {
          font-weight: 500;
          padding-block: 12px 4px;
          padding-inline: var(--cr-section-padding, 20px);
        }
        .aerie-inline {
          align-items: center;
          display: flex;
          flex-wrap: wrap;
          gap: 8px;
        }
        .aerie-list {
          list-style: none;
          margin: 0;
          padding: 0;
        }
        .aerie-item {
          align-items: center;
          border: 1px solid var(--cr-separator-color);
          border-radius: 8px;
          display: flex;
          gap: 8px;
          margin-block-end: 4px;
          min-height: 40px;
          padding: 4px 8px;
        }
        .aerie-item:focus-visible, button:focus-visible, input:focus-visible,
        select:focus-visible {
          outline: 2px solid var(--cr-focus-outline-color,
              var(--color-sys-state-focus-ring));
          outline-offset: 2px;
        }
        .aerie-item[aria-selected='true'] {
          background: var(--cr-hover-background-color);
        }
        .aerie-item[draggable='true'] {
          cursor: grab;
        }
        .aerie-item.dragging {
          opacity: 0.5;
        }
        .aerie-item.drop-target {
          box-shadow: inset 0 2px 0 var(--cr-focus-outline-color,
              var(--color-sys-primary));
        }
        .aerie-item.off .aerie-item-text {
          opacity: 0.6;
        }
        .aerie-item-text {
          flex: 1;
          min-width: 0;
        }
        .aerie-item-name, .aerie-item-summary {
          overflow: hidden;
          text-overflow: ellipsis;
          white-space: nowrap;
        }
        .aerie-item-summary {
          color: var(--cr-secondary-text-color);
          font-size: 12px;
        }
        .aerie-switch {
          background: transparent;
          border: 1px solid var(--cr-separator-color);
          border-radius: 10px;
          cursor: pointer;
          height: 20px;
          padding: 0;
          position: relative;
          width: 36px;
        }
        .aerie-switch::after {
          background: var(--cr-primary-text-color);
          border-radius: 50%;
          content: '';
          height: 14px;
          inset-block-start: 2px;
          inset-inline-start: 2px;
          position: absolute;
          width: 14px;
        }
        .aerie-switch[aria-checked='true'] {
          background: var(--color-sys-primary, var(--cr-checked-color));
        }
        .aerie-switch[aria-checked='true']::after {
          background: var(--color-sys-on-primary, #fff);
          inset-inline-start: 18px;
        }
        .aerie-button {
          background: transparent;
          border: 1px solid var(--cr-separator-color);
          border-radius: 8px;
          color: var(--cr-primary-text-color);
          cursor: pointer;
          font: inherit;
          min-height: 28px;
          padding: 2px 10px;
        }
        .aerie-button:hover:not([disabled]) {
          background: var(--cr-hover-background-color);
        }
        .aerie-button[disabled] {
          cursor: default;
          opacity: 0.5;
        }
        .aerie-form {
          border: 1px solid var(--cr-separator-color);
          border-radius: 8px;
          display: grid;
          gap: 8px;
          margin-inline: var(--cr-section-padding, 20px);
          padding: 12px;
        }
        .aerie-field {
          display: grid;
          gap: 2px;
        }
        input, select {
          background: transparent;
          border: 1px solid var(--cr-separator-color);
          border-radius: 6px;
          box-sizing: border-box;
          color: var(--cr-primary-text-color);
          font: inherit;
          min-height: 28px;
          padding: 2px 8px;
          width: 100%;
        }
        .aerie-error {
          color: var(--cr-error-color, var(--color-sys-error, #b3261e));
        }
        .aerie-error:empty, .aerie-status:empty {
          display: none;
        }
        .aerie-hint {
          color: var(--cr-secondary-text-color);
          font-size: 12px;
        }
        .aerie-result {
          font-weight: 500;
        }
        @media (forced-colors: active) {
          .aerie-item, .aerie-switch, .aerie-button, input, select {
            border-color: CanvasText;
          }
          .aerie-switch[aria-checked='true'] {
            background: Highlight;
            forced-color-adjust: none;
          }
          .aerie-switch::after {
            background: CanvasText;
            forced-color-adjust: none;
          }
          .aerie-switch[aria-checked='true']::after {
            background: HighlightText;
          }
          .aerie-item[aria-selected='true'] {
            outline: 2px solid Highlight;
          }
        }
        /* Nothing here animates, so reduced motion changes nothing. */
      `,
    ];
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      tidyOn_: {type: Boolean},
      rulesOn_: {type: Boolean},
      rules_: {type: Array},
      offSites_: {type: Array},
      draft_: {type: Object},
      error_: {type: String},
      selected_: {type: Number},
      siteDraft_: {type: String},
      announcement_: {type: String},
      dragFrom_: {type: Number},
      dropAt_: {type: Number},
      tryUrl_: {type: String},
      tryTitle_: {type: String},
      tryResult_: {type: Object},
    };
  }

  protected accessor tidyOn_: boolean = true;
  protected accessor rulesOn_: boolean = true;
  protected accessor rules_: AerieRenameRule[] = [];
  protected accessor offSites_: string[] = [];
  protected accessor draft_: Draft|null = null;
  protected accessor error_: AerieRuleError = '';
  // The option that has the tab stop.
  protected accessor selected_: number = 0;
  protected accessor siteDraft_: string = '';
  // The note for a screen reader after a rule moved.
  protected accessor announcement_: string = '';
  // Mouse reorder: the row being dragged and the row it hovers over (-1: none).
  protected accessor dragFrom_: number = -1;
  protected accessor dropAt_: number = -1;
  protected accessor tryUrl_: string = '';
  protected accessor tryTitle_: string = '';
  protected accessor tryResult_: AerieTitleTryResult|null = null;

  private tryTimer_: number|null = null;
  private tryToken_: number = 0;
  private focusOptionAfterRender_: number|null = null;
  private focusDraftAfterRender_: boolean = false;

  override connectedCallback() {
    super.connectedCallback();
    this.addPrefObserver<unknown>(RULES_PREF, pref => {
      this.rules_ = rulesFromPref(pref.value);
      if (this.selected_ >= this.rules_.length) {
        this.selected_ = Math.max(0, this.rules_.length - 1);
      }
    });
    this.addPrefObserver<unknown>(TIDY_PREF, pref => {
      this.tidyOn_ = pref.value !== false;
    });
    this.addPrefObserver<unknown>(RULES_ON_PREF, pref => {
      this.rulesOn_ = pref.value !== false;
    });
    this.addPrefObserver<unknown>(OFF_SITES_PREF, pref => {
      this.offSites_ = Array.isArray(pref.value) ?
          pref.value.filter((s): s is string => typeof s === 'string') :
          [];
    });
    this.openEditorFromUrl_();
  }

  override disconnectedCallback() {
    super.disconnectedCallback();
    if (this.tryTimer_ !== null) {
      window.clearTimeout(this.tryTimer_);
      this.tryTimer_ = null;
    }
  }

  override updated(changed: PropertyValues<this>) {
    super.updated(changed);
    if (this.focusOptionAfterRender_ !== null) {
      const options = this.shadowRoot.querySelectorAll<HTMLElement>(
          '.aerie-item');
      options[this.focusOptionAfterRender_]?.focus();
      this.focusOptionAfterRender_ = null;
    }
    if (this.focusDraftAfterRender_) {
      this.focusDraftAfterRender_ = false;
      const name = this.shadowRoot.querySelector<HTMLElement>(
          '#aerieRuleName');
      name?.focus();
      name?.scrollIntoView({block: 'nearest'});
    }
  }

  /** "Make a rename rule from this tab...": ?aerieRuleHost and ?aerieRuleTitle. */
  private openEditorFromUrl_() {
    const params = new URLSearchParams(window.location.search);
    const host = params.get('aerieRuleHost');
    const title = params.get('aerieRuleTitle');
    if (host === null && title === null) {
      return;
    }
    const cleanHost = hostFromInput(host || '');
    this.draft_ = {
      id: null,
      name: cleanHost,
      type: cleanHost ? 'domain' : 'title-contains',
      value: cleanHost || (title || '').slice(0, 100),
      mode: 'template',
      text: '{title}',
    };
    this.error_ = '';
    this.tryUrl_ = cleanHost ? 'https://' + cleanHost + '/' : '';
    this.tryTitle_ = (title || '').slice(0, 200);
    this.focusDraftAfterRender_ = true;
    this.scheduleTry_();
  }

  // --- Prefs. ---

  private saveRules_(rules: AerieRenameRule[]) {
    // Write the plain objects the browser reads (rename_rules.cc).
    void PrefService.getInstance().setPrefValue(
        RULES_PREF, rules.map(r => ({...r})));
  }

  private saveOffSites_(sites: string[]) {
    void PrefService.getInstance().setPrefValue(OFF_SITES_PREF, sites);
  }

  protected onTidyClick_() {
    void PrefService.getInstance().setPrefValue(TIDY_PREF, !this.tidyOn_);
  }

  protected onRulesClick_() {
    void PrefService.getInstance().setPrefValue(RULES_ON_PREF, !this.rulesOn_);
  }

  // --- The tidy's per-site list. ---

  protected onSiteInput_(e: Event) {
    this.siteDraft_ = (e.target as HTMLInputElement).value;
  }

  protected onSiteKeydown_(e: KeyboardEvent) {
    if (e.key === 'Enter') {
      e.preventDefault();
      this.onAddSiteClick_();
    }
  }

  protected onAddSiteClick_() {
    const host = hostFromInput(this.siteDraft_);
    if (host === '') {
      return;
    }
    if (!this.offSites_.includes(host)) {
      this.saveOffSites_([...this.offSites_, host]);
    }
    this.siteDraft_ = '';
  }

  protected onRemoveSiteClick_(e: Event) {
    const host = (e.currentTarget as HTMLElement).dataset['host']!;
    this.saveOffSites_(this.offSites_.filter(s => s !== host));
  }

  // --- The list. ---

  protected typeLabel_(type: AerieRuleType): string {
    switch (type) {
      case 'domain':
        return loadTimeData.getString('aerieRuleTypeDomain');
      case 'url-starts-with':
        return loadTimeData.getString('aerieRuleTypeUrlStartsWith');
      case 'url-contains':
        return loadTimeData.getString('aerieRuleTypeUrlContains');
      case 'title-contains':
        return loadTimeData.getString('aerieRuleTypeTitleContains');
    }
  }

  /** "Domain: example.com" */
  protected summary_(rule: AerieRenameRule): string {
    return this.typeLabel_(rule.type) + ': ' + rule.value;
  }

  protected displayName_(rule: AerieRenameRule): string {
    return rule.name || rule.value;
  }

  private indexOfTarget_(e: Event): number {
    const element = (e.currentTarget as HTMLElement).closest<HTMLElement>(
        '.aerie-item');
    return Number(element?.dataset['index'] ?? -1);
  }

  protected onToggleRuleClick_(e: Event) {
    e.stopPropagation();
    this.toggleRule_(this.indexOfTarget_(e));
  }

  private toggleRule_(index: number) {
    const rule = this.rules_[index];
    if (!rule) {
      return;
    }
    const copy = this.rules_.slice();
    copy[index] = {...rule, enabled: !rule.enabled};
    this.saveRules_(copy);
  }

  protected onMoveUpClick_(e: Event) {
    this.moveSelected_(this.indexOfTarget_(e), -1);
  }

  protected onMoveDownClick_(e: Event) {
    this.moveSelected_(this.indexOfTarget_(e), 1);
  }

  private moveSelected_(index: number, delta: number) {
    this.moveTo_(index, index + delta);
  }

  // Keyboard and mouse reorder share this: one save, one announcement.
  private moveTo_(from: number, to: number, focus: boolean = true) {
    const moved = moveRuleTo(this.rules_, from, to);
    if (!moved) {
      return;
    }
    const rule = this.rules_[from];
    this.selected_ = to;
    this.focusOptionAfterRender_ = focus ? to : null;
    this.announcement_ = loadTimeData.getStringF(
        'aerieRuleMoved', this.displayName_(rule), to + 1, this.rules_.length);
    this.saveRules_(moved);
  }

  protected onSubmit_(e: Event) {
    e.preventDefault();
  }

  protected rowClass_(rule: AerieRenameRule, index: number): string {
    const classes = ['aerie-item'];
    if (!rule.enabled) {
      classes.push('off');
    }
    if (index === this.dragFrom_) {
      classes.push('dragging');
    } else if (this.dragFrom_ >= 0 && index === this.dropAt_) {
      classes.push('drop-target');
    }
    return classes.join(' ');
  }

  protected onDragstart_(e: DragEvent) {
    const from = this.indexOfTarget_(e);
    if (from < 0 || !e.dataTransfer) {
      e.preventDefault();
      return;
    }
    this.dragFrom_ = from;
    this.dropAt_ = from;
    // Only the index travels, as an opaque marker; no rule text is exported.
    e.dataTransfer.effectAllowed = 'move';
    e.dataTransfer.setData('application/x-aerie-rule-index', String(from));
  }

  protected onDragover_(e: DragEvent) {
    if (this.dragFrom_ < 0) {
      return;  // Not our drag (a file or text from elsewhere): not a target.
    }
    e.preventDefault();
    if (e.dataTransfer) {
      e.dataTransfer.dropEffect = 'move';
    }
    const at = this.indexOfTarget_(e);
    if (at >= 0 && at !== this.dropAt_) {
      this.dropAt_ = at;
    }
  }

  protected onDrop_(e: DragEvent) {
    if (this.dragFrom_ < 0) {
      return;
    }
    e.preventDefault();
    const from = this.dragFrom_;
    const to = this.indexOfTarget_(e);
    this.dragFrom_ = -1;
    this.dropAt_ = -1;
    this.moveTo_(from, to);
  }

  protected onDragend_() {
    this.dragFrom_ = -1;
    this.dropAt_ = -1;
  }

  protected onRemoveClick_(e: Event) {
    const index = this.indexOfTarget_(e);
    const rule = this.rules_[index];
    if (!rule) {
      return;
    }
    const copy = this.rules_.slice();
    copy.splice(index, 1);
    this.announcement_ = loadTimeData.getStringF(
        'aerieRuleRemoved', this.displayName_(rule));
    this.focusOptionAfterRender_ = copy.length ? Math.min(index, copy.length - 1) : null;
    this.saveRules_(copy);
  }

  protected onEditClick_(e: Event) {
    this.startEdit_(this.indexOfTarget_(e));
  }

  protected onListKeydown_(e: KeyboardEvent) {
    const target = e.target as HTMLElement;
    // Keys on the buttons inside an option are the buttons'.
    if (!target.classList.contains('aerie-item')) {
      return;
    }
    const index = Number(target.dataset['index']);
    const last = this.rules_.length - 1;
    switch (e.key) {
      case 'ArrowUp':
      case 'ArrowDown': {
        e.preventDefault();
        const delta = e.key === 'ArrowUp' ? -1 : 1;
        if (e.altKey) {
          this.moveSelected_(index, delta);
        } else {
          this.focusOption_(Math.min(last, Math.max(0, index + delta)));
        }
        break;
      }
      case 'Home':
        e.preventDefault();
        this.focusOption_(0);
        break;
      case 'End':
        e.preventDefault();
        this.focusOption_(last);
        break;
      case 'Enter':
        e.preventDefault();
        this.startEdit_(index);
        break;
      case ' ':
        e.preventDefault();
        this.toggleRule_(index);
        break;
    }
  }

  private focusOption_(index: number) {
    this.selected_ = index;
    this.focusOptionAfterRender_ = index;
  }

  protected onOptionFocus_(e: Event) {
    const index = Number((e.currentTarget as HTMLElement).dataset['index']);
    if (index !== this.selected_) {
      this.selected_ = index;
    }
  }

  // --- The editor. ---

  protected startEdit_(index: number) {
    const rule = this.rules_[index];
    if (!rule) {
      return;
    }
    this.draft_ = {
      id: rule.id,
      name: rule.name,
      type: rule.type,
      value: rule.value,
      mode: rule.mode,
      text: rule.text,
    };
    this.error_ = '';
    this.focusDraftAfterRender_ = true;
  }

  protected onAddRuleClick_() {
    this.draft_ = {
      id: null,
      name: '',
      type: 'domain',
      value: '',
      mode: 'template',
      text: '{title}',
    };
    this.error_ = '';
    this.focusDraftAfterRender_ = true;
  }

  protected onDraftInput_(e: Event) {
    const target = e.target as HTMLInputElement|HTMLSelectElement;
    if (!this.draft_) {
      return;
    }
    const field = target.dataset['field'] as keyof Draft;
    this.draft_ = {...this.draft_, [field]: target.value} as Draft;
  }

  protected onDraftKeydown_(e: KeyboardEvent) {
    if (e.key === 'Escape') {
      e.preventDefault();
      e.stopPropagation();
      this.onCancelEdit_();
    } else if (
        e.key === 'Enter' && (e.target as HTMLElement).tagName === 'INPUT') {
      e.preventDefault();
      this.onSaveEdit_();
    }
  }

  protected onCancelEdit_() {
    const wasNew = this.draft_ ? this.draft_.id === null : false;
    const id = this.draft_?.id;
    this.draft_ = null;
    this.error_ = '';
    // Focus goes back to the rule that was edited, or to "Add rule".
    const index = this.rules_.findIndex(r => r.id === id);
    if (!wasNew && index >= 0) {
      this.focusOptionAfterRender_ = index;
    } else {
      this.updateComplete.then(() => {
        this.shadowRoot.querySelector<HTMLElement>('#aerieAddRule')?.focus();
      });
    }
  }

  protected onSaveEdit_() {
    const draft = this.draft_;
    if (!draft) {
      return;
    }
    const isNew = draft.id === null;
    const rule: AerieRenameRule = {
      id: draft.id ?? newRuleId(Date.now(), Math.random()),
      name: draft.name.trim(),
      type: draft.type,
      value: draft.value.trim(),
      mode: draft.mode,
      text: draft.text,
      enabled: true,
    };
    const error = validateRule(rule, this.rules_.length, isNew);
    if (error !== '') {
      this.error_ = error;
      return;
    }
    const copy = this.rules_.slice();
    let focusIndex = copy.length;
    if (isNew) {
      copy.push(rule);
    } else {
      focusIndex = copy.findIndex(r => r.id === rule.id);
      if (focusIndex < 0) {
        copy.push(rule);
        focusIndex = copy.length - 1;
      } else {
        copy[focusIndex] = {...rule, enabled: copy[focusIndex].enabled};
      }
    }
    this.draft_ = null;
    this.error_ = '';
    this.selected_ = focusIndex;
    this.focusOptionAfterRender_ = focusIndex;
    this.saveRules_(copy);
    // The test box shows the result of the new list.
    this.scheduleTry_();
  }

  protected errorText_(): string {
    switch (this.error_) {
      case 'too-many':
        return loadTimeData.getString('aerieRuleErrorTooMany');
      case 'name-long':
        return loadTimeData.getString('aerieRuleErrorNameLong');
      case 'value-empty':
        return loadTimeData.getString('aerieRuleErrorValueEmpty');
      case 'value-long':
        return loadTimeData.getString('aerieRuleErrorValueLong');
      case 'text-long':
        return loadTimeData.getString('aerieRuleErrorTextLong');
      case 'template-empty':
        return loadTimeData.getString('aerieRuleErrorTemplateEmpty');
      case 'remove-empty':
        return loadTimeData.getString('aerieRuleErrorRemoveEmpty');
      default:
        return '';
    }
  }

  // --- Try a title. ---

  protected onTryInput_(e: Event) {
    const target = e.target as HTMLInputElement;
    if (target.dataset['field'] === 'url') {
      this.tryUrl_ = target.value;
    } else {
      this.tryTitle_ = target.value;
    }
    this.scheduleTry_();
  }

  private scheduleTry_() {
    if (this.tryTimer_ !== null) {
      window.clearTimeout(this.tryTimer_);
    }
    this.tryTimer_ = window.setTimeout(() => {
      this.tryTimer_ = null;
      this.runTry_();
    }, TRY_DELAY_MS);
  }

  private async runTry_() {
    const token = ++this.tryToken_;
    if (this.tryTitle_.trim() === '' && this.tryUrl_.trim() === '') {
      this.tryResult_ = null;
      return;
    }
    try {
      const result = await AerieTitleRulesBrowserProxyImpl.getInstance()
                         .tryTitle(this.tryUrl_, this.tryTitle_);
      // A later call has started: its answer is the one to show.
      if (token === this.tryToken_) {
        this.tryResult_ = result;
      }
    } catch {
      if (token === this.tryToken_) {
        this.tryResult_ = null;
      }
    }
  }

  protected tryResultText_(): string {
    const result = this.tryResult_;
    if (!result) {
      return '';
    }
    switch (result.source) {
      case 'rule':
        return loadTimeData.getStringF(
            'aerieTryResultRule', result.text, result.rule);
      case 'tidy':
        return loadTimeData.getStringF('aerieTryResultTidy', result.text);
      default:
        return loadTimeData.getStringF('aerieTryResultPage', result.text);
    }
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-aerie-title-rules-row': SettingsAerieTitleRulesRowElement;
  }
}

customElements.define(
    SettingsAerieTitleRulesRowElement.is, SettingsAerieTitleRulesRowElement);
