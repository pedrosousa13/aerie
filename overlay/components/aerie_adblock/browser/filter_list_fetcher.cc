// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/filter_list_fetcher.h"

#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "components/aerie_adblock/browser/filter_list_state.h"
#include "net/base/load_flags.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace aerie_adblock {

struct FilterListFetcher::Download {
  std::unique_ptr<network::SimpleURLLoader> loader;
  Callback callback;
};

namespace {

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("aerie_filter_list_download", R"(
      semantics {
        sender: "Aerie ad blocking"
        description:
          "Downloads a filter list (a text file of ad and tracker rules) "
          "from the address of its publisher: one of the built-in lists, or "
          "an address the user added in the filter list settings."
        trigger:
          "The user adds or updates a list, or the daily scheduled update "
          "(on by default, can be turned off in the filter list settings; "
          "the first check is a day after install)."
        data: "Only the list's address. No cookies, no identifiers."
        destination: OTHER
        destination_other: "The list publisher's server"
      }
      policy {
        cookies_allowed: NO
        setting:
          "Turn off 'Update lists automatically' in the filter list "
          "settings. Manual updates only happen on a click."
        policy_exception_justification: "Not implemented."
      })");

// The same download for an address the user added. Its own annotation tells
// the request policy (#257) that the user chose the host.
constexpr net::NetworkTrafficAnnotationTag kCustomTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("aerie_custom_filter_list_download", R"(
      semantics {
        sender: "Aerie ad blocking"
        description:
          "Downloads a filter list (a text file of ad and tracker rules) "
          "from an address the user added in the filter list settings."
        trigger:
          "The user adds or updates the list, or the daily scheduled update "
          "(on by default, can be turned off in the filter list settings)."
        data: "Only the list's address. No cookies, no identifiers."
        destination: OTHER
        destination_other: "The server at the address the user added"
      }
      policy {
        cookies_allowed: NO
        setting:
          "Remove the list, or turn off 'Update lists automatically' in the "
          "filter list settings."
        policy_exception_justification: "Not implemented."
      })");

// The same download for a list of the catalogue (issue #105): the address is
// one the build ships, the user only turned the list on.
constexpr net::NetworkTrafficAnnotationTag kCatalogueTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("aerie_catalogue_filter_list_download",
                                        R"(
      semantics {
        sender: "Aerie ad blocking"
        description:
          "Downloads a filter list (a text file of ad and tracker rules) "
          "from the address of its publisher. The address is one of a fixed "
          "set the browser ships; the user chose to turn that list on in the "
          "filter list settings."
        trigger:
          "The user turns the list on or updates it, or the daily scheduled "
          "update of the lists that are on (can be turned off in the filter "
          "list settings)."
        data: "Only the list's address. No cookies, no identifiers."
        destination: OTHER
        destination_other: "The list publisher's server"
      }
      policy {
        cookies_allowed: NO
        setting:
          "Turn the list off, or turn off 'Update lists automatically' in "
          "the filter list settings. Nothing is downloaded until the user "
          "turns a list on."
        policy_exception_justification: "Not implemented."
      })");

net::NetworkTrafficAnnotationTag TrafficAnnotationFor(
    FilterListFetcher::Source source) {
  switch (source) {
    case FilterListFetcher::Source::kUserAdded:
      return kCustomTrafficAnnotation;
    case FilterListFetcher::Source::kCatalogue:
      return kCatalogueTrafficAnnotation;
    case FilterListFetcher::Source::kBuiltIn:
      break;
  }
  return kTrafficAnnotation;
}

}  // namespace

FilterListFetcher::FilterListFetcher(
    scoped_refptr<network::SharedURLLoaderFactory> factory,
    bool allow_loopback)
    : factory_(std::move(factory)), allow_loopback_(allow_loopback) {}

FilterListFetcher::~FilterListFetcher() = default;

void FilterListFetcher::Fetch(const GURL& url,
                              Callback callback,
                              Source source) {
  int id = next_id_++;
  auto download = std::make_unique<Download>();
  download->callback = std::move(callback);

  const ListFailure url_failure = CheckListUrl(url, allow_loopback_);
  if (!factory_ || url_failure != ListFailure::kNone) {
    downloads_[id] = std::move(download);
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&FilterListFetcher::Finish, weak_factory_.GetWeakPtr(),
                       id,
                       factory_ ? url_failure : ListFailure::kDownload,
                       std::nullopt));
    return;
  }

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->method = "GET";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->referrer_policy = net::ReferrerPolicy::NO_REFERRER;
  request->load_flags = net::LOAD_DISABLE_CACHE;
  request->headers.SetHeader("Accept", "text/plain, */*;q=0.1");

  download->loader = network::SimpleURLLoader::Create(
      std::move(request), TrafficAnnotationFor(source));
  download->loader->SetTimeoutDuration(kListDownloadTimeout);
  download->loader->SetOnRedirectCallback(base::BindRepeating(
      [](base::WeakPtr<FilterListFetcher> self, int id,
         const GURL& url_before_redirect, const net::RedirectInfo& info,
         const network::mojom::URLResponseHead&,
         std::vector<std::string>*) {
        if (self) {
          self->OnRedirect(id, info.new_url);
        }
      },
      weak_factory_.GetWeakPtr(), id));
  network::SimpleURLLoader* loader = download->loader.get();
  downloads_[id] = std::move(download);
  loader->DownloadToString(
      factory_.get(),
      base::BindOnce(
          [](base::WeakPtr<FilterListFetcher> self, int id,
             network::SimpleURLLoader* loader,
             std::optional<std::string> body) {
            if (!self) {
              return;
            }
            bool ok = body && loader->NetError() == net::OK &&
                      loader->ResponseInfo() && loader->ResponseInfo()->headers &&
                      loader->ResponseInfo()->headers->response_code() == 200;
            self->Finish(id, ok ? ListFailure::kNone : ListFailure::kDownload,
                         std::move(body));
          },
          weak_factory_.GetWeakPtr(), id, loader),
      kMaxListBytes);
}

void FilterListFetcher::OnRedirect(int id, const GURL& new_url) {
  const ListFailure failure = CheckListUrl(new_url, allow_loopback_);
  if (failure == ListFailure::kNone) {
    return;
  }
  // SimpleURLLoader allows deleting the loader inside its redirect callback
  // and does not follow the redirect if it was deleted. Finish() deletes the
  // loader now, so no request goes to the unacceptable address.
  Finish(id, failure, std::nullopt);
}

void FilterListFetcher::Finish(int id,
                               ListFailure failure,
                               std::optional<std::string> body) {
  auto it = downloads_.find(id);
  if (it == downloads_.end()) {
    return;  // Already finished (a redirect stop raced the completion).
  }
  std::unique_ptr<Download> download = std::move(it->second);
  downloads_.erase(it);
  download->loader.reset();  // Cancels the request if it is still running.
  Result result;
  result.failure = failure;
  if (failure == ListFailure::kNone && body) {
    result.body = std::move(*body);
  }
  std::move(download->callback).Run(std::move(result));
}

}  // namespace aerie_adblock
