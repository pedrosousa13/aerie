// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/webui/aerie_guard_settings/aerie_list_source_ui.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/aerie_adblock/browser/filter_list_manager.h"
#include "components/aerie_adblock/browser/filter_list_state.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_ui.h"
#include "content/public/common/url_constants.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"

namespace aerie_guard {

namespace {

const char kMimeType[] = "text/plain";

// Serves the saved text of a list as text/plain. The list id is the whole
// request path; the manager accepts only the id of a list the user added by
// address, so no other file, address or id is ever read.
class ListSourceDataSource : public content::URLDataSource {
 public:
  ListSourceDataSource() = default;
  ListSourceDataSource(const ListSourceDataSource&) = delete;
  ListSourceDataSource& operator=(const ListSourceDataSource&) = delete;
  ~ListSourceDataSource() override = default;

  // content::URLDataSource:
  std::string GetSource() override { return kListSourceHost; }

  void StartDataRequest(const GURL& url,
                        const content::WebContents::Getter& wc_getter,
                        GotDataCallback callback) override {
    aerie_adblock::FilterListManager* manager =
        aerie_adblock::FilterListManager::Get();
    const std::string id = URLToRequestPath(url);
    if (!manager || !manager->CanViewListSource(id)) {
      std::move(callback).Run(nullptr);
      return;
    }
    manager->ReadListSource(
        id, base::BindOnce(
                [](GotDataCallback callback, std::optional<std::string> text) {
                  if (!text) {
                    std::move(callback).Run(nullptr);
                    return;
                  }
                  std::move(callback).Run(
                      base::MakeRefCounted<base::RefCountedString>(
                          std::move(*text)));
                },
                std::move(callback)));
  }

  // No charset: the mime type is also the response's mime_type, so
  // "text/plain; charset=utf-8" makes document.contentType carry the
  // parameter and the data source API has no field for a charset (a browser
  // test showed the page still decodes as windows-1252). A non-ASCII
  // character in a list therefore shows in that encoding. Chromium adds no
  // X-Content-Type-Options: nosniff to a chrome:// data source response
  // (URLDataManagerBackend::GetHeaders) and the API has no hook for it. The
  // type is explicit and the page has no resource to sniff.
  std::string GetMimeType(const GURL& url) override { return kMimeType; }
  // The type must reach the response as its Content-Type header.
  bool ShouldServeMimeTypeAsContentTypeHeader() override { return true; }
  // The text of a list changes when it is updated.
  bool AllowCaching() override { return false; }
  // Nothing in a text document may run or load, and no page may frame it.
  std::string GetContentSecurityPolicy(
      network::mojom::CSPDirectiveName directive) override {
    switch (directive) {
      case network::mojom::CSPDirectiveName::DefaultSrc:
        return "default-src 'none';";
      case network::mojom::CSPDirectiveName::FrameAncestors:
        return "frame-ancestors 'none';";
      default:
        return content::URLDataSource::GetContentSecurityPolicy(directive);
    }
  }
  bool ShouldDenyXFrameOptions() override { return true; }
};

}  // namespace

GURL ListSourceUrl(const std::string& id) {
  if (!aerie_adblock::IsValidListId(id)) {
    return GURL();
  }
  return GURL(std::string(content::kChromeUIScheme) + "://" +
              kListSourceHost + "/" + id);
}

AerieListSourceUIConfig::AerieListSourceUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, kListSourceHost) {}

bool AerieListSourceUIConfig::IsWebUIEnabled(
    content::BrowserContext* browser_context) {
  // The saved lists are the regular profile's: Settings opens the page there
  // and nowhere else. An incognito or guest window gets no page at all.
  Profile* profile = Profile::FromBrowserContext(browser_context);
  return profile && profile->IsRegularProfile();
}

AerieListSourceUI::AerieListSourceUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  content::URLDataSource::Add(Profile::FromWebUI(web_ui),
                              std::make_unique<ListSourceDataSource>());
}

AerieListSourceUI::~AerieListSourceUI() = default;

}  // namespace aerie_guard
