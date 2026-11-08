// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/filter_list_fetcher.h"

#include <string>
#include <vector>

#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "net/base/load_flags.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "net/url_request/redirect_info.h"
#include "net/url_request/referrer_policy.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_adblock {
namespace {

class FilterListFetcherTest : public testing::Test {
 protected:
  FilterListFetcher::Result Fetch(FilterListFetcher& fetcher, const char* url) {
    FilterListFetcher::Result out;
    base::RunLoop loop;
    fetcher.Fetch(GURL(url), base::BindLambdaForTesting(
                                 [&](FilterListFetcher::Result result) {
                                   out = std::move(result);
                                   loop.Quit();
                                 }));
    loop.Run();
    return out;
  }

  std::unique_ptr<FilterListFetcher> MakeFetcher(bool allow_loopback = false) {
    return std::make_unique<FilterListFetcher>(
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(&factory_),
        allow_loopback);
  }

  base::test::TaskEnvironment task_environment_;
  network::TestURLLoaderFactory factory_;
};

TEST_F(FilterListFetcherTest, DownloadsAnHttpsList) {
  factory_.AddResponse("https://lists.example/a.txt", "||ads.test^\n");
  auto fetcher = MakeFetcher();
  FilterListFetcher::Result result = Fetch(*fetcher, "https://lists.example/a.txt");
  EXPECT_EQ(result.failure, ListFailure::kNone);
  EXPECT_EQ(result.body, "||ads.test^\n");
}

TEST_F(FilterListFetcherTest, NeverRequestsAnInsecureAddress) {
  auto fetcher = MakeFetcher();
  for (const char* url : {"http://lists.example/a.txt", "ftp://lists.example/a",
                          "https://u:p@lists.example/a.txt", "http://127.0.0.1/a.txt"}) {
    EXPECT_EQ(Fetch(*fetcher, url).failure, ListFailure::kInsecureUrl) << url;
  }
  EXPECT_EQ(factory_.NumPending(), 0);
}

TEST_F(FilterListFetcherTest, LoopbackHttpOnlyWithTheTestSwitch) {
  factory_.AddResponse("http://127.0.0.1:8000/a.txt", "||ads.test^\n");
  auto fetcher = MakeFetcher(/*allow_loopback=*/true);
  EXPECT_EQ(Fetch(*fetcher, "http://127.0.0.1:8000/a.txt").failure, ListFailure::kNone);
  EXPECT_EQ(Fetch(*fetcher, "http://lists.example/a.txt").failure,
            ListFailure::kInsecureUrl);
}

TEST_F(FilterListFetcherTest, ARequestCarriesNoCredentialsOrCache) {
  network::ResourceRequest seen;
  factory_.SetInterceptor(base::BindLambdaForTesting(
      [&](const network::ResourceRequest& request) { seen = request; }));
  factory_.AddResponse("https://lists.example/a.txt", "x");
  auto fetcher = MakeFetcher();
  Fetch(*fetcher, "https://lists.example/a.txt");
  EXPECT_EQ(seen.url, GURL("https://lists.example/a.txt"));
  EXPECT_EQ(seen.credentials_mode, network::mojom::CredentialsMode::kOmit);
  EXPECT_TRUE(seen.load_flags & net::LOAD_DISABLE_CACHE);
  EXPECT_EQ(seen.referrer_policy, net::ReferrerPolicy::NO_REFERRER);
  EXPECT_TRUE(seen.referrer.is_empty());
}

TEST_F(FilterListFetcherTest, AnErrorStatusIsAFailure) {
  factory_.AddResponse("https://lists.example/missing.txt", "Not found",
                       net::HTTP_NOT_FOUND);
  auto fetcher = MakeFetcher();
  EXPECT_EQ(Fetch(*fetcher, "https://lists.example/missing.txt").failure,
            ListFailure::kDownload);
}

TEST_F(FilterListFetcherTest, ANetworkErrorIsAFailure) {
  factory_.AddResponse(GURL("https://lists.example/a.txt"),
                       network::mojom::URLResponseHead::New(), "",
                       network::URLLoaderCompletionStatus(net::ERR_CONNECTION_RESET));
  auto fetcher = MakeFetcher();
  EXPECT_EQ(Fetch(*fetcher, "https://lists.example/a.txt").failure,
            ListFailure::kDownload);
}

net::RedirectInfo RedirectTo(const char* new_url) {
  net::RedirectInfo info;
  info.new_url = GURL(new_url);
  info.new_method = "GET";
  info.status_code = net::HTTP_FOUND;
  return info;
}

network::TestURLLoaderFactory::Redirects OneRedirect(const char* new_url) {
  network::TestURLLoaderFactory::Redirects redirects;
  auto head = network::mojom::URLResponseHead::New();
  head->headers = base::MakeRefCounted<net::HttpResponseHeaders>(
      "HTTP/1.1 302 Found\nLocation: " + std::string(new_url) + "\n\n");
  redirects.emplace_back(RedirectTo(new_url), std::move(head));
  return redirects;
}

TEST_F(FilterListFetcherTest, ARedirectToHttpIsStoppedBeforeAnyRequestToIt) {
  std::vector<GURL> requested;
  factory_.SetInterceptor(base::BindLambdaForTesting(
      [&](const network::ResourceRequest& request) {
        requested.push_back(request.url);
      }));
  factory_.AddResponse(GURL("https://lists.example/a.txt"),
                       network::mojom::URLResponseHead::New(), "",
                       network::URLLoaderCompletionStatus(net::OK),
                       OneRedirect("http://lists.example/b.txt"),
                       network::TestURLLoaderFactory::
                           kResponseOnlyRedirectsNoDestination);
  factory_.AddResponse("http://lists.example/b.txt", "||ads.test^\n");
  auto fetcher = MakeFetcher();
  FilterListFetcher::Result result = Fetch(*fetcher, "https://lists.example/a.txt");
  EXPECT_EQ(result.failure, ListFailure::kInsecureUrl);
  EXPECT_TRUE(result.body.empty());
  // Only the original https request was ever made, and nothing is left running.
  EXPECT_EQ(requested, std::vector<GURL>{GURL("https://lists.example/a.txt")});
  EXPECT_EQ(factory_.NumPending(), 0);
}

TEST_F(FilterListFetcherTest, ARedirectToHttpsIsFollowed) {
  factory_.AddResponse(GURL("https://lists.example/a.txt"),
                       network::mojom::URLResponseHead::New(), "",
                       network::URLLoaderCompletionStatus(net::OK),
                       OneRedirect("https://mirror.example/b.txt"),
                       network::TestURLLoaderFactory::
                           kResponseOnlyRedirectsNoDestination);
  factory_.AddResponse("https://mirror.example/b.txt", "||ads.test^\n");
  auto fetcher = MakeFetcher();
  FilterListFetcher::Result result = Fetch(*fetcher, "https://lists.example/a.txt");
  EXPECT_EQ(result.failure, ListFailure::kNone);
  EXPECT_EQ(result.body, "||ads.test^\n");
}

}  // namespace
}  // namespace aerie_adblock
