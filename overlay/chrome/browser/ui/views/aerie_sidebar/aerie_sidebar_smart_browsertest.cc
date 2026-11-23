// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of smart folders (issue #140): a live view of the Pinned and
// Today tabs of the Workspace that match rules, with rows that stand for real
// tabs. The matching itself, its cost and the row order are unit tested in
// smart_folder_unittest.cc; these tests check what the sidebar makes of them.

#include <string>
#include <vector>

#include "base/json/json_writer.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_context_menu.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_folder_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_folder_sheet.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_switch.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_tab_model/folder_rules.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/smart_folder.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_prefs.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view_utils.h"

namespace {

using aerie_tab_model::FolderKind;
using aerie_tab_model::RuleType;
using aerie_tab_model::Section;

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

ui::AXNodeData DataOf(views::View* view) {
  ui::AXNodeData data;
  view->GetViewAccessibility().GetAccessibleNodeData(&data);
  return data;
}

std::string NameOf(views::View* view) {
  return DataOf(view).GetStringAttribute(ax::mojom::StringAttribute::kName);
}

void Click(views::Button* button) {
  views::test::ButtonTestApi(button).NotifyClick(
      ui::MouseEvent(ui::EventType::kMousePressed, gfx::Point(1, 1),
                     gfx::Point(1, 1), base::TimeTicks::Now(),
                     ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON));
}

// A left click on the row itself.
void ClickRow(AerieSidebarRowView* row) {
  ui::MouseEvent press(ui::EventType::kMousePressed, gfx::Point(40, 4),
                       gfx::Point(40, 4), base::TimeTicks::Now(),
                       ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  row->OnMousePressed(press);
  ui::MouseEvent release(ui::EventType::kMouseReleased, gfx::Point(40, 4),
                         gfx::Point(40, 4), base::TimeTicks::Now(),
                         ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  row->OnMouseReleased(release);
}

void Hover(AerieSidebarRowView* row) {
  ui::MouseEvent entered(ui::EventType::kMouseEntered, gfx::Point(), gfx::Point(),
                         base::TimeTicks::Now(), 0, 0);
  row->OnMouseEntered(entered);
}

void Key(AerieSidebarRowView* row, ui::KeyboardCode code) {
  ui::KeyEvent event(ui::EventType::kKeyPressed, code, ui::EF_NONE);
  row->OnKeyPressed(event);
}

}  // namespace

class AerieSidebarSmartBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    reduced_motion_.emplace(
        gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(true));
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return sidebar() && sidebar()->row_count() == 1u; }));
  }
  void TearDownOnMainThread() override {
    reduced_motion_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  AerieSidebarView* sidebar() {
    return FindDescendant<AerieSidebarView>(
        BrowserView::GetBrowserViewForBrowser(browser())
            ->vertical_tab_strip_region_view_for_testing());
  }
  aerie_tab_model::TabModelService* service() {
    return aerie_tab_model::TabModelServiceFactory::GetForProfile(
        browser()->GetProfile());
  }
  aerie_tab_model::TabModel* model() { return service()->model(); }
  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }
  TabStripModel* strip() { return browser()->tab_strip_model(); }

  GURL UrlOn(const std::string& host, const std::string& path = "/title1.html") {
    return embedded_test_server()->GetURL(host, path);
  }

  std::string OpenTabAt(const GURL& url) {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), url, WindowOpenDisposition::kNewForegroundTab,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    return service()->adapter()->RecordIdForTab(strip()->GetActiveTab());
  }

  void NavigateActive(const GURL& url) {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  }

  // A smart folder at the top of `section` with one rule.
  std::string MakeSmartFolder(const std::string& name,
                              RuleType type,
                              const std::string& value,
                              Section section = Section::kToday) {
    const std::string id = aerie_tab_model::CreateFolder(
        *model(), section, std::string(), name);
    EXPECT_FALSE(id.empty());
    int moved = -1;
    EXPECT_TRUE(aerie_tab_model::SetFolderRuleKind(*model(), id,
                                                   FolderKind::kSmart, &moved));
    EXPECT_FALSE(aerie_tab_model::AddRule(*model(), id, type, value).empty());
    return id;
  }

  std::string SmartRow(const std::string& folder, const std::string& tab) {
    return aerie_tab_model::MakeSmartRowId(folder, tab);
  }

  bool WaitForRow(const std::string& id, bool present = true) {
    return base::test::RunUntil(
        [&] { return (sidebar()->RowForId(id) != nullptr) == present; });
  }

  // How many smart rows of `folder` the Today list shows.
  size_t SmartRowCount(const std::string& folder) {
    size_t count = 0;
    for (const auto& row : sidebar()->tree_rows(Section::kToday)) {
      count += row.smart_member && row.parent == folder;
    }
    for (const auto& row : sidebar()->tree_rows(Section::kPinned)) {
      count += row.smart_member && row.parent == folder;
    }
    return count;
  }

  bool WaitForSmartRowCount(const std::string& folder, size_t count) {
    return base::test::RunUntil(
        [&] { return SmartRowCount(folder) == count; });
  }

  void Pin(const std::string& tab_id) {
    sidebar()->ContextCommandForTesting(
        tab_id, AerieSidebarContextMenu::Command::kPin);
  }

  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest,
                       ListsExactlyTheMatchingPinnedAndTodayTabs) {
  const std::string today = OpenTabAt(UrlOn("docs.localhost"));
  const std::string pinned = OpenTabAt(UrlOn("docs.localhost", "/title2.html"));
  const std::string other = OpenTabAt(UrlOn("elsewhere.localhost"));
  Pin(pinned);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return model()->GetTab(pinned)->section == Section::kPinned;
  }));
  const std::string folder =
      MakeSmartFolder("Docs", RuleType::kDomain, "docs.localhost");
  ASSERT_TRUE(WaitForSmartRowCount(folder, 2u));
  EXPECT_TRUE(sidebar()->RowForId(SmartRow(folder, today)));
  EXPECT_TRUE(sidebar()->RowForId(SmartRow(folder, pinned)));
  EXPECT_FALSE(sidebar()->RowForId(SmartRow(folder, other)));
  // The Pinned tab sits before the Today tab, as the folder lists Pinned first.
  const auto& rows = sidebar()->tree_rows(Section::kToday);
  size_t pinned_at = rows.size(), today_at = rows.size();
  for (size_t i = 0; i < rows.size(); ++i) {
    if (rows[i].id == SmartRow(folder, pinned)) {
      pinned_at = i;
    }
    if (rows[i].id == SmartRow(folder, today)) {
      today_at = i;
    }
  }
  EXPECT_LT(pinned_at, today_at);
  // Nothing moved: the tabs are where they were.
  EXPECT_EQ(model()->GetTab(today)->section, Section::kToday);
  EXPECT_EQ(model()->GetTab(today)->parent, "");
  EXPECT_EQ(model()->GetTab(pinned)->section, Section::kPinned);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest, SleepingTabsAreListed) {
  const std::string tab = OpenTabAt(UrlOn("docs.localhost"));
  const std::string folder =
      MakeSmartFolder("Docs", RuleType::kDomain, "docs.localhost");
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab)));
  model()->UpdateTab(tab, [](aerie_tab_model::TabRecord& t) { t.sleeping = true; });
  ASSERT_TRUE(base::test::RunUntil([&] {
    AerieSidebarRowView* row = sidebar()->RowForId(SmartRow(folder, tab));
    return row && row->sleeping();
  }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest,
                       TheListFollowsATabThatOpensNavigatesRetitlesMovesAndCloses) {
  const std::string folder =
      MakeSmartFolder("Docs", RuleType::kDomain, "docs.localhost");
  ASSERT_TRUE(WaitForSmartRowCount(folder, 0u));

  // Opens.
  const std::string tab = OpenTabAt(UrlOn("elsewhere.localhost"));
  EXPECT_FALSE(sidebar()->RowForId(SmartRow(folder, tab)));
  // Navigates in.
  NavigateActive(UrlOn("docs.localhost"));
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab)));
  // Navigates out.
  NavigateActive(UrlOn("elsewhere.localhost"));
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab), /*present=*/false));
  NavigateActive(UrlOn("docs.localhost"));
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab)));

  // Moves section: the row stays, and its hint follows.
  Pin(tab);
  ASSERT_TRUE(base::test::RunUntil([&] {
    for (const auto& row : sidebar()->tree_rows(Section::kToday)) {
      if (row.id == SmartRow(folder, tab) && row.origin == Section::kPinned) {
        return true;
      }
    }
    return false;
  }));
  EXPECT_TRUE(sidebar()->RowForId(SmartRow(folder, tab)));

  // Back in Today (a Pinned tab sleeps instead of closing), then closes: both
  // rows go.
  sidebar()->ContextCommandForTesting(tab,
                                      AerieSidebarContextMenu::Command::kUnpin);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return model()->GetTab(tab)->section == Section::kToday;
  }));
  strip()->CloseWebContentsAt(strip()->active_index(), TabCloseTypes::CLOSE_NONE);
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab), /*present=*/false));
  EXPECT_TRUE(base::test::RunUntil([&] { return !sidebar()->RowForId(tab); }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest,
                       ATitleChangeAddsAndRemovesTheRow) {
  const std::string folder =
      MakeSmartFolder("Roadmaps", RuleType::kTitleContains, "roadmap");
  const std::string tab = OpenTabAt(UrlOn("elsewhere.localhost"));
  ASSERT_TRUE(WaitForSmartRowCount(folder, 0u));
  ASSERT_TRUE(content::ExecJs(strip()->GetActiveWebContents(),
                              "document.title = 'Q4 Roadmap'"));
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab)));
  ASSERT_TRUE(content::ExecJs(strip()->GetActiveWebContents(),
                              "document.title = 'Something else'"));
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab), /*present=*/false));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest, ARuleChangeUpdatesTheList) {
  const std::string docs = OpenTabAt(UrlOn("docs.localhost"));
  const std::string news = OpenTabAt(UrlOn("news.localhost"));
  const std::string folder =
      MakeSmartFolder("Reading", RuleType::kDomain, "docs.localhost");
  ASSERT_TRUE(WaitForRow(SmartRow(folder, docs)));
  EXPECT_FALSE(sidebar()->RowForId(SmartRow(folder, news)));
  const std::string rule = aerie_tab_model::AddRule(
      *model(), folder, RuleType::kDomain, "news.localhost");
  ASSERT_FALSE(rule.empty());
  ASSERT_TRUE(WaitForRow(SmartRow(folder, news)));
  ASSERT_TRUE(aerie_tab_model::RemoveRule(*model(), folder, rule));
  ASSERT_TRUE(WaitForRow(SmartRow(folder, news), /*present=*/false));
  EXPECT_TRUE(sidebar()->RowForId(SmartRow(folder, docs)));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest,
                       ATabShowsInBothPlacesAndEitherRowSelectsIt) {
  const std::string tab = OpenTabAt(UrlOn("docs.localhost"));
  OpenTabAt(UrlOn("elsewhere.localhost"));
  const std::string folder =
      MakeSmartFolder("Docs", RuleType::kDomain, "docs.localhost");
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab)));
  AerieSidebarRowView* real = sidebar()->RowForId(tab);
  AerieSidebarRowView* smart = sidebar()->RowForId(SmartRow(folder, tab));
  ASSERT_TRUE(real && smart);
  EXPECT_NE(real, smart);
  // The rows are laid out, so that a click lands on them.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return smart->width() > 60 && smart->height() > 10 && real->height() > 10;
  }));
  EXPECT_FALSE(real->selected());
  EXPECT_FALSE(smart->selected());

  // Selecting the smart row selects the real tab, and both rows say so.
  ClickRow(smart);
  EXPECT_EQ(service()->adapter()->RecordIdForTab(strip()->GetActiveTab()), tab);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return real->selected() && smart->selected(); }));

  // Selecting another tab clears both.
  ClickRow(sidebar()->row_at(sidebar()->row_count() - 1));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !real->selected() && !smart->selected(); }));
  // Selecting the real row selects the same tab again.
  ClickRow(real);
  EXPECT_EQ(service()->adapter()->RecordIdForTab(strip()->GetActiveTab()), tab);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return real->selected() && smart->selected(); }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest,
                       ClosingFromTheSmartFolderClosesTheRealTabAndBothRows) {
  const std::string tab = OpenTabAt(UrlOn("docs.localhost"));
  const std::string folder =
      MakeSmartFolder("Docs", RuleType::kDomain, "docs.localhost");
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab)));
  const int tabs = strip()->count();
  AerieSidebarRowView* smart = sidebar()->RowForId(SmartRow(folder, tab));
  Hover(smart);
  ASSERT_TRUE(smart->close_button()->GetVisible());
  Click(smart->close_button());
  ASSERT_TRUE(base::test::RunUntil([&] { return strip()->count() == tabs - 1; }));
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab), /*present=*/false));
  EXPECT_TRUE(base::test::RunUntil([&] { return !sidebar()->RowForId(tab); }));
  EXPECT_EQ(model()->GetTab(tab), nullptr);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest,
                       TheContextMenuCloseOfASmartRowClosesTheRealTab) {
  const std::string tab = OpenTabAt(UrlOn("docs.localhost"));
  const std::string folder =
      MakeSmartFolder("Docs", RuleType::kDomain, "docs.localhost");
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab)));
  const int tabs = strip()->count();
  sidebar()->ContextCommandForTesting(
      SmartRow(folder, tab), AerieSidebarContextMenu::Command::kClose);
  ASSERT_TRUE(base::test::RunUntil([&] { return strip()->count() == tabs - 1; }));
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab), /*present=*/false));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest,
                       ASmartRowShowsItsSectionOnHoverAndItsCloseTooltipSaysRealTab) {
  const std::string today = OpenTabAt(UrlOn("docs.localhost"));
  const std::string pinned = OpenTabAt(UrlOn("docs.localhost", "/title2.html"));
  Pin(pinned);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return model()->GetTab(pinned)->section == Section::kPinned;
  }));
  const std::string folder =
      MakeSmartFolder("Docs", RuleType::kDomain, "docs.localhost");
  ASSERT_TRUE(WaitForSmartRowCount(folder, 2u));
  AerieSidebarRowView* today_row = sidebar()->RowForId(SmartRow(folder, today));
  AerieSidebarRowView* pinned_row = sidebar()->RowForId(SmartRow(folder, pinned));
  ASSERT_TRUE(today_row && pinned_row);
  EXPECT_TRUE(today_row->smart_member());

  // The hint is quiet: not there until the pointer is.
  ASSERT_TRUE(today_row->origin_hint());
  EXPECT_FALSE(today_row->origin_hint()->GetVisible());
  Hover(today_row);
  EXPECT_TRUE(today_row->origin_hint()->GetVisible());
  EXPECT_EQ(today_row->origin_hint()->GetText(), u"Today");
  Hover(pinned_row);
  EXPECT_EQ(pinned_row->origin_hint()->GetText(), u"Pinned");

  // The close button names what it closes.
  EXPECT_NE(today_row->close_button()
                ->GetRenderedTooltipText(gfx::Point())
                .find(u"the real tab"),
            std::u16string::npos);
  EXPECT_NE(today_row->close_button()
                ->GetRenderedTooltipText(gfx::Point())
                .find(u"Today"),
            std::u16string::npos);
  EXPECT_NE(pinned_row->close_button()
                ->GetRenderedTooltipText(gfx::Point())
                .find(u"Pinned"),
            std::u16string::npos);
  // The real row keeps its own tooltip.
  EXPECT_EQ(sidebar()->RowForId(today)->close_button()->GetRenderedTooltipText(
                gfx::Point()),
            u"Close tab");
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest,
                       TheRolesAndNamesAreTheOnesTheDesignSays) {
  const std::string tab = OpenTabAt(UrlOn("docs.localhost"));
  const std::string folder =
      MakeSmartFolder("Docs", RuleType::kDomain, "docs.localhost");
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab)));
  AerieSidebarFolderRowView* folder_row = sidebar()->FolderRowForId(folder);
  ASSERT_TRUE(folder_row);
  EXPECT_EQ(DataOf(folder_row).role, ax::mojom::Role::kTreeItem);
  EXPECT_EQ(NameOf(folder_row), "Docs, smart folder, 1 item");
  EXPECT_TRUE(DataOf(folder_row).HasState(ax::mojom::State::kExpanded));

  AerieSidebarRowView* smart = sidebar()->RowForId(SmartRow(folder, tab));
  ASSERT_TRUE(smart);
  EXPECT_EQ(DataOf(smart).role, ax::mojom::Role::kTreeItem);
  EXPECT_EQ(NameOf(smart),
            base::UTF16ToUTF8(smart->title_view()->GetText()) +
                ", shown from Today");
  EXPECT_EQ(DataOf(smart).GetIntAttribute(
                ax::mojom::IntAttribute::kHierarchicalLevel),
            2);
  // The real row has no suffix.
  AerieSidebarRowView* real = sidebar()->RowForId(tab);
  EXPECT_EQ(NameOf(real).find("shown from"), std::string::npos);
  // The shut folder says so, and keeps its count.
  ASSERT_TRUE(aerie_tab_model::SetFolderExpanded(*model(), folder, false));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return DataOf(sidebar()->FolderRowForId(folder))
        .HasState(ax::mojom::State::kCollapsed);
  }));
  // The state is the tree role's, not a word in the name.
  EXPECT_EQ(NameOf(sidebar()->FolderRowForId(folder)),
            "Docs, smart folder, 1 item");
  EXPECT_FALSE(sidebar()->RowForId(SmartRow(folder, tab)));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest,
                       TheFolderRowHasTheSmartLookWithAnItalicCount) {
  const std::string tab = OpenTabAt(UrlOn("docs.localhost"));
  const std::string folder =
      MakeSmartFolder("Docs", RuleType::kDomain, "docs.localhost");
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab)));
  AerieSidebarFolderRowView* folder_row = sidebar()->FolderRowForId(folder);
  ASSERT_TRUE(folder_row);
  EXPECT_TRUE(folder_row->smart());
  ASSERT_TRUE(folder_row->count_label());
  EXPECT_TRUE(folder_row->count_label()->GetVisible());
  EXPECT_EQ(folder_row->count_label()->GetText(), u"1");
  EXPECT_TRUE(folder_row->count_label()->font_list().GetFontStyle() &
              gfx::Font::ITALIC);
  // No funnel: that glyph is the auto-filing folder's.
  EXPECT_FALSE(folder_row->funnel_button()->GetVisible());
  // A manual folder has neither.
  const std::string manual = aerie_tab_model::CreateFolder(
      *model(), Section::kToday, "", "Manual");
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->FolderRowForId(manual) != nullptr; }));
  EXPECT_FALSE(sidebar()->FolderRowForId(manual)->smart());
  EXPECT_FALSE(sidebar()->FolderRowForId(manual)->count_label()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest,
                       SwitchingAFolderWithTabsToSmartMovesThemOutAndSaysSo) {
  const std::string a = OpenTabAt(UrlOn("docs.localhost"));
  const std::string b = OpenTabAt(UrlOn("docs.localhost", "/title2.html"));
  const std::string c = OpenTabAt(UrlOn("elsewhere.localhost"));
  const std::string folder = aerie_tab_model::CreateFolder(
      *model(), Section::kToday, "", "Docs");
  for (const std::string& id : {a, b, c}) {
    ASSERT_TRUE(aerie_tab_model::MoveTabToFolder(*model(), id, folder));
  }
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->FolderRowForId(folder) != nullptr; }));
  sidebar()->OpenFolderSheet(folder);
  AerieSidebarFolderSheet* sheet = sidebar()->folder_sheet();
  ASSERT_TRUE(sheet);
  ASSERT_TRUE(sheet->kind_button(2));
  Click(sheet->kind_button(2));
  sheet->FlushForTesting();

  EXPECT_EQ(model()->GetFolder(folder)->kind, FolderKind::kSmart);
  for (const std::string& id : {a, b, c}) {
    EXPECT_EQ(model()->GetTab(id)->parent, "");
  }
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->toast_visible(); }));
  EXPECT_EQ(sidebar()->toast()->GetText(), u"Moved 3 tabs out of Docs");
  EXPECT_EQ(DataOf(sidebar()->toast()).role, ax::mojom::Role::kStatus);
  // The sheet shows the Smart body, with the one-line explanation, and no
  // Behaviour section.
  EXPECT_TRUE(sheet->smart_body_slot()->GetVisible());
  EXPECT_FALSE(sheet->behaviour_slot()->GetVisible());
  EXPECT_TRUE(sheet->kind_checked(2));

  // Switching back to Manual leaves it empty.
  Click(sheet->kind_button(0));
  sheet->FlushForTesting();
  EXPECT_EQ(model()->GetFolder(folder)->kind, FolderKind::kManual);
  EXPECT_TRUE(aerie_tab_model::FolderIsEmpty(*model(), folder));
  EXPECT_FALSE(sheet->smart_body_slot()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest,
                       SwitchingAnEmptyFolderToSmartShowsNoToast) {
  const std::string folder = aerie_tab_model::CreateFolder(
      *model(), Section::kToday, "", "Docs");
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->FolderRowForId(folder) != nullptr; }));
  sidebar()->OpenFolderSheet(folder);
  AerieSidebarFolderSheet* sheet = sidebar()->folder_sheet();
  ASSERT_TRUE(sheet);
  Click(sheet->kind_button(2));
  sheet->FlushForTesting();
  EXPECT_EQ(model()->GetFolder(folder)->kind, FolderKind::kSmart);
  EXPECT_FALSE(sidebar()->toast_visible());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest,
                       TheRuleTheAllAndTheGlobalSwitchEachEmptyTheFolder) {
  const std::string tab = OpenTabAt(UrlOn("docs.localhost"));
  const std::string folder =
      MakeSmartFolder("Docs", RuleType::kDomain, "docs.localhost");
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab)));
  const std::string rule =
      aerie_tab_model::ReadFolderRules(*model()->GetFolder(folder)).rules[0].id;

  ASSERT_TRUE(aerie_tab_model::SetRuleEnabled(*model(), folder, rule, false));
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab), false));
  ASSERT_TRUE(aerie_tab_model::SetRuleEnabled(*model(), folder, rule, true));
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab)));

  ASSERT_TRUE(aerie_tab_model::SetFolderRulesEnabled(*model(), folder, false));
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab), false));
  ASSERT_TRUE(aerie_tab_model::SetFolderRulesEnabled(*model(), folder, true));
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab)));

  prefs()->SetBoolean(aerie_tab_model::kFolderRulesEnabled, false);
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab), false));
  // An off smart folder shows no rows and says it is empty.
  EXPECT_NE(NameOf(sidebar()->FolderRowForId(folder)).find("empty"),
            std::string::npos);
  prefs()->SetBoolean(aerie_tab_model::kFolderRulesEnabled, true);
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab)));
  // The real tab never moved.
  EXPECT_TRUE(sidebar()->RowForId(tab));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest,
                       AnExceptionAndTheSiteExclusionEmptyTheFolder) {
  const std::string tab = OpenTabAt(UrlOn("docs.localhost"));
  const std::string folder =
      MakeSmartFolder("Docs", RuleType::kDomain, "docs.localhost");
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab)));
  ASSERT_TRUE(aerie_tab_model::AddExcludedTab(*model(), folder, tab));
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab), false));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest,
                       ArrowsAndDeleteWorkInsideTheFolder) {
  const std::string a = OpenTabAt(UrlOn("docs.localhost"));
  const std::string b = OpenTabAt(UrlOn("docs.localhost", "/title2.html"));
  const std::string folder =
      MakeSmartFolder("Docs", RuleType::kDomain, "docs.localhost");
  ASSERT_TRUE(WaitForSmartRowCount(folder, 2u));
  AerieSidebarRowView* folder_row = sidebar()->RowForId(folder);
  ASSERT_TRUE(folder_row);
  folder_row->RequestFocus();

  // Right on an open folder goes to its first row; Left comes back.
  Key(folder_row, ui::VKEY_RIGHT);
  AerieSidebarRowView* first = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    for (const auto& row : sidebar()->tree_rows(Section::kToday)) {
      if (row.smart_member && row.parent == folder) {
        first = sidebar()->RowForId(row.id);
        return first && first->HasFocus();
      }
    }
    return false;
  }));
  Key(first, ui::VKEY_LEFT);
  EXPECT_TRUE(base::test::RunUntil([&] { return folder_row->HasFocus(); }));

  // Delete on a smart row closes the real tab, not just the row.
  const std::string smart_id = SmartRow(folder, b);
  AerieSidebarRowView* row_b = sidebar()->RowForId(smart_id);
  ASSERT_TRUE(row_b);
  row_b->RequestFocus();
  const int tabs = strip()->count();
  Key(row_b, ui::VKEY_DELETE);
  ASSERT_TRUE(base::test::RunUntil([&] { return strip()->count() == tabs - 1; }));
  EXPECT_EQ(model()->GetTab(b), nullptr);
  ASSERT_TRUE(WaitForRow(SmartRow(folder, b), false));
  EXPECT_TRUE(sidebar()->RowForId(SmartRow(folder, a)));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest,
                       ASmartFolderInPinnedListsTheTodayTabsToo) {
  const std::string tab = OpenTabAt(UrlOn("docs.localhost"));
  const std::string folder = MakeSmartFolder(
      "Docs", RuleType::kDomain, "docs.localhost", Section::kPinned);
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab)));
  // The tab is still in Today, and its archive rules are its own.
  EXPECT_EQ(model()->GetTab(tab)->section, Section::kToday);
  EXPECT_FALSE(model()->GetTab(tab)->archived_at.has_value());
  bool in_pinned_tree = false;
  for (const auto& row : sidebar()->tree_rows(Section::kPinned)) {
    in_pinned_tree |= row.id == SmartRow(folder, tab);
  }
  EXPECT_TRUE(in_pinned_tree);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest,
                       TheKindAndTheRulesAreInTheFolderRecordAndTheListIsNot) {
  const std::string tab = OpenTabAt(UrlOn("docs.localhost"));
  const std::string folder =
      MakeSmartFolder("Docs", RuleType::kDomain, "docs.localhost");
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab)));
  const base::DictValue state = model()->ToValue();
  const std::string json = [&] {
    std::string out;
    base::JSONWriter::Write(state, &out);
    return out;
  }();
  EXPECT_NE(json.find("\"smart\""), std::string::npos);
  EXPECT_NE(json.find("docs.localhost"), std::string::npos);
  EXPECT_EQ(json.find("smart:"), std::string::npos);
  aerie_tab_model::ParseResult parsed = aerie_tab_model::ParseState(state);
  ASSERT_EQ(parsed.status, aerie_tab_model::LoadStatus::kOk);
  bool found = false;
  for (const auto& f : parsed.state->folders) {
    if (f.id == folder) {
      found = true;
      EXPECT_EQ(f.kind, FolderKind::kSmart);
    }
  }
  EXPECT_TRUE(found);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest,
                       ASmartFolderMenuHasNoNewFolderAndTheTabMenuListsNoSmartFolder) {
  const std::string folder =
      MakeSmartFolder("Docs", RuleType::kDomain, "docs.localhost");
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->FolderRowForId(folder) != nullptr; }));
  EXPECT_FALSE(aerie_tab_model::CanCreateFolderIn(*model(), folder));
  for (const auto& choice :
       aerie_tab_model::FolderChoices(*model(), Section::kToday)) {
    EXPECT_NE(choice.id, folder);
  }
}

// A row of a smart folder has no Rename and no Reset name: both would do
// nothing, and the real row has them.
IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest,
                       TheMenuOfASmartRowHasNoRenameAndNoResetName) {
  using Command = AerieSidebarContextMenu::Command;
  const std::string tab = OpenTabAt(UrlOn("docs.localhost"));
  model()->UpdateTab(tab, [](aerie_tab_model::TabRecord& t) {
    t.manual_name = "Mine";
  });
  const std::string folder =
      MakeSmartFolder("Docs", RuleType::kDomain, "docs.localhost");
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab)));

  sidebar()->ShowRowContextMenuForTesting(SmartRow(folder, tab));
  ASSERT_TRUE(sidebar()->context_menu());
  EXPECT_EQ(sidebar()->context_menu()->item(Command::kRename), nullptr);
  EXPECT_EQ(sidebar()->context_menu()->item(Command::kResetName), nullptr);
  EXPECT_NE(sidebar()->context_menu()->item(Command::kClose), nullptr);
  sidebar()->context_menu()->Close();

  sidebar()->ShowRowContextMenuForTesting(tab);
  ASSERT_TRUE(sidebar()->context_menu());
  EXPECT_NE(sidebar()->context_menu()->item(Command::kRename), nullptr);
  EXPECT_NE(sidebar()->context_menu()->item(Command::kResetName), nullptr);
  sidebar()->context_menu()->Close();
}

// The smart row follows the tab's sleeping state at once, from the record's own
// change and not from the next sync.
IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest,
                       ASmartRowFollowsTheSleepAndTheWakeOfItsTabAtOnce) {
  const std::string tab = OpenTabAt(UrlOn("docs.localhost"));
  const std::string folder =
      MakeSmartFolder("Docs", RuleType::kDomain, "docs.localhost");
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab)));
  AerieSidebarRowView* smart = sidebar()->RowForId(SmartRow(folder, tab));
  ASSERT_TRUE(smart);
  EXPECT_FALSE(smart->sleeping());
  model()->UpdateTab(tab, [](aerie_tab_model::TabRecord& t) { t.sleeping = true; });
  EXPECT_TRUE(smart->sleeping());
  EXPECT_TRUE(sidebar()->RowForId(tab)->sleeping());
  model()->UpdateTab(tab, [](aerie_tab_model::TabRecord& t) { t.sleeping = false; });
  EXPECT_FALSE(smart->sleeping());
}

// Shutting the folder with the keyboard focus on one of its rows puts the focus
// on the folder row, not on an unrelated row.
IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest,
                       CollapsingWithFocusOnASmartRowMovesFocusToTheFolderRow) {
  const std::string tab = OpenTabAt(UrlOn("docs.localhost"));
  const std::string folder =
      MakeSmartFolder("Docs", RuleType::kDomain, "docs.localhost");
  ASSERT_TRUE(WaitForRow(SmartRow(folder, tab)));
  AerieSidebarRowView* smart = sidebar()->RowForId(SmartRow(folder, tab));
  ASSERT_TRUE(base::test::RunUntil([&] {
    smart->RequestFocus();
    return smart->HasFocus();
  }));
  sidebar()->SetFolderExpanded(folder, false);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !sidebar()->RowForId(SmartRow(folder, tab)); }));
  AerieSidebarRowView* folder_row = sidebar()->RowForId(folder);
  ASSERT_TRUE(folder_row);
  EXPECT_TRUE(folder_row->HasFocus());
}

// A smart folder inside a folder that is toggled counts as inside it.
IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest,
                       CollapsingTheFolderAroundASmartFolderMovesFocusToo) {
  const std::string tab = OpenTabAt(UrlOn("docs.localhost"));
  const std::string outer =
      aerie_tab_model::CreateFolder(*model(), Section::kToday, "", "Outer");
  const std::string inner = aerie_tab_model::CreateFolder(
      *model(), Section::kToday, outer, "Docs");
  ASSERT_FALSE(inner.empty());
  ASSERT_TRUE(aerie_tab_model::SetFolderRuleKind(*model(), inner,
                                                 FolderKind::kSmart));
  ASSERT_FALSE(aerie_tab_model::AddRule(*model(), inner, RuleType::kDomain,
                                        "docs.localhost")
                   .empty());
  ASSERT_TRUE(WaitForRow(SmartRow(inner, tab)));
  ASSERT_TRUE(base::test::RunUntil([&] {
    AerieSidebarRowView* row = sidebar()->RowForId(SmartRow(inner, tab));
    row->RequestFocus();
    return row->HasFocus();
  }));
  sidebar()->SetFolderExpanded(outer, false);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !sidebar()->RowForId(SmartRow(inner, tab)); }));
  EXPECT_TRUE(sidebar()->RowForId(outer)->HasFocus());
}

// A theme change reaches every bubble anchored in the window. A closed folder
// sheet must not be one of them (issue #247).
IN_PROC_BROWSER_TEST_F(AerieSidebarSmartBrowserTest,
                       AThemeChangeAfterTheSheetClosedDoesNotCrash) {
  const std::string folder = aerie_tab_model::CreateFolder(
      *model(), Section::kToday, "", "Docs");
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->FolderRowForId(folder) != nullptr; }));
  sidebar()->OpenFolderSheet(folder);
  ASSERT_TRUE(sidebar()->folder_sheet());
  base::WeakPtr<views::Widget> widget =
      sidebar()->folder_sheet()->GetWidget()->GetWeakPtr();
  sidebar()->folder_sheet()->Dismiss(/*refocus=*/false);
  ASSERT_TRUE(base::test::RunUntil([&] { return !widget; }));
  views::Widget* window =
      BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
  window->ResetLastColorProviderKey();
  window->ThemeChanged();
}
