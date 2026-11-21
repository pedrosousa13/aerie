// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {sendWithPromise} from 'chrome://resources/js/cr.js';

/**
 * What the Arc import answers (AerieArcImportHandler,
 * chrome/browser/ui/webui/aerie_arc_import). Every string in an answer is the
 * file's own text: show it as text only, never as markup.
 */
export interface AerieArcSkipped {
  reason: string;
  count: number;
}

export interface AerieArcSpace {
  index: number;
  // Empty when Arc left the Space without a name.
  name: string;
  // An emoji, or empty.
  icon: string;
  pinnedTabs: number;
  folders: number;
  favorites: number;
  customNames: number;
}

export type AerieArcChooseResult = {
  status: 'cancelled',
}|{
  status: 'error',
  error: string,
}|{
  status: 'ok',
  spaces: AerieArcSpace[],
  skipped: AerieArcSkipped[],
};

export type AerieArcImportResult = {
  status: 'error',
  error: string,
}|{
  status: 'ok',
  pinnedTabs: number,
  folders: number,
  favorites: number,
  customNames: number,
  skipped: AerieArcSkipped[],
};

export interface AerieArcImportBrowserProxy {
  // Opens the file chooser on a .json file and reads the file.
  choose(): Promise<AerieArcChooseResult>;
  importSpace(index: number): Promise<AerieArcImportResult>;
  // The dialog closed: forget the file.
  reset(): void;
}

let instance: AerieArcImportBrowserProxy|null = null;

export class AerieArcImportBrowserProxyImpl implements
    AerieArcImportBrowserProxy {
  choose(): Promise<AerieArcChooseResult> {
    return sendWithPromise('aerieArcImport.choose');
  }

  importSpace(index: number): Promise<AerieArcImportResult> {
    return sendWithPromise('aerieArcImport.import', index);
  }

  reset() {
    chrome.send('aerieArcImport.reset');
  }

  static getInstance(): AerieArcImportBrowserProxy {
    return instance || (instance = new AerieArcImportBrowserProxyImpl());
  }

  static setInstance(proxy: AerieArcImportBrowserProxy) {
    instance = proxy;
  }
}
