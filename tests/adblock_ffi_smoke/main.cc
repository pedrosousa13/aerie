#include "components/aerie_adblock/browser/adblock_engine.h"
#include <cassert>
#include <cstdio>
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
  std::puts("ok");
}
