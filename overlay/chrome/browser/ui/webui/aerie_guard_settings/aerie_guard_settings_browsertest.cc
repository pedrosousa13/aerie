// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// The catalogue door of the Aerie Guard section of Settings (issue #105): the
// handler takes a catalogue id and nothing else, the rows reach the page
// grouped by category, and every word on them is a string of the grd. The
// tests drive the handler the way the page does, with sendWithPromise from the
// settings page itself.

#include <optional>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/test/run_until.h"
#include "base/values.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/chrome_test_utils.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_adblock/browser/catalogue.h"
#include "components/aerie_adblock/browser/filter_list_manager.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
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
    std::optional<base::Value> parsed = base::JSONReader::Read(text);
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

}  // namespace
