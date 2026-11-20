// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of Today's Clear (issue #266): Clear archives every unpinned
// Today tab with the reason "cleared" and the Archive lists them as such; Undo
// brings back what is still archived, in the order and the folders it had;
// Alt+Clear and the "Close all" item of the Clear menu close the tabs and keep
// no record; Pinned and Favorites are never touched; a private window keeps no
// Archive, so Clear closes there. The toast itself is tested in
// aerie_sidebar_clear_uitest.cc.

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/browser/aerie_tab_model/pinned_tabs_controller.h"
#include "chrome/browser/aerie_tab_model/tab_archiver.h"
#include "chrome/browser/aerie_tab_model/tab_archiver_factory.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_archive_controller.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_archive_panel.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_context_menu.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_tab_model/archive.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/tab_model.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace {

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

AerieSidebarView* SidebarOf(BrowserWindowInterface* browser) {
  return FindDescendant<AerieSidebarView>(
      BrowserView::GetBrowserViewForBrowser(browser)
          ->vertical_tab_strip_region_view_for_testing());
}

}  // namespace

class AerieSidebarClearBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    service_ = aerie_tab_model::TabModelServiceFactory::GetForProfile(
        browser()->GetProfile());
    ASSERT_TRUE(service_);
    archiver_ = aerie_tab_model::TabArchiverFactory::GetForProfile(
        browser()->GetProfile());
    ASSERT_TRUE(archiver_);
    sidebar()->archive()->Attach();
    reduced_motion_.emplace(
        gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(true));
  }

  void TearDownOnMainThread() override {
    if (sidebar() && sidebar()->archive()->panel()) {
      sidebar()->archive()->panel()->ClosePanel(false);
    }
    archiver_ = nullptr;
    reduced_motion_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  AerieSidebarView* sidebar() { return SidebarOf(browser()); }
  aerie_tab_model::TabModel* model() { return service_->model(); }
  TabStripModel* strip() { return browser()->tab_strip_model(); }

  std::string IdAt(int index) {
    return service_->adapter()->RecordIdForTab(strip()->GetTabAtIndex(index));
  }

  // A loaded tab behind the selected one; returns its record id.
  std::string OpenBackgroundTab() {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), embedded_test_server()->GetURL("/title1.html"),
        WindowOpenDisposition::NEW_BACKGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    const std::string id = IdAt(strip()->count() - 1);
    EXPECT_FALSE(id.empty());
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return sidebar()->RowForId(id) != nullptr; }));
    return id;
  }

  // Today's tab records in the order the sidebar draws them, folders opened.
  std::vector<std::string> TodayOrder() {
    std::vector<std::string> ids;
    for (const aerie_tab_model::TreeRow& row :
         aerie_tab_model::BuildVisibleRows(
             *model(), Section::kToday,
             [](const std::string&) { return true; })) {
      if (!row.is_folder) {
        ids.push_back(row.id);
      }
    }
    return ids;
  }

  std::map<std::string, std::string> ParentsOf(
      const std::vector<std::string>& ids) {
    std::map<std::string, std::string> parents;
    for (const std::string& id : ids) {
      parents[id] = model()->GetTab(id)->parent;
    }
    return parents;
  }

  // How many open tabs belong to the record `id`.
  int OpenTabsOf(const std::string& id) {
    int count = 0;
    for (int i = 0; i < strip()->count(); ++i) {
      count += IdAt(i) == id ? 1 : 0;
    }
    return count;
  }

  // Four Today tabs (the first one is open), one Pinned tab and one Favorite.
  struct Setup {
    std::vector<std::string> today;
    std::string pinned;
    std::string favorite;
  };
  Setup MakeTabs() {
    Setup setup;
    const std::string first = IdAt(0);
    const std::string a = OpenBackgroundTab();
    const std::string b = OpenBackgroundTab();
    const std::string c = OpenBackgroundTab();
    setup.pinned = OpenBackgroundTab();
    setup.favorite = OpenBackgroundTab();
    EXPECT_TRUE(service_->pinned_tabs()->Pin(TabFor(setup.pinned)));
    EXPECT_EQ(service_->pinned_tabs()->AddToFavorites(TabFor(setup.favorite)),
              aerie_tab_model::FavoriteResult::kAdded);
    setup.today = {first, a, b, c};
    EXPECT_TRUE(base::test::RunUntil([&] {
      return TodayOrder().size() == 4u;
    }));
    return setup;
  }

  tabs::TabInterface* TabFor(const std::string& id) {
    for (int i = 0; i < strip()->count(); ++i) {
      if (IdAt(i) == id) {
        return strip()->GetTabAtIndex(i);
      }
    }
    return nullptr;
  }

  void PressClear(AerieSidebarView* view, int flags = ui::EF_NONE) {
    views::test::ButtonTestApi(view->clear_button())
        .NotifyClick(ui::MouseEvent(
            ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
            base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON | flags,
            ui::EF_LEFT_MOUSE_BUTTON));
  }
  void PressClear(int flags = ui::EF_NONE) { PressClear(sidebar(), flags); }

  void PressUndo() {
    ASSERT_TRUE(sidebar()->toast_visible());
    ASSERT_TRUE(sidebar()->toast_undo_button()->GetVisible());
    views::test::ButtonTestApi(sidebar()->toast_undo_button())
        .NotifyClick(ui::MouseEvent(
            ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
            base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
            ui::EF_LEFT_MOUSE_BUTTON));
  }

  // The tabs of Pinned and Favorites are where they were, open and not archived.
  void ExpectPinnedAndFavoritesUntouched(const Setup& setup) {
    const aerie_tab_model::TabRecord* pinned = model()->GetTab(setup.pinned);
    ASSERT_TRUE(pinned);
    EXPECT_EQ(pinned->section, Section::kPinned);
    EXPECT_FALSE(pinned->archived_at);
    EXPECT_EQ(OpenTabsOf(setup.pinned), 1);
    const aerie_tab_model::TabRecord* favorite = model()->GetTab(setup.favorite);
    ASSERT_TRUE(favorite);
    EXPECT_EQ(favorite->section, Section::kFavorite);
    EXPECT_FALSE(favorite->archived_at);
    EXPECT_EQ(OpenTabsOf(setup.favorite), 1);
  }

  raw_ptr<aerie_tab_model::TabModelService> service_ = nullptr;
  raw_ptr<aerie_tab_model::TabArchiver> archiver_ = nullptr;

 private:
  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

IN_PROC_BROWSER_TEST_F(AerieSidebarClearBrowserTest,
                       ClearArchivesEveryUnpinnedTodayTabAndTheArchiveSaysCleared) {
  const Setup setup = MakeTabs();
  PressClear();

  // Every Today tab is archived with the reason, and its tab is closed.
  EXPECT_TRUE(TodayOrder().empty());
  for (const std::string& id : setup.today) {
    const aerie_tab_model::TabRecord* record = model()->GetTab(id);
    ASSERT_TRUE(record) << id;
    EXPECT_TRUE(record->archived_at) << id;
    ASSERT_TRUE(record->archive) << id;
    EXPECT_EQ(record->archive->reason, "cleared") << id;
    EXPECT_EQ(OpenTabsOf(id), 0) << id;
  }
  // The Archive lists exactly them, each with the reason.
  const std::vector<aerie_tab_model::ArchivedRow> rows =
      aerie_tab_model::ArchivedRows(*model());
  EXPECT_EQ(rows.size(), setup.today.size());
  for (const aerie_tab_model::ArchivedRow& row : rows) {
    EXPECT_EQ(row.reason, "cleared");
  }
  // And the panel says so in the name of each row.
  sidebar()->archive()->TogglePanel();
  ASSERT_TRUE(sidebar()->archive()->panel());
  ASSERT_EQ(sidebar()->archive()->panel()->rows().size(), setup.today.size());
  for (views::View* row : sidebar()->archive()->panel()->rows()) {
    ui::AXNodeData data;
    row->GetViewAccessibility().GetAccessibleNodeData(&data);
    EXPECT_NE(data.GetStringAttribute(ax::mojom::StringAttribute::kName)
                  .find(", cleared, "),
              std::string::npos);
  }
  ExpectPinnedAndFavoritesUntouched(setup);
  // Pinned and Favorites keep the window open: nothing else was opened.
  EXPECT_EQ(strip()->count(), 2);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarClearBrowserTest,
                       UndoBringsTheTabsBackInTheirOrderAndFolders) {
  const Setup setup = MakeTabs();
  // File the second and the fourth tab in a folder, so that the order is a mix
  // of folder and top-level tabs.
  const std::string folder = aerie_tab_model::CreateFolder(
      *model(), Section::kToday, std::string(), "Trips");
  ASSERT_TRUE(aerie_tab_model::MoveTabToFolder(*model(), setup.today[1], folder));
  ASSERT_TRUE(aerie_tab_model::MoveTabToFolder(*model(), setup.today[3], folder));
  ASSERT_TRUE(base::test::RunUntil([&] {
    for (const std::string& id : setup.today) {
      if (!sidebar()->RowForId(id)) {
        return false;
      }
    }
    return true;
  }));
  const std::vector<std::string> before = TodayOrder();
  const std::map<std::string, std::string> parents = ParentsOf(before);
  ASSERT_EQ(before.size(), 4u);

  PressClear();
  ASSERT_TRUE(TodayOrder().empty());
  EXPECT_TRUE(sidebar()->toast_visible());
  EXPECT_EQ(sidebar()->toast()->GetText(), u"4 tabs archived");
  EXPECT_EQ(sidebar()->cleared_ids_for_testing(), before);

  PressUndo();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return TodayOrder().size() == before.size(); }));
  EXPECT_EQ(TodayOrder(), before);
  EXPECT_EQ(ParentsOf(TodayOrder()), parents);
  for (const std::string& id : before) {
    EXPECT_EQ(OpenTabsOf(id), 1) << id;
    EXPECT_FALSE(model()->GetTab(id)->archived_at) << id;
  }
  EXPECT_TRUE(aerie_tab_model::ArchivedRows(*model()).empty());
  ExpectPinnedAndFavoritesUntouched(setup);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarClearBrowserTest,
                       UndoRestoresOnlyWhatIsStillArchived) {
  const Setup setup = MakeTabs();
  PressClear();
  ASSERT_TRUE(TodayOrder().empty());

  // The user opens one of them from the Archive panel before pressing Undo.
  const std::string already = setup.today[2];
  ASSERT_TRUE(sidebar()->archive()->RestoreRecord(already));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return TodayOrder().size() == 1u; }));

  PressUndo();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return TodayOrder().size() == setup.today.size(); }));
  // Each tab is open once: the one that was restored already is not doubled.
  for (const std::string& id : setup.today) {
    EXPECT_EQ(OpenTabsOf(id), 1) << id;
  }
  EXPECT_TRUE(aerie_tab_model::ArchivedRows(*model()).empty());
  EXPECT_EQ(strip()->count(), 6);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarClearBrowserTest,
                       AltClearClosesTheTabsAndKeepsNoRecord) {
  const Setup setup = MakeTabs();
  PressClear(ui::EF_ALT_DOWN);

  EXPECT_TRUE(TodayOrder().empty());
  EXPECT_TRUE(aerie_tab_model::ArchivedRows(*model()).empty());
  for (const std::string& id : setup.today) {
    EXPECT_EQ(OpenTabsOf(id), 0) << id;
    EXPECT_FALSE(model()->GetTab(id) && model()->GetTab(id)->archived_at) << id;
  }
  EXPECT_FALSE(sidebar()->toast_visible());
  ExpectPinnedAndFavoritesUntouched(setup);
  EXPECT_EQ(strip()->count(), 2);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarClearBrowserTest,
                       TheClearMenuHasArchiveAllAndCloseAll) {
  using Command = AerieSidebarContextMenu::Command;
  Setup setup = MakeTabs();
  sidebar()->ShowClearMenuForTesting(gfx::Point(5, 5));
  AerieSidebarContextMenu* menu = sidebar()->context_menu();
  ASSERT_TRUE(menu && menu->is_running());
  ASSERT_TRUE(menu->item(Command::kClearArchive));
  EXPECT_EQ(menu->item(Command::kClearArchive)->title(), u"Archive all");
  ASSERT_TRUE(menu->item(Command::kClearClose));
  EXPECT_EQ(menu->item(Command::kClearClose)->title(), u"Close all");
  menu->ExecuteCommand(Command::kClearClose);
  menu->Close();

  EXPECT_TRUE(TodayOrder().empty());
  EXPECT_TRUE(aerie_tab_model::ArchivedRows(*model()).empty());
  EXPECT_FALSE(sidebar()->toast_visible());
  ExpectPinnedAndFavoritesUntouched(setup);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarClearBrowserTest,
                       ClearInAPrivateWindowClosesAndKeepsNothing) {
  BrowserWindowInterface* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      incognito, embedded_test_server()->GetURL("/title1.html"),
      WindowOpenDisposition::NEW_BACKGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  AerieSidebarView* view = SidebarOf(incognito);
  ASSERT_TRUE(view);
  ASSERT_TRUE(base::test::RunUntil([&] { return view->row_count() == 2u; }));

  PressClear(view);
  // The window stays (it opens a new tab when nothing would be left); nothing
  // was archived and there is nothing to undo.
  ASSERT_TRUE(base::test::RunUntil([&] { return view->row_count() == 1u; }));
  EXPECT_FALSE(view->toast_visible());
  EXPECT_TRUE(view->cleared_ids_for_testing().empty());
  EXPECT_TRUE(aerie_tab_model::ArchivedRows(*model()).empty());
}
