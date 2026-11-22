// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/filter_list_fetcher.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "services/network/test/test_utils.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "net/url_request/redirect_info.h"
#include "net/url_request/referrer_policy.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_adblock {
namespace {

// A URLLoaderFactory that follows redirects the way the network service does:
// a redirect is announced to the client, and only when the client answers
// FollowRedirect() does a request go to the new address. network::
// TestURLLoaderFactory cannot do this. It drops the loader's receiver when it
// has a canned response, so a redirect that is announced can never be followed
// and the client sees the pipe close (the fetch ended with kDownload).
// |requested| lists every URL a request was actually made to, in order.
class RedirectFollowingFactory : public network::mojom::URLLoaderFactory {
 public:
  void AddBody(const char* url, std::string body) { bodies_[GURL(url)] = std::move(body); }
  void AddRedirect(const char* from, const char* to) { redirects_[GURL(from)] = GURL(to); }

  const std::vector<GURL>& requested() const { return requested_; }

  // network::mojom::URLLoaderFactory:
  void CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> receiver,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation) override {
    auto loader = std::make_unique<Loader>(this, request.url, std::move(client));
    Loader* raw = loader.get();
    mojo::MakeSelfOwnedReceiver(std::move(loader), std::move(receiver));
    raw->Start();
  }

  void Clone(mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver) override {
    receivers_.Add(this, std::move(receiver));
  }

 private:
  class Loader : public network::mojom::URLLoader {
   public:
    Loader(RedirectFollowingFactory* factory,
           const GURL& url,
           mojo::PendingRemote<network::mojom::URLLoaderClient> client)
        : factory_(factory), url_(url), client_(std::move(client)) {}

    void Start() {
      factory_->requested_.push_back(url_);
      auto redirect = factory_->redirects_.find(url_);
      if (redirect != factory_->redirects_.end()) {
        net::RedirectInfo info;
        info.new_url = redirect->second;
        info.new_method = "GET";
        info.status_code = net::HTTP_FOUND;
        auto head = network::CreateURLResponseHead(net::HTTP_FOUND);
        client_->OnReceiveRedirect(info, std::move(head));
        return;
      }
      auto body = factory_->bodies_.find(url_);
      if (body == factory_->bodies_.end()) {
        client_->OnComplete(
            network::URLLoaderCompletionStatus(net::ERR_FILE_NOT_FOUND));
        return;
      }
      mojo::ScopedDataPipeProducerHandle producer;
      mojo::ScopedDataPipeConsumerHandle consumer;
      ASSERT_EQ(MOJO_RESULT_OK,
                mojo::CreateDataPipe(body->second.size(), producer, consumer));
      ASSERT_EQ(MOJO_RESULT_OK,
                producer->WriteAllData(base::as_byte_span(body->second)));
      auto head = network::CreateURLResponseHead(net::HTTP_OK);
      head->mime_type = "text/plain";
      client_->OnReceiveResponse(std::move(head), std::move(consumer), std::nullopt);
      network::URLLoaderCompletionStatus status(net::OK);
      status.decoded_body_length = base::ByteSize(body->second.size());
      client_->OnComplete(status);
    }

    // network::mojom::URLLoader:
    void FollowRedirect(
        network::HttpRequestHeadersUpdateParams headers_update_params,
        const std::optional<GURL>& new_url) override {
      url_ = factory_->redirects_.at(url_);
      Start();
    }
    void SetPriority(net::RequestPriority priority, int32_t intra_priority_value) override {}

   private:
    raw_ptr<RedirectFollowingFactory> factory_;
    GURL url_;
    mojo::Remote<network::mojom::URLLoaderClient> client_;
  };

  std::map<GURL, std::string> bodies_;
  std::map<GURL, GURL> redirects_;
  std::vector<GURL> requested_;
  mojo::ReceiverSet<network::mojom::URLLoaderFactory> receivers_;
};

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

  // A fetcher over |following_|, which follows redirects (see its comment).
  std::unique_ptr<FilterListFetcher> MakeFollowingFetcher(bool allow_loopback = false) {
    return std::make_unique<FilterListFetcher>(
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(&following_),
        allow_loopback);
  }

  base::test::TaskEnvironment task_environment_;
  network::TestURLLoaderFactory factory_;
  RedirectFollowingFactory following_;
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

// A redirect from a public list to each of |targets| ends the fetch with
// |expected| before any request goes to the target.
void ExpectRedirectsRefused(network::TestURLLoaderFactory& factory,
                            FilterListFetcher& fetcher,
                            const std::vector<const char*>& targets,
                            ListFailure expected,
                            const std::function<FilterListFetcher::Result(const char*)>& fetch) {
  std::vector<GURL> requested;
  factory.SetInterceptor(base::BindLambdaForTesting(
      [&](const network::ResourceRequest& request) {
        requested.push_back(request.url);
      }));
  for (const char* target : targets) {
    requested.clear();
    factory.ClearResponses();
    factory.AddResponse(GURL("https://lists.example/a.txt"),
                        network::mojom::URLResponseHead::New(), "",
                        network::URLLoaderCompletionStatus(net::OK),
                        OneRedirect(target),
                        network::TestURLLoaderFactory::
                            kResponseOnlyRedirectsNoDestination);
    factory.AddResponse(target, "||ads.test^\n");
    FilterListFetcher::Result result = fetch("https://lists.example/a.txt");
    EXPECT_EQ(result.failure, expected) << target;
    EXPECT_TRUE(result.body.empty()) << target;
    EXPECT_EQ(requested, std::vector<GURL>{GURL("https://lists.example/a.txt")})
        << target;
    EXPECT_EQ(factory.NumPending(), 0) << target;
  }
}

TEST_F(FilterListFetcherTest, ARedirectToANonPublicHostIsStoppedBeforeAnyRequestToIt) {
  auto fetcher = MakeFetcher();
  ExpectRedirectsRefused(
      factory_, *fetcher,
      {"https://127.0.0.1:8443/probe", "https://192.168.0.1/", "https://10.0.0.1/",
       "https://169.254.169.254/", "https://100.64.0.1/", "https://0.0.0.0/",
       "https://224.0.0.1/", "https://255.255.255.255/", "https://[::1]/",
       "https://[::]/", "https://[ff02::1]/", "https://[fe80::1]/",
       "https://[fc00::1]/", "https://[::ffff:10.0.0.1]/",
       "https://[::ffff:192.168.0.1]/", "https://[2002:c0a8:101::1]/",
       "https://localhost/", "https://printer.localhost/", "https://intranet/",
       "https://2130706433/", "https://0x7f000001/", "https://0177.0.0.1/"},
      ListFailure::kNonPublicUrl,
      [&](const char* url) { return Fetch(*fetcher, url); });
}

TEST_F(FilterListFetcherTest, ANonPublicStartAddressIsNeverRequested) {
  auto fetcher = MakeFetcher();
  for (const char* url :
       {"https://127.0.0.1/a.txt", "https://192.168.0.1/a.txt", "https://[::1]/a.txt",
        "https://localhost/a.txt", "https://intranet/a.txt"}) {
    EXPECT_EQ(Fetch(*fetcher, url).failure, ListFailure::kNonPublicUrl) << url;
  }
  EXPECT_EQ(factory_.NumPending(), 0);
  EXPECT_EQ(factory_.total_requests(), 0u);
}

TEST_F(FilterListFetcherTest, TheLoopbackSwitchStillStopsRedirectsToPrivateRanges) {
  auto fetcher = MakeFetcher(/*allow_loopback=*/true);
  ExpectRedirectsRefused(
      factory_, *fetcher,
      {"https://192.168.0.1/b.txt", "https://10.0.0.1/b.txt",
       "https://169.254.169.254/b.txt", "https://intranet/b.txt"},
      ListFailure::kNonPublicUrl,
      [&](const char* url) { return Fetch(*fetcher, url); });
}

TEST_F(FilterListFetcherTest, TheLoopbackSwitchDoesNotStopARedirectToLoopback) {
  following_.AddRedirect("https://lists.example/a.txt", "https://127.0.0.1:8443/b.txt");
  following_.AddBody("https://127.0.0.1:8443/b.txt", "||ads.test^\n");
  auto fetcher = MakeFollowingFetcher(/*allow_loopback=*/true);
  FilterListFetcher::Result result = Fetch(*fetcher, "https://lists.example/a.txt");
  EXPECT_EQ(result.failure, ListFailure::kNone);
  EXPECT_EQ(result.body, "||ads.test^\n");
  EXPECT_EQ(following_.requested(),
            (std::vector<GURL>{GURL("https://lists.example/a.txt"),
                               GURL("https://127.0.0.1:8443/b.txt")}));
}

TEST_F(FilterListFetcherTest, ARedirectToHttpsIsFollowed) {
  following_.AddRedirect("https://lists.example/a.txt", "https://mirror.example/b.txt");
  following_.AddBody("https://mirror.example/b.txt", "||ads.test^\n");
  auto fetcher = MakeFollowingFetcher();
  FilterListFetcher::Result result = Fetch(*fetcher, "https://lists.example/a.txt");
  EXPECT_EQ(result.failure, ListFailure::kNone);
  EXPECT_EQ(result.body, "||ads.test^\n");
  EXPECT_EQ(following_.requested(),
            (std::vector<GURL>{GURL("https://lists.example/a.txt"),
                               GURL("https://mirror.example/b.txt")}));
}

TEST_F(FilterListFetcherTest, ARedirectChainOfPublicAddressesIsFollowedToTheEnd) {
  following_.AddRedirect("https://lists.example/a.txt", "https://mirror.example/b.txt");
  following_.AddRedirect("https://mirror.example/b.txt", "https://cdn.example/c.txt");
  following_.AddBody("https://cdn.example/c.txt", "||ads.test^\n");
  auto fetcher = MakeFollowingFetcher();
  EXPECT_EQ(Fetch(*fetcher, "https://lists.example/a.txt").body, "||ads.test^\n");
  EXPECT_EQ(following_.requested().size(), 3u);
}

TEST_F(FilterListFetcherTest, ARedirectToANonPublicHostIsNotFollowedByTheRealRedirectPath) {
  following_.AddRedirect("https://lists.example/a.txt", "https://192.168.0.1/b.txt");
  following_.AddBody("https://192.168.0.1/b.txt", "||ads.test^\n");
  auto fetcher = MakeFollowingFetcher();
  FilterListFetcher::Result result = Fetch(*fetcher, "https://lists.example/a.txt");
  EXPECT_EQ(result.failure, ListFailure::kNonPublicUrl);
  EXPECT_TRUE(result.body.empty());
  task_environment_.RunUntilIdle();
  EXPECT_EQ(following_.requested(),
            std::vector<GURL>{GURL("https://lists.example/a.txt")});
}

}  // namespace
}  // namespace aerie_adblock
