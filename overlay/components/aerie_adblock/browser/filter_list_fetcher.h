// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_FILTER_LIST_FETCHER_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_FILTER_LIST_FETCHER_H_

#include <map>
#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "components/aerie_adblock/browser/list_failure.h"
#include "url/gurl.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace aerie_adblock {

// Downloads filter lists, straight from the address the user (or the built-in
// list table) gave, and from nowhere else:
//   - https only (loopback http only with the Bench's switch), and a redirect
//     that leaves https stops the download;
//   - no cookies, no credentials, no referrer, no cache;
//   - at most kMaxListDownloadBytes of body and kListDownloadTimeout of time;
//   - a status other than 200 is a failure.
// It does not look at the content; the engine's check_list does that.
//
// UI thread. Destroying the fetcher cancels every download in flight.
class FilterListFetcher {
 public:
  // The body size cap: the most SimpleURLLoader::DownloadToString accepts.
  static constexpr size_t kMaxListDownloadBytes = 5 * 1024 * 1024;
  static constexpr base::TimeDelta kListDownloadTimeout = base::Seconds(60);

  struct Result {
    ListFailure failure = ListFailure::kNone;
    std::string body;
  };
  using Callback = base::OnceCallback<void(Result)>;

  FilterListFetcher(scoped_refptr<network::SharedURLLoaderFactory> factory,
                    bool allow_loopback);
  ~FilterListFetcher();

  FilterListFetcher(const FilterListFetcher&) = delete;
  FilterListFetcher& operator=(const FilterListFetcher&) = delete;

  // Starts a download. |callback| runs once, later, never from inside Fetch().
  void Fetch(const GURL& url, Callback callback);

 private:
  struct Download;

  void Finish(int id, ListFailure failure, std::optional<std::string> body);
  void OnRedirect(int id, const GURL& new_url);

  scoped_refptr<network::SharedURLLoaderFactory> factory_;
  const bool allow_loopback_;
  int next_id_ = 0;
  std::map<int, std::unique_ptr<Download>> downloads_;
  base::WeakPtrFactory<FilterListFetcher> weak_factory_{this};
};

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_FILTER_LIST_FETCHER_H_
