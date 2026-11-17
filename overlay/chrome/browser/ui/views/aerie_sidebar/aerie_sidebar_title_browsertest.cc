// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of the tidy and the rename rules (issue #146): the sidebar
// shows the tidied or renamed title and a tooltip with the original, the page's
// real document.title never changes, the precedence (manual name, rule, tidy,
// page) holds with the switches and with the order of the rules, the "Make a
// rename rule" item is in the menu, and the settings survive a restart (the
// PRE_ test is the first run of a pair).

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_context_menu.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_tab_model/rename_rules.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_prefs.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace {

using aerie_tab_model::RenameRule;
using aerie_tab_model::RewriteMode;
using aerie_tab_model::RuleType;

template <typename T>
T* FindDescendant(views::View* root) {
  if (auto* match = views::AsViewClass<T>(root)) {
    return match;
  }
  for (views::View* child : root->children()) {
    if (T* found = FindDescendant<T>(child)) {
      return found;
    }
  }
  return nullptr;
}

constexpr char kMailTitle[] = "Inbox (3) - Mail";
constexpr char kPlainTitle[] = "Just a plain title";

std::unique_ptr<net::test_server::HttpResponse> ServeTitles(
    const net::test_server::HttpRequest& request) {
  std::string title;
  if (request.relative_url == "/mail.html") {
    title = kMailTitle;
  } else if (request.relative_url == "/plain.html") {
    title = kPlainTitle;
  } else {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  response->set_content("<!doctype html><title>" + title + "</title><p>x</p>");
  return response;
}

RenameRule MakeRule(const std::string& id,
                    const std::string& name,
                    RuleType type,
                    const std::string& value,
                    const std::string& text) {
  RenameRule rule;
  rule.id = id;
  rule.name = name;
  rule.match.id = id;
  rule.match.type = type;
  rule.match.value = value;
  rule.mode = RewriteMode::kTemplate;
  rule.text = text;
  return rule;
}

}  // namespace

class AerieSidebarTitleBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    // The test server is plain HTTP; HTTPS-only mode is on by default.
    prefs()->SetBoolean(prefs::kHttpsOnlyModeEnabled, false);
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&ServeTitles));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  AerieSidebarView* sidebar() {
    return FindDescendant<AerieSidebarView>(
        BrowserView::GetBrowserViewForBrowser(browser())
            ->vertical_tab_strip_region_view_for_testing());
  }
  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }
  aerie_tab_model::TabModelService* service() {
    return aerie_tab_model::TabModelServiceFactory::GetForProfile(
        browser()->GetProfile());
  }
  TabStripModel* strip() { return browser()->tab_strip_model(); }

  // Opens `path` of host `host` in a new tab and returns its row once it
  // exists.
  AerieSidebarRowView* OpenRow(const std::string& host,
                               const std::string& path,
                               std::string* id = nullptr) {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), embedded_test_server()->GetURL(host, path),
        WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    tabs::TabInterface* tab = strip()->GetTabAtIndex(strip()->active_index());
    const std::string record_id = service()->adapter()->RecordIdForTab(tab);
    if (id) {
      *id = record_id;
    }
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return sidebar()->RowForId(record_id) != nullptr; }));
    return sidebar()->RowForId(record_id);
  }

  void model_set_manual_name(const std::string& id, const std::string& name) {
    service()->model()->UpdateTab(id, [&name](aerie_tab_model::TabRecord& tab) {
      if (name.empty()) {
        tab.manual_name.reset();
      } else {
        tab.manual_name = name;
      }
    });
  }

  void SetRules(const std::vector<RenameRule>& rules) {
    prefs()->SetList(aerie_tab_model::kRenameRules,
                     aerie_tab_model::RenameRulesToList(rules));
  }

  // The row's shown title, once it is `expected`.
  void ExpectRowTitle(AerieSidebarRowView* row, const std::string& expected) {
    EXPECT_TRUE(base::test::RunUntil([&] {
      return base::UTF16ToUTF8(std::u16string(row->title_view()->GetText())) == expected;
    })) << "row shows: " << base::UTF16ToUTF8(std::u16string(row->title_view()->GetText()));
  }

  std::string DocumentTitle() {
    return content::EvalJs(
               strip()->GetActiveWebContents()->GetPrimaryMainFrame(),
               "document.title")
        .ExtractString();
  }
};

IN_PROC_BROWSER_TEST_F(AerieSidebarTitleBrowserTest,
                       TheTidyShowsTheShortTitleWithTheOriginalInTheTooltip) {
  AerieSidebarRowView* row = OpenRow("mail.test", "/mail.html");
  ExpectRowTitle(row, "Inbox");
  EXPECT_EQ(base::UTF16ToUTF8(row->title_note()),
            "Tidied, original: " + std::string(kMailTitle));
  // The page itself is untouched.
  EXPECT_EQ(DocumentTitle(), kMailTitle);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarTitleBrowserTest,
                       ATitleTheTidyWouldNotChangeHasNoNote) {
  AerieSidebarRowView* row = OpenRow("example.test", "/plain.html");
  ExpectRowTitle(row, kPlainTitle);
  EXPECT_TRUE(row->title_note().empty());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarTitleBrowserTest,
                       TheTidyCanBeOffGloballyAndForOneSite) {
  AerieSidebarRowView* row = OpenRow("mail.test", "/mail.html");
  ExpectRowTitle(row, "Inbox");
  prefs()->SetBoolean(aerie_tab_model::kTidyTitles, false);
  ExpectRowTitle(row, kMailTitle);
  EXPECT_TRUE(row->title_note().empty());
  prefs()->SetBoolean(aerie_tab_model::kTidyTitles, true);
  ExpectRowTitle(row, "Inbox");
  base::ListValue sites;
  sites.Append("mail.test");
  prefs()->SetList(aerie_tab_model::kTidyOffSites, std::move(sites));
  ExpectRowTitle(row, kMailTitle);
  EXPECT_EQ(DocumentTitle(), kMailTitle);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarTitleBrowserTest,
                       ARuleBeatsTheTidyAndTheTooltipNamesTheRule) {
  AerieSidebarRowView* row = OpenRow("mail.test", "/mail.html");
  SetRules({MakeRule("r1", "Work mail", RuleType::kDomain, "mail.test",
                     "Work: {title}")});
  ExpectRowTitle(row, "Work: Inbox (3) - Mail");
  EXPECT_EQ(base::UTF16ToUTF8(row->title_note()),
            "Renamed by rule Work mail, original: " + std::string(kMailTitle));
  EXPECT_EQ(DocumentTitle(), kMailTitle);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarTitleBrowserTest,
                       RulesMatchByPrefixContainsAndTitle) {
  AerieSidebarRowView* row = OpenRow("mail.test", "/mail.html");
  // The test server has a port, which is part of the address.
  SetRules({MakeRule("r1", "Prefix", RuleType::kUrlStartsWith,
                     "mail.test:" + base::NumberToString(
                                        embedded_test_server()->port()) +
                         "/mail",
                     "By prefix")});
  ExpectRowTitle(row, "By prefix");
  SetRules({MakeRule("r1", "Contains", RuleType::kUrlContains, "/mail.",
                     "By contains")});
  ExpectRowTitle(row, "By contains");
  SetRules({MakeRule("r1", "Title", RuleType::kTitleContains, "inbox",
                     "By title")});
  ExpectRowTitle(row, "By title");
}

IN_PROC_BROWSER_TEST_F(AerieSidebarTitleBrowserTest,
                       ReorderingAndSwitchesChangeWhichSourceWins) {
  AerieSidebarRowView* row = OpenRow("mail.test", "/mail.html");
  RenameRule first = MakeRule("r1", "One", RuleType::kDomain, "mail.test", "One");
  RenameRule second =
      MakeRule("r2", "Two", RuleType::kUrlContains, "mail", "Two");
  SetRules({first, second});
  ExpectRowTitle(row, "One");
  SetRules({second, first});
  ExpectRowTitle(row, "Two");
  // One rule off: the other one still wins.
  second.enabled = false;
  SetRules({second, first});
  ExpectRowTitle(row, "One");
  // All rules off: the next source, the tidy.
  prefs()->SetBoolean(aerie_tab_model::kRenameRulesEnabled, false);
  ExpectRowTitle(row, "Inbox");
  prefs()->SetBoolean(aerie_tab_model::kRenameRulesEnabled, true);
  ExpectRowTitle(row, "One");
}

IN_PROC_BROWSER_TEST_F(AerieSidebarTitleBrowserTest,
                       AManualNameBeatsARuleAndTheTidy) {
  std::string id;
  AerieSidebarRowView* row = OpenRow("mail.test", "/mail.html", &id);
  SetRules({MakeRule("r1", "One", RuleType::kDomain, "mail.test", "One")});
  ExpectRowTitle(row, "One");
  model_set_manual_name(id, "Mine");
  ExpectRowTitle(row, "Mine");
  EXPECT_TRUE(row->title_note().empty());
  model_set_manual_name(id, "");
  ExpectRowTitle(row, "One");
}

IN_PROC_BROWSER_TEST_F(AerieSidebarTitleBrowserTest,
                       TheContextMenuOffersMakeARenameRule) {
  AerieSidebarRowView* row = OpenRow("mail.test", "/mail.html");
  row->ShowContextMenu(gfx::Point(5, 5), ui::mojom::MenuSourceType::kMouse);
  ASSERT_TRUE(sidebar()->context_menu());
  views::MenuItemView* item = sidebar()->context_menu()->item(
      AerieSidebarContextMenu::Command::kMakeRule);
  ASSERT_TRUE(item);
  EXPECT_TRUE(item->GetEnabled());
  EXPECT_EQ(base::UTF16ToUTF8(item->title()),
            l10n_util::GetStringUTF8(IDS_AERIE_SIDEBAR_MAKE_RULE));
  sidebar()->context_menu()->Close();
}

// The first run of the restart pair: set everything.
IN_PROC_BROWSER_TEST_F(AerieSidebarTitleBrowserTest,
                       PRE_TheSettingsSurviveARestart) {
  RenameRule second =
      MakeRule("r2", "Two", RuleType::kTitleContains, "needle", "Two");
  second.enabled = false;
  SetRules({MakeRule("r1", "One", RuleType::kDomain, "mail.test", "One"),
            second});
  prefs()->SetBoolean(aerie_tab_model::kTidyTitles, false);
  prefs()->SetBoolean(aerie_tab_model::kRenameRulesEnabled, false);
  base::ListValue sites;
  sites.Append("example.test");
  prefs()->SetList(aerie_tab_model::kTidyOffSites, std::move(sites));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarTitleBrowserTest,
                       TheSettingsSurviveARestart) {
  const aerie_tab_model::TitleSettings settings =
      aerie_tab_model::GetTitleSettings(*prefs());
  EXPECT_FALSE(settings.tidy_enabled);
  EXPECT_FALSE(settings.rename.enabled);
  ASSERT_EQ(settings.tidy_off_sites.size(), 1u);
  EXPECT_EQ(settings.tidy_off_sites[0], "example.test");
  ASSERT_EQ(settings.rename.rules.size(), 2u);
  EXPECT_EQ(settings.rename.rules[0].id, "r1");
  EXPECT_EQ(settings.rename.rules[1].id, "r2");
  EXPECT_FALSE(settings.rename.rules[1].enabled);
  // And the sidebar uses them: tidy and rules are off, so the page title shows.
  AerieSidebarRowView* row = OpenRow("mail.test", "/mail.html");
  ExpectRowTitle(row, kMailTitle);
}
