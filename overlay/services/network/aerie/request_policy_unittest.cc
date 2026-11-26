// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "services/network/aerie/request_policy.h"

#include <string>

#include "base/test/metrics/histogram_tester.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace aerie_network {
namespace {

// The annotation hash of a name, the way the network stack computes it.
template <size_t N>
constexpr int32_t Hash(const char (&name)[N]) {
  return net::internal::ComputeAnnotationHash(name);
}

RequestFacts Facts(RequestOrigin origin, std::optional<int32_t> annotation, const char* url) {
  RequestFacts facts;
  facts.origin = origin;
  facts.annotation_hash = annotation;
  facts.url = GURL(url);
  return facts;
}

// --- Browser requests: allowlisted hosts -------------------------------------

TEST(AerieRequestPolicyTest, AllowsASystemRequestToAnAllowlistedHost) {
  EXPECT_EQ(Verdict::kAllow,
            Decide(Facts(RequestOrigin::kBrowserProcess, Hash("aerie_filter_list_download"),
                         "https://easylist.to/easylist/easylist.txt")));
}

TEST(AerieRequestPolicyTest, AllowsSubdomainsOfAnAllowlistedHostOnLabelBoundaries) {
  EXPECT_EQ(Verdict::kAllow,
            Decide(Facts(RequestOrigin::kBrowserProcess, Hash("aerie_filter_list_download"),
                         "https://mirror.easylist.to/x.txt")));
  EXPECT_EQ(Verdict::kAllow,
            Decide(Facts(RequestOrigin::kBrowserProcess, Hash("aerie_filter_list_download"),
                         "https://easylist.to./x.txt")));
  EXPECT_EQ(Verdict::kRefusedUnlistedHost,
            Decide(Facts(RequestOrigin::kBrowserProcess, Hash("aerie_filter_list_download"),
                         "https://noteasylist.to/x.txt")));
  EXPECT_EQ(Verdict::kRefusedUnlistedHost,
            Decide(Facts(RequestOrigin::kBrowserProcess, Hash("aerie_filter_list_download"),
                         "https://easylist.to.evil.example/x.txt")));
}

TEST(AerieRequestPolicyTest, ARowIsForItsOwnAnnotationOnly) {
  // The updater's host is not open to the filter list code, and the other way round.
  EXPECT_EQ(Verdict::kRefusedUnlistedHost,
            Decide(Facts(RequestOrigin::kBrowserProcess, Hash("aerie_filter_list_download"),
                         "https://update.googleapis.com/service/update2/json")));
  EXPECT_EQ(Verdict::kAllow,
            Decide(Facts(RequestOrigin::kBrowserProcess, Hash("update_client"),
                         "https://update.googleapis.com/service/update2/json")));
  EXPECT_EQ(Verdict::kRefusedUnlistedHost,
            Decide(Facts(RequestOrigin::kBrowserProcess, Hash("update_client"),
                         "https://easylist.to/easylist/easylist.txt")));
}

// The catalogue lists (issue #105) have their own annotation and their own
// hosts.
TEST(AerieRequestPolicyTest,
     CatalogueDownloadsReachTheirPublishersAndNoOtherHost) {
  const int32_t catalogue = Hash("aerie_catalogue_filter_list_download");
  for (const char* url :
       {"https://easylist.to/easylist/fanboy-annoyance.txt",
        "https://easylist-downloads.adblockplus.org/easylistitaly.txt",
        "https://secure.fanboy.co.nz/fanboy-cookiemonster.txt",
        "https://ublockorigin.github.io/uAssets/filters/privacy.min.txt",
        "https://filters.adtidy.org/extension/ublock/filters/17.txt"}) {
    EXPECT_EQ(Verdict::kAllow,
              Decide(Facts(RequestOrigin::kBrowserProcess, catalogue, url)))
        << url;
  }
  for (const char* url : {"https://evil.example/list.txt",
                          "https://update.googleapis.com/service/update2/json",
                          "https://raw.githubusercontent.com/x/y/z.txt"}) {
    EXPECT_EQ(Verdict::kRefusedUnlistedHost,
              Decide(Facts(RequestOrigin::kBrowserProcess, catalogue, url)))
        << url;
  }
  // A catalogue host is not open to the built-in list code, and the other way
  // round.
  EXPECT_EQ(Verdict::kRefusedUnlistedHost,
            Decide(Facts(
                RequestOrigin::kBrowserProcess,
                Hash("aerie_filter_list_download"),
                "https://filters.adtidy.org/extension/ublock/filters/17.txt")));
  EXPECT_EQ(Verdict::kRefusedUnlistedHost,
            Decide(Facts(
                RequestOrigin::kBrowserProcess, catalogue,
                "https://update.googleapis.com/service/update2/json")));
}

// --- Browser requests: refused -----------------------------------------------

TEST(AerieRequestPolicyTest, RefusesASystemRequestToAnUnlistedHost) {
  EXPECT_EQ(Verdict::kRefusedUnlistedHost,
            Decide(Facts(RequestOrigin::kBrowserProcess, Hash("some_new_chromium_fetch"),
                         "https://telemetry.example/collect")));
  EXPECT_EQ(Verdict::kRefusedUnlistedHost,
            Decide(Facts(RequestOrigin::kBrowserProcess, Hash("some_new_chromium_fetch"),
                         "wss://telemetry.example/socket")));
  // An IP address is no host on the list.
  EXPECT_EQ(Verdict::kRefusedUnlistedHost,
            Decide(Facts(RequestOrigin::kBrowserProcess, Hash("some_new_chromium_fetch"),
                         "http://142.250.80.46/")));
}

TEST(AerieRequestPolicyTest, FailsClosedOnAnUnknownInitiator) {
  // No page, no process: treated as the browser's own.
  EXPECT_EQ(Verdict::kRefusedUnlistedHost,
            Decide(Facts(RequestOrigin::kUnknown, Hash("some_new_chromium_fetch"),
                         "https://telemetry.example/collect")));
  // A default-constructed request is the browser's own too, never a page's.
  RequestFacts facts;
  facts.annotation_hash = Hash("some_new_chromium_fetch");
  facts.url = GURL("https://telemetry.example/collect");
  EXPECT_EQ(Verdict::kRefusedUnlistedHost, Decide(facts));
  // An unknown initiator gets what the browser gets, not more: a listed host works.
  EXPECT_EQ(Verdict::kAllow,
            Decide(Facts(RequestOrigin::kUnknown, Hash("aerie_filter_list_download"),
                         "https://easylist.to/easylist/easylist.txt")));
}

TEST(AerieRequestPolicyTest, FailsClosedOnAMissingAnnotation) {
  const char* url = "https://easylist.to/easylist/easylist.txt";  // a listed host
  EXPECT_EQ(Verdict::kRefusedNoAnnotation,
            Decide(Facts(RequestOrigin::kBrowserProcess, std::nullopt, url)));
  EXPECT_EQ(Verdict::kRefusedNoAnnotation,
            Decide(Facts(RequestOrigin::kBrowserProcess, Hash("missing"), url)));
  EXPECT_EQ(Verdict::kRefusedNoAnnotation,
            Decide(Facts(RequestOrigin::kBrowserProcess, net::internal::UNDEFINED_HASH, url)));
  EXPECT_EQ(Verdict::kRefusedNoAnnotation,
            Decide(Facts(RequestOrigin::kBrowserProcess, net::internal::TRAFFIC_ANNOTATION_UNINITIALIZED, url)));
  EXPECT_EQ(Verdict::kRefusedNoAnnotation,
            Decide(Facts(RequestOrigin::kUnknown, std::nullopt, "https://telemetry.example/")));
}

// --- Traffic the policy leaves alone -------------------------------------------

TEST(AerieRequestPolicyTest, LeavesPageTrafficAlone) {
  // A frame or any renderer: any host, any annotation, even a missing one.
  EXPECT_EQ(Verdict::kAllow,
            Decide(Facts(RequestOrigin::kRenderer, Hash("blink_resource_loader"), "https://news.example/a.js")));
  EXPECT_EQ(Verdict::kAllow,
            Decide(Facts(RequestOrigin::kRenderer, std::nullopt, "https://news.example/a.js")));
  EXPECT_EQ(Verdict::kAllow,
            Decide(Facts(RequestOrigin::kRenderer, Hash("some_new_chromium_fetch"), "https://www.google.com/")));
}

TEST(AerieRequestPolicyTest, LeavesExtensionTrafficAlone) {
  // An extension's fetch comes from a renderer process, like a page's.
  EXPECT_EQ(Verdict::kAllow,
            Decide(Facts(RequestOrigin::kRenderer, Hash("blink_extension_resource_loader"),
                         "https://api.extension-author.example/v1")));
}

TEST(AerieRequestPolicyTest, LeavesRequestsTheUserStartsAlone) {
  // A navigation, a download and a filter list address the user added: any host.
  for (const int32_t annotation :
       {Hash("navigation_url_loader"), Hash("download_web_contents_frame"), Hash("parallel_download_job"),
        Hash("aerie_custom_filter_list_download")}) {
    EXPECT_EQ(Verdict::kAllow,
              Decide(Facts(RequestOrigin::kBrowserProcess, annotation, "https://anything.example/file")))
        << annotation;
  }
}

TEST(AerieRequestPolicyTest, TheBuiltInFilterListAnnotationDoesNotReachArbitraryHosts) {
  // Only the user-added kind is open; the built-in lists' annotation keeps to its publisher.
  EXPECT_EQ(Verdict::kRefusedUnlistedHost,
            Decide(Facts(RequestOrigin::kBrowserProcess, Hash("aerie_filter_list_download"),
                         "https://elsewhere.example/list.txt")));
}

TEST(AerieRequestPolicyTest, OnlyTheNetworkSchemesAreJudged) {
  // data: and blob: never reach a host; trk: is refused by the network stack itself.
  for (const char* url : {"data:text/plain,hi", "blob:https://a.example/1", "trk:1:https://x.example/"}) {
    EXPECT_EQ(Verdict::kAllow, Decide(Facts(RequestOrigin::kBrowserProcess, Hash("some_new_chromium_fetch"), url)))
        << url;
  }
}

// --- No pref, no extension -----------------------------------------------------

TEST(AerieRequestPolicyTest, TheDecisionTakesNothingButTheRequestFacts) {
  // Decide() is a pure function of RequestFacts and the tables compiled in. Nothing a pref, a
  // header or a page can set is in RequestFacts, so none can widen the list.
  RequestFacts a = Facts(RequestOrigin::kBrowserProcess, Hash("some_new_chromium_fetch"), "https://x.example/");
  EXPECT_EQ(Decide(a), Decide(a));
  EXPECT_EQ(Verdict::kRefusedUnlistedHost, Decide(a));
}

// --- Who is a renderer: only a factory positively known to serve one -------

FactoryFacts RendererFactory() {
  FactoryFacts f;
  f.is_browser_process = false;
  f.is_valid_renderer_process = true;
  f.is_trusted = false;
  f.has_initiator_origin_lock = true;
  return f;
}

TEST(AerieRequestPolicyTest, AFactoryThatPositivelyServesARendererIsSkipped) {
  EXPECT_EQ(RequestOrigin::kRenderer, ClassifyFactory(RendererFactory()));
}

TEST(AerieRequestPolicyTest, TheBrowsersOwnFactoryIsJudged) {
  FactoryFacts f = RendererFactory();
  f.is_browser_process = true;
  f.is_valid_renderer_process = false;
  f.is_trusted = true;
  f.has_initiator_origin_lock = false;
  EXPECT_EQ(RequestOrigin::kBrowserProcess, ClassifyFactory(f));
}

TEST(AerieRequestPolicyTest, ARequestWithNoLoaderIsJudged) {
  EXPECT_EQ(RequestOrigin::kUnknown, ClassifyFactory(std::nullopt));
  EXPECT_EQ(Verdict::kRefusedUnlistedHost,
            Decide(Facts(ClassifyFactory(std::nullopt), Hash("some_new_chromium_fetch"),
                         "https://telemetry.example/")));
}

TEST(AerieRequestPolicyTest, AUtilityOrNullProcessIsJudged) {
  // A utility process has no valid renderer process id.
  FactoryFacts utility = RendererFactory();
  utility.is_valid_renderer_process = false;
  EXPECT_NE(RequestOrigin::kRenderer, ClassifyFactory(utility));
  EXPECT_EQ(Verdict::kRefusedUnlistedHost,
            Decide(Facts(ClassifyFactory(utility), Hash("some_new_chromium_fetch"),
                         "https://telemetry.example/")));
}

TEST(AerieRequestPolicyTest, ABrowserMadeFactoryWithARendererIdIsJudgedUnlessLockedAndUntrusted) {
  FactoryFacts trusted = RendererFactory();
  trusted.is_trusted = true;
  FactoryFacts unlocked = RendererFactory();
  unlocked.has_initiator_origin_lock = false;
  for (const FactoryFacts& f : {trusted, unlocked}) {
    EXPECT_NE(RequestOrigin::kRenderer, ClassifyFactory(f));
    EXPECT_EQ(Verdict::kRefusedUnlistedHost,
              Decide(Facts(ClassifyFactory(f), Hash("some_new_chromium_fetch"),
                           "https://telemetry.example/")));
  }
}

// --- Preconnect and name lookups -------------------------------------------

TEST(AerieRequestPolicyTest, APreconnectWithAnAnnotationIsDecidedLikeARequest) {
  EXPECT_EQ(Verdict::kRefusedUnlistedHost,
            DecideConnection(Facts(RequestOrigin::kBrowserProcess, Hash("predictive_preconnect"),
                                   "https://news.example/")));
  EXPECT_EQ(Verdict::kAllow,
            DecideConnection(Facts(RequestOrigin::kBrowserProcess, Hash("aerie_filter_list_download"),
                                   "https://easylist.to/")));
}

TEST(AerieRequestPolicyTest, ANameLookupOrPreconnectWithNoAnnotationIsJudgedByHost) {
  EXPECT_EQ(Verdict::kRefusedUnlistedHost,
            DecideConnection(Facts(RequestOrigin::kBrowserProcess, std::nullopt, "https://news.example/")));
  EXPECT_EQ(Verdict::kRefusedUnlistedHost,
            DecideConnection(Facts(RequestOrigin::kUnknown, net::internal::TRAFFIC_ANNOTATION_UNINITIALIZED,
                                   "https://www.google.com/")));
  // A host that has a row, for any annotation, may be resolved and connected to.
  EXPECT_EQ(Verdict::kAllow,
            DecideConnection(Facts(RequestOrigin::kBrowserProcess, std::nullopt, "https://easylist.to/")));
  EXPECT_EQ(Verdict::kAllow,
            DecideConnection(Facts(RequestOrigin::kBrowserProcess, std::nullopt, "https://update.googleapis.com/")));
  EXPECT_EQ(Verdict::kRefusedUnlistedHost,
            DecideConnection(Facts(RequestOrigin::kBrowserProcess, std::nullopt, "https://noteasylist.to/")));
}

TEST(AerieRequestPolicyTest, ADirectedAnnotationOpensAPreconnectLikeARequest) {
  // With an annotation the rule is the same as for a request.
  EXPECT_EQ(Verdict::kAllow,
            DecideConnection(Facts(RequestOrigin::kBrowserProcess, Hash("navigation_url_loader"),
                                   "https://typed-by-user.example/")));
}

// --- Refusals: histogram and log ----------------------------------------------

TEST(AerieRequestPolicyTest, IsRefusalIsTrueForEveryRefusalAndOnlyThem) {
  EXPECT_FALSE(IsRefusal(Verdict::kAllow));
  EXPECT_TRUE(IsRefusal(Verdict::kRefusedUnlistedHost));
  EXPECT_TRUE(IsRefusal(Verdict::kRefusedNoAnnotation));
}

TEST(AerieRequestPolicyTest, ARefusalIsCountedInTheHistogram) {
  base::HistogramTester histograms;
  RecordRefusal(Verdict::kRefusedUnlistedHost, GURL("https://telemetry.example/collect?id=7"));
  RecordRefusal(Verdict::kRefusedUnlistedHost, GURL("https://telemetry.example/collect?id=8"));
  RecordRefusal(Verdict::kRefusedNoAnnotation, GURL("https://telemetry.example/"));
  histograms.ExpectBucketCount(kRefusedHistogram, Verdict::kRefusedUnlistedHost, 2);
  histograms.ExpectBucketCount(kRefusedHistogram, Verdict::kRefusedNoAnnotation, 1);
  histograms.ExpectTotalCount(kRefusedHistogram, 3);
}

TEST(AerieRequestPolicyTest, TheLogLineHoldsTheHostAndNothingElseOfTheUrl) {
  const std::string line = RefusalLogMessage(
      Verdict::kRefusedUnlistedHost,
      GURL("https://user:hunter2@Telemetry.example:8443/secret/path/name?token=abc123&q=1#frag"));
  EXPECT_NE(std::string::npos, line.find("telemetry.example")) << line;
  for (const char* leaked : {"user", "hunter2", "8443", "secret", "path", "token", "abc123", "q=1", "frag", "https", "://"}) {
    EXPECT_EQ(std::string::npos, line.find(leaked)) << leaked << " in: " << line;
  }
}

TEST(AerieRequestPolicyTest, TheLogLineNeverHoldsCookiesOrHeaders) {
  // The function takes the URL only; a Cookie header cannot get in. Check the line for an IP host too.
  const std::string line = RefusalLogMessage(Verdict::kRefusedNoAnnotation, GURL("http://203.0.113.9/a?b=c"));
  EXPECT_NE(std::string::npos, line.find("203.0.113.9"));
  EXPECT_EQ(std::string::npos, line.find("b=c"));
  EXPECT_EQ(std::string::npos, line.find("/a"));
}

}  // namespace
}  // namespace aerie_network
