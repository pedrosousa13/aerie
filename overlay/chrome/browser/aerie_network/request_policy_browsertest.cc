// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// The request policy in the network service (issue #257), seen from the
// browser: a request the browser makes on its own to a host that is not on the
// allowlist is refused and counted, and the same host loads in a tab. The
// browser's own requests are made through the system URLLoaderFactory, the one
// every browser-process service uses.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/metrics/content/subprocess_metrics_provider.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/histogram_fetcher.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/base/net_errors.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "net/base/address_list.h"
#include "net/base/host_port_pair.h"
#include "net/base/network_anonymization_key.h"
#include "net/dns/public/host_resolver_results.h"
#include "net/dns/public/resolve_error_info.h"
#include "services/network/public/cpp/constants.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/host_resolver.mojom.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/aerie/request_policy.h"

namespace {

// Collects the answer to NetworkContext::ResolveHost.
class LookupClient : public network::mojom::ResolveHostClient {
 public:
  LookupClient() = default;

  mojo::PendingRemote<network::mojom::ResolveHostClient> BindAndGetRemote() {
    return receiver_.BindNewPipeAndPassRemote();
  }
  int Wait() {
    run_loop_.Run();
    return result_;
  }

  void OnComplete(int32_t error,
                  const net::ResolveErrorInfo& resolve_error_info,
                  const net::AddressList& resolved_addresses,
                  const net::HostResolverEndpointResults& alternatives) override {
    result_ = error;
    run_loop_.Quit();
  }
  void OnTextResults(const std::vector<std::string>&) override {}
  void OnHostnameResults(const std::vector<net::HostPortPair>&) override {}

 private:
  mojo::Receiver<network::mojom::ResolveHostClient> receiver_{this};
  base::RunLoop run_loop_;
  int result_ = net::ERR_UNEXPECTED;
};

constexpr char kHistogram[] = "Aerie.Network.RequestPolicy.Refused";

class AerieRequestPolicyBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    // Every name goes to the embedded test server; nothing leaves the machine.
    host_resolver()->AddRule("*", "127.0.0.1");
    // Aerie turns HTTPS-only mode on; these pages are served over http.
    browser()->GetProfile()->GetPrefs()->SetBoolean(prefs::kHttpsOnlyModeEnabled, false);
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
        [](const net::test_server::HttpRequest& request) -> std::unique_ptr<net::test_server::HttpResponse> {
          auto response = std::make_unique<net::test_server::BasicHttpResponse>();
          response->set_content_type("text/html");
          response->set_content("<!doctype html><title>Policy test page</title><p>ok</p>");
          response->AddCustomHeader("Access-Control-Allow-Origin", "*");
          return response;
        }));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  // Fetches |url| through the system URLLoaderFactory with |annotation|, as a
  // browser service would. Returns the net error.
  int SystemFetch(const GURL& url, const net::NetworkTrafficAnnotationTag& annotation) {
    auto request = std::make_unique<network::ResourceRequest>();
    request->url = url;
    auto loader = network::SimpleURLLoader::Create(std::move(request), annotation);
    scoped_refptr<network::SharedURLLoaderFactory> factory =
        g_browser_process->system_network_context_manager()->GetSharedURLLoaderFactory();
    base::RunLoop run_loop;
    loader->DownloadToString(
        factory.get(), base::BindOnce([](base::OnceClosure quit, std::optional<std::string>) {
                         std::move(quit).Run();
                       }, run_loop.QuitClosure()),
        1024 * 1024);
    run_loop.Run();
    return loader->NetError();
  }

  // Asks the system NetworkContext to preconnect, as the browser's predictors do, and waits until the
  // network service has handled the call.
  void Preconnect(const GURL& url, const net::MutableNetworkTrafficAnnotationTag& annotation) {
    g_browser_process->system_network_context_manager()->GetContext()->PreconnectSockets(
        1, url, network::mojom::CredentialsMode::kOmit, net::NetworkAnonymizationKey::CreateTransient(),
        network::GetNoOpNetworkRestrictionsId(), annotation, std::nullopt, mojo::NullRemote());
    g_browser_process->system_network_context_manager()->FlushNetworkInterfaceForTesting();
  }

  // Asks the system NetworkContext to resolve |host|, as DNS prefetch does. Returns the net error.
  int Lookup(const std::string& host) {
    LookupClient client;
    g_browser_process->system_network_context_manager()->GetContext()->ResolveHost(
        network::mojom::HostResolverHost::NewHostPortPair(net::HostPortPair(host, 80)),
        net::NetworkAnonymizationKey::CreateTransient(), nullptr, client.BindAndGetRemote());
    return client.Wait();
  }

  // The network service may run in its own process; bring its histograms in.
  void MergeNetworkServiceHistograms() {
    base::RunLoop run_loop;
    content::FetchHistogramsAsynchronously(base::SingleThreadTaskRunner::GetCurrentDefault(),
                                           run_loop.QuitClosure(), base::Seconds(10));
    run_loop.Run();
    metrics::SubprocessMetricsProvider::MergeHistogramDeltasForTesting();
  }
};

// A system request to a host that is not listed: refused with a net error and counted.
IN_PROC_BROWSER_TEST_F(AerieRequestPolicyBrowserTest, ASystemRequestToAnUnlistedHostIsRefusedAndCounted) {
  base::HistogramTester histograms;
  const GURL url = embedded_test_server()->GetURL("unlisted.example", "/title1.html");

  EXPECT_EQ(net::ERR_BLOCKED_BY_ADMINISTRATOR, SystemFetch(url, TRAFFIC_ANNOTATION_FOR_TESTS));

  MergeNetworkServiceHistograms();
  histograms.ExpectBucketCount(kHistogram, aerie_network::Verdict::kRefusedUnlistedHost, 1);
  histograms.ExpectTotalCount(kHistogram, 1);
}

// The same host in a tab is a page load: untouched, and nothing more is counted.
IN_PROC_BROWSER_TEST_F(AerieRequestPolicyBrowserTest, APageLoadToTheSameHostSucceeds) {
  base::HistogramTester histograms;
  const GURL url = embedded_test_server()->GetURL("unlisted.example", "/title1.html");

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  content::WebContents* tab = browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(url, tab->GetLastCommittedURL());
  EXPECT_EQ(u"Policy test page", tab->GetTitle());
  // A page's own fetch() to that host is a renderer request too.
  EXPECT_EQ(true, content::EvalJs(tab, content::JsReplace(
                                           "fetch($1).then(r => r.ok)", url.spec())));

  MergeNetworkServiceHistograms();
  histograms.ExpectTotalCount(kHistogram, 0);
}

// The refusal does not depend on the host being unknown to the test server: the
// listed row works only for its own annotation.
IN_PROC_BROWSER_TEST_F(AerieRequestPolicyBrowserTest, AListedHostIsReachedWithItsOwnAnnotationOnly) {
  base::HistogramTester histograms;
  const GURL listed = embedded_test_server()->GetURL("easylist.to", "/title1.html");
  const net::NetworkTrafficAnnotationTag own =
      net::DefineNetworkTrafficAnnotation("aerie_filter_list_download", "Test copy of the filter list annotation.");

  EXPECT_EQ(net::OK, SystemFetch(listed, own));
  EXPECT_EQ(net::ERR_BLOCKED_BY_ADMINISTRATOR, SystemFetch(listed, TRAFFIC_ANNOTATION_FOR_TESTS));

  MergeNetworkServiceHistograms();
  histograms.ExpectBucketCount(kHistogram, aerie_network::Verdict::kRefusedUnlistedHost, 1);
  histograms.ExpectTotalCount(kHistogram, 1);
}

// A preconnect the browser opens on its own is judged like a request: refused, counted, nothing else.
IN_PROC_BROWSER_TEST_F(AerieRequestPolicyBrowserTest, APreconnectToAnUnlistedHostIsRefusedAndCounted) {
  base::HistogramTester histograms;
  const GURL url = embedded_test_server()->GetURL("unlisted.example", "/");

  // With an annotation that no row names, and with none at all (navigation preconnects pass none).
  Preconnect(url, net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));
  Preconnect(url, net::MutableNetworkTrafficAnnotationTag());

  MergeNetworkServiceHistograms();
  histograms.ExpectBucketCount(kHistogram, aerie_network::Verdict::kRefusedUnlistedHost, 2);
  histograms.ExpectTotalCount(kHistogram, 2);
}

// A host with a row may be preconnected to, with its own annotation or with none.
IN_PROC_BROWSER_TEST_F(AerieRequestPolicyBrowserTest, APreconnectToAListedHostIsNotRefused) {
  base::HistogramTester histograms;
  const GURL url = embedded_test_server()->GetURL("easylist.to", "/");

  Preconnect(url, net::MutableNetworkTrafficAnnotationTag(net::DefineNetworkTrafficAnnotation(
                      "aerie_filter_list_download", "Test copy of the filter list annotation.")));
  Preconnect(url, net::MutableNetworkTrafficAnnotationTag());

  MergeNetworkServiceHistograms();
  histograms.ExpectTotalCount(kHistogram, 0);
}

// DNS prefetch: a name the browser looks up on its own is refused unless a row names it.
IN_PROC_BROWSER_TEST_F(AerieRequestPolicyBrowserTest, ANameLookupForAnUnlistedHostIsRefusedAndCounted) {
  base::HistogramTester histograms;

  EXPECT_EQ(net::ERR_BLOCKED_BY_ADMINISTRATOR, Lookup("unlisted.example"));
  EXPECT_EQ(net::OK, Lookup("easylist.to"));  // a listed host resolves (to the test server)

  MergeNetworkServiceHistograms();
  histograms.ExpectBucketCount(kHistogram, aerie_network::Verdict::kRefusedUnlistedHost, 1);
  histograms.ExpectTotalCount(kHistogram, 1);
}

}  // namespace
