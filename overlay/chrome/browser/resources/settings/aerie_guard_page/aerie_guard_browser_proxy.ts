// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {sendWithPromise} from 'chrome://resources/js/cr.js';

/** One filter list, as AerieGuardSettingsHandler reports it. */
export interface AerieGuardList {
  id: string;
  url: string;
  title: string;
  builtin: boolean;
  rules: number;
  /** Exception rules that match every address; the engine ignores them. */
  ignoredExceptions: number;
  /** False when the user turned the list off. */
  enabled: boolean;
  /** True for a list that cannot be turned off (EasyList). */
  required: boolean;
  status: 'idle'|'updating'|'updated'|'failed';
  /** A fixed code for the last error ('' for none); the page owns the words. */
  errorCode: string;
  /** Milliseconds since the epoch, or null (the shipped copy is in use). */
  updated: number|null;
  error: string;
}

/** One list of the catalogue (issue #105), named by its catalogue id. */
export interface AerieGuardCatalogueList {
  /** The catalogue id; the page finds its name and description by it. */
  id: string;
  /** An https address; the page links it. */
  homepage: string;
  licence: string;
  /** Language codes of a regional list; empty for the others. */
  langs: string[];
  /** True when the user turned the list on and it is not switched off. */
  on: boolean;
  rules: number;
  status: 'idle'|'updating'|'updated'|'failed';
  /** A fixed code for the last error ('' for none); the page owns the words. */
  errorCode: string;
  /** Milliseconds since the epoch, or null when nothing was downloaded. */
  updated: number|null;
}

/** The catalogue lists of one category, in the order of the page. */
export interface AerieGuardCatalogueGroup {
  /** 'annoyances', 'cookies', 'social', 'privacy' or 'regional'. */
  category: string;
  lists: AerieGuardCatalogueList[];
}

/** One line of the user's own rules that is dropped or ignored. */
export interface AerieGuardCustomIssue {
  /** 1-based. */
  line: number;
  /** A fixed code, e.g. 'scriptlet'; the page owns the words. */
  reason: string;
  /** True for an exception rule left out because it matches every address. */
  ignored: boolean;
  /** The line, cut short. Untrusted: show it as text only. */
  rule: string;
}

/** The saved text of the user's own rules and what was found in it. */
export interface AerieGuardCustomRules {
  /** Untrusted: show it as text only. */
  text: string;
  networkRules: number;
  hideRules: number;
  ignoredExceptions: number;
  dropped: number;
  /** All issues; `issues` holds the first ones only. */
  issueCount: number;
  issues: AerieGuardCustomIssue[];
}

/**
 * What every message of AerieGuardSettingsHandler answers
 * (chrome/browser/ui/webui/aerie_guard_settings).
 */
export interface AerieGuardState {
  ready: boolean;
  busy: boolean;
  autoUpdate: boolean;
  /** 'aggressive' or 'standard': the level of sites with none of their own. */
  defaultLevel: string;
  /** The manager's text for a list it refused. Empty for none. */
  addError: string;
  /** '', 'invalid_host', 'table_full' or 'locked'. */
  exceptionError: string;
  /** '', 'required', 'unknown_list' or 'too_many'. */
  listError: string;
  /** Hosts where blocking is off, saved ones only, sorted. */
  exceptions: string[];
  lists: AerieGuardList[];
  /** The filter list catalogue, grouped by category (issue #105). */
  catalogue: AerieGuardCatalogueGroup[];
  /** Why the last save of the user's own rules was refused; '' for none. */
  customError: string;
  customRules: AerieGuardCustomRules;
}

/**
 * Every call rejects its promise when the browser refuses an argument.
 * The handler is the one door to FilterListManager; the Bench drives it too.
 */
export interface AerieGuardBrowserProxy {
  getState(): Promise<AerieGuardState>;
  addList(url: string): Promise<AerieGuardState>;
  removeList(id: string): Promise<AerieGuardState>;
  setListEnabled(id: string, enabled: boolean): Promise<AerieGuardState>;
  /**
   * Turns a catalogue list on (the browser downloads it from its publisher) or
   * off. The argument is a catalogue id, never an address: the promise rejects
   * for any other text.
   */
  setCatalogueList(id: string, on: boolean): Promise<AerieGuardState>;
  updateList(id: string): Promise<AerieGuardState>;
  updateAll(): Promise<AerieGuardState>;
  setException(host: string, enabled: boolean): Promise<AerieGuardState>;
  setAutoUpdate(enabled: boolean): Promise<AerieGuardState>;
  setDefaultLevel(level: string): Promise<AerieGuardState>;
  /** Saves the user's own rules; a blank text removes them. */
  setCustomRules(text: string): Promise<AerieGuardState>;
}

let instance: AerieGuardBrowserProxy|null = null;

export class AerieGuardBrowserProxyImpl implements AerieGuardBrowserProxy {
  getState(): Promise<AerieGuardState> {
    return sendWithPromise('aerieGuard.getState');
  }

  addList(url: string): Promise<AerieGuardState> {
    return sendWithPromise('aerieGuard.addList', url);
  }

  removeList(id: string): Promise<AerieGuardState> {
    return sendWithPromise('aerieGuard.removeList', id);
  }

  setListEnabled(id: string, enabled: boolean): Promise<AerieGuardState> {
    return sendWithPromise('aerieGuard.setListEnabled', id, enabled);
  }

  setCatalogueList(id: string, on: boolean): Promise<AerieGuardState> {
    return sendWithPromise('aerieGuard.setCatalogueList', id, on);
  }

  updateList(id: string): Promise<AerieGuardState> {
    return sendWithPromise('aerieGuard.updateList', id);
  }

  updateAll(): Promise<AerieGuardState> {
    return sendWithPromise('aerieGuard.updateAll');
  }

  setException(host: string, enabled: boolean): Promise<AerieGuardState> {
    return sendWithPromise('aerieGuard.setException', host, enabled);
  }

  setAutoUpdate(enabled: boolean): Promise<AerieGuardState> {
    return sendWithPromise('aerieGuard.setAutoUpdate', enabled);
  }

  setDefaultLevel(level: string): Promise<AerieGuardState> {
    return sendWithPromise('aerieGuard.setDefaultLevel', level);
  }

  setCustomRules(text: string): Promise<AerieGuardState> {
    return sendWithPromise('aerieGuard.setCustomRules', text);
  }

  static getInstance(): AerieGuardBrowserProxy {
    return instance || (instance = new AerieGuardBrowserProxyImpl());
  }

  static setInstance(obj: AerieGuardBrowserProxy) {
    instance = obj;
  }
}
