// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_AERIE_ADBLOCK_HOOK_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_AERIE_ADBLOCK_HOOK_H_

#include <memory>

#include "base/files/file_path.h"
#include "base/memory/scoped_refptr.h"
#include "components/aerie_adblock/browser/filter_list_manager.h"
#include "components/aerie_adblock/browser/filter_engine_service.h"
#include "content/public/browser/content_browser_client.h"
#include "net/base/isolation_info.h"
#include "services/network/public/cpp/url_loader_factory_builder.h"
#include "url/origin.h"

namespace network {
class SharedURLLoaderFactory;
}

namespace aerie_adblock {

// The one call Aerie adds to ChromeContentBrowserClient::
// WillCreateURLLoaderFactory(). It puts the Filter Engine's proxying factory
// in |factory_builder| for the factory types that carry web content. See
// patches/adblock/0001-hook-url-loader-factory.patch.
//
// Order: call it after the extensions' webRequest proxy has been added. The
// first proxy added sees requests first, so extensions (the user's own, for
// example uBlock Origin) see every request, and the engine's verdict is the
// last word before the network.
//
// Redirects: a judged request that is allowed is wrapped in an
// AdblockProxyingURLLoader, which judges every redirect target too, before the
// hop goes out.
//
// |isolation_info| is the one WillCreateURLLoaderFactory() was given; the proxy
// uses its top-frame origin as the first party. |frame| is the frame the
// factory is for, if any; the per-page blocked count uses it. |browser_context|
// is the profile: an off-the-record one gets exceptions of its own that are
// never saved (SessionKeyFor()). Does nothing when no Filter
// Engine service is registered (unit tests; before the browser creates it).
void MaybeProxyURLLoaderFactory(
    content::ContentBrowserClient::URLLoaderFactoryType type,
    const url::Origin& request_initiator,
    const net::IsolationInfo& isolation_info,
    network::URLLoaderFactoryBuilder& factory_builder,
    content::RenderFrameHost* frame = nullptr,
    content::BrowserContext* browser_context = nullptr);

// Creates the browser's Filter Engine service, registers it as the process
// instance and starts loading the shipped engine. The caller owns it: see
// chrome/browser/aerie_adblock. UI thread, after the resource bundle and the
// thread pool exist.
std::unique_ptr<FilterEngineService> CreateAndStartFilterEngineService();

// Creates the filter list manager (issue #10) over |service|, registers it
// as the process instance and loads its saved state. |dir| holds its files.
// The caller owns it and must destroy it before |service|. UI thread, before
// any page loads.
std::unique_ptr<FilterListManager> CreateAndInitFilterListManager(
    FilterEngineService& service,
    const base::FilePath& dir,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_AERIE_ADBLOCK_HOOK_H_
