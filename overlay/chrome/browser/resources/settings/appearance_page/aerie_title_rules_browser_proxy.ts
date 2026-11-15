// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {sendWithPromise} from 'chrome://resources/js/cr.js';

/**
 * What "Try a title" answers (AerieTitleRulesHandler,
 * chrome/browser/ui/webui/aerie_title_rules): the title the sidebar would show,
 * where it came from, and the name of the rule when it came from a rule. `text`
 * and `rule` hold a page's text and a user's rule name: show them as text only.
 */
export interface AerieTitleTryResult {
  text: string;
  source: 'rule'|'tidy'|'page';
  rule: string;
}

export interface AerieTitleRulesBrowserProxy {
  tryTitle(url: string, title: string): Promise<AerieTitleTryResult>;
}

let instance: AerieTitleRulesBrowserProxy|null = null;

export class AerieTitleRulesBrowserProxyImpl implements
    AerieTitleRulesBrowserProxy {
  tryTitle(url: string, title: string): Promise<AerieTitleTryResult> {
    return sendWithPromise('aerieTitleRules.try', url, title);
  }

  static getInstance(): AerieTitleRulesBrowserProxy {
    return instance || (instance = new AerieTitleRulesBrowserProxyImpl());
  }

  static setInstance(proxy: AerieTitleRulesBrowserProxy) {
    instance = proxy;
  }
}
