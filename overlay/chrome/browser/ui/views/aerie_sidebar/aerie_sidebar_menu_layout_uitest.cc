// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Interactive UI tests for issue #250: opening a menu in the sidebar must not
// move any view of it. The bounds and visibility of every view in the sidebar
// (the rows, the folder, the tiles, the bottom bar and the Archive button) are
// recorded before, while the pointer hovers the menu's button, while the menu
// is open, and after it closes by Esc and by a click outside. Every snapshot
// must equal the first, to the pixel. Run with
// ./aerie test-cpp --target aerie_interactive_ui_tests.

#include <string>
#include <algorithm>
#include <string_view>
#include <vector>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/test/run_until.h"
#include "chrome/browser/aerie_tab_model/pinned_tabs_controller.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_archive_button.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_context_menu.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_folder_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_peek_driver.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_tile_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_sidebar/aerie_sidebar_peek.h"
#include "components/aerie_sidebar/aerie_sidebar_prefs.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/pinned_tabs.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "ui/aura/window.h"
#include "ui/compositor/layer.h"
#include "ui/events/test/event_generator.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/base/test/ui_controls.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

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

views::View* FindByClassName(views::View* root, std::string_view name) {
  if (root->GetClassName() == name) {
    return root;
  }
  for (views::View* child : root->children()) {
    if (views::View* found = FindByClassName(child, name)) {
      return found;
    }
  }
  return nullptr;
}

// A row's close button or title, which sit in the row's content view.
bool IsRowHoverPart(views::View* view) {
  for (views::View* up = view->parent(); up; up = up->parent()) {
    if (auto* row = views::AsViewClass<AerieSidebarRowView>(up)) {
      return view == row->close_button() || view == row->title_view();
    }
  }
  return false;
}

// One line per view: its place in the tree, its class, whether it is visible
// and where it is on the screen.
//
// Two things are left out because they are hover effects by design, not
// moves: the ink-drop container of a button (a view of size 0x0 that is only
// shown while the button is hot), and a row's own close button and title. The
// close button shows "on hover or focus" (docs/sidebar-design.md, "Today
// list") and the title gives it room by ellipsizing. The design doc confirms
// the close button on hover; it does not say in so many words that the title
// narrows, that follows from the button taking its width.
void Record(views::View* view, const std::string& path, std::string* out) {
  if (view->GetClassName() == "InkDropContainerView") {
    return;
  }
  base::StrAppend(out, {path, " ", std::string(view->GetClassName()),
                        view->GetVisible() ? " visible " : " hidden ",
                        view->GetBoundsInScreen().ToString(), "\n"});
  for (size_t i = 0; i < view->children().size(); ++i) {
    views::View* child = view->children()[i];
    if (IsRowHoverPart(child)) {
      continue;
    }
    Record(child, path + "/" + base::NumberToString(i), out);
  }
}

// The lines of `after` that differ from `before`, as "was ... / now ...".
std::string Changes(const std::string& before, const std::string& after) {
  const std::vector<std::string> a = base::SplitString(
      before, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  const std::vector<std::string> b = base::SplitString(
      after, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  std::string out;
  for (size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
    const std::string was = i < a.size() ? a[i] : "(none)";
    const std::string now = i < b.size() ? b[i] : "(none)";
    if (was != now) {
      base::StrAppend(&out, {"\n  was ", was, "\n  now ", now});
    }
  }
  return out;
}

}  // namespace

// Every test runs in each density: the docked sidebar, the rail and auto-hide.
class AerieSidebarMenuLayoutUiTest
    : public InProcessBrowserTest,
      public testing::WithParamInterface<const char*> {
 protected:
  bool auto_hide() const {
    return std::string_view(GetParam()) ==
           aerie_sidebar::kDensityAutoHide;
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
    BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser());
    view->GetWidget()->SetBounds(gfx::Rect(0, 0, 1100, 750));
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return sidebar() && sidebar()->row_count() == 1u; }));
    PrefService* prefs = browser()->GetProfile()->GetPrefs();
    if (auto_hide()) {
      prefs->SetString(aerie_sidebar::kDensityPref,
                       aerie_sidebar::kDensityAutoHide);
      prefs->SetBoolean(aerie_sidebar::kVisiblePref, false);
      ASSERT_TRUE(
          base::test::RunUntil([&] { return sidebar()->peek_driver()->active(); }));
      ASSERT_TRUE(base::test::RunUntil(
          [&] { return view->multi_contents_view()->x() == 0; }));
      Reveal();
    } else if (std::string_view(GetParam()) != aerie_sidebar::kDensityComfortable) {
      prefs->SetString(aerie_sidebar::kDensityPref, GetParam());
    }
    Settle();
  }

  // Auto-hide: the pointer goes to the edge and the sidebar peeks.
  void Reveal() {
    BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser());
    views::Widget* widget = view->GetWidget();
    ui::test::EventGenerator generator(
        widget->GetNativeWindow()->GetRootWindow(), widget->GetNativeWindow());
    generator.MoveMouseRelativeTo(
        widget->GetNativeWindow(),
        sidebar()->peek_driver()->HotZoneInScreen().CenterPoint() -
            widget->GetWindowBoundsInScreen().OffsetFromOrigin());
    ASSERT_TRUE(base::test::RunUntil([&] {
      return sidebar()->peek_driver()->controller().state() ==
             aerie_sidebar::PeekState::kShown;
    }));
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

  // The sidebar's views, the region that holds them (bounds, layer transform
  // and opacity, so that a slide shows) and the page card.
  std::string Snapshot() {
    std::string out;
    views::View* region = BrowserView::GetBrowserViewForBrowser(browser())
                             ->vertical_tab_strip_region_view_for_testing();
    base::StrAppend(&out, {"region ", region->GetBoundsInScreen().ToString(),
                           region->GetVisible() ? " visible" : " hidden"});
    if (region->layer()) {
      base::StrAppend(&out, {" transform ",
                             region->layer()->GetTargetTransform().ToString(),
                             " opacity ",
                             base::NumberToString(region->layer()->GetTargetOpacity())});
    }
    base::StrAppend(
        &out, {"\npage ",
               BrowserView::GetBrowserViewForBrowser(browser())
                   ->multi_contents_view()
                   ->GetBoundsInScreen()
                   .ToString(),
               "\n"});
    Record(sidebar(), "s", &out);
    return out;
  }

  // After a click outside, auto-hide slides the sidebar away on its own: that
  // is its job. Only the page card is compared then.
  std::string AfterClickOutside(const std::string& before) {
    if (!auto_hide()) {
      return Changes(before, Snapshot());
    }
    const std::string page_before = before.substr(
        before.find("\npage ") + 1, before.find("\ns ") - before.find("\npage ") - 1);
    const std::string now = Snapshot();
    const std::string page_now = now.substr(
        now.find("\npage ") + 1, now.find("\ns ") - now.find("\npage ") - 1);
    return Changes(page_before, page_now);
  }

  // Lets the model, the layout and the animations settle.
  void Settle() {
    base::RunLoop loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), base::Milliseconds(700));
    loop.Run();
  }

  void AddTodayTab() {
    chrome::AddTabAt(browser(), GURL("about:blank"), -1, /*foreground=*/false);
    ASSERT_TRUE(base::test::RunUntil([&] {
      return aerie_tab_model::SectionTabIds(*model(), Section::kToday).size() ==
             static_cast<size_t>(browser()->tab_strip_model()->count());
    }));
    Settle();
  }

  void MoveMouseTo(views::View* view) {
    ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
        view->GetBoundsInScreen().CenterPoint()));
  }

  void ClickOutside() {
    // The middle of the page card, far from the sidebar.
    BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser());
    gfx::Point point = view->GetBoundsInScreen().CenterPoint();
    point.set_x(view->GetBoundsInScreen().right() - 40);
    ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(point));
    ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(
        ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP));
  }

  void PressEsc() {
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_ESCAPE,
                                                false, false, false, false));
  }

  // Hover `target`, open its context menu, close it by Esc, then do it again
  // and close by a click outside. Every step must leave the layout as it was.
  void CheckContextMenu(views::View* target, const std::string& what) {
    SCOPED_TRACE(what);
    const std::string before = Snapshot();
    MoveMouseTo(target);
    ASSERT_NO_FATAL_FAILURE(Settle());
    EXPECT_EQ("", Changes(before, Snapshot())) << "after hovering";
    for (bool by_click : {false, true}) {
      target->ShowContextMenu(target->GetLocalBounds().CenterPoint(),
                              ui::mojom::MenuSourceType::kMouse);
      ASSERT_TRUE(base::test::RunUntil([&] {
        return sidebar()->context_menu() && sidebar()->context_menu()->is_running();
      }));
      ASSERT_NO_FATAL_FAILURE(Settle());
      EXPECT_EQ("", Changes(before, Snapshot())) << "while the menu is open";
      if (by_click) {
        ClickOutside();
      } else {
        PressEsc();
      }
      ASSERT_TRUE(base::test::RunUntil(
          [&] { return !sidebar()->context_menu()->is_running(); }));
      ASSERT_NO_FATAL_FAILURE(Settle());
      EXPECT_EQ("", by_click ? AfterClickOutside(before)
                             : Changes(before, Snapshot()))
          << (by_click ? "after a click outside" : "after Esc");
      if (by_click && auto_hide()) {
        ASSERT_NO_FATAL_FAILURE(Reveal());
      }
    }
  }
};

// The three dots of the toolbar sit in the bottom bar, next to the Archive
// button (issue #250).
IN_PROC_BROWSER_TEST_P(AerieSidebarMenuLayoutUiTest,
                       TheAppMenuLeavesTheSidebarStill) {
  views::View* button = FindByClassName(sidebar(), "BrowserAppMenuButton");
  ASSERT_TRUE(button);
  ASSERT_NO_FATAL_FAILURE(Settle());
  const std::string before = Snapshot();

  MoveMouseTo(button);
  ASSERT_NO_FATAL_FAILURE(Settle());
  EXPECT_EQ("", Changes(before, Snapshot())) << "after hovering the three dots";

  for (bool by_click : {false, true}) {
    ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
        button->GetBoundsInScreen().CenterPoint()));
    ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(
        ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP));
    ASSERT_NO_FATAL_FAILURE(Settle());
    EXPECT_EQ("", Changes(before, Snapshot())) << "while the app menu is open";
    if (by_click) {
      ClickOutside();
    } else {
      PressEsc();
    }
    ASSERT_NO_FATAL_FAILURE(Settle());
    EXPECT_EQ("", by_click ? AfterClickOutside(before)
                           : Changes(before, Snapshot()))
        << (by_click ? "after a click outside" : "after Esc");
    if (by_click && auto_hide()) {
      ASSERT_NO_FATAL_FAILURE(Reveal());
    }
  }
}

IN_PROC_BROWSER_TEST_P(AerieSidebarMenuLayoutUiTest,
                       ARowContextMenuLeavesTheSidebarStill) {
  AddTodayTab();
  // A Pinned tab, a Favorite and a folder.
  AddTodayTab();
  AddTodayTab();
  AddTodayTab();
  TabStripModel* strip = browser()->tab_strip_model();
  ASSERT_TRUE(service()->pinned_tabs()->Pin(strip->GetTabAtIndex(1)));
  ASSERT_EQ(service()->pinned_tabs()->AddToFavorites(strip->GetTabAtIndex(2)),
            aerie_tab_model::FavoriteResult::kAdded);
  const std::string folder = aerie_tab_model::CreateFolder(
      *model(), Section::kToday, std::string(), "Folder");
  ASSERT_TRUE(aerie_tab_model::MoveTabToFolder(
      *model(), service()->adapter()->RecordIdForTab(strip->GetTabAtIndex(3)),
      folder));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return sidebar()->pinned_row_count() == 1u &&
           sidebar()->favorite_tile_count() == 1u &&
           FindDescendant<AerieSidebarFolderRowView>(sidebar());
  }));
  ASSERT_NO_FATAL_FAILURE(Settle());

  ASSERT_NO_FATAL_FAILURE(
      CheckContextMenu(sidebar()->row_at(0), "a Today tab"));
  ASSERT_NO_FATAL_FAILURE(
      CheckContextMenu(sidebar()->pinned_row_at(0), "a Pinned tab"));
  ASSERT_NO_FATAL_FAILURE(
      CheckContextMenu(sidebar()->tile_at(0), "a Favorite"));
  ASSERT_NO_FATAL_FAILURE(CheckContextMenu(
      FindDescendant<AerieSidebarFolderRowView>(sidebar()), "a folder"));
}

INSTANTIATE_TEST_SUITE_P(
    AerieDensity,
    AerieSidebarMenuLayoutUiTest,
    testing::Values(aerie_sidebar::kDensityComfortable,
                    aerie_sidebar::kDensityRail,
                    aerie_sidebar::kDensityAutoHide),
    [](const testing::TestParamInfo<const char*>& info) {
      std::string name = info.param;
      std::replace(name.begin(), name.end(), '-', '_');
      return name;
    });
