// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/filter_list_state.h"

#include <map>
#include <optional>
#include <set>
#include <string>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/values.h"
#include "components/aerie_adblock/browser/catalogue.h"
#include "crypto/hash.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace aerie_adblock {
namespace {

TEST(FilterListUrlTest, OnlyHttpsIsAcceptable) {
  EXPECT_TRUE(IsAcceptableListUrl(GURL("https://lists.example/a.txt"), false));
  for (const char* bad :
       {"http://lists.example/a.txt", "ftp://lists.example/a.txt",
        "file:///etc/passwd", "data:text/plain,x", "javascript:alert(1)",
        "https://user:pw@lists.example/a.txt", "https://lists.example/a#frag",
        "https://", "https://:8443/a.txt", "", "not a url"}) {
    EXPECT_FALSE(IsAcceptableListUrl(GURL(bad), false)) << bad;
  }
}

// GURL turns the raw "https:///a.txt" into https://a.txt/, the way every
// browser address bar does. By the time IsAcceptableListUrl() sees it, it is an
// ordinary https URL for the public host "a.txt", and fetching that host is
// right: the list is added and shown under that address. (A real empty host
// stays invalid, see above.)
TEST(FilterListUrlTest, ExtraSlashesAreCanonicalisedToTheHostThatFollows) {
  GURL url("https:///a.txt");
  EXPECT_EQ("a.txt", url.host());
  EXPECT_TRUE(IsAcceptableListUrl(url, false));
}

TEST(FilterListUrlTest, LoopbackHttpNeedsTheTestSwitch) {
  for (const char* loop : {"http://127.0.0.1:8000/a.txt",
                           "http://localhost:8000/a.txt", "http://[::1]/a.txt"}) {
    EXPECT_FALSE(IsAcceptableListUrl(GURL(loop), false)) << loop;
    EXPECT_TRUE(IsAcceptableListUrl(GURL(loop), true)) << loop;
  }
  // The switch never opens http for another host, including look-alikes.
  for (const char* other : {"http://lists.example/a.txt", "http://127.0.0.1.evil.test/a.txt",
                            "http://10.0.0.1/a.txt", "http://192.168.1.1/a.txt"}) {
    EXPECT_FALSE(IsAcceptableListUrl(GURL(other), true)) << other;
  }
}

TEST(FilterListUrlTest, PublicHostsAreAcceptable) {
  for (const char* good :
       {"https://lists.example/a.txt", "https://8.8.8.8/a.txt",
        "https://[2001:4860:4860::8888]/a.txt", "https://sub.lists.example./a.txt",
        "https://[::ffff:8.8.8.8]/a.txt"}) {
    EXPECT_TRUE(IsAcceptableListUrl(GURL(good), false)) << good;
    EXPECT_TRUE(IsAcceptableListUrl(GURL(good), true)) << good;
  }
}

TEST(FilterListUrlTest, NonPublicAddressesAreRefused) {
  for (const char* bad : {
           // IPv4 literals.
           "https://127.0.0.1/a.txt", "https://127.8.9.10/a.txt",
           "https://10.0.0.1/a.txt", "https://172.16.0.1/a.txt",
           "https://172.31.255.255/a.txt", "https://192.168.1.1/a.txt",
           "https://169.254.169.254/a.txt", "https://100.64.0.1/a.txt",
           "https://100.127.255.254/a.txt", "https://0.0.0.0/a.txt",
           "https://224.0.0.1/a.txt", "https://239.255.255.250/a.txt",
           "https://255.255.255.255/a.txt",
           // IPv6 literals.
           "https://[::1]/a.txt", "https://[::]/a.txt",
           "https://[fc00::1]/a.txt", "https://[fd12:3456::1]/a.txt",
           "https://[fe80::1]/a.txt", "https://[ff02::1]/a.txt",
           "https://[ff05::2]/a.txt",
           // IPv4-mapped IPv6.
           "https://[::ffff:127.0.0.1]/a.txt", "https://[::ffff:10.0.0.1]/a.txt",
           "https://[::ffff:192.168.1.1]/a.txt",
           "https://[::ffff:169.254.1.1]/a.txt",
           "https://[::ffff:100.64.0.1]/a.txt", "https://[::ffff:0.0.0.0]/a.txt",
           "https://[::ffff:224.0.0.1]/a.txt",
           "https://[::ffff:255.255.255.255]/a.txt",
           // Names.
           "https://localhost/a.txt", "https://LOCALHOST/a.txt",
           "https://localhost./a.txt", "https://foo.localhost/a.txt",
           "https://a.b.localhost./a.txt", "https://intranet/a.txt",
           "https://intranet./a.txt", "https://printer/a.txt",
           // Forms that GURL canonicalises to an address above.
           "https://2130706433/a.txt", "https://0x7f000001/a.txt",
           "https://017700000001/a.txt", "https://127.1/a.txt",
           "https://0x7f.0.0.1/a.txt", "https://3232235777/a.txt",
           "https://0xa9fea9fe/a.txt", "https://[2002:7f00:1::]/a.txt", "https://[2002:c0a8:101::1]/a.txt",
           "https://[2002:808:808::]/a.txt", "https://[0:0:0:0:0:0:0:1]/a.txt",
           "https://[::ffff:7f00:1]/a.txt"}) {
    EXPECT_FALSE(IsAcceptableListUrl(GURL(bad), false)) << bad;
  }
}

TEST(FilterListUrlTest, TheRefusalSaysWhyAHostIsRefused) {
  EXPECT_EQ(CheckListUrl(GURL("https://lists.example/a.txt"), false), ListFailure::kNone);
  for (const char* url : {"https://10.0.0.1/a.txt", "https://nas/a.txt",
                          "https://[::1]/a.txt", "https://[2002:c0a8:101::1]/a.txt"}) {
    EXPECT_EQ(CheckListUrl(GURL(url), false), ListFailure::kNonPublicUrl) << url;
  }
  for (const char* url : {"http://lists.example/a.txt", "http://10.0.0.1/a.txt",
                          "https://u:p@lists.example/a.txt", "ftp://lists.example/a"}) {
    EXPECT_EQ(CheckListUrl(GURL(url), false), ListFailure::kInsecureUrl) << url;
  }
}

TEST(FilterListStateTest, ANonPublicAddressIsRefusedWhenAddedAndSaysSo) {
  FilterListState state;
  std::string id;
  for (const char* url : {"https://192.168.1.5/x.txt", "https://nas/list.txt",
                          "https://localhost/x.txt", "https://[::1]/x.txt"}) {
    EXPECT_EQ(state.AddCustomList(GURL(url), false, &id),
              FilterListState::AddResult::kNonPublicUrl) << url;
  }
  EXPECT_EQ(state.AddCustomList(GURL("http://lists.example/x.txt"), false, &id),
            FilterListState::AddResult::kInvalidUrl);
  EXPECT_EQ(state.lists().size(), 2u);
}

// The id this build gives a custom list: "c" and 16 hex digits of the hash of
// the address.
std::string IdOf(const char* url) {
  auto digest = crypto::hash::Sha256(GURL(url).spec());
  return "c" + base::ToLowerASCII(base::HexEncode(base::span(digest).first<8u>()));
}

TEST(FilterListStateTest, ASavedListAtAHostThatIsNowRefusedIsKept) {
  // Saved by a build that accepted any https host. Dropping it on load would
  // lose it for good at the next save. The fetch refuses it instead, and the
  // user sees the reason and can remove the list.
  const char* urls[] = {"https://nas/list.txt", "https://192.168.1.5/x.txt"};
  base::ListValue lists;
  for (const char* url : urls) {
    lists.Append(base::DictValue()
                     .Set("id", IdOf(url))
                     .Set("url", GURL(url).spec())
                     .Set("title", GURL(url).host()));
  }
  base::DictValue dict;
  dict.Set("lists", std::move(lists));
  FilterListState state = FilterListState::FromValue(dict);
  ASSERT_EQ(state.lists().size(), 4u);
  for (const char* url : urls) {
    const FilterListEntry* entry = state.Find(IdOf(url));
    ASSERT_TRUE(entry) << url;
    EXPECT_EQ(entry->url, GURL(url).spec());
    EXPECT_FALSE(IsAcceptableListUrl(GURL(url), false)) << url;
  }
  // It survives a save and a load too.
  EXPECT_EQ(FilterListState::FromValue(state.ToValue()).lists().size(), 4u);
}

TEST(FilterListUrlTest, TheLoopbackSwitchNeverOpensPrivateRanges) {
  for (const char* bad :
       {"https://10.0.0.1/a.txt", "https://192.168.1.1/a.txt",
        "https://172.16.0.1/a.txt", "https://169.254.169.254/a.txt",
        "https://100.64.0.1/a.txt", "https://[fc00::1]/a.txt",
        "https://[fe80::1]/a.txt", "https://[::ffff:10.0.0.1]/a.txt",
        "https://intranet/a.txt", "http://intranet/a.txt",
        "http://[::ffff:127.0.0.1]/a.txt", "http://0.0.0.0/a.txt",
        "http://169.254.1.1/a.txt"}) {
    EXPECT_FALSE(IsAcceptableListUrl(GURL(bad), true)) << bad;
  }
  // Loopback itself stays open for the Bench and tests, with https and http.
  for (const char* ok : {"https://127.0.0.1:8443/a.txt", "http://127.0.0.1:8000/a.txt",
                         "http://localhost:8000/a.txt", "http://[::1]/a.txt",
                         "https://localhost/a.txt", "https://[::1]/a.txt"}) {
    EXPECT_TRUE(IsAcceptableListUrl(GURL(ok), true)) << ok;
  }
}

TEST(FilterListUrlTest, TooLongIsRefused) {
  EXPECT_FALSE(IsAcceptableListUrl(
      GURL("https://lists.example/" + std::string(2100, 'a')), false));
}

TEST(FilterListIdTest, OnlyShortLowercaseAlphanumericIdsAreValid) {
  EXPECT_TRUE(IsValidListId("easylist"));
  EXPECT_TRUE(IsValidListId("c0123456789abcdef"));
  for (const char* bad : {"", "../x", "a/b", "A", "a.b", "a b", "a\\0b"}) {
    EXPECT_FALSE(IsValidListId(bad)) << bad;
  }
  EXPECT_FALSE(IsValidListId(std::string(33, 'a')));
}

TEST(ExceptionHostTest, NormalizesOrRefuses) {
  EXPECT_EQ(NormalizeExceptionHost("News.Test"), "news.test");
  EXPECT_EQ(NormalizeExceptionHost("news.test."), "news.test");
  EXPECT_EQ(NormalizeExceptionHost("127.0.0.1"), "127.0.0.1");
  for (const char* bad : {"", ".", "a b", "a/b", "a@b", "https://a.test", "ä.test",
                          "a..b"}) {
    EXPECT_EQ(NormalizeExceptionHost(bad), "") << bad;
  }
  EXPECT_EQ(NormalizeExceptionHost(std::string(300, 'a')), "");
}

TEST(ExceptionHostTest, MatchesTheHostAndItsSubdomainsOnly) {
  std::set<std::string> ex = {"news.test"};
  EXPECT_TRUE(HostHasException("news.test", ex));
  EXPECT_TRUE(HostHasException("www.news.test", ex));
  EXPECT_TRUE(HostHasException("a.b.news.test", ex));
  EXPECT_FALSE(HostHasException("notnews.test", ex));
  EXPECT_FALSE(HostHasException("news.test.evil.test", ex));
  EXPECT_FALSE(HostHasException("test", ex));
  EXPECT_FALSE(HostHasException("", ex));
}

TEST(FilterListStateTest, StartsWithTheBuiltInListsAndNothingElse) {
  FilterListState state;
  ASSERT_EQ(state.lists().size(), 2u);
  EXPECT_EQ(state.lists()[0].id, "easylist");
  EXPECT_EQ(state.lists()[1].id, "easyprivacy");
  EXPECT_TRUE(state.lists()[0].builtin);
  EXPECT_TRUE(state.auto_update());
  EXPECT_TRUE(state.exceptions().empty());
  EXPECT_FALSE(state.NeedsRebuild());
}

TEST(FilterListStateTest, AddsRemovesAndRejectsCustomLists) {
  FilterListState state;
  std::string id;
  EXPECT_EQ(state.AddCustomList(GURL("https://lists.example/a.txt"), false, &id),
            FilterListState::AddResult::kAdded);
  EXPECT_TRUE(IsValidListId(id));
  EXPECT_TRUE(state.NeedsRebuild());
  EXPECT_EQ(state.AddCustomList(GURL("https://lists.example/a.txt"), false, &id),
            FilterListState::AddResult::kDuplicate);
  EXPECT_EQ(state.AddCustomList(GURL("http://lists.example/b.txt"), false, &id),
            FilterListState::AddResult::kInvalidUrl);
  // A built-in list's address is a duplicate too.
  EXPECT_EQ(state.AddCustomList(GURL(kEasyListUrl), false, &id),
            FilterListState::AddResult::kDuplicate);
  EXPECT_EQ(state.lists().size(), 3u);

  std::string added = state.lists()[2].id;
  EXPECT_FALSE(state.RemoveList("easylist"));
  EXPECT_FALSE(state.RemoveList("nothing"));
  EXPECT_TRUE(state.RemoveList(added));
  EXPECT_EQ(state.lists().size(), 2u);
  EXPECT_FALSE(state.NeedsRebuild());
}

TEST(FilterListStateTest, CustomListsAreCapped) {
  FilterListState state;
  std::string id;
  for (size_t i = 0; i < kMaxCustomLists; ++i) {
    ASSERT_EQ(state.AddCustomList(GURL("https://l.example/" + std::to_string(i)),
                                  false, &id),
              FilterListState::AddResult::kAdded);
  }
  EXPECT_EQ(state.AddCustomList(GURL("https://l.example/one-more"), false, &id),
            FilterListState::AddResult::kTooMany);
}

TEST(FilterListStateTest, ExceptionsAreNormalizedAndCapped) {
  FilterListState state;
  EXPECT_TRUE(state.SetException("News.Test", true));
  EXPECT_EQ(state.exceptions().count("news.test"), 1u);
  EXPECT_FALSE(state.SetException("not a host", true));
  EXPECT_TRUE(state.SetException("news.test", false));
  EXPECT_TRUE(state.exceptions().empty());
  for (size_t i = 0; i < kMaxExceptions; ++i) {
    ASSERT_TRUE(state.SetException("h" + std::to_string(i) + ".test", true));
  }
  EXPECT_FALSE(state.SetException("one-more.test", true));
  EXPECT_TRUE(state.SetException("h1.test", false));  // Removing still works.
}

TEST(FilterListStateTest, CosmeticOffHostsAreNormalizedAndCapped) {
  FilterListState state;
  EXPECT_TRUE(state.cosmetic_off_hosts().empty());
  EXPECT_TRUE(state.SetCosmeticOff("News.Test", true));
  EXPECT_EQ(state.cosmetic_off_hosts().count("news.test"), 1u);
  EXPECT_FALSE(state.SetCosmeticOff("not a host", true));
  // The set is separate from the blocking exceptions.
  EXPECT_TRUE(state.exceptions().empty());
  EXPECT_TRUE(state.SetCosmeticOff("news.test", false));
  EXPECT_TRUE(state.cosmetic_off_hosts().empty());
  for (size_t i = 0; i < kMaxCosmeticOffHosts; ++i) {
    ASSERT_TRUE(state.SetCosmeticOff("h" + std::to_string(i) + ".test", true));
  }
  EXPECT_FALSE(state.SetCosmeticOff("one-more.test", true));
  EXPECT_TRUE(state.SetCosmeticOff("h1.test", true));   // Already in: fine.
  EXPECT_TRUE(state.SetCosmeticOff("h1.test", false));  // Removing still works.
}

TEST(FilterListStateTest, CosmeticOffHostsSurviveASaveAndLoad) {
  FilterListState state;
  state.SetCosmeticOff("a.test", true);
  state.SetCosmeticOff("b.test", true);
  std::optional<base::Value> json =
      base::JSONReader::Read(*base::WriteJson(state.ToValue()), base::JSON_PARSE_RFC);
  ASSERT_TRUE(json && json->is_dict());
  FilterListState back = FilterListState::FromValue(json->GetDict());
  EXPECT_EQ(back.cosmetic_off_hosts(), (std::set<std::string>{"a.test", "b.test"}));
  EXPECT_TRUE(back.exceptions().empty());
}

TEST(FilterListStateTest, HostileCosmeticOffContentFallsBackToSafeValues) {
  // Wrong types for the whole field, then for its items.
  for (const char* text : {R"({"cosmetic_off": "a.test"})", R"({"cosmetic_off": 5})",
                           R"({"cosmetic_off": {"a.test": true}})",
                           R"({"cosmetic_off": null})"}) {
    std::optional<base::Value> json = base::JSONReader::Read(text, base::JSON_PARSE_RFC);
    ASSERT_TRUE(json && json->is_dict()) << text;
    FilterListState state = FilterListState::FromValue(json->GetDict());
    EXPECT_TRUE(state.cosmetic_off_hosts().empty()) << text;
    EXPECT_EQ(state.lists().size(), 2u) << text;
  }
  std::optional<base::Value> json = base::JSONReader::Read(
      R"({"cosmetic_off": ["ok.test", "bad host", 5, null, [], "../x", "OK2.test", ".lead.test"]})",
      base::JSON_PARSE_RFC);
  ASSERT_TRUE(json && json->is_dict());
  EXPECT_EQ(FilterListState::FromValue(json->GetDict()).cosmetic_off_hosts(),
            (std::set<std::string>{"ok.test", "ok2.test"}));
  // A huge string is dropped, not stored.
  base::DictValue huge;
  base::ListValue hosts;
  hosts.Append(std::string(100000, 'a'));
  hosts.Append("fine.test");
  huge.Set("cosmetic_off", std::move(hosts));
  EXPECT_EQ(FilterListState::FromValue(huge).cosmetic_off_hosts(),
            (std::set<std::string>{"fine.test"}));
}

TEST(FilterListStateTest, TooManyCosmeticOffHostsAreCut) {
  base::DictValue dict;
  base::ListValue hosts;
  for (size_t i = 0; i < kMaxCosmeticOffHosts + 500; ++i) {
    hosts.Append("h" + std::to_string(i) + ".test");
  }
  dict.Set("cosmetic_off", std::move(hosts));
  EXPECT_EQ(FilterListState::FromValue(dict).cosmetic_off_hosts().size(),
            kMaxCosmeticOffHosts);
}

TEST(FilterListStateTest, SurvivesASaveAndLoad) {
  FilterListState state;
  std::string id;
  state.AddCustomList(GURL("https://lists.example/a.txt"), false, &id);
  FilterListEntry* entry = state.Find(id);
  entry->title = "Mine";
  entry->rules = 42;
  entry->updated = base::Time::FromSecondsSinceUnixEpoch(1700000000);
  state.Find("easylist")->error = "The list could not be downloaded.";
  state.SetException("news.test", true);
  state.SetAutoUpdate(false);
  state.set_last_update_check(base::Time::FromSecondsSinceUnixEpoch(1700000500));

  std::optional<base::Value> json =
      base::JSONReader::Read(*base::WriteJson(state.ToValue()), base::JSON_PARSE_RFC);
  ASSERT_TRUE(json && json->is_dict());
  FilterListState back = FilterListState::FromValue(json->GetDict());

  ASSERT_EQ(back.lists().size(), 3u);
  const FilterListEntry* e = back.Find(id);
  ASSERT_TRUE(e);
  EXPECT_EQ(e->url, "https://lists.example/a.txt");
  EXPECT_EQ(e->title, "Mine");
  EXPECT_EQ(e->rules, 42);
  EXPECT_EQ(e->updated, base::Time::FromSecondsSinceUnixEpoch(1700000000));
  EXPECT_FALSE(e->builtin);
  EXPECT_EQ(back.Find("easylist")->error, "The list could not be downloaded.");
  EXPECT_EQ(back.exceptions(), std::set<std::string>{"news.test"});
  EXPECT_FALSE(back.auto_update());
  EXPECT_EQ(back.last_update_check(), base::Time::FromSecondsSinceUnixEpoch(1700000500));
}

TEST(FilterListStateTest, HostileFileContentFallsBackToSafeValues) {
  std::string fine_id;
  FilterListState().AddCustomList(GURL("https://fine.example/x"), false, &fine_id);
  std::string text = R"json({
    "auto_update": "yes",
    "exceptions": ["ok.test", "bad host", 5, "../x", "OK2.test"],
    "lists": [
      {"id": "../../etc/passwd", "url": "https://a.example/x"},
      {"id": "cabc", "url": "https://wrong-id.example/x"},
      {"id": "easylist", "url": "https://evil.example/x", "builtin": false},
      {"id": "ID", "url": "http://insecure.example/x"},
      {"id": "easyprivacy", "rules": -7, "error": "<script>"},
      {"id": "FINE_ID", "url": "https://fine.example/x", "rules": -7, "title": 5,
       "error": "<script>"}
    ]
  })json";
  text.replace(text.find("FINE_ID"), 7, fine_id);
  std::optional<base::Value> json = base::JSONReader::Read(text, base::JSON_PARSE_RFC);
  ASSERT_TRUE(json && json->is_dict());
  FilterListState state = FilterListState::FromValue(json->GetDict());
  EXPECT_TRUE(state.auto_update());
  EXPECT_EQ(state.exceptions(), (std::set<std::string>{"ok.test", "ok2.test"}));
  // The two built-in lists, with their own addresses, plus one good custom list.
  ASSERT_EQ(state.lists().size(), 3u);
  EXPECT_EQ(state.Find("easylist")->url, kEasyListUrl);
  EXPECT_TRUE(state.Find("easylist")->builtin);
  EXPECT_EQ(state.Find("easyprivacy")->rules, 0);
  EXPECT_EQ(state.Find("easyprivacy")->error, "");
  const FilterListEntry* custom = state.Find(fine_id);
  ASSERT_TRUE(custom);
  EXPECT_EQ(custom->url, "https://fine.example/x");
  EXPECT_EQ(custom->rules, 0);
  EXPECT_EQ(custom->title, "fine.example");
  EXPECT_EQ(custom->error, "");  // Only known messages come back.
}

TEST(FilterListStateTest, EveryListStartsOn) {
  FilterListState state;
  for (const FilterListEntry& entry : state.lists()) {
    EXPECT_TRUE(entry.enabled) << entry.id;
  }
  EXPECT_FALSE(state.NeedsRebuild());
}

TEST(FilterListStateTest, EasyPrivacyCanBeTurnedOffAndOn) {
  FilterListState state;
  EXPECT_EQ(state.SetEnabled(kEasyPrivacyId, false),
            FilterListState::EnableResult::kOk);
  EXPECT_FALSE(state.Find(kEasyPrivacyId)->enabled);
  EXPECT_TRUE(state.NeedsRebuild());  // The shipped engine has it.
  EXPECT_EQ(state.SetEnabled(kEasyPrivacyId, true),
            FilterListState::EnableResult::kOk);
  EXPECT_TRUE(state.Find(kEasyPrivacyId)->enabled);
  EXPECT_FALSE(state.NeedsRebuild());
}

TEST(FilterListStateTest, EasyListCannotBeTurnedOff) {
  FilterListState state;
  EXPECT_EQ(state.SetEnabled(kEasyListId, false),
            FilterListState::EnableResult::kRequired);
  EXPECT_TRUE(state.Find(kEasyListId)->enabled);
  EXPECT_EQ(state.SetEnabled(kEasyListId, true),
            FilterListState::EnableResult::kOk);
  EXPECT_EQ(state.SetEnabled("nosuchlist", false),
            FilterListState::EnableResult::kUnknownList);
}

TEST(FilterListStateTest, ACustomListCanBeTurnedOffAndStaysListed) {
  FilterListState state;
  std::string id;
  ASSERT_EQ(state.AddCustomList(GURL("https://lists.example/a.txt"), false, &id),
            FilterListState::AddResult::kAdded);
  EXPECT_EQ(state.SetEnabled(id, false), FilterListState::EnableResult::kOk);
  ASSERT_TRUE(state.Find(id));
  EXPECT_FALSE(state.Find(id)->enabled);
  EXPECT_EQ(state.lists().size(), 3u);  // Off is not removed.
}

TEST(FilterListStateTest, EnabledAndIgnoredCountSurviveASaveAndLoad) {
  FilterListState state;
  state.SetEnabled(kEasyPrivacyId, false);
  state.Find(kEasyListId)->ignored_exceptions = 4;
  FilterListState loaded = FilterListState::FromValue(state.ToValue());
  EXPECT_FALSE(loaded.Find(kEasyPrivacyId)->enabled);
  EXPECT_TRUE(loaded.Find(kEasyListId)->enabled);
  EXPECT_EQ(loaded.Find(kEasyListId)->ignored_exceptions, 4);
}

TEST(FilterListStateTest, ADamagedEnabledFieldGivesOn) {
  for (const char* bad : {"\"enabled\": \"no\"", "\"enabled\": 0",
                          "\"enabled\": null", "\"enabled\": []"}) {
    std::string text = std::string(R"json({"lists": [
      {"id": "easyprivacy", "url": "x", "ignored_exceptions": -3, )json") +
                       bad + "}]}";
    std::optional<base::Value> json =
        base::JSONReader::Read(text, base::JSON_PARSE_RFC);
    ASSERT_TRUE(json && json->is_dict()) << bad;
    FilterListState state = FilterListState::FromValue(json->GetDict());
    EXPECT_TRUE(state.Find(kEasyPrivacyId)->enabled) << bad;
    EXPECT_EQ(state.Find(kEasyPrivacyId)->ignored_exceptions, 0) << bad;
  }
}

TEST(FilterListStateTest, AFileThatTurnsEasyListOffIsIgnoredForEasyList) {
  std::optional<base::Value> json = base::JSONReader::Read(
      R"json({"lists": [{"id": "easylist", "url": "x", "enabled": false},
                        {"id": "easyprivacy", "url": "x", "enabled": false}]})json",
      base::JSON_PARSE_RFC);
  ASSERT_TRUE(json && json->is_dict());
  FilterListState state = FilterListState::FromValue(json->GetDict());
  EXPECT_TRUE(state.Find(kEasyListId)->enabled);
  EXPECT_FALSE(state.Find(kEasyPrivacyId)->enabled);
}

TEST(FilterListStateTest, BlockingLevelDefaultsToAggressive) {
  FilterListState state;
  EXPECT_EQ(state.default_level(), BlockingLevel::kAggressive);
  EXPECT_TRUE(state.level_overrides().empty());
  EXPECT_EQ(FilterListState::FromValue(base::DictValue()).default_level(),
            BlockingLevel::kAggressive);
}

TEST(FilterListStateTest, LevelOverridesAreNormalizedAndCapped) {
  FilterListState state;
  EXPECT_TRUE(state.SetLevelOverride("News.Test", BlockingLevel::kStandard));
  EXPECT_EQ(state.level_overrides().at("news.test"), BlockingLevel::kStandard);
  EXPECT_FALSE(state.SetLevelOverride("not a host", BlockingLevel::kStandard));
  EXPECT_TRUE(state.SetLevelOverride("news.test", BlockingLevel::kAggressive));
  EXPECT_EQ(state.level_overrides().size(), 1u);
  EXPECT_TRUE(state.ClearLevelOverride("news.test"));
  EXPECT_TRUE(state.level_overrides().empty());
  for (size_t i = 0; i < kMaxLevelOverrides; ++i) {
    ASSERT_TRUE(state.SetLevelOverride("h" + std::to_string(i) + ".test",
                                       BlockingLevel::kStandard));
  }
  EXPECT_FALSE(state.SetLevelOverride("one-more.test", BlockingLevel::kStandard));
  EXPECT_TRUE(state.SetLevelOverride("h1.test", BlockingLevel::kAggressive));
  EXPECT_TRUE(state.ClearLevelOverride("h1.test"));  // Removing still works.
}

TEST(FilterListStateTest, FindBlockingLevelPrefersTheNearestHost) {
  std::map<std::string, BlockingLevel> levels = {
      {"news.test", BlockingLevel::kStandard},
      {"ads.news.test", BlockingLevel::kAggressive}};
  EXPECT_EQ(FindBlockingLevel("news.test", levels), BlockingLevel::kStandard);
  EXPECT_EQ(FindBlockingLevel("www.news.test", levels),
            BlockingLevel::kStandard);
  EXPECT_EQ(FindBlockingLevel("x.ads.news.test", levels),
            BlockingLevel::kAggressive);
  EXPECT_EQ(FindBlockingLevel("other.test", levels), std::nullopt);
  EXPECT_EQ(FindBlockingLevel("", levels), std::nullopt);
}

TEST(FilterListStateTest, BlockingLevelsSurviveASaveAndLoad) {
  FilterListState state;
  state.SetDefaultLevel(BlockingLevel::kStandard);
  state.SetLevelOverride("a.test", BlockingLevel::kAggressive);
  state.SetLevelOverride("b.test", BlockingLevel::kStandard);
  std::optional<base::Value> json =
      base::JSONReader::Read(*base::WriteJson(state.ToValue()), base::JSON_PARSE_RFC);
  ASSERT_TRUE(json && json->is_dict());
  FilterListState back = FilterListState::FromValue(json->GetDict());
  EXPECT_EQ(back.default_level(), BlockingLevel::kStandard);
  EXPECT_EQ(back.level_overrides(), state.level_overrides());
}

TEST(FilterListStateTest, HostileLevelContentFallsBackToSafeValues) {
  for (const char* text :
       {R"({"default_level": "loud"})", R"({"default_level": 5})",
        R"({"default_level": null})", R"({"default_level": ["standard"]})"}) {
    std::optional<base::Value> json = base::JSONReader::Read(text, base::JSON_PARSE_RFC);
    ASSERT_TRUE(json && json->is_dict()) << text;
    EXPECT_EQ(FilterListState::FromValue(json->GetDict()).default_level(),
              BlockingLevel::kAggressive)
        << text;
  }
  std::optional<base::Value> json = base::JSONReader::Read(
      R"({"levels": {"ok.test": "standard", "bad host": "standard",
          "x.test": "loud", "y.test": 5, "z.test": null, "OK2.test": "aggressive"}})",
      base::JSON_PARSE_RFC);
  ASSERT_TRUE(json && json->is_dict());
  std::map<std::string, BlockingLevel> expected = {
      {"ok.test", BlockingLevel::kStandard},
      {"ok2.test", BlockingLevel::kAggressive}};
  EXPECT_EQ(FilterListState::FromValue(json->GetDict()).level_overrides(),
            expected);
  for (const char* text : {R"({"levels": ["a.test"]})", R"({"levels": 5})"}) {
    std::optional<base::Value> bad = base::JSONReader::Read(text, base::JSON_PARSE_RFC);
    ASSERT_TRUE(bad && bad->is_dict()) << text;
    EXPECT_TRUE(FilterListState::FromValue(bad->GetDict()).level_overrides().empty());
  }
}

TEST(FilterListStateTest, TooManyLevelOverridesAreCut) {
  base::DictValue dict;
  base::DictValue levels;
  for (size_t i = 0; i < kMaxLevelOverrides + 500; ++i) {
    levels.Set("h" + std::to_string(i) + ".test", "standard");
  }
  dict.Set("levels", std::move(levels));
  EXPECT_EQ(FilterListState::FromValue(dict).level_overrides().size(),
            kMaxLevelOverrides);
}

TEST(FilterListStateTest, TheUsersOwnRulesAreAListWithNoAddress) {
  FilterListState state;
  EXPECT_EQ(nullptr, state.Find(kCustomRulesId));
  FilterListEntry& entry = state.EnsureCustomRulesEntry();
  EXPECT_EQ(kCustomRulesId, entry.id);
  EXPECT_TRUE(entry.url.empty());
  EXPECT_FALSE(entry.builtin);
  EXPECT_TRUE(entry.enabled);
  EXPECT_TRUE(IsValidListId(entry.id));
  EXPECT_TRUE(state.NeedsRebuild());
  // A second call finds the same entry.
  state.EnsureCustomRulesEntry().rules = 3;
  EXPECT_EQ(3u, state.lists().size());
  EXPECT_EQ(3, state.Find(kCustomRulesId)->rules);
  EXPECT_TRUE(state.RemoveList(kCustomRulesId));
  EXPECT_FALSE(state.NeedsRebuild());
}

TEST(FilterListStateTest, TheUsersOwnRulesDoNotUseUpACustomListSlot) {
  FilterListState state;
  state.EnsureCustomRulesEntry();
  std::string id;
  for (size_t i = 0; i < kMaxCustomLists; ++i) {
    ASSERT_EQ(state.AddCustomList(
                  GURL("https://l.example/" + std::to_string(i)), false, &id),
              FilterListState::AddResult::kAdded);
  }
  EXPECT_EQ(state.AddCustomList(GURL("https://l.example/one-more"), false, &id),
            FilterListState::AddResult::kTooMany);
}

TEST(FilterListStateTest, TheUsersOwnRulesSurviveASaveAndLoad) {
  FilterListState state;
  FilterListEntry& entry = state.EnsureCustomRulesEntry();
  entry.rules = 7;
  entry.ignored_exceptions = 1;
  entry.enabled = false;
  entry.updated = base::Time::FromSecondsSinceUnixEpoch(1700000000);
  std::optional<base::Value> json = base::JSONReader::Read(
      *base::WriteJson(state.ToValue()), base::JSON_PARSE_RFC);
  ASSERT_TRUE(json && json->is_dict());
  FilterListState back = FilterListState::FromValue(json->GetDict());
  const FilterListEntry* e = back.Find(kCustomRulesId);
  ASSERT_TRUE(e);
  EXPECT_TRUE(e->url.empty());
  EXPECT_FALSE(e->builtin);
  EXPECT_FALSE(e->enabled);
  EXPECT_EQ(7, e->rules);
  EXPECT_EQ(1, e->ignored_exceptions);
  EXPECT_EQ(base::Time::FromSecondsSinceUnixEpoch(1700000000), e->updated);
}

TEST(FilterListStateTest, AFileCannotGiveTheUsersOwnRulesAnAddressOrATitle) {
  std::optional<base::Value> json = base::JSONReader::Read(
      R"json({"lists": [{"id": "custom", "url": "https://evil.example/x",
                         "title": "Evil", "rules": 5}]})json",
      base::JSON_PARSE_RFC);
  ASSERT_TRUE(json && json->is_dict());
  EXPECT_EQ(nullptr,
            FilterListState::FromValue(json->GetDict()).Find(kCustomRulesId));

  json = base::JSONReader::Read(
      R"json({"lists": [{"id": "custom", "url": "", "title": "Evil", "rules": 5}]})json",
      base::JSON_PARSE_RFC);
  ASSERT_TRUE(json && json->is_dict());
  const FilterListEntry* e =
      FilterListState::FromValue(json->GetDict()).Find(kCustomRulesId);
  ASSERT_TRUE(e);
  EXPECT_EQ("", e->title);  // The page owns the name.
  EXPECT_EQ(5, e->rules);
}

// The catalogue (issue #105): a list is turned on by its id, and its address
// is the one the build ships.

TEST(FilterListStateTest, ACatalogueListIsAddedByIdWithTheBuildTimeAddress) {
  FilterListState state;
  EXPECT_EQ(FilterListState::CatalogueAddResult::kAdded,
            state.AddCatalogueList("fanboyannoyances"));
  const FilterListEntry* e = state.Find("fanboyannoyances");
  ASSERT_TRUE(e);
  EXPECT_EQ("https://easylist.to/easylist/fanboy-annoyance.txt", e->url);
  EXPECT_TRUE(e->catalogue);
  EXPECT_FALSE(e->builtin);
  EXPECT_TRUE(e->enabled);
  EXPECT_TRUE(e->updated.is_null());  // Not downloaded yet.
  EXPECT_TRUE(state.NeedsRebuild());
  EXPECT_EQ(FilterListState::CatalogueAddResult::kAlreadyOn,
            state.AddCatalogueList("fanboyannoyances"));
  EXPECT_EQ(3u, state.lists().size());
}

TEST(FilterListStateTest, AnUnknownIdOrARawAddressAddsNothing) {
  FilterListState state;
  for (const char* bad :
       {"nope", "", "easylist", "custom", "FanboyAnnoyances",
        "https://easylist.to/easylist/fanboy-annoyance.txt",
        "https://evil.example/a.txt", "http://127.0.0.1:8000/a.txt"}) {
    EXPECT_EQ(FilterListState::CatalogueAddResult::kUnknownId,
              state.AddCatalogueList(bad))
        << bad;
  }
  EXPECT_EQ(2u, state.lists().size());
  EXPECT_FALSE(state.NeedsRebuild());
}

TEST(FilterListStateTest, ACatalogueAddressIsNotAddedAgainAsACustomList) {
  FilterListState state;
  ASSERT_EQ(FilterListState::CatalogueAddResult::kAdded,
            state.AddCatalogueList("fanboyannoyances"));
  std::string id;
  EXPECT_EQ(FilterListState::AddResult::kDuplicate,
            state.AddCustomList(
                GURL("https://easylist.to/easylist/fanboy-annoyance.txt"),
                false, &id));
}

// What counts towards kMaxListsInUse: every list but the user's own rules.
size_t InUse(const FilterListState& state) {
  size_t n = 0;
  for (const FilterListEntry& entry : state.lists()) {
    n += entry.id != kCustomRulesId;
  }
  return n;
}

TEST(FilterListStateTest, TheNumberOfListsInUseIsCapped) {
  FilterListState state;
  std::string id;
  // Twenty custom lists and two built-in ones leave room for the rest.
  for (size_t i = 0; i < kMaxCustomLists; ++i) {
    ASSERT_EQ(state.AddCustomList(
                  GURL("https://l.example/" + std::to_string(i)), false, &id),
              FilterListState::AddResult::kAdded);
  }
  size_t added = 0;
  FilterListState::CatalogueAddResult last =
      FilterListState::CatalogueAddResult::kAdded;
  for (const CatalogueEntry& entry : Catalogue()) {
    last = state.AddCatalogueList(entry.id);
    if (last != FilterListState::CatalogueAddResult::kAdded) {
      break;
    }
    ++added;
  }
  EXPECT_EQ(kMaxListsInUse, InUse(state));
  EXPECT_EQ(kMaxListsInUse - 2 - kMaxCustomLists, added);
  EXPECT_EQ(FilterListState::CatalogueAddResult::kTooMany, last);
  // And the other door is shut too.
  EXPECT_EQ(FilterListState::AddResult::kTooMany,
            state.AddCustomList(GURL("https://l.example/one-more"), false,
                                &id));
  // The user's own rules never use up a place.
  state.EnsureCustomRulesEntry();
  EXPECT_EQ(kMaxListsInUse, InUse(state));
}

TEST(FilterListStateTest, TheTotalSizeOfTheListsInUseIsCapped) {
  FilterListState state;
  ASSERT_EQ(FilterListState::CatalogueAddResult::kAdded,
            state.AddCatalogueList("fanboyannoyances"));
  ASSERT_EQ(FilterListState::CatalogueAddResult::kAdded,
            state.AddCatalogueList("uboprivacy"));
  state.Find("fanboyannoyances")->bytes =
      static_cast<int>(kMaxTotalListBytes - 1000);
  // A new copy of the same list replaces the old one: only the difference
  // counts.
  EXPECT_TRUE(state.FitsByteBudget("fanboyannoyances", kMaxListBytes));
  EXPECT_TRUE(state.FitsByteBudget("uboprivacy", 1000));
  EXPECT_FALSE(state.FitsByteBudget("uboprivacy", 1001));
  // A list that is off is not in use.
  state.Find("fanboyannoyances")->enabled = false;
  EXPECT_TRUE(state.FitsByteBudget("uboprivacy", kMaxListBytes));
}

TEST(FilterListStateTest, ACatalogueListSurvivesASaveAndLoad) {
  FilterListState state;
  ASSERT_EQ(FilterListState::CatalogueAddResult::kAdded,
            state.AddCatalogueList("easylistcookie"));
  FilterListEntry* e = state.Find("easylistcookie");
  e->rules = 12;
  e->bytes = 3456;
  e->updated = base::Time::FromSecondsSinceUnixEpoch(1700000000);
  std::optional<base::Value> json = base::JSONReader::Read(
      *base::WriteJson(state.ToValue()), base::JSON_PARSE_RFC);
  ASSERT_TRUE(json && json->is_dict());
  FilterListState back = FilterListState::FromValue(json->GetDict());
  const FilterListEntry* b = back.Find("easylistcookie");
  ASSERT_TRUE(b);
  EXPECT_TRUE(b->catalogue);
  EXPECT_EQ("https://secure.fanboy.co.nz/fanboy-cookiemonster.txt", b->url);
  EXPECT_EQ(12, b->rules);
  EXPECT_EQ(3456, b->bytes);
  EXPECT_EQ(base::Time::FromSecondsSinceUnixEpoch(1700000000), b->updated);
}

TEST(FilterListStateTest, AFileCannotChangeTheAddressOfACatalogueList) {
  std::optional<base::Value> json = base::JSONReader::Read(
      R"json({"lists": [
        {"id": "fanboyannoyances", "catalogue": true,
         "url": "https://evil.example/steal.txt", "title": "Evil"}]})json",
      base::JSON_PARSE_RFC);
  ASSERT_TRUE(json && json->is_dict());
  FilterListState state = FilterListState::FromValue(json->GetDict());
  const FilterListEntry* e = state.Find("fanboyannoyances");
  ASSERT_TRUE(e);
  EXPECT_EQ("https://easylist.to/easylist/fanboy-annoyance.txt", e->url);
  EXPECT_NE("Evil", e->title);
}

TEST(FilterListStateTest, AFileCannotMakeAListUpOutOfTheCatalogue) {
  // An id the build does not have, even with a good address: dropped. So is a
  // list that claims to be from the catalogue under an id of its own making,
  // and a custom list that claims an id of the catalogue.
  std::optional<base::Value> json = base::JSONReader::Read(
      R"json({"lists": [
        {"id": "madeup", "catalogue": true, "url": "https://lists.example/a.txt"},
        {"id": "fanboyannoyances", "url": "https://evil.example/a.txt"}]})json",
      base::JSON_PARSE_RFC);
  ASSERT_TRUE(json && json->is_dict());
  FilterListState state = FilterListState::FromValue(json->GetDict());
  EXPECT_EQ(nullptr, state.Find("madeup"));
  EXPECT_EQ(nullptr, state.Find("fanboyannoyances"));
  EXPECT_EQ(2u, state.lists().size());
}

TEST(FilterListStateTest, TooManySavedListsAreCutAtTheCap) {
  base::ListValue lists;
  for (const CatalogueEntry& entry : Catalogue()) {
    base::DictValue item;
    item.Set("id", std::string(entry.id));
    item.Set("catalogue", true);
    item.Set("url", std::string(entry.url));
    lists.Append(std::move(item));
  }
  for (size_t i = 0; i < kMaxCustomLists; ++i) {
    GURL url("https://l.example/" + std::to_string(i));
    FilterListState one;
    std::string id;
    ASSERT_EQ(FilterListState::AddResult::kAdded,
              one.AddCustomList(url, false, &id));
    base::DictValue item;
    item.Set("id", id);
    item.Set("url", url.spec());
    lists.Append(std::move(item));
  }
  base::DictValue dict;
  dict.Set("lists", std::move(lists));
  EXPECT_LE(InUse(FilterListState::FromValue(dict)), kMaxListsInUse);
}

TEST(FilterListStateTest, ADamagedSizeFallsBackToZero) {
  std::optional<base::Value> json = base::JSONReader::Read(
      R"json({"lists": [{"id": "easylist", "url": "x", "bytes": -5},
                        {"id": "easyprivacy", "url": "x", "bytes": 999999999}]})json",
      base::JSON_PARSE_RFC);
  ASSERT_TRUE(json && json->is_dict());
  FilterListState state = FilterListState::FromValue(json->GetDict());
  EXPECT_EQ(0, state.Find(kEasyListId)->bytes);
  EXPECT_EQ(0, state.Find(kEasyPrivacyId)->bytes);
}

TEST(FilterListStateTest, GarbageGivesDefaults) {
  FilterListState state = FilterListState::FromValue(base::DictValue());
  EXPECT_EQ(state.lists().size(), 2u);
  EXPECT_TRUE(state.Find(kEasyListId)->enabled);
  EXPECT_TRUE(state.Find(kEasyPrivacyId)->enabled);
  EXPECT_TRUE(state.auto_update());
}

}  // namespace
}  // namespace aerie_adblock
