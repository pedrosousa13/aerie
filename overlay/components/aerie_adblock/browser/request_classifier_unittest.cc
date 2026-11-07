// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/request_classifier.h"

#include <optional>

#include "net/base/isolation_info.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "third_party/blink/public/mojom/loader/resource_load_info.mojom-shared.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace aerie_adblock {
namespace {

using network::mojom::RequestDestination;

network::ResourceRequest MakeRequest(
    const char* url,
    RequestDestination destination,
    const char* initiator = "https://news.test") {
  network::ResourceRequest request;
  request.url = GURL(url);
  request.destination = destination;
  if (initiator) {
    request.request_initiator = url::Origin::Create(GURL(initiator));
  }
  return request;
}

const url::Origin& NoFactoryInitiator() {
  static const url::Origin origin;  // opaque
  return origin;
}

// What WillCreateURLLoaderFactory passes for a factory with no isolation info.
const net::IsolationInfo& NoIsolationInfo() {
  static const net::IsolationInfo info;
  return info;
}

net::IsolationInfo TopFrame(const char* origin) {
  return net::IsolationInfo::CreateForInternalRequest(
      url::Origin::Create(GURL(origin)));
}

std::optional<ClassifiedRequest> Classify(
    const network::ResourceRequest& request,
    const url::Origin& factory_initiator = url::Origin(),
    const net::IsolationInfo& isolation_info = net::IsolationInfo()) {
  return ClassifyRequest(request, factory_initiator, isolation_info);
}

struct KindCase {
  RequestDestination destination;
  RequestKind kind;
};

TEST(RequestClassifierTest, MapsEveryDestinationToAKind) {
  const KindCase cases[] = {
      {RequestDestination::kIframe, RequestKind::kSubdocument},
      {RequestDestination::kFrame, RequestKind::kSubdocument},
      {RequestDestination::kFencedframe, RequestKind::kSubdocument},
      {RequestDestination::kScript, RequestKind::kScript},
      {RequestDestination::kServiceWorker, RequestKind::kScript},
      {RequestDestination::kSharedWorker, RequestKind::kScript},
      {RequestDestination::kWorker, RequestKind::kScript},
      {RequestDestination::kStyle, RequestKind::kStylesheet},
      {RequestDestination::kXslt, RequestKind::kStylesheet},
      {RequestDestination::kImage, RequestKind::kImage},
      {RequestDestination::kFont, RequestKind::kFont},
      {RequestDestination::kAudio, RequestKind::kMedia},
      {RequestDestination::kVideo, RequestKind::kMedia},
      {RequestDestination::kTrack, RequestKind::kMedia},
      {RequestDestination::kEmpty, RequestKind::kXmlHttpRequest},
      {RequestDestination::kReport, RequestKind::kPing},
      {RequestDestination::kEmbed, RequestKind::kObject},
      {RequestDestination::kObject, RequestKind::kObject},
      {RequestDestination::kManifest, RequestKind::kOther},
  };
  for (const KindCase& c : cases) {
    auto result = Classify(
        MakeRequest("https://ads.example/x", c.destination),
        NoFactoryInitiator());
    ASSERT_TRUE(result.has_value()) << static_cast<int>(c.destination);
    EXPECT_EQ(c.kind, result->kind) << static_cast<int>(c.destination);
  }
}

TEST(RequestClassifierTest, OnlyBeaconsAndPingsArePings) {
  auto beacon = MakeRequest("https://t.example/b", RequestDestination::kEmpty);
  beacon.resource_type = static_cast<int>(blink::mojom::ResourceType::kPing);
  auto result = Classify(beacon, NoFactoryInitiator());
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(RequestKind::kPing, result->kind);

  // A fetch() with keepalive is still a fetch.
  auto fetch = MakeRequest("https://t.example/f", RequestDestination::kEmpty);
  fetch.keepalive = true;
  result = Classify(fetch, NoFactoryInitiator());
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(RequestKind::kXmlHttpRequest, result->kind);
}

TEST(RequestClassifierTest, MainFrameDocumentsAreNotJudged) {
  EXPECT_FALSE(Classify(
      MakeRequest("https://ads.example/", RequestDestination::kDocument),
      NoFactoryInitiator()));
}

TEST(RequestClassifierTest, OnlyHttpAndHttpsAreJudged) {
  for (const char* url :
       {"data:text/plain,hi", "blob:https://news.test/uuid",
        "chrome://version/", "file:///etc/hosts", "ftp://a.test/x",
        "chrome-extension://abc/x.js", "about:blank"}) {
    EXPECT_FALSE(Classify(
        MakeRequest(url, RequestDestination::kScript), NoFactoryInitiator()))
        << url;
  }
}

TEST(RequestClassifierTest, BrowserPagesAndExtensionsAreNotJudged) {
  for (const char* initiator :
       {"chrome://settings", "chrome-untrusted://x", "chrome-extension://abc",
        "file:///tmp/x.html"}) {
    EXPECT_FALSE(Classify(
        MakeRequest("https://ads.example/a.js", RequestDestination::kScript,
                    initiator),
        NoFactoryInitiator()))
        << initiator;
  }
}

TEST(RequestClassifierTest, ThirdPartyIsDecidedBySite) {
  auto third = Classify(
      MakeRequest("https://ads.example/a.js", RequestDestination::kScript),
      NoFactoryInitiator());
  ASSERT_TRUE(third.has_value());
  EXPECT_TRUE(third->third_party);
  EXPECT_EQ("ads.example", third->hostname);
  EXPECT_EQ("news.test", third->source_hostname);
  EXPECT_EQ("https://ads.example/a.js", third->url);

  // Same site, different host: first party.
  auto same_site = Classify(
      MakeRequest("https://cdn.news.test/a.js", RequestDestination::kScript),
      NoFactoryInitiator());
  ASSERT_TRUE(same_site.has_value());
  EXPECT_FALSE(same_site->third_party);

  // Same host, different scheme and port: still first party for a filter.
  auto same_host = Classify(
      MakeRequest("http://news.test:8080/a.js", RequestDestination::kScript),
      NoFactoryInitiator());
  ASSERT_TRUE(same_host.has_value());
  EXPECT_FALSE(same_host->third_party);
}

TEST(RequestClassifierTest, LoopbackIpHostsCompareByHost) {
  auto same = Classify(
      MakeRequest("http://127.0.0.1:8000/a.js", RequestDestination::kScript,
                  "http://127.0.0.1:9000"),
      NoFactoryInitiator());
  ASSERT_TRUE(same.has_value());
  EXPECT_FALSE(same->third_party);
}

TEST(RequestClassifierTest, OpaqueInitiatorCountsAsThirdParty) {
  auto request = MakeRequest("https://ads.example/a.js",
                             RequestDestination::kScript, nullptr);
  auto result = Classify(request, NoFactoryInitiator());
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->third_party);
  EXPECT_EQ("", result->source_hostname);
}

TEST(RequestClassifierTest, FactoryInitiatorIsUsedWhenRequestHasNone) {
  auto request = MakeRequest("https://cdn.news.test/a.js",
                             RequestDestination::kScript, nullptr);
  auto result = Classify(
      request, url::Origin::Create(GURL("https://news.test")));
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->third_party);
  EXPECT_EQ("news.test", result->source_hostname);
}

TEST(RequestClassifierTest, InvalidUrlIsNotJudged) {
  network::ResourceRequest request;
  request.destination = RequestDestination::kScript;
  EXPECT_FALSE(Classify(request, NoFactoryInitiator()));
}

// First party context is the top-level site.

TEST(RequestClassifierTest, AdIframeSubresourcesAreThirdPartyToThePage) {
  // The page news.test embeds an iframe from ads.example. The iframe's own
  // pixel is first party to the iframe, but third party to the page.
  auto request = MakeRequest("https://ads.example/pixel.gif",
                             RequestDestination::kImage, "https://ads.example");
  auto result = Classify(request, url::Origin::Create(GURL("https://ads.example")),
                         TopFrame("https://news.test"));
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->third_party);
  EXPECT_EQ("news.test", result->source_hostname);
}

TEST(RequestClassifierTest, WithoutTopFrameInfoTheInitiatorIsTheFirstParty) {
  auto request = MakeRequest("https://ads.example/pixel.gif",
                             RequestDestination::kImage, "https://ads.example");
  auto result = Classify(request, NoFactoryInitiator(), NoIsolationInfo());
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->third_party);
  EXPECT_EQ("ads.example", result->source_hostname);
}

TEST(RequestClassifierTest, TopFrameDecidesFirstPartyContent) {
  // Same site as the page, different host: first party, whatever the iframe is.
  auto request = MakeRequest("https://cdn.news.test/app.js",
                             RequestDestination::kScript, "https://ads.example");
  auto result = Classify(request, NoFactoryInitiator(), TopFrame("https://news.test"));
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->third_party);
}

TEST(RequestClassifierTest, BrowserInitiatedSubframeNavigationKnowsItsPage) {
  // A reload or back navigation of an ad subframe: no initiator, an opaque
  // factory initiator, and the page in the request's trusted IsolationInfo.
  network::ResourceRequest request;
  request.url = GURL("https://ads.example/frame.html");
  request.destination = RequestDestination::kIframe;
  request.trusted_params.emplace();
  request.trusted_params->isolation_info = TopFrame("https://news.test");
  auto result = Classify(request, NoFactoryInitiator(), NoIsolationInfo());
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(RequestKind::kSubdocument, result->kind);
  EXPECT_TRUE(result->third_party);
  EXPECT_EQ("news.test", result->source_hostname);
}

TEST(RequestClassifierTest, TrustedRequestIsolationInfoWinsOverTheFactorys) {
  network::ResourceRequest request;
  request.url = GURL("https://news.test/frame.html");
  request.destination = RequestDestination::kIframe;
  request.trusted_params.emplace();
  request.trusted_params->isolation_info = TopFrame("https://news.test");
  auto result = Classify(request, NoFactoryInitiator(), TopFrame("https://other.test"));
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->third_party);
  EXPECT_EQ("news.test", result->source_hostname);
}

TEST(RequestClassifierTest, BrowserPageAsTopFrameIsNotJudged) {
  // chrome://newtab embedding something over https.
  auto request = MakeRequest("https://ads.example/a.js", RequestDestination::kScript,
                             "https://ads.example");
  EXPECT_FALSE(Classify(request, NoFactoryInitiator(), TopFrame("chrome://newtab")));
}

}  // namespace
}  // namespace aerie_adblock
