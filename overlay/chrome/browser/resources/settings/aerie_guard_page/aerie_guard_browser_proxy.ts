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
  /** Milliseconds since the epoch, or null (the shipped copy is in use). */
  updated: number|null;
  error: string;
}

/**
 * What every message of AerieGuardSettingsHandler answers
 * (chrome/browser/ui/webui/aerie_guard_settings).
 */
export interface AerieGuardState {
  ready: boolean;
  busy: boolean;
  autoUpdate: boolean;
  /** The manager's text for a list it refused. Empty for none. */
  addError: string;
  /** '', 'invalid_host', 'table_full' or 'locked'. */
  exceptionError: string;
  /** Hosts where blocking is off, saved ones only, sorted. */
  exceptions: string[];
  lists: AerieGuardList[];
}

/**
 * Every call rejects its promise when the browser refuses an argument.
 * Lists have no rows on the page yet; the calls are here because the handler
 * is the one door to FilterListManager, and the Bench drives it.
 */
export interface AerieGuardBrowserProxy {
  getState(): Promise<AerieGuardState>;
  addList(url: string): Promise<AerieGuardState>;
  removeList(id: string): Promise<AerieGuardState>;
  updateList(id: string): Promise<AerieGuardState>;
  updateAll(): Promise<AerieGuardState>;
  setException(host: string, enabled: boolean): Promise<AerieGuardState>;
  setAutoUpdate(enabled: boolean): Promise<AerieGuardState>;
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

  static getInstance(): AerieGuardBrowserProxy {
    return instance || (instance = new AerieGuardBrowserProxyImpl());
  }

  static setInstance(obj: AerieGuardBrowserProxy) {
    instance = obj;
  }
}
