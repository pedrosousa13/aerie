// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/adblock_proxying_url_loader_factory.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/self_deleting.h"
#include "base/task/thread_pool.h"
#include "base/test/task_environment.h"
#include "components/aerie_adblock/browser/adblock_engine.h"
#include "components/aerie_adblock/browser/filter_engine_service.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/net_errors.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "services/network/test/test_url_loader_client.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace aerie_adblock {
namespace {

using Type = content::ContentBrowserClient::URLLoaderFactoryType;

const char kList[] =
    "||ads.example^$script\n"
    "||tracker.example^\n";

std::string SerializedEngine() {
  AdblockEngine engine;
  engine.LoadListForTesting(kList);
  std::vector<uint8_t> bytes = engine.SerializeForTesting();
  return std::string(bytes.begin(), bytes.end());
}

class AdblockProxyingURLLoaderFactoryTest : public testing::Test {
 protected:
  AdblockProxyingURLLoaderFactoryTest()
      : service_(base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()})) {
    static const std::string kEngine = SerializedEngine();
    service_.Start(kEngine);

    mojo::PendingRemote<network::mojom::URLLoaderFactory> target;
    target_.Clone(target.InitWithNewPipeAndPassReceiver());
    mojo::PendingRemote<network::mojom::URLLoaderFactory> front;
    base::MakeSelfDeleting<AdblockProxyingURLLoaderFactory>(
        front.InitWithNewPipeAndPassReceiver(), std::move(target),
        url::Origin::Create(GURL("https://news.test")), net::IsolationInfo(),
        service_.GetWeakPtr());
    factory_.Bind(std::move(front));
  }

  // Starts a request through the proxy and waits for it to complete.
  int Fetch(const char* url,
            network::mojom::RequestDestination destination =
                network::mojom::RequestDestination::kScript) {
    network::ResourceRequest request;
    request.url = GURL(url);
    request.destination = destination;
    request.request_initiator = url::Origin::Create(GURL("https://news.test"));
    return FetchRequest(request);
  }

  int FetchRequest(const network::ResourceRequest& request) {
    network::TestURLLoaderClient client;
    mojo::Remote<network::mojom::URLLoader> loader;
    factory_->CreateLoaderAndStart(
        loader.BindNewPipeAndPassReceiver(), 0, 0, request,
        client.CreateRemote(),
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));
    client.RunUntilComplete();
    return client.completion_status().error_code;
  }

  base::test::TaskEnvironment task_environment_;
  FilterEngineService service_;
  network::TestURLLoaderFactory target_;
  mojo::Remote<network::mojom::URLLoaderFactory> factory_;
};

TEST_F(AdblockProxyingURLLoaderFactoryTest, BlockedRequestFailsAndNeverReachesTheNetwork) {
  target_.AddResponse("https://ads.example/a.js", "x");
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, Fetch("https://ads.example/a.js"));
  EXPECT_EQ(0u, target_.total_requests());
}

TEST_F(AdblockProxyingURLLoaderFactoryTest, AllowedRequestPassesThrough) {
  target_.AddResponse("https://news.test/app.js", "ok");
  EXPECT_EQ(net::OK, Fetch("https://news.test/app.js"));
  EXPECT_EQ(1u, target_.total_requests());
}

TEST_F(AdblockProxyingURLLoaderFactoryTest, TypeOptionDecidesPerRequest) {
  target_.AddResponse("https://ads.example/a.png", "img");
  // The rule is $script only.
  EXPECT_EQ(net::OK, Fetch("https://ads.example/a.png",
                           network::mojom::RequestDestination::kImage));
  // The rule for tracker.example has no type option.
  target_.AddResponse("https://tracker.example/p.gif", "gif");
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT,
            Fetch("https://tracker.example/p.gif",
                  network::mojom::RequestDestination::kImage));
}

TEST_F(AdblockProxyingURLLoaderFactoryTest, UnjudgedRequestsPassThrough) {
  // Main frame documents are not judged, even for a listed host.
  target_.AddResponse("https://tracker.example/", "page");
  EXPECT_EQ(net::OK, Fetch("https://tracker.example/",
                           network::mojom::RequestDestination::kDocument));
  // Neither are non-http schemes.
  target_.AddResponse("data:text/plain,hi", "hi");
  EXPECT_EQ(net::OK, Fetch("data:text/plain,hi"));
}

TEST_F(AdblockProxyingURLLoaderFactoryTest, SubframeNavigationsAreJudged) {
  target_.AddResponse("https://tracker.example/frame", "frame");
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT,
            Fetch("https://tracker.example/frame",
                  network::mojom::RequestDestination::kIframe));
}

// First party is the top-level site. This list blocks ads.example only as a
// third party; the page news.test embeds an ad iframe from ads.example, and the
// iframe's own pixel is third party to the page.
TEST_F(AdblockProxyingURLLoaderFactoryTest, AdIframeOwnRequestsAreThirdPartyToThePage) {
  AdblockEngine builder;
  builder.LoadListForTesting("||thirdparty.example^$third-party\n");
  std::vector<uint8_t> bytes = builder.SerializeForTesting();
  static const std::string kEngine(bytes.begin(), bytes.end());
  FilterEngineService service(
      base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()}));
  service.Start(kEngine);

  network::TestURLLoaderFactory target;
  target.AddResponse("https://thirdparty.example/pixel.gif", "gif");
  mojo::PendingRemote<network::mojom::URLLoaderFactory> target_remote;
  target.Clone(target_remote.InitWithNewPipeAndPassReceiver());
  mojo::PendingRemote<network::mojom::URLLoaderFactory> front;
  // An untrusted factory for the iframe: the initiator is the iframe's origin
  // and the isolation info names the top-level page.
  base::MakeSelfDeleting<AdblockProxyingURLLoaderFactory>(
      front.InitWithNewPipeAndPassReceiver(), std::move(target_remote),
      url::Origin::Create(GURL("https://thirdparty.example")),
      net::IsolationInfo::CreateForInternalRequest(
          url::Origin::Create(GURL("https://news.test"))),
      service.GetWeakPtr());
  mojo::Remote<network::mojom::URLLoaderFactory> factory(std::move(front));

  network::ResourceRequest request;
  request.url = GURL("https://thirdparty.example/pixel.gif");
  request.destination = network::mojom::RequestDestination::kImage;
  request.request_initiator =
      url::Origin::Create(GURL("https://thirdparty.example"));
  network::TestURLLoaderClient client;
  mojo::Remote<network::mojom::URLLoader> loader;
  factory->CreateLoaderAndStart(
      loader.BindNewPipeAndPassReceiver(), 0, 0, request, client.CreateRemote(),
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));
  client.RunUntilComplete();
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, client.completion_status().error_code);
}

TEST_F(AdblockProxyingURLLoaderFactoryTest, SubframeNavigationWithoutInitiatorUsesItsTopFrame) {
  // A browser-initiated navigation (reload of an ad subframe): no initiator, the
  // page only in the request's trusted IsolationInfo.
  target_.AddResponse("https://tracker.example/frame", "frame");
  network::ResourceRequest request;
  request.url = GURL("https://tracker.example/frame");
  request.destination = network::mojom::RequestDestination::kIframe;
  request.trusted_params.emplace();
  request.trusted_params->isolation_info = net::IsolationInfo::CreateForInternalRequest(
      url::Origin::Create(GURL("https://news.test")));
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, FetchRequest(request));
}

// The browser destroys the service at shutdown while factories live on. A
// request then is aborted, never let through unchecked.
TEST(AdblockProxyingURLLoaderFactoryShutdownTest, RequestsAfterTheServiceIsGoneAreAborted) {
  base::test::TaskEnvironment task_environment;
  network::TestURLLoaderFactory target;
  target.AddResponse("https://news.test/app.js", "ok");
  mojo::PendingRemote<network::mojom::URLLoaderFactory> target_remote;
  target.Clone(target_remote.InitWithNewPipeAndPassReceiver());
  mojo::PendingRemote<network::mojom::URLLoaderFactory> front;
  base::WeakPtr<FilterEngineService> gone;
  {
    FilterEngineService service(
        base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()}));
    gone = service.GetWeakPtr();
  }
  base::MakeSelfDeleting<AdblockProxyingURLLoaderFactory>(
      front.InitWithNewPipeAndPassReceiver(), std::move(target_remote),
      url::Origin::Create(GURL("https://news.test")), net::IsolationInfo(), gone);
  mojo::Remote<network::mojom::URLLoaderFactory> factory(std::move(front));
  network::ResourceRequest request;
  request.url = GURL("https://news.test/app.js");
  request.destination = network::mojom::RequestDestination::kScript;
  network::TestURLLoaderClient client;
  mojo::Remote<network::mojom::URLLoader> loader;
  factory->CreateLoaderAndStart(
      loader.BindNewPipeAndPassReceiver(), 0, 0, request, client.CreateRemote(),
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));
  client.RunUntilComplete();
  EXPECT_EQ(net::ERR_ABORTED, client.completion_status().error_code);
  EXPECT_EQ(0u, target.total_requests());
}

TEST(AdblockProxyingURLLoaderFactoryCoverageTest, CoveredFactoryTypes) {
  for (Type type :
       {Type::kNavigation, Type::kDocumentSubResource, Type::kWorkerMainResource,
        Type::kWorkerSubResource, Type::kServiceWorkerScript,
        Type::kServiceWorkerSubResource, Type::kPrefetch, Type::kEarlyHints}) {
    EXPECT_TRUE(AdblockProxyingURLLoaderFactory::IsCoveredType(type));
  }
  for (Type type : {Type::kDownload, Type::kDevTools}) {
    EXPECT_FALSE(AdblockProxyingURLLoaderFactory::IsCoveredType(type));
  }
}

}  // namespace
}  // namespace aerie_adblock
