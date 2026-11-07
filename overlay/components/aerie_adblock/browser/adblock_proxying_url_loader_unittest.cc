// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Redirect judging. TestURLLoaderFactory sends all redirects at once and does
// not wait for FollowRedirect(), so these tests use a small fake network
// service that does what the real one does: it announces a redirect and then
// waits for the client's FollowRedirect().

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/self_deleting.h"
#include "base/run_loop.h"
#include "base/task/thread_pool.h"
#include "base/test/task_environment.h"
#include "components/aerie_adblock/browser/adblock_engine.h"
#include "components/aerie_adblock/browser/adblock_proxying_url_loader_factory.h"
#include "components/aerie_adblock/browser/filter_engine_service.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/system/data_pipe_utils.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/net_errors.h"
#include "services/network/public/mojom/early_hints.mojom.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/http_request_headers_update_params.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "services/network/test/test_url_loader_client.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace aerie_adblock {
namespace {

// What the fake network saw.
struct NetworkLog {
  std::vector<GURL> started;                    // Every request it started.
  int follow_redirect_calls = 0;
  std::optional<GURL> last_follow_redirect_url;
  std::vector<std::pair<net::RequestPriority, int>> priorities;
  int loaders_disconnected = 0;   // The proxy closed its pipe to a network loader.
  int upload_acks = 0;
  int loaders_dropped_by_network = 0;
};

class FakeLoader : public network::mojom::URLLoader {
 public:
  FakeLoader(NetworkLog* log,
             const std::map<GURL, GURL>* redirects,
             mojo::PendingReceiver<network::mojom::URLLoader> receiver,
             mojo::PendingRemote<network::mojom::URLLoaderClient> client,
             GURL start)
      : log_(log),
        redirects_(redirects),
        receiver_(this, std::move(receiver)),
        client_(std::move(client)),
        current_(std::move(start)) {
    // The proxy closing its end of the loader pipe is how a blocked hop
    // cancels the network request.
    receiver_.set_disconnect_handler(base::BindOnce(
        [](NetworkLog* log) { ++log->loaders_disconnected; }, log_.get()));
  }

  enum class Mode { kNormal, kEverything, kRedirectThenFail };
  void set_mode(Mode mode) { mode_ = mode; }

  // Announces a redirect if the current URL has one, else completes.
  void Begin() {
    if (mode_ == Mode::kEverything) {
      SendEverything();
      return;
    }
    auto it = redirects_->find(current_);
    if (it != redirects_->end()) {
      announced_ = it->second;
      net::RedirectInfo info;
      info.new_url = it->second;
      info.new_method = "GET";
      info.status_code = 302;
      client_->OnReceiveRedirect(info, network::mojom::URLResponseHead::New());
      if (mode_ == Mode::kRedirectThenFail) {
        // The network service gives up before the client answers.
        client_->OnComplete(network::URLLoaderCompletionStatus(net::ERR_FAILED));
      }
    } else {
      client_->OnComplete(network::URLLoaderCompletionStatus(net::OK));
    }
  }

  // Every URLLoaderClient call, in the order the network service makes them.
  void SendEverything() {
    client_->OnReceiveEarlyHints(network::mojom::EarlyHints::New());
    client_->OnUploadProgress(10, 100,
                              base::BindOnce([](NetworkLog* log) { ++log->upload_acks; }, log_.get()));
    mojo::ScopedDataPipeProducerHandle producer;
    mojo::ScopedDataPipeConsumerHandle consumer;
    ASSERT_EQ(MOJO_RESULT_OK, mojo::CreateDataPipe(nullptr, producer, consumer));
    ASSERT_TRUE(mojo::BlockingCopyFromString("hello body", producer));
    client_->OnReceiveResponse(network::mojom::URLResponseHead::New(), std::move(consumer),
                               std::nullopt);
    client_->OnTransferSizeUpdated(42);
    client_->OnComplete(network::URLLoaderCompletionStatus(net::OK));
  }

  void FollowRedirect(
      network::HttpRequestHeadersUpdateParams headers_update_params,
      const std::optional<GURL>& new_url) override {
    ++log_->follow_redirect_calls;
    log_->last_follow_redirect_url = new_url;
    // The next request starts: it may redirect again, or end.
    current_ = new_url.has_value() ? *new_url : announced_;
    Begin();
  }

  void SetPriority(net::RequestPriority priority,
                   int intra_priority_value) override {
    log_->priorities.emplace_back(priority, intra_priority_value);
  }

 private:
  raw_ptr<NetworkLog> log_;
  raw_ptr<const std::map<GURL, GURL>> redirects_;
  mojo::Receiver<network::mojom::URLLoader> receiver_;
  mojo::Remote<network::mojom::URLLoaderClient> client_;
  GURL current_;
  GURL announced_;
  Mode mode_ = Mode::kNormal;
};

class FakeNetwork : public network::mojom::URLLoaderFactory {
 public:
  explicit FakeNetwork(NetworkLog* log) : log_(log) {}

  void Redirect(const char* from, const char* to) {
    redirects_[GURL(from)] = GURL(to);
  }
  void set_mode(FakeLoader::Mode mode) { mode_ = mode; }

  // The network service goes away: every loader pipe closes.
  void DropLoaders() {
    log_->loaders_dropped_by_network += loaders_.size();
    loaders_.clear();
  }

  void CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> receiver,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& annotation) override {
    log_->started.push_back(request.url);
    auto loader = std::make_unique<FakeLoader>(
        log_, &redirects_, std::move(receiver), std::move(client), request.url);
    loader->set_mode(mode_);
    FakeLoader* raw = loader.get();
    loaders_.push_back(std::move(loader));
    raw->Begin();
  }

  void Clone(
      mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver) override {
    receivers_.Add(this, std::move(receiver));
  }

 private:
  raw_ptr<NetworkLog> log_;
  std::map<GURL, GURL> redirects_;
  std::vector<std::unique_ptr<FakeLoader>> loaders_;
  FakeLoader::Mode mode_ = FakeLoader::Mode::kNormal;
  mojo::ReceiverSet<network::mojom::URLLoaderFactory> receivers_;
};

std::string SerializedEngine(const char* list) {
  AdblockEngine engine;
  engine.LoadListForTesting(list);
  std::vector<uint8_t> bytes = engine.SerializeForTesting();
  return std::string(bytes.begin(), bytes.end());
}

class AdblockProxyingURLLoaderTest : public testing::Test {
 protected:
  AdblockProxyingURLLoaderTest()
      : service_(std::make_unique<FilterEngineService>(
            base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()}))),
        network_(&log_) {
    // Listed: any request to tracker.example; thirdparty.example and news.test
    // only as a third party; tpb.example only as a third party.
    static const std::string kEngine = SerializedEngine(
        "||tracker.example^\n"
        "||thirdparty.example^$third-party\n"
        "||news.test^$third-party\n"
        "||tpb.example^$third-party\n");
    service_->Start(kEngine);
  }

  // The renderer side of one request: a factory for a page at news.test.
  void StartRequest(const char* url,
                    network::mojom::RequestDestination destination =
                        network::mojom::RequestDestination::kScript,
                    const char* initiator = "https://news.test",
                    const net::IsolationInfo& isolation_info = net::IsolationInfo()) {
    mojo::PendingRemote<network::mojom::URLLoaderFactory> target;
    network_.Clone(target.InitWithNewPipeAndPassReceiver());
    mojo::PendingRemote<network::mojom::URLLoaderFactory> front;
    base::MakeSelfDeleting<AdblockProxyingURLLoaderFactory>(
        front.InitWithNewPipeAndPassReceiver(), std::move(target),
        url::Origin::Create(GURL(initiator)), isolation_info,
        service_->GetWeakPtr());
    factory_.Bind(std::move(front));

    network::ResourceRequest request;
    request.url = GURL(url);
    request.destination = destination;
    request.request_initiator = url::Origin::Create(GURL(initiator));
    factory_->CreateLoaderAndStart(
        loader_.BindNewPipeAndPassReceiver(), 1, 0, request,
        client_.CreateRemote(),
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));
  }

  void FollowRedirect() {
    loader_->FollowRedirect(network::HttpRequestHeadersUpdateParams(),
                            std::nullopt);
  }

  base::test::TaskEnvironment task_environment_;
  NetworkLog log_;
  std::unique_ptr<FilterEngineService> service_;
  FakeNetwork network_;
  mojo::Remote<network::mojom::URLLoaderFactory> factory_;
  mojo::Remote<network::mojom::URLLoader> loader_;
  network::TestURLLoaderClient client_;
};

TEST_F(AdblockProxyingURLLoaderTest, FirstPartyUrlRedirectingToAListedTrackerIsBlocked) {
  network_.Redirect("https://news.test/go", "https://tracker.example/pixel");
  StartRequest("https://news.test/go");
  client_.RunUntilRedirectReceived();
  EXPECT_EQ(GURL("https://tracker.example/pixel"),
            client_.redirect_info().new_url);

  FollowRedirect();
  client_.RunUntilComplete();
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, client_.completion_status().error_code);
  // The hop never went out: the network service was not told to follow it.
  EXPECT_EQ(0, log_.follow_redirect_calls);
  ASSERT_EQ(1u, log_.started.size());
}

TEST_F(AdblockProxyingURLLoaderTest, RedirectToAnUnlistedUrlProceeds) {
  network_.Redirect("https://news.test/go", "https://news.test/next");
  StartRequest("https://news.test/go");
  client_.RunUntilRedirectReceived();
  FollowRedirect();
  client_.RunUntilComplete();
  EXPECT_EQ(net::OK, client_.completion_status().error_code);
  EXPECT_EQ(1, log_.follow_redirect_calls);
}

TEST_F(AdblockProxyingURLLoaderTest, RedirectHopsAreJudgedWithTheSameTopLevelContext) {
  // thirdparty.example is blocked as a third party only. Reached from a page
  // at news.test it is third party: blocked.
  network_.Redirect("https://news.test/go", "https://thirdparty.example/x");
  StartRequest("https://news.test/go");
  client_.RunUntilRedirectReceived();
  FollowRedirect();
  client_.RunUntilComplete();
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, client_.completion_status().error_code);
}

TEST_F(AdblockProxyingURLLoaderTest, SameSiteRedirectIsFirstParty) {
  // The list has a rule for news.test as a third party only. The hop stays on
  // the page's own site, so the rule does not apply.
  network_.Redirect("https://cdn.news.test/a", "https://www.news.test/b");
  StartRequest("https://cdn.news.test/a");
  client_.RunUntilRedirectReceived();
  FollowRedirect();
  client_.RunUntilComplete();
  EXPECT_EQ(net::OK, client_.completion_status().error_code);
}

TEST_F(AdblockProxyingURLLoaderTest, FollowRedirectIsForwardedWithTheClientsUrl) {
  network_.Redirect("https://news.test/go", "https://news.test/next");
  StartRequest("https://news.test/go");
  client_.RunUntilRedirectReceived();
  loader_->FollowRedirect(network::HttpRequestHeadersUpdateParams(),
                          GURL("https://news.test/rewritten"));
  client_.RunUntilComplete();
  EXPECT_EQ(GURL("https://news.test/rewritten"), log_.last_follow_redirect_url);
}

TEST_F(AdblockProxyingURLLoaderTest, ARewrittenUrlIsJudgedNotTheAnnouncedOne) {
  network_.Redirect("https://news.test/go", "https://news.test/next");
  StartRequest("https://news.test/go");
  client_.RunUntilRedirectReceived();
  loader_->FollowRedirect(network::HttpRequestHeadersUpdateParams(),
                          GURL("https://tracker.example/pixel"));
  client_.RunUntilComplete();
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, client_.completion_status().error_code);
  EXPECT_EQ(0, log_.follow_redirect_calls);
}

TEST_F(AdblockProxyingURLLoaderTest, SetPriorityIsForwarded) {
  network_.Redirect("https://news.test/go", "https://news.test/next");
  StartRequest("https://news.test/go");
  client_.RunUntilRedirectReceived();
  loader_->SetPriority(net::HIGHEST, 7);
  FollowRedirect();
  client_.RunUntilComplete();
  ASSERT_EQ(1u, log_.priorities.size());
  EXPECT_EQ(net::HIGHEST, log_.priorities[0].first);
  EXPECT_EQ(7, log_.priorities[0].second);
}

TEST_F(AdblockProxyingURLLoaderTest, ClientMethodsAreForwarded) {
  // A request without redirects: completion reaches the client unchanged.
  StartRequest("https://news.test/plain.js");
  client_.RunUntilComplete();
  EXPECT_EQ(net::OK, client_.completion_status().error_code);
  EXPECT_FALSE(client_.has_received_redirect());
  EXPECT_EQ(1u, log_.started.size());
}

TEST_F(AdblockProxyingURLLoaderTest, BlockedHopCancelsTheNetworkRequest) {
  network_.Redirect("https://news.test/go", "https://tracker.example/pixel");
  StartRequest("https://news.test/go");
  client_.RunUntilRedirectReceived();
  FollowRedirect();
  client_.RunUntilComplete();
  // The proxy dropped its pipe to the network loader: the fake's receiver saw
  // the disconnect.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1, log_.loaders_disconnected);
}

// Every hop of a chain is judged, not only the first.
TEST_F(AdblockProxyingURLLoaderTest, ChainOfRedirectsIsJudgedAtEveryHop) {
  network_.Redirect("https://news.test/a", "https://news.test/b");
  network_.Redirect("https://news.test/b", "https://tracker.example/c");
  StartRequest("https://news.test/a");
  client_.RunUntilRedirectReceived();
  client_.ClearHasReceivedRedirect();
  FollowRedirect();  // to news.test/b: allowed, so it goes to the network.
  client_.RunUntilRedirectReceived();  // the second redirect arrives
  EXPECT_EQ(1, log_.follow_redirect_calls);
  EXPECT_EQ(GURL("https://tracker.example/c"), client_.redirect_info().new_url);
  FollowRedirect();  // to the tracker: blocked here.
  client_.RunUntilComplete();
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, client_.completion_status().error_code);
  EXPECT_EQ(1, log_.follow_redirect_calls);
}

// The browser destroys the service at shutdown. A hop then is aborted, never
// let through unchecked.
TEST_F(AdblockProxyingURLLoaderTest, HopAfterTheServiceIsGoneIsAborted) {
  network_.Redirect("https://news.test/go", "https://news.test/next");
  StartRequest("https://news.test/go");
  client_.RunUntilRedirectReceived();
  service_.reset();
  FollowRedirect();
  client_.RunUntilComplete();
  EXPECT_EQ(net::ERR_ABORTED, client_.completion_status().error_code);
  EXPECT_EQ(0, log_.follow_redirect_calls);
}

// Requests that are never judged skip the wrapper, so their redirects are not
// judged either. A main-frame navigation (out of scope) that redirects to a
// listed host is followed; a judged subresource doing the same is blocked (see
// FirstPartyUrlRedirectingToAListedTrackerIsBlocked).
TEST_F(AdblockProxyingURLLoaderTest, UnjudgedMainFrameRedirectToAListedHostIsNotJudged) {
  network_.Redirect("https://news.test/", "https://tracker.example/next");
  StartRequest("https://news.test/", network::mojom::RequestDestination::kDocument);
  client_.RunUntilRedirectReceived();
  FollowRedirect();
  client_.RunUntilComplete();
  EXPECT_EQ(net::OK, client_.completion_status().error_code);
  EXPECT_EQ(1, log_.follow_redirect_calls);
}

// Every URLLoaderClient call reaches the renderer's client.
TEST_F(AdblockProxyingURLLoaderTest, EveryClientCallIsForwarded) {
  network_.set_mode(FakeLoader::Mode::kEverything);
  StartRequest("https://news.test/all.js");
  client_.RunUntilComplete();
  EXPECT_TRUE(client_.has_received_early_hints());
  EXPECT_EQ(1u, client_.early_hints().size());
  EXPECT_TRUE(client_.has_received_upload_progress());
  EXPECT_EQ(10, client_.current_upload_position());
  EXPECT_EQ(100, client_.total_upload_size());
  EXPECT_TRUE(client_.has_received_response());
  std::string body;
  ASSERT_TRUE(mojo::BlockingCopyToString(client_.response_body_release(), &body));
  EXPECT_EQ("hello body", body);
  EXPECT_EQ(42, client_.body_transfer_size());
  EXPECT_EQ(net::OK, client_.completion_status().error_code);
  // The upload progress ack travelled back to the network service.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1, log_.upload_acks);
}

// First party is the top-level site, for redirect hops too. The iframe from
// tpb.example (a third party to the page) is redirected, by an unlisted URL, to
// a tpb.example URL. Judged by the initiator alone that hop would be first party
// and pass; judged by the page (news.test) it is third party and is blocked.
TEST_F(AdblockProxyingURLLoaderTest, RedirectHopUsesTheTopLevelSiteNotTheInitiator) {
  network_.Redirect("https://other.example/a", "https://tpb.example/b");
  StartRequest("https://other.example/a", network::mojom::RequestDestination::kImage,
               "https://tpb.example",
               net::IsolationInfo::CreateForInternalRequest(
                   url::Origin::Create(GURL("https://news.test"))));
  client_.RunUntilRedirectReceived();
  FollowRedirect();
  client_.RunUntilComplete();
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, client_.completion_status().error_code);
  EXPECT_EQ(0, log_.follow_redirect_calls);
}

// With no top-level site known, the initiator is all there is: the same chain is
// first party then.
TEST_F(AdblockProxyingURLLoaderTest, WithoutATopFrameTheInitiatorIsTheContext) {
  network_.Redirect("https://other.example/a", "https://tpb.example/b");
  StartRequest("https://other.example/a", network::mojom::RequestDestination::kImage,
               "https://tpb.example");
  client_.RunUntilRedirectReceived();
  FollowRedirect();
  client_.RunUntilComplete();
  EXPECT_EQ(net::OK, client_.completion_status().error_code);
  EXPECT_EQ(1, log_.follow_redirect_calls);
}

// The renderer goes away while the verdict is pending: nothing is sent, nothing
// crashes (the callback holds a weak pointer).
TEST_F(AdblockProxyingURLLoaderTest, RendererDisconnectWhileAVerdictIsPending) {
  network_.Redirect("https://news.test/go", "https://news.test/next");
  StartRequest("https://news.test/go");
  client_.RunUntilRedirectReceived();
  FollowRedirect();
  loader_.reset();
  base::RunLoop().RunUntilIdle();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(0, log_.follow_redirect_calls);
}

// The network service goes away while the verdict is pending: the renderer's
// client sees a disconnect, and the late verdict follows nothing.
TEST_F(AdblockProxyingURLLoaderTest, NetworkDisconnectWhileAVerdictIsPending) {
  network_.Redirect("https://news.test/go", "https://news.test/next");
  StartRequest("https://news.test/go");
  client_.RunUntilRedirectReceived();
  FollowRedirect();
  network_.DropLoaders();
  client_.RunUntilDisconnect();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(0, log_.follow_redirect_calls);
  EXPECT_FALSE(client_.has_received_completion());
}

// The fixed path: after the network side closed, the proxy's remote is reset (unbound).
// Remote::is_connected() must not be called on it (it DCHECKs, and in official builds
// reports true), so a flag and is_bound() guard every use. Both a judged and an unjudged
// hop come back through FollowRedirect and then through the verdict.
TEST_F(AdblockProxyingURLLoaderTest, JudgedHopAfterANetworkDropTouchesNoUnboundRemote) {
  network_.Redirect("https://news.test/go", "https://news.test/next");
  StartRequest("https://news.test/go");
  client_.RunUntilRedirectReceived();
  network_.DropLoaders();
  client_.RunUntilDisconnect();
  FollowRedirect();   // judged: the verdict comes back after the drop
  FollowRedirect();   // a second answer is ignored too
  task_environment_.RunUntilIdle();
  EXPECT_EQ(0, log_.follow_redirect_calls);
}

// A second FollowRedirect after the proxy itself cancelled the request (blocked hop).
TEST_F(AdblockProxyingURLLoaderTest, SecondFollowRedirectAfterABlockedHopIsIgnored) {
  network_.Redirect("https://news.test/go", "https://tracker.example/pixel");
  StartRequest("https://news.test/go");
  client_.RunUntilRedirectReceived();
  FollowRedirect();
  client_.RunUntilComplete();
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, client_.completion_status().error_code);
  FollowRedirect();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, client_.completion_status().error_code);
  EXPECT_EQ(0, log_.follow_redirect_calls);
}

// Same, for a hop the engine does not judge (a non-http scheme): the late
// FollowRedirect must not touch the dead network pipe.
TEST_F(AdblockProxyingURLLoaderTest, UnjudgedHopAfterANetworkDropFollowsNothing) {
  network_.Redirect("https://news.test/go", "ftp://news.test/file");
  StartRequest("https://news.test/go");
  client_.RunUntilRedirectReceived();
  network_.DropLoaders();
  client_.RunUntilDisconnect();
  FollowRedirect();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(0, log_.follow_redirect_calls);
}

// The network completes the request (it gave up) before the client answers the
// redirect. A blocked verdict that comes later must not send a second OnComplete:
// the client keeps the network's status.
TEST_F(AdblockProxyingURLLoaderTest, VerdictAfterNetworkCompletionDoesNotCompleteTwice) {
  network_.set_mode(FakeLoader::Mode::kRedirectThenFail);
  network_.Redirect("https://news.test/go", "https://tracker.example/pixel");
  StartRequest("https://news.test/go");
  client_.RunUntilComplete();
  EXPECT_EQ(net::ERR_FAILED, client_.completion_status().error_code);
  FollowRedirect();  // the verdict would be "blocked"
  task_environment_.RunUntilIdle();
  EXPECT_EQ(net::ERR_FAILED, client_.completion_status().error_code);
  EXPECT_EQ(0, log_.follow_redirect_calls);
}

}  // namespace
}  // namespace aerie_adblock
