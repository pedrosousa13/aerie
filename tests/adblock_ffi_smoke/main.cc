#include "components/aerie_adblock/browser/adblock_engine.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace aerie_adblock;
int main() {
  AdblockEngine e;
  ClassifiedRequest r{"https://ads.example/a.js", "ads.example", "news.test", RequestKind::kScript, true};
  assert(!e.ShouldBlock(r));
  e.LoadListForTesting("||ads.example^$script\n@@||ads.example/ok.js\n");
  assert(e.ShouldBlock(r));
  r.kind = RequestKind::kImage; assert(!e.ShouldBlock(r));
  r.kind = RequestKind::kScript; r.url = "https://ads.example/ok.js"; assert(!e.ShouldBlock(r));
  r.url = "https://ads.example/\xc3\xa9"; assert(!e.ShouldBlock(r));  // non-ASCII: never passed to Rust
  AdblockEngine f;
  auto bytes = e.SerializeForTesting();
  assert(!bytes.empty());
  assert(f.LoadSerialized(bytes));
  r.url = "https://ads.example/a.js"; assert(f.ShouldBlock(r));
  std::vector<uint8_t> junk{1,2,3};
  assert(!f.LoadSerialized(junk));
  assert(f.ShouldBlock(r));
  // Cosmetic stylesheets cross the bridge as strings.
  AdblockEngine c;
  c.LoadListForTesting("news.test##.site-ad\n##.ad-banner\n##div[class*=\"Ads\"]\n");
  std::string page = c.CosmeticStylesheet("https://news.test/a");
  assert(page.find(".site-ad{display:none!important}") != std::string::npos);
  assert(page.find("div[class*=\"Ads\"]{display:none!important}") != std::string::npos);
  assert(c.CosmeticStylesheet("https://other.test/").find(".site-ad") == std::string::npos);
  assert(c.CosmeticStylesheet("https://news.test/\xc3\xa9").empty());  // non-ASCII: never passed to Rust
  assert(c.CosmeticStylesheet("chrome://settings/").empty());
  std::string generic = c.GenericStylesheet("https://news.test/a", "ad-banner x", "");
  assert(generic == ".ad-banner{display:none!important}\n");
  assert(c.GenericStylesheet("https://news.test/a", "x", "y").empty());
  assert(c.GenericStylesheet("https://news.test/a", "\xc3\xa9", "").empty());
  // Ignored match-all exceptions cross the bridge in both check paths.
  const std::string broad = "||ads.example^\n@@http\n@@|ws\n@@||ads.example/ok\n";
  ListCheck check = CheckList(base::as_byte_span(broad), base::Seconds(5));
  assert(check.failure == ListFailure::kNone);
  assert(check.ignored_exceptions == 2);
  assert(CheckList(base::as_byte_span(std::string("||ads.example^\n")), base::Seconds(5)).ignored_exceptions == 0);
  AdblockEngine g;
  BuildResult built = g.LoadLists({broad, "||t.example^\n@@.\n"}, base::Seconds(5));
  assert(built.failure == ListFailure::kNone);
  assert(built.ignored_exceptions == 3);
  std::puts("ok");
}
