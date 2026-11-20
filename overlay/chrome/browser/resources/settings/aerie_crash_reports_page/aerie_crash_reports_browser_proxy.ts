// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {sendWithPromise} from 'chrome://resources/js/cr.js';

/**
 * One local crash report, as AerieCrashReportsHandler reports it
 * (chrome/browser/ui/webui/aerie_crash_reports). There is no field for the
 * contents of the report and none for a path: a report may hold page memory,
 * and the page names it by `id` only.
 */
export interface AerieCrashReport {
  id: string;
  /** Milliseconds since the epoch. */
  time: number;
  /**
   * 'browser', 'renderer', 'gpu-process', ... or '' when the report does
   * not say. A plain token; the page owns the words.
   */
  processType: string;
  /** The version of the crashing build, or ''. */
  version: string;
}

/** What every message of AerieCrashReportsHandler answers. */
export interface AerieCrashReportsState {
  /** False when a delete or the open failed; the list is current anyway. */
  ok: boolean;
  /** The most reports Aerie keeps. */
  limit: number;
  /** Newest first. */
  reports: AerieCrashReport[];
}

/** Every call rejects its promise when the browser refuses an argument. */
export interface AerieCrashReportsBrowserProxy {
  list(): Promise<AerieCrashReportsState>;
  deleteReport(id: string): Promise<AerieCrashReportsState>;
  deleteAll(): Promise<AerieCrashReportsState>;
  openFolder(): Promise<AerieCrashReportsState>;
}

let instance: AerieCrashReportsBrowserProxy|null = null;

export class AerieCrashReportsBrowserProxyImpl implements
    AerieCrashReportsBrowserProxy {
  list(): Promise<AerieCrashReportsState> {
    return sendWithPromise('aerieCrashReports.list');
  }

  deleteReport(id: string): Promise<AerieCrashReportsState> {
    return sendWithPromise('aerieCrashReports.delete', id);
  }

  deleteAll(): Promise<AerieCrashReportsState> {
    return sendWithPromise('aerieCrashReports.deleteAll');
  }

  openFolder(): Promise<AerieCrashReportsState> {
    return sendWithPromise('aerieCrashReports.openFolder');
  }

  static getInstance(): AerieCrashReportsBrowserProxy {
    return instance || (instance = new AerieCrashReportsBrowserProxyImpl());
  }

  static setInstance(obj: AerieCrashReportsBrowserProxy) {
    instance = obj;
  }
}
