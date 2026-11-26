// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// The catalogue door of the Aerie Guard section of Settings (issue #105): the
// handler takes a catalogue id and nothing else, the rows reach the page
// grouped by category, and every word on them is a string of the grd. The
// tests drive the handler the way the page does, with sendWithPromise from the
// settings page itself.

#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/aerie_guard_settings/aerie_list_source_ui.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/test/base/chrome_test_utils.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_adblock/browser/aerie_adblock_switches.h"
#include "components/aerie_adblock/browser/aerie_guard_prefs.h"
#include "components/aerie_adblock/browser/catalogue.h"
#include "components/aerie_adblock/browser/filter_list_manager.h"
#include "components/aerie_adblock/browser/filter_list_storage.h"
#include "components/language/core/browser/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "url/gurl.h"

namespace {

using aerie_adblock::FilterListManager;

constexpr char kSettingsUrl[] = "chrome://settings/aerieGuard";

// Counts how often the manager says its lists changed. A turned-on list, a
// started download and a removed list all do, so a refused message must leave
// the count at zero.
class ChangeCounter : public FilterListManager::Observer {
 public:
  void OnFilterListsChanged() override { ++count_; }
  int count() const { return count_; }

 private:
  int count_ = 0;
};

class AerieGuardSettingsBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(kSettingsUrl)));
    manager_ = FilterListManager::Get();
    ASSERT_TRUE(manager_);
    // The state file is read first; until then the manager is busy.
    ASSERT_TRUE(base::test::RunUntil([&] { return !manager_->busy(); }));
    // The page's own module is loaded before the first call.
    ASSERT_TRUE(content::EvalJs(web_contents(),
                                "typeof chrome !== 'undefined' && "
                                "typeof chrome.send === 'function'")
                    .ExtractBool());
  }

  content::WebContents* web_contents() {
    return chrome_test_utils::GetActiveWebContents(this);
  }

  // One aerieGuard.<name> message with |args_json| as its arguments. Returns
  // {ok: bool, state or error} as a dictionary.
  base::DictValue Call(const std::string& name, const std::string& args_json) {
    const std::string script = R"((async () => {
      const {sendWithPromise} = await import('chrome://resources/js/cr.js');
      try {
        return JSON.stringify(
            {ok: true, state: await sendWithPromise('aerieGuard.)" +
                               name + R"(', ...)" + args_json + R"()});
      } catch (e) {
        return JSON.stringify({ok: false, error: String(e)});
      }
    })())";
    std::string text = content::EvalJs(web_contents(), script).ExtractString();
    std::optional<base::Value> parsed = base::JSONReader::Read(text, base::JSON_PARSE_RFC);
    EXPECT_TRUE(parsed && parsed->is_dict()) << text;
    return parsed && parsed->is_dict() ? std::move(parsed->GetDict())
                                       : base::DictValue();
  }

  bool Refused(const std::string& args_json) {
    base::DictValue answer = Call("setCatalogueList", args_json);
    return answer.FindBool("ok") == false;
  }

  raw_ptr<FilterListManager> manager_ = nullptr;
};

IN_PROC_BROWSER_TEST_F(AerieGuardSettingsBrowserTest,
                       TheCatalogueReachesThePageGroupedByCategoryAndAllOff) {
  base::DictValue answer = Call("getState", "[]");
  ASSERT_EQ(true, answer.FindBool("ok"));
  const base::DictValue* state = answer.FindDict("state");
  ASSERT_TRUE(state);
  const base::ListValue* groups = state->FindList("catalogue");
  ASSERT_TRUE(groups);

  std::vector<std::string> categories;
  size_t rows = 0;
  for (const base::Value& group : *groups) {
    ASSERT_TRUE(group.is_dict());
    const std::string* category = group.GetDict().FindString("category");
    ASSERT_TRUE(category);
    categories.push_back(*category);
    const base::ListValue* lists = group.GetDict().FindList("lists");
    ASSERT_TRUE(lists);
    ASSERT_FALSE(lists->empty());
    for (const base::Value& row : *lists) {
      const base::DictValue& item = row.GetDict();
      const std::string* id = item.FindString("id");
      ASSERT_TRUE(id);
      const aerie_adblock::CatalogueEntry* entry =
          aerie_adblock::FindCatalogueEntry(*id);
      ASSERT_TRUE(entry) << *id;
      EXPECT_EQ(*category, entry->category);
      EXPECT_EQ(false, item.FindBool("on")) << *id;  // Nothing by default.
      ++rows;
      // The page never learns the download address: only the website.
      EXPECT_FALSE(item.contains("url")) << *id;
      EXPECT_TRUE(item.FindString("homepage"));
      EXPECT_TRUE(item.FindString("licence"));
    }
  }
  EXPECT_EQ((std::vector<std::string>{"annoyances", "cookies", "social",
                                      "privacy", "regional"}),
            categories);
  EXPECT_EQ(aerie_adblock::Catalogue().size(), rows);
  // And no catalogue list is among the user's lists.
  const base::ListValue* lists = state->FindList("lists");
  ASSERT_TRUE(lists);
  for (const base::Value& list : *lists) {
    EXPECT_FALSE(aerie_adblock::FindCatalogueEntry(
        *list.GetDict().FindString("id")));
  }
}

IN_PROC_BROWSER_TEST_F(AerieGuardSettingsBrowserTest,
                       AnUnknownIdOrARawAddressIsRefusedAndNoFetchStarts) {
  ChangeCounter counter;
  manager_->AddObserver(&counter);
  const size_t lists_before = manager_->state().lists().size();

  for (const char* args :
       {R"(["notacataloguelist", true])", R"(["notacataloguelist", false])",
        R"(["FANBOYANNOYANCES", true])", R"(["", true])",
        R"(["https://easylist.to/easylist/fanboy-annoyance.txt", true])",
        R"(["https://evil.example/list.txt", true])",
        R"(["http://127.0.0.1:8000/list.txt", true])",
        R"(["easylist", false])", R"(["custom", true])",
        // Not an id at all.
        R"([123, true])", R"(["fanboyannoyances", "yes"])",
        R"(["fanboyannoyances"])"}) {
    EXPECT_TRUE(Refused(args)) << args;
  }
  // 5000 characters is over what the handler reads at all.
  EXPECT_TRUE(Refused(R"(["a".repeat(5000), true])"));

  EXPECT_EQ(0, counter.count());  // Nothing was added, removed or started.
  EXPECT_FALSE(manager_->busy());
  EXPECT_EQ(lists_before, manager_->state().lists().size());
  manager_->RemoveObserver(&counter);
}

IN_PROC_BROWSER_TEST_F(AerieGuardSettingsBrowserTest,
                       EveryNameAndDescriptionOfTheCatalogueIsAGrdString) {
  std::string ids = "[";
  for (const aerie_adblock::CatalogueEntry& entry :
       aerie_adblock::Catalogue()) {
    ids += "\"" + std::string(entry.id) + "\",";
  }
  ids += "]";
  const std::string script = R"((async () => {
    const {loadTimeData} =
        await import('chrome://resources/js/load_time_data.js');
    const missing = [];
    for (const id of )" + ids + R"() {
      for (const prefix of ['aerieGuardCatTitle_', 'aerieGuardCatDesc_']) {
        const key = prefix + id;
        if (!loadTimeData.valueExists(key) ||
            loadTimeData.getString(key).length < 3 ||
            loadTimeData.getString(key) === id) {
          missing.push(key);
        }
      }
    }
    for (const key of ['aerieGuardCatalogueHeading', 'aerieGuardCatalogueDesc',
        'aerieGuardCatalogueSearchLabel', 'aerieGuardCatalogueNoResults',
        'aerieGuardCatalogueCategoryAnnoyances',
        'aerieGuardCatalogueCategoryCookies',
        'aerieGuardCatalogueCategorySocial',
        'aerieGuardCatalogueCategoryPrivacy',
        'aerieGuardCatalogueCategoryRegional', 'aerieGuardCatalogueLicence',
        'aerieGuardCatalogueHomepage', 'aerieGuardCatalogueToggleAria',
        'aerieGuardListErrorTooMany']) {
      if (!loadTimeData.valueExists(key)) {
        missing.push(key);
      }
    }
    return JSON.stringify(missing);
  })())";
  EXPECT_EQ("[]", content::EvalJs(web_contents(), script).ExtractString());
}

// The regional suggestions (issue #106): the handler names the catalogue lists
// that fit the browser's languages and whether the one-time card was
// dismissed. It turns nothing on and starts no download.
std::vector<std::string> Strings(const base::ListValue* list) {
  std::vector<std::string> out;
  if (list) {
    for (const base::Value& v : *list) {
      out.push_back(v.GetString());
    }
  }
  return out;
}

IN_PROC_BROWSER_TEST_F(AerieGuardSettingsBrowserTest,
                       TheSuggestionsFollowTheLanguagesAndTurnNothingOn) {
  ChangeCounter counter;
  manager_->AddObserver(&counter);
  const size_t lists_before = manager_->state().lists().size();

  chrome_test_utils::GetProfile(this)->GetPrefs()->SetString(
      language::prefs::kAcceptLanguages, "de,en-US;q=0.8");
  base::DictValue answer = Call("getState", "[]");
  ASSERT_EQ(true, answer.FindBool("ok"));
  const base::DictValue* state = answer.FindDict("state");
  ASSERT_TRUE(state);
  std::vector<std::string> suggested = Strings(state->FindList("suggested"));
  EXPECT_NE(suggested.end(),
            std::find(suggested.begin(), suggested.end(), "easylistgermany"));
  for (const std::string& id : suggested) {
    const aerie_adblock::CatalogueEntry* entry =
        aerie_adblock::FindCatalogueEntry(id);
    ASSERT_TRUE(entry) << id;
    EXPECT_FALSE(entry->langs.empty()) << id;
  }
  EXPECT_EQ(false, state->FindBool("languageCardDismissed"));

  // Receiving the suggestions changed nothing: no list is on, the manager did
  // not start a download and nothing told it to change.
  for (const base::Value& group : *state->FindList("catalogue")) {
    for (const base::Value& row : *group.GetDict().FindList("lists")) {
      EXPECT_EQ(false, row.GetDict().FindBool("on"));
      EXPECT_EQ("idle", *row.GetDict().FindString("status"));
    }
  }
  EXPECT_EQ(0, counter.count());
  EXPECT_FALSE(manager_->busy());
  EXPECT_EQ(lists_before, manager_->state().lists().size());
  manager_->RemoveObserver(&counter);

  // A profile with English only has no regional suggestion.
  chrome_test_utils::GetProfile(this)->GetPrefs()->SetString(
      language::prefs::kAcceptLanguages, "en-US,en");
  answer = Call("getState", "[]");
  EXPECT_TRUE(Strings(answer.FindDict("state")->FindList("suggested")).empty());
}

IN_PROC_BROWSER_TEST_F(AerieGuardSettingsBrowserTest,
                       ADismissedCardStaysDismissedWhenThePageIsOpenedAgain) {
  PrefService* prefs = chrome_test_utils::GetProfile(this)->GetPrefs();
  EXPECT_FALSE(prefs->GetBoolean(aerie_guard::kLanguageCardDismissed));
  EXPECT_EQ(false, Call("getState", "[]")
                       .FindDict("state")
                       ->FindBool("languageCardDismissed"));

  base::DictValue answer = Call("dismissLanguageCard", "[]");
  ASSERT_EQ(true, answer.FindBool("ok"));
  EXPECT_EQ(true, answer.FindDict("state")->FindBool("languageCardDismissed"));
  EXPECT_TRUE(prefs->GetBoolean(aerie_guard::kLanguageCardDismissed));

  // A new page makes a new handler. It reads the profile, not a field.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(kSettingsUrl)));
  ASSERT_TRUE(content::EvalJs(web_contents(),
                              "typeof chrome.send === 'function'")
                  .ExtractBool());
  EXPECT_EQ(true, Call("getState", "[]")
                      .FindDict("state")
                      ->FindBool("languageCardDismissed"));

  // Dismissing turned no list on.
  for (const aerie_adblock::CatalogueEntry& entry :
       aerie_adblock::Catalogue()) {
    const aerie_adblock::FilterListEntry* list =
        manager_->state().Find(entry.id);
    EXPECT_FALSE(list && list->enabled) << entry.id;
  }
}

IN_PROC_BROWSER_TEST_F(AerieGuardSettingsBrowserTest,
                       EveryWordOfTheSuggestionsIsAGrdString) {
  const std::string script = R"((async () => {
    const {loadTimeData} =
        await import('chrome://resources/js/load_time_data.js');
    const missing = [];
    for (const key of ['aerieGuardSuggestedHeading', 'aerieGuardSuggestedDesc',
        'aerieGuardLanguageCardTitle', 'aerieGuardLanguageCardBody',
        'aerieGuardLanguageCardDismiss', 'aerieGuardLanguageCardReview']) {
      if (!loadTimeData.valueExists(key)) {
        missing.push(key);
      }
    }
    return JSON.stringify(missing);
  })())";
  EXPECT_EQ("[]", content::EvalJs(web_contents(), script).ExtractString());
}

// Custom list subscriptions (issue #108). A list added by address is shown with
// its title, can be updated, unsubscribed and viewed. The list is untrusted:
// its source is opened as text/plain and never as a page.
constexpr char kListPath[] = "/subscription.txt";
// A list that tries everything a list could to run: markup in the title, a
// script element, an event handler and a rule. As text/plain none of it runs.
constexpr char kHostileList[] =
    "! Title: <b>Hostile</b> list\n"
    "<script>window.__aerieListRan = 1;</script>\n"
    "<img src=x onerror=\"window.__aerieListRan = 2\">\n"
    "||ads.subscription.example^\n";

class AerieListSubscriptionBrowserTest : public AerieGuardSettingsBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    AerieGuardSettingsBrowserTest::SetUpCommandLine(command_line);
    // The fixture server has no TLS: http for loopback only.
    command_line->AppendSwitch(
        aerie_adblock::switches::kAllowLoopbackFilterLists);
  }

  void SetUpOnMainThread() override {
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
        &AerieListSubscriptionBrowserTest::ServeList, base::Unretained(this)));
    ASSERT_TRUE(embedded_test_server()->Start());
    AerieGuardSettingsBrowserTest::SetUpOnMainThread();
  }

  std::unique_ptr<net::test_server::HttpResponse> ServeList(
      const net::test_server::HttpRequest& request) {
    if (request.GetURL().path() != kListPath) {
      return nullptr;
    }
    ++list_requests_;
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    response->set_content_type("text/plain");
    response->set_content(kHostileList);
    return response;
  }

  std::string ListUrl() {
    return embedded_test_server()->GetURL(kListPath).spec();
  }

  // The state's list that has |url|, or null.
  const base::DictValue* FindList(const base::DictValue& state,
                                  const std::string& url) {
    const base::ListValue* lists = state.FindList("lists");
    if (!lists) {
      return nullptr;
    }
    for (const base::Value& list : *lists) {
      const std::string* list_url = list.GetDict().FindString("url");
      if (list_url && *list_url == url) {
        return &list.GetDict();
      }
    }
    return nullptr;
  }

  // Adds the fixture list and waits until it is downloaded.
  std::string AddFixtureList() {
    base::DictValue answer = Call("addList", "[\"" + ListUrl() + "\"]");
    EXPECT_EQ(true, answer.FindBool("ok"));
    EXPECT_TRUE(base::test::RunUntil([&] { return !manager_->busy(); }));
    const aerie_adblock::FilterListEntry* entry = nullptr;
    for (const aerie_adblock::FilterListEntry& list :
         manager_->state().lists()) {
      if (list.url == ListUrl()) {
        entry = &list;
      }
    }
    EXPECT_TRUE(entry);
    return entry ? entry->id : std::string();
  }

  base::FilePath ListFile(const std::string& id) {
    base::FilePath dir;
    EXPECT_TRUE(base::PathService::Get(chrome::DIR_USER_DATA, &dir));
    return aerie_adblock::filter_list_storage::ListPath(
        dir.AppendASCII("AerieFilterLists"), id);
  }

  std::atomic<int> list_requests_{0};
};

IN_PROC_BROWSER_TEST_F(AerieListSubscriptionBrowserTest,
                       AddListRefusesAnythingButHttps) {
  const size_t lists_before = manager_->state().lists().size();
  for (const char* url :
       {"http://lists.example/x.txt", "ftp://lists.example/x.txt",
        "file:///etc/passwd", "javascript:alert(1)",
        "data:text/plain,||ads.example^", "lists.example/x.txt", ""}) {
    base::DictValue answer =
        Call("addList", std::string("[\"") + url + "\"]");
    ASSERT_EQ(true, answer.FindBool("ok")) << url;
    EXPECT_EQ("insecure_url",
              *answer.FindDict("state")->FindString("addErrorCode"))
        << url;
  }
  // An https address on the user's own network is refused for its own reason.
  base::DictValue answer = Call("addList", "[\"https://192.168.1.1/x.txt\"]");
  EXPECT_EQ("non_public_url",
            *answer.FindDict("state")->FindString("addErrorCode"));
  // A message that is not an address at all is rejected.
  EXPECT_EQ(false, Call("addList", "[123]").FindBool("ok"));
  EXPECT_EQ(false, Call("addList", "[]").FindBool("ok"));
  EXPECT_EQ(lists_before, manager_->state().lists().size());
  EXPECT_EQ(0, list_requests_.load());
}

IN_PROC_BROWSER_TEST_F(AerieListSubscriptionBrowserTest,
                       AddListRefusesMoreThanTwentyCustomLists) {
  std::vector<std::string> urls;
  for (size_t i = 0; i < aerie_adblock::kMaxCustomLists; ++i) {
    urls.push_back("https://lists.example/" + base::NumberToString(i) +
                   ".txt");
    base::DictValue answer = Call("addList", "[\"" + urls.back() + "\"]");
    ASSERT_EQ(true, answer.FindBool("ok"));
    EXPECT_EQ("", *answer.FindDict("state")->FindString("addErrorCode"))
        << i;
  }
  base::DictValue answer =
      Call("addList", "[\"https://lists.example/one-too-many.txt\"]");
  EXPECT_EQ("too_many", *answer.FindDict("state")->FindString("addErrorCode"));
  EXPECT_EQ(nullptr, FindList(*answer.FindDict("state"),
                              "https://lists.example/one-too-many.txt"));
  // The same address twice is its own error.
  answer = Call("addList", "[\"" + urls[0] + "\"]");
  EXPECT_NE("", *answer.FindDict("state")->FindString("addErrorCode"));
  // Removing one makes room.
  for (const aerie_adblock::FilterListEntry& list :
       manager_->state().lists()) {
    if (list.url == urls[0]) {
      EXPECT_EQ(true, Call("removeList", "[\"" + list.id + "\"]")
                          .FindBool("ok"));
      break;
    }
  }
  answer = Call("addList", "[\"https://lists.example/now-fits.txt\"]");
  EXPECT_EQ("", *answer.FindDict("state")->FindString("addErrorCode"));
}

IN_PROC_BROWSER_TEST_F(AerieListSubscriptionBrowserTest,
                       TheRowShowsTheTitleOfTheHeaderAndWhenItWasUpdated) {
  const std::string id = AddFixtureList();
  base::DictValue answer = Call("getState", "[]");
  const base::DictValue* list =
      FindList(*answer.FindDict("state"), ListUrl());
  ASSERT_TRUE(list);
  // The title is text, with its markup kept: the page shows it as text.
  EXPECT_EQ("<b>Hostile</b> list", *list->FindString("title"));
  EXPECT_TRUE(list->Find("updated")->is_double());
  EXPECT_EQ("", *list->FindString("errorCode"));
  EXPECT_EQ(false, list->FindBool("builtin"));
}

IN_PROC_BROWSER_TEST_F(AerieListSubscriptionBrowserTest,
                       UpdateNowFetchesTheListAgainByItsId) {
  const std::string id = AddFixtureList();
  const int after_add = list_requests_.load();
  ASSERT_GE(after_add, 1);

  base::DictValue answer = Call("updateList", "[\"" + id + "\"]");
  ASSERT_EQ(true, answer.FindBool("ok"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !manager_->busy() && list_requests_.load() > after_add;
  }));
  EXPECT_EQ(after_add + 1, list_requests_.load());

  // An id the state does not have starts no request.
  const int before = list_requests_.load();
  Call("updateList", "[\"nosuchlist\"]");
  Call("updateList", "[\"" + ListUrl() + "\"]");
  EXPECT_FALSE(manager_->busy());
  EXPECT_EQ(before, list_requests_.load());
}

IN_PROC_BROWSER_TEST_F(AerieListSubscriptionBrowserTest,
                       UnsubscribeRemovesTheEntryAndItsFile) {
  const std::string id = AddFixtureList();
  base::ScopedAllowBlockingForTesting allow_blocking;
  ASSERT_TRUE(base::PathExists(ListFile(id)));

  base::DictValue answer = Call("removeList", "[\"" + id + "\"]");
  ASSERT_EQ(true, answer.FindBool("ok"));
  EXPECT_EQ(nullptr, FindList(*answer.FindDict("state"), ListUrl()));
  EXPECT_FALSE(manager_->state().Find(id));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return !base::PathExists(ListFile(id)); }));
}

IN_PROC_BROWSER_TEST_F(AerieListSubscriptionBrowserTest,
                       ViewSourceIsAddressedByListIdAndAnUnknownIdIsRefused) {
  const std::string id = AddFixtureList();
  content::WebContents* settings = web_contents();
  const int tabs_before = browser()->tab_strip_model()->count();

  // Not an id of a list the user added: an address, an unknown id, the
  // built-in lists, the user's own rules, a path out of the list directory and
  // things that are not ids.
  for (const std::string& bad :
       {ListUrl(), std::string("nosuchlist"), std::string("easylist"),
        std::string("easyprivacy"), std::string("custom"),
        std::string("../state"), std::string("chrome://settings"),
        std::string("")}) {
    EXPECT_EQ(false, Call("viewSource", "[\"" + bad + "\"]").FindBool("ok"))
        << bad;
  }
  EXPECT_EQ(false, Call("viewSource", "[123]").FindBool("ok"));
  EXPECT_EQ(false, Call("viewSource", "[]").FindBool("ok"));
  EXPECT_EQ(false, Call("viewSource", "[\"" + id + "\", \"extra\"]")
                       .FindBool("ok"));
  content::RunAllTasksUntilIdle();
  EXPECT_EQ(tabs_before, browser()->tab_strip_model()->count());

  // The list's id opens the source page in a new tab, and the page is the
  // chrome:// page for that id, never the list's address.
  EXPECT_EQ(true, Call("viewSource", "[\"" + id + "\"]").FindBool("ok"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return browser()->tab_strip_model()->count() == tabs_before + 1;
  }));
  content::WebContents* source =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_NE(source, settings);
  EXPECT_EQ(aerie_guard::ListSourceUrl(id), source->GetVisibleURL());
  EXPECT_NE(ListUrl(), source->GetVisibleURL().spec());
}

// The saved text is shown as text and nothing else: the document is
// text/plain, no element of the list is in the page, nothing ran.
IN_PROC_BROWSER_TEST_F(AerieListSubscriptionBrowserTest,
                       TheSourcePageShowsTheListAsPlainText) {
  const std::string id = AddFixtureList();
  const int requests = list_requests_.load();

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           aerie_guard::ListSourceUrl(id)));
  content::WebContents* page = web_contents();
  EXPECT_EQ("text/plain", content::EvalJs(page, "document.contentType"));
  EXPECT_EQ("", content::EvalJs(page, "window.__aerieListRan === undefined ? "
                                      "'' : String(window.__aerieListRan)"));
  // No script, image or bold element of the list: the list's markup is text.
  EXPECT_EQ(0, content::EvalJs(
                   page, "document.querySelectorAll('script, img, b').length"));
  const std::string shown =
      content::EvalJs(page, "document.body.innerText").ExtractString();
  EXPECT_NE(std::string::npos, shown.find("<script>window.__aerieListRan = 1;"));
  EXPECT_NE(std::string::npos, shown.find("<img src=x onerror="));
  EXPECT_NE(std::string::npos, shown.find("||ads.subscription.example^"));
  // Reading the source asked the list's server for nothing.
  EXPECT_EQ(requests, list_requests_.load());
}

IN_PROC_BROWSER_TEST_F(AerieListSubscriptionBrowserTest,
                       TheSourcePageHasNothingForAListThatIsNotTheUsers) {
  AddFixtureList();
  for (const char* id : {"easylist", "easyprivacy", "custom", "nosuchlist"}) {
    // The page is empty: no text of a built-in list, the user's own rules or
    // any file is served (EasyList alone has thousands of "||" rules).
    ui_test_utils::NavigateToURL(browser(), aerie_guard::ListSourceUrl(id));
    const std::string shown =
        content::EvalJs(web_contents(), "document.body.innerText")
            .ExtractString();
    EXPECT_EQ(std::string::npos, shown.find("||")) << id;
    EXPECT_EQ(std::string::npos, shown.find("hostile")) << id;
  }
}

// A web page cannot navigate to the source page, link to it or frame it.
IN_PROC_BROWSER_TEST_F(AerieListSubscriptionBrowserTest,
                       AWebPageCannotReachTheSourcePage) {
  const std::string id = AddFixtureList();
  const GURL source = aerie_guard::ListSourceUrl(id);
  const GURL page = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page));

  EXPECT_TRUE(content::ExecJs(
      web_contents(), "location.href = '" + source.spec() + "';"));
  EXPECT_TRUE(content::ExecJs(
      web_contents(),
      "const a = document.createElement('a'); a.href = '" + source.spec() +
          "'; document.body.appendChild(a); a.click();"));
  EXPECT_TRUE(content::ExecJs(
      web_contents(),
      "const f = document.createElement('iframe'); f.src = '" + source.spec() +
          "'; document.body.appendChild(f);"));
  content::RunAllTasksUntilIdle();
  EXPECT_EQ(page, web_contents()->GetLastCommittedURL());
  web_contents()->GetPrimaryMainFrame()->ForEachRenderFrameHost(
      [&](content::RenderFrameHost* frame) {
        EXPECT_NE(aerie_guard::kListSourceHost,
                  frame->GetLastCommittedURL().host());
      });
}

IN_PROC_BROWSER_TEST_F(AerieListSubscriptionBrowserTest,
                       EveryWordOfTheSubscriptionFlowIsAGrdString) {
  const std::string script = R"((async () => {
    const {loadTimeData} =
        await import('chrome://resources/js/load_time_data.js');
    const missing = [];
    for (const key of ['aerieGuardSubscriptionsHeading',
        'aerieGuardSubscriptionsDesc', 'aerieGuardAddListLabel',
        'aerieGuardAddListPlaceholder', 'aerieGuardAddListButton',
        'aerieGuardAddListIpWarning', 'aerieGuardListErrorInsecureUrl',
        'aerieGuardListErrorNonPublicUrl',
        'aerieGuardAddListErrorDuplicate', 'aerieGuardAddListErrorTooMany',
        'aerieGuardListViewSource', 'aerieGuardListViewSourceAria',
        'aerieGuardListUnsubscribe', 'aerieGuardListUnsubscribeAria']) {
      if (!loadTimeData.valueExists(key) ||
          loadTimeData.getString(key).length < 3) {
        missing.push(key);
      }
    }
    return JSON.stringify(missing);
  })())";
  EXPECT_EQ("[]", content::EvalJs(web_contents(), script).ExtractString());
}

}  // namespace
