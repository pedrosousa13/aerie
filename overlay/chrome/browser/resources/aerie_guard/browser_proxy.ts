// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

import {PageCallbackRouter, PageHandlerFactory, PageHandlerRemote} from './aerie_guard.mojom-webui.js';

// The page's one connection to the browser. Mojo only: every call is a method
// of PageHandler, and the page never sends a host, a URL or a tab id.
export class BrowserProxy {
  callbackRouter: PageCallbackRouter = new PageCallbackRouter();
  handler: PageHandlerRemote = new PageHandlerRemote();

  constructor() {
    PageHandlerFactory.getRemote().createPageHandler(
        this.callbackRouter.$.bindNewPipeAndPassRemote(),
        this.handler.$.bindNewPipeAndPassReceiver());
  }

  private static instance_: BrowserProxy|null = null;

  static getInstance(): BrowserProxy {
    return BrowserProxy.instance_ || (BrowserProxy.instance_ = new BrowserProxy());
  }

  static setInstance(instance: BrowserProxy) {
    BrowserProxy.instance_ = instance;
  }
}
