// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Interactive UI tests of selecting several sidebar rows (issue #265): Ctrl and
// Shift click, the batch menu and what each of its items does to the whole
// selection, a drag of a selection, the keyboard (Shift+Up/Down, Ctrl+A, Delete
// with a second press to confirm, Esc), and what a screen reader gets: rows that
// are selected, a multiselectable tree, and "Select range to here" and "Clear
// selection". Run with ./aerie test-cpp --target aerie_interactive_ui_tests.

#include <optional>
#include <string>
#include <vector>

#include "base/test/run_until.h"
#include "chrome/browser/aerie_tab_model/pinned_tabs_controller.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_context_menu.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_drag_controller.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_drag_overlay.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_tile_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_tab_model/pinned_tabs.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace {

using aerie_tab_model::Section;
using Command = AerieSidebarContextMenu::Command;
using Ids = std::vector<std::string>;

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

}  // namespace

class AerieSidebarSelectionUiTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    // Motion is tested elsewhere; here a drop and a close land at once.
    reduced_motion_.emplace(
        gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(true));
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    browser_view()->GetWidget()->SetBounds(gfx::Rect(0, 0, 1200, 900));
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return sidebar() && sidebar()->row_count() == 1u; }));
  }
  void TearDownOnMainThread() override {
    reduced_motion_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  AerieSidebarView* sidebar() {
    return FindDescendant<AerieSidebarView>(
        browser_view()->vertical_tab_strip_region_view_for_testing());
  }
  aerie_tab_model::TabModelService* service() {
    return aerie_tab_model::TabModelServiceFactory::GetForProfile(
        browser()->GetProfile());
  }
  views::View* focused() {
    return browser_view()->GetWidget()->GetFocusManager()->GetFocusedView();
  }
  void Press(ui::KeyboardCode key, bool control = false, bool shift = false) {
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), key, control, shift,
                                                false, false));
  }

  // Opens background tabs until the Today list has `total` rows.
  void OpenTabs(size_t total) {
    while (sidebar()->row_count() < total) {
      const size_t before = sidebar()->row_count();
      ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
          browser(), embedded_test_server()->GetURL("/title1.html"),
          WindowOpenDisposition::NEW_BACKGROUND_TAB,
          ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
      ASSERT_TRUE(base::test::RunUntil(
          [&] { return sidebar()->row_count() == before + 1; }));
    }
    SettleSidebar();
  }
  // Waits until every row and tile has a size, no row grows or shrinks, and
  // nothing has moved for ten polls: a click on a row that is not laid out yet
  // misses it.
  void SettleSidebar() {
    std::vector<gfx::Rect> last;
    int same = 0;
    ASSERT_TRUE(base::test::RunUntil([&] {
      std::vector<gfx::Rect> now;
      auto note = [&](views::View* view) {
        now.push_back(view->GetBoundsInScreen());
        return view->width() > 0 && view->height() > 0;
      };
      bool ready = true;
      for (size_t i = 0; i < sidebar()->row_count(); ++i) {
        ready &= note(sidebar()->row_at(i));
        ready &= !sidebar()->row_at(i)->height_animating();
      }
      for (size_t i = 0; i < sidebar()->pinned_row_count(); ++i) {
        ready &= note(sidebar()->pinned_row_at(i));
      }
      for (size_t i = 0; i < sidebar()->favorite_tile_count(); ++i) {
        ready &= note(sidebar()->tile_at(i));
      }
      ready &= sidebar()->closing_row_count() == 0u;
      same = (now == last) ? same + 1 : 0;
      last = now;
      return ready && same >= 10;
    }));
  }
  // Focuses `view` and waits until it has the focus.
  void FocusView(views::View* view) {
    view->RequestFocus();
    ASSERT_TRUE(base::test::RunUntil([&] { return focused() == view; }));
  }
  std::string IdAt(size_t index) { return sidebar()->tab_id_at(index); }
  Ids TodayIds() {
    Ids ids;
    for (size_t i = 0; i < sidebar()->row_count(); ++i) {
      ids.push_back(IdAt(i));
    }
    return ids;
  }
  tabs::TabInterface* TabOf(const std::string& id) {
    return service()->adapter()->TabForRecord(id);
  }
  Section SectionOf(const std::string& id) {
    return service()->model()->GetTab(id)->section;
  }

  // A click on `view` with the modifier flags `flags` (ui::EF_*).
  void Click(views::View* view, int flags = 0) {
    gfx::Point root(view->width() / 2, view->height() / 2);
    views::View::ConvertPointToWidget(view, &root);
    ui::MouseEvent press = Mouse(ui::EventType::kMousePressed, view, root, flags);
    view->OnMousePressed(press);
    ui::MouseEvent release =
        Mouse(ui::EventType::kMouseReleased, view, root, flags);
    view->OnMouseReleased(release);
  }
  ui::MouseEvent Mouse(ui::EventType type,
                       views::View* view,
                       const gfx::Point& root,
                       int flags = 0) {
    gfx::Point local = root;
    views::View::ConvertPointFromWidget(view, &local);
    return ui::MouseEvent(type, local, root, base::TimeTicks::Now(),
                          ui::EF_LEFT_MOUSE_BUTTON | flags,
                          ui::EF_LEFT_MOUSE_BUTTON);
  }
  void CtrlClick(views::View* view) { Click(view, ui::EF_CONTROL_DOWN); }
  void ShiftClick(views::View* view) { Click(view, ui::EF_SHIFT_DOWN); }

  // Rows 0 and 1 and 2 selected: Ctrl+click on the first, Shift+click on the
  // last.
  void SelectRows(size_t first, size_t last) {
    CtrlClick(sidebar()->row_at(first));
    if (last != first) {
      ShiftClick(sidebar()->row_at(last));
    }
  }

  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

// --- Seam 2: clicks --------------------------------------------------------

IN_PROC_BROWSER_TEST_F(AerieSidebarSelectionUiTest, ControlClickTogglesRowsAndShiftClickSelectsARange) {
  OpenTabs(5);
  const Ids ids = TodayIds();
  CtrlClick(sidebar()->row_at(1));
  CtrlClick(sidebar()->row_at(3));
  EXPECT_EQ(sidebar()->selected_ids(), (Ids{ids[1], ids[3]}));
  EXPECT_TRUE(sidebar()->row_at(1)->multi_selected());
  EXPECT_FALSE(sidebar()->row_at(2)->multi_selected());
  EXPECT_TRUE(sidebar()->row_at(3)->multi_selected());
  // Ctrl+click on a selected row takes it out.
  CtrlClick(sidebar()->row_at(1));
  EXPECT_EQ(sidebar()->selected_ids(), (Ids{ids[3]}));
  // Shift+click runs from the last row that was clicked, even one that the
  // click took out.
  CtrlClick(sidebar()->row_at(3));
  CtrlClick(sidebar()->row_at(3));
  ASSERT_EQ(sidebar()->selected_ids(), (Ids{ids[3]}));
  ShiftClick(sidebar()->row_at(4));
  EXPECT_EQ(sidebar()->selected_ids(), (Ids{ids[3], ids[4]}));
  ShiftClick(sidebar()->row_at(1));
  EXPECT_EQ(sidebar()->selected_ids(), (Ids{ids[1], ids[2], ids[3]}));
  // Ctrl+click does not open the tab.
  EXPECT_NE(browser()->tab_strip_model()->GetActiveTab(), TabOf(ids[3]));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSelectionUiTest, ShiftClickWithNothingSelectedStartsAtTheOpenTab) {
  OpenTabs(4);
  const Ids ids = TodayIds();
  ASSERT_EQ(browser()->tab_strip_model()->GetActiveTab(), TabOf(ids[0]));
  ShiftClick(sidebar()->row_at(2));
  EXPECT_EQ(sidebar()->selected_ids(), (Ids{ids[0], ids[1], ids[2]}));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSelectionUiTest, APlainClickEndsTheSelectionAndOpensTheTab) {
  OpenTabs(4);
  const Ids ids = TodayIds();
  SelectRows(1, 2);
  ASSERT_EQ(sidebar()->selected_ids().size(), 2u);
  Click(sidebar()->row_at(3));
  EXPECT_TRUE(sidebar()->selected_ids().empty());
  EXPECT_FALSE(sidebar()->row_at(1)->multi_selected());
  EXPECT_EQ(browser()->tab_strip_model()->GetActiveTab(), TabOf(ids[3]));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSelectionUiTest, ARangeDoesNotCrossFromPinnedIntoToday) {
  OpenTabs(4);
  const Ids ids = TodayIds();
  ASSERT_TRUE(service()->pinned_tabs()->Pin(TabOf(ids[1])));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->pinned_row_count() == 1u; }));
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->row_count() == 3u; }));
  SettleSidebar();
  CtrlClick(sidebar()->pinned_row_at(0));
  ASSERT_EQ(sidebar()->selected_ids().size(), 1u);
  ShiftClick(sidebar()->row_at(2));
  // The anchor is in Pinned and the click in Today: the selection is the click.
  EXPECT_EQ(sidebar()->selected_ids(), (Ids{sidebar()->tab_id_at(2)}));
}

// --- Seam 2: the batch menu ------------------------------------------------

IN_PROC_BROWSER_TEST_F(AerieSidebarSelectionUiTest, RightClickOnASelectedRowOpensTheBatchMenuThatNamesTheCount) {
  OpenTabs(4);
  SelectRows(1, 2);
  sidebar()->row_at(1)->ShowContextMenu(gfx::Point(5, 5),
                                        ui::mojom::MenuSourceType::kMouse);
  AerieSidebarContextMenu* menu = sidebar()->context_menu();
  ASSERT_TRUE(menu && menu->is_running());
  for (Command command :
       {Command::kClose, Command::kArchive, Command::kSleepTab, Command::kPin,
        Command::kAddToFavorites, Command::kMoveToNewFolder,
        Command::kClearSelection}) {
    EXPECT_TRUE(menu->item(command)) << static_cast<int>(command);
  }
  // It acts on the selection, so its items say so; a single tab's items that
  // make no sense for many are not in it.
  EXPECT_EQ(menu->item(Command::kClose)->title(), u"Close 2 tabs");
  EXPECT_EQ(menu->item(Command::kArchive)->title(), u"Archive 2 tabs");
  EXPECT_FALSE(menu->item(Command::kRename));
  EXPECT_FALSE(menu->item(Command::kUnpin));
  menu->Close();
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSelectionUiTest, RightClickOnARowOutsideTheSelectionOpensTheMenuOfThatTabAlone) {
  OpenTabs(4);
  SelectRows(1, 2);
  sidebar()->row_at(3)->ShowContextMenu(gfx::Point(5, 5),
                                        ui::mojom::MenuSourceType::kMouse);
  AerieSidebarContextMenu* menu = sidebar()->context_menu();
  ASSERT_TRUE(menu && menu->is_running());
  EXPECT_TRUE(menu->item(Command::kRename));
  EXPECT_EQ(menu->item(Command::kClose)->title(), u"Close tab");
  // While rows are selected a row's menu can extend the range or end it.
  EXPECT_TRUE(menu->item(Command::kSelectRange));
  EXPECT_TRUE(menu->item(Command::kClearSelection));
  menu->Close();
  // The selection is still there.
  EXPECT_EQ(sidebar()->selected_ids().size(), 2u);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSelectionUiTest, CloseInTheBatchMenuClosesEveryTabOfTheSelection) {
  OpenTabs(4);
  const Ids ids = TodayIds();
  SelectRows(1, 2);
  sidebar()->row_at(1)->ShowContextMenu(gfx::Point(5, 5),
                                        ui::mojom::MenuSourceType::kMouse);
  sidebar()->SelectionCommandForTesting(Command::kClose);
  sidebar()->CloseContextMenuForTesting();
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->row_count() == 2u; }));
  EXPECT_EQ(TodayIds(), (Ids{ids[0], ids[3]}));
  EXPECT_TRUE(sidebar()->selected_ids().empty());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSelectionUiTest, TabsThatCloseWhileTheBatchMenuIsOpenAreLeftAlone) {
  OpenTabs(4);
  const Ids ids = TodayIds();
  SelectRows(1, 2);
  sidebar()->row_at(1)->ShowContextMenu(gfx::Point(5, 5),
                                        ui::mojom::MenuSourceType::kMouse);
  // Both selected tabs close under the open menu.
  TabStripModel* strip_model = browser()->tab_strip_model();
  for (size_t i : {2u, 1u}) {
    const int index = strip_model->GetIndexOfWebContents(TabOf(ids[i])->GetContents());
    ASSERT_GE(index, 0);
    strip_model->CloseWebContentsAt(index, TabCloseTypes::CLOSE_NONE);
  }
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->row_count() == 2u; }));
  const size_t folders = service()->model()->folders().size();
  // Nothing is left for the menu to act on: no crash and no effect.
  sidebar()->SelectionCommandForTesting(Command::kMoveToNewFolder);
  EXPECT_EQ(service()->model()->folders().size(), folders);
  EXPECT_EQ(TodayIds(), (Ids{ids[0], ids[3]}));
  sidebar()->SelectionCommandForTesting(Command::kClose);
  sidebar()->CloseContextMenuForTesting();
  EXPECT_EQ(TodayIds(), (Ids{ids[0], ids[3]}));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSelectionUiTest, ArchiveInTheBatchMenuArchivesEveryTabOfTheSelection) {
  OpenTabs(4);
  const Ids ids = TodayIds();
  SelectRows(1, 2);
  ASSERT_EQ(sidebar()->selected_ids(), (Ids{ids[1], ids[2]}));
  sidebar()->SelectionCommandForTesting(Command::kArchive);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return service()->model()->GetTab(ids[1])->archived_at.has_value() &&
           service()->model()->GetTab(ids[2])->archived_at.has_value();
  }));
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->row_count() == 2u; }));
  EXPECT_EQ(TodayIds(), (Ids{ids[0], ids[3]}));
  for (size_t i : {1u, 2u}) {
    const aerie_tab_model::TabRecord* record = service()->model()->GetTab(ids[i]);
    ASSERT_TRUE(record);
    EXPECT_TRUE(record->archived_at.has_value()) << i;
  }
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSelectionUiTest, SleepInTheBatchMenuPutsEveryTabOfTheSelectionToSleep) {
  OpenTabs(4);
  SelectRows(1, 2);
  sidebar()->SelectionCommandForTesting(Command::kSleepTab);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return sidebar()->row_at(1)->sleeping() && sidebar()->row_at(2)->sleeping();
  }));
  EXPECT_FALSE(sidebar()->row_at(3)->sleeping());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSelectionUiTest, PinAddToFavoritesAndUnpinActOnTheWholeSelection) {
  OpenTabs(5);
  const Ids ids = TodayIds();
  SelectRows(1, 2);
  sidebar()->SelectionCommandForTesting(Command::kPin);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->pinned_row_count() == 2u; }));
  EXPECT_EQ(SectionOf(ids[1]), Section::kPinned);
  EXPECT_EQ(SectionOf(ids[2]), Section::kPinned);
  EXPECT_EQ(SectionOf(ids[3]), Section::kToday);
  // The two rows are still selected, now in Pinned: Unpin undoes it.
  EXPECT_EQ(sidebar()->selected_ids(), (Ids{ids[1], ids[2]}));
  sidebar()->SelectionCommandForTesting(Command::kUnpin);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->pinned_row_count() == 0u; }));
  EXPECT_EQ(SectionOf(ids[1]), Section::kToday);

  // Add to Favorites.
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->row_count() == 5u; }));
  SettleSidebar();
  sidebar()->ClearSelection();
  SelectRows(3, 4);
  ASSERT_EQ(sidebar()->selected_ids(), (Ids{ids[3], ids[4]}));
  sidebar()->SelectionCommandForTesting(Command::kAddToFavorites);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->favorite_tile_count() == 2u; }));
  EXPECT_EQ(SectionOf(ids[3]), Section::kFavorite);
  EXPECT_EQ(SectionOf(ids[4]), Section::kFavorite);
  EXPECT_EQ(sidebar()->selected_ids(), (Ids{ids[3], ids[4]}));
  // And the tiles can be selected the same way.
  SettleSidebar();
  sidebar()->ClearSelection();
  CtrlClick(sidebar()->tile_at(0));
  ShiftClick(sidebar()->tile_at(1));
  EXPECT_EQ(sidebar()->selected_ids().size(), 2u);
  EXPECT_TRUE(sidebar()->tile_at(0)->multi_selected());
  sidebar()->SelectionCommandForTesting(Command::kRemoveFromFavorites);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->favorite_tile_count() == 0u; }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSelectionUiTest, MoveToFolderPutsTheSelectionInTheFolderInOrder) {
  OpenTabs(5);
  const Ids ids = TodayIds();
  const std::string folder = sidebar()->CreateFolderIn(Section::kToday, "");
  ASSERT_FALSE(folder.empty());
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->FolderRowForId(folder); }));
  // The rows shifted by the folder row: find the tab rows by id.
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->RowForId(ids[1]); }));
  CtrlClick(sidebar()->RowForId(ids[3]));
  CtrlClick(sidebar()->RowForId(ids[1]));
  sidebar()->RowForId(ids[1])->ShowContextMenu(
      gfx::Point(5, 5), ui::mojom::MenuSourceType::kMouse);
  AerieSidebarContextMenu* menu = sidebar()->context_menu();
  ASSERT_TRUE(menu && menu->item(Command::kMoveToFolderList));
  // The one folder is the one entry of the list.
  sidebar()->SelectionCommandForTesting(
      AerieSidebarContextMenu::MoveToFolderCommand(0));
  menu->Close();
  for (const std::string& id : {ids[1], ids[3]}) {
    EXPECT_EQ(service()->model()->GetTab(id)->parent, folder) << id;
  }
  EXPECT_LT(service()->model()->GetTab(ids[1])->position,
            service()->model()->GetTab(ids[3])->position);
  EXPECT_EQ(service()->model()->GetTab(ids[2])->parent, "");
}

// --- Seam 2: drag ----------------------------------------------------------

IN_PROC_BROWSER_TEST_F(AerieSidebarSelectionUiTest, DraggingASelectedRowMovesTheWholeSelectionInOrder) {
  OpenTabs(5);
  const Ids ids = TodayIds();
  CtrlClick(sidebar()->row_at(0));
  CtrlClick(sidebar()->row_at(2));
  CtrlClick(sidebar()->row_at(3));
  ASSERT_EQ(sidebar()->selected_ids(), (Ids{ids[0], ids[2], ids[3]}));

  views::View* row = sidebar()->row_at(2);
  gfx::Point start(row->width() / 2, row->height() / 2);
  views::View::ConvertPointToWidget(row, &start);
  ui::MouseEvent press = Mouse(ui::EventType::kMousePressed, row, start);
  row->OnMousePressed(press);
  // The last row's lower half: after it.
  views::View* last = sidebar()->row_at(4);
  gfx::Point target(last->width() / 2, last->height() * 9 / 10);
  views::View::ConvertPointToWidget(last, &target);
  for (int i = 1; i <= 6; ++i) {
    gfx::Point p(start.x() + (target.x() - start.x()) * i / 6,
                 start.y() + (target.y() - start.y()) * i / 6);
    ui::MouseEvent drag = Mouse(ui::EventType::kMouseDragged, row, p);
    row->OnMouseDragged(drag);
  }
  ASSERT_TRUE(sidebar()->dragging());
  AerieSidebarDragController* controller = sidebar()->drag_controller();
  ASSERT_TRUE(controller && controller->ghost());
  // The ghost carries a count badge, and the three rows are out of sight.
  EXPECT_TRUE(controller->ghost()->has_count_badge());
  EXPECT_EQ(controller->hidden_ids().size(), 3u);
  ui::MouseEvent release = Mouse(ui::EventType::kMouseReleased, row, target);
  row->OnMouseReleased(release);

  // The three go to the end, in the order they had; the others keep theirs.
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !sidebar()->dragging() && TodayIds().size() == 5u &&
                   TodayIds()[0] == ids[1]; }));
  EXPECT_EQ(TodayIds(), (Ids{ids[1], ids[4], ids[0], ids[2], ids[3]}));
  EXPECT_EQ(sidebar()->selected_ids(), (Ids{ids[0], ids[2], ids[3]}));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSelectionUiTest, DraggingARowOutsideTheSelectionDragsThatRowAndEndsTheSelection) {
  OpenTabs(4);
  const Ids ids = TodayIds();
  SelectRows(0, 1);
  views::View* row = sidebar()->row_at(3);
  gfx::Point start(row->width() / 2, row->height() / 2);
  views::View::ConvertPointToWidget(row, &start);
  ui::MouseEvent press = Mouse(ui::EventType::kMousePressed, row, start);
  row->OnMousePressed(press);
  for (int i = 1; i <= 3; ++i) {
    ui::MouseEvent drag = Mouse(ui::EventType::kMouseDragged, row,
                                start + gfx::Vector2d(0, -8 * i));
    row->OnMouseDragged(drag);
  }
  ASSERT_TRUE(sidebar()->dragging());
  EXPECT_FALSE(sidebar()->drag_controller()->ghost()->has_count_badge());
  EXPECT_TRUE(sidebar()->selected_ids().empty());
  sidebar()->EndDragForTesting();
}

// --- Seam 2: keyboard ------------------------------------------------------

IN_PROC_BROWSER_TEST_F(AerieSidebarSelectionUiTest, ShiftArrowsExtendAndCtrlASelectsTheSectionAndEscClears) {
  OpenTabs(4);
  const Ids ids = TodayIds();
  FocusView(sidebar()->row_at(0));
  Press(ui::VKEY_DOWN, /*control=*/false, /*shift=*/true);
  EXPECT_EQ(sidebar()->selected_ids(), (Ids{ids[0], ids[1]}));
  EXPECT_EQ(focused(), sidebar()->row_at(1));
  Press(ui::VKEY_DOWN, false, true);
  EXPECT_EQ(sidebar()->selected_ids(), (Ids{ids[0], ids[1], ids[2]}));
  Press(ui::VKEY_UP, false, true);
  EXPECT_EQ(sidebar()->selected_ids(), (Ids{ids[0], ids[1]}));
  EXPECT_EQ(sidebar()->last_announcement_for_testing(), u"2 selected");

  Press(ui::VKEY_ESCAPE);
  EXPECT_TRUE(sidebar()->selected_ids().empty());
  // Esc with nothing selected is not the sidebar's.
  EXPECT_FALSE(sidebar()->row_at(0)->multi_selected());

  Press(ui::VKEY_A, /*control=*/true);
  EXPECT_EQ(sidebar()->selected_ids(), ids);
  EXPECT_EQ(sidebar()->last_announcement_for_testing(), u"4 selected");
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSelectionUiTest, DeleteOnASelectionAsksFirstAndClosesOnTheSecondPress) {
  OpenTabs(4);
  const Ids ids = TodayIds();
  FocusView(sidebar()->row_at(0));
  Press(ui::VKEY_DOWN, false, true);
  Press(ui::VKEY_DOWN, false, true);
  ASSERT_EQ(sidebar()->selected_ids().size(), 3u);
  Press(ui::VKEY_DELETE);
  // The first press only asks.
  EXPECT_TRUE(sidebar()->close_selection_armed());
  EXPECT_TRUE(sidebar()->toast_visible());
  EXPECT_EQ(sidebar()->toast()->GetText(), u"Press Delete again to close 3 tabs");
  EXPECT_EQ(sidebar()->row_count(), 4u);
  EXPECT_EQ(sidebar()->selected_ids().size(), 3u);
  Press(ui::VKEY_DELETE);
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->row_count() == 1u; }));
  EXPECT_EQ(TodayIds(), (Ids{ids[3]}));
  EXPECT_FALSE(sidebar()->close_selection_armed());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSelectionUiTest, ChangingTheSelectionTakesTheDeleteQuestionBack) {
  OpenTabs(4);
  FocusView(sidebar()->row_at(0));
  Press(ui::VKEY_DOWN, false, true);
  Press(ui::VKEY_DELETE);
  ASSERT_TRUE(sidebar()->close_selection_armed());
  Press(ui::VKEY_DOWN, false, true);
  EXPECT_FALSE(sidebar()->close_selection_armed());
  Press(ui::VKEY_DELETE);
  EXPECT_EQ(sidebar()->row_count(), 4u);
  EXPECT_TRUE(sidebar()->close_selection_armed());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSelectionUiTest, DeleteOnASingleFocusedRowStillClosesAtOnce) {
  OpenTabs(3);
  // Only the roving stop takes focus; the arrow key moves on from it.
  FocusView(sidebar()->row_at(0));
  Press(ui::VKEY_DOWN);
  ASSERT_EQ(focused(), sidebar()->row_at(1));
  Press(ui::VKEY_DELETE);
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->row_count() == 2u; }));
}

// --- Seam 3: accessibility -------------------------------------------------

IN_PROC_BROWSER_TEST_F(AerieSidebarSelectionUiTest, SelectedRowsReportSelectedAndTheTreeIsMultiselectable) {
  OpenTabs(4);
  SelectRows(1, 2);
  auto data_of = [](views::View* view) {
    ui::AXNodeData data;
    view->GetViewAccessibility().GetAccessibleNodeData(&data);
    return data;
  };
  // The open tab is the first row and reads as selected as it always did; the
  // rows of the selection join it, and the one left out does not.
  EXPECT_TRUE(data_of(sidebar()->row_at(1))
                  .GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
  EXPECT_TRUE(data_of(sidebar()->row_at(2))
                  .GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
  EXPECT_FALSE(data_of(sidebar()->row_at(3))
                   .GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
  EXPECT_TRUE(data_of(sidebar()->today_tree())
                  .HasState(ax::mojom::State::kMultiselectable));
  EXPECT_TRUE(data_of(sidebar()->pinned_tree())
                  .HasState(ax::mojom::State::kMultiselectable));
  EXPECT_TRUE(data_of(sidebar()->favorites_grid())
                  .HasState(ax::mojom::State::kMultiselectable));
  sidebar()->ClearSelection();
  EXPECT_FALSE(data_of(sidebar()->row_at(1))
                   .GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSelectionUiTest, SelectRangeToHereAndClearSelectionAreActionsForAssistiveTechnology) {
  OpenTabs(5);
  const Ids ids = TodayIds();
  CtrlClick(sidebar()->row_at(1));
  // "Select range to here" is a standard set-selection action on the row, and
  // an item of the row's menu; "Clear selection" is an item of that menu.
  ui::AXNodeData data;
  sidebar()->row_at(3)->GetViewAccessibility().GetAccessibleNodeData(&data);
  EXPECT_TRUE(data.HasAction(ax::mojom::Action::kSetSelection));
  ui::AXActionData action;
  action.action = ax::mojom::Action::kSetSelection;
  EXPECT_TRUE(sidebar()->row_at(3)->HandleAccessibleAction(action));
  EXPECT_EQ(sidebar()->selected_ids(), (Ids{ids[1], ids[2], ids[3]}));
  EXPECT_EQ(sidebar()->last_announcement_for_testing(), u"3 selected");

  sidebar()->row_at(4)->ShowContextMenu(gfx::Point(5, 5),
                                        ui::mojom::MenuSourceType::kKeyboard);
  AerieSidebarContextMenu* menu = sidebar()->context_menu();
  ASSERT_TRUE(menu && menu->is_running());
  ASSERT_TRUE(menu->item(Command::kSelectRange));
  EXPECT_EQ(menu->item(Command::kSelectRange)->title(), u"Select range to here");
  ASSERT_TRUE(menu->item(Command::kClearSelection));
  EXPECT_EQ(menu->item(Command::kClearSelection)->title(), u"Clear selection");
  menu->ExecuteCommand(Command::kSelectRange);
  EXPECT_EQ(sidebar()->selected_ids(), (Ids{ids[1], ids[2], ids[3], ids[4]}));
  menu->ExecuteCommand(Command::kClearSelection);
  menu->Close();
  EXPECT_TRUE(sidebar()->selected_ids().empty());
}
