// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Dragging tabs and folders between Today, Pinned, folders and Favorites
// (issue #182), with synthetic mouse events on the rows and tiles. Motion is
// forced off here; aerie_sidebar_drag_uitest.cc has it. Run with
// ./aerie test-cpp --target aerie_browser_tests --gtest-filter='AerieSidebarDrag*'.

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_drag_controller.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_drag_host.h"
#include "components/aerie_sidebar/aerie_sidebar_drop_target.h"
#include "components/aerie_sidebar/aerie_sidebar_move.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_tile_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/pinned_tabs.h"
#include "components/aerie_tab_model/tab_model_types.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/compositor_observer.h"
#include "ui/base/cursor/mojom/cursor_type.mojom.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/focus/focus_manager.h"
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

}  // namespace

class AerieSidebarDragBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    // The test window can be tiny; a drop is "outside" past its bounds.
    BrowserView::GetBrowserViewForBrowser(browser())->GetWidget()->SetBounds(
        gfx::Rect(0, 0, 1200, 900));
    // Motion is not what these tests are about; the uitest has it.
    reduced_motion_.emplace(
        gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(true));
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

  template <typename Predicate>
  bool WaitFor(Predicate done) {
    const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(10);
    while (!done() && base::TimeTicks::Now() < deadline) {
      base::RunLoop loop;
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE, loop.QuitClosure(), base::Milliseconds(20));
      loop.Run();
    }
    return done();
  }

  // Opens tabs until the window has `total`, and waits for the rows to rest.
  void OpenTabs(size_t total) {
    while (browser()->tab_strip_model()->count() < static_cast<int>(total)) {
      chrome::ExecuteCommand(browser(), IDC_NEW_TAB);
    }
    ASSERT_TRUE(WaitFor([&] { return sidebar()->row_count() == total; }));
    ASSERT_TRUE(WaitFor([&] { return AllRowsRest(); }));
  }

  bool AllRowsRest() {
    for (size_t i = 0; i < sidebar()->row_count(); ++i) {
      if (sidebar()->row_at(i)->height_animating()) {
        return false;
      }
    }
    for (size_t i = 0; i < sidebar()->pinned_row_count(); ++i) {
      if (sidebar()->pinned_row_at(i)->height_animating()) {
        return false;
      }
    }
    return sidebar()->closing_row_count() == 0u;
  }

  void WaitForRows() {
    ASSERT_TRUE(WaitFor([&] { return AllRowsRest(); }));
    // The model and the sidebar agree: the scheduled sync has run.
    base::RunLoop().RunUntilIdle();
  }

  std::string IdAt(int index) {
    return service()->adapter()->RecordIdForTab(
        browser()->tab_strip_model()->GetTabAtIndex(index));
  }

  std::string NewFolder(Section section,
                        const std::string& parent,
                        const std::string& name) {
    const std::string id =
        aerie_tab_model::CreateFolder(*model(), section, parent, name);
    EXPECT_FALSE(id.empty());
    EXPECT_TRUE(WaitFor([&] { return sidebar()->FolderRowForId(id); }));
    return id;
  }

  // The ids of the children of a folder (empty: of the section) in order.
  std::vector<std::string> Children(Section section,
                                    const std::string& parent = std::string()) {
    std::vector<std::string> ids;
    for (const auto& child : aerie_tab_model::OrderedChildren(
             *model(), section, parent, false,
             [](const std::string&) { return true; })) {
      ids.push_back(child.id);
    }
    return ids;
  }

  views::View* ViewFor(const std::string& id) {
    if (views::View* row = sidebar()->RowForId(id)) {
      return row;
    }
    for (size_t i = 0; i < sidebar()->favorite_tile_count(); ++i) {
      if (sidebar()->favorite_tab_id_at(i) == id) {
        return sidebar()->tile_at(i);
      }
    }
    return nullptr;
  }

  // A point of `view` in the widget's coordinates; `y_percent` of its height.
  gfx::Point PointIn(views::View* view, int y_percent = 50, int x = -1) {
    gfx::Point p(x < 0 ? view->width() / 2 : x,
                 view->height() * y_percent / 100);
    views::View::ConvertPointToWidget(view, &p);
    return p;
  }

  ui::MouseEvent Mouse(ui::EventType type,
                       views::View* view,
                       const gfx::Point& root) {
    gfx::Point local = root;
    views::View::ConvertPointFromWidget(view, &local);
    return ui::MouseEvent(type, local, root, base::TimeTicks::Now(),
                          ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  }

  void Press(views::View* view) {
    ui::MouseEvent event =
        Mouse(ui::EventType::kMousePressed, view, PointIn(view));
    view->OnMousePressed(event);
    start_ = PointIn(view);
  }
  // Moves the pointer to `target` in `steps` steps.
  void DragTo(views::View* view, const gfx::Point& target, int steps = 4) {
    for (int i = 1; i <= steps; ++i) {
      gfx::Point p(start_.x() + (target.x() - start_.x()) * i / steps,
                   start_.y() + (target.y() - start_.y()) * i / steps);
      ui::MouseEvent event = Mouse(ui::EventType::kMouseDragged, view, p);
      view->OnMouseDragged(event);
    }
  }
  void Release(views::View* view, const gfx::Point& at) {
    ui::MouseEvent event = Mouse(ui::EventType::kMouseReleased, view, at);
    view->OnMouseReleased(event);
  }
  // Starts the drag. The empty Pinned and Favorites zones open when it begins,
  // so every point is read after this.
  void Lift(views::View* view) {
    DragTo(view, start_ + gfx::Vector2d(0, 12), 2);
  }
  // A whole drag from the item `id` to `y_percent` of the view `target()`
  // returns, read once the drag has begun.
  template <typename Target>
  void DragAndDrop(const std::string& id, const Target& target, int y_percent) {
    views::View* view = ViewFor(id);
    ASSERT_TRUE(view);
    Press(view);
    Lift(view);
    const gfx::Point point = PointIn(target(), y_percent);
    DragTo(view, point);
    Release(view, point);
  }

  AerieSidebarDragController* controller() {
    return sidebar()->drag_controller();
  }

  gfx::Point start_;

 private:
  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, TheControllerIsMadeOnTheFirstDrag) {
  OpenTabs(3);
  EXPECT_EQ(controller(), nullptr);
  views::View* row = sidebar()->row_at(0);
  Press(row);
  Lift(row);
  DragTo(row, PointIn(sidebar()->row_at(2), 80));
  ASSERT_NE(controller(), nullptr);
  EXPECT_TRUE(sidebar()->dragging());
  Release(row, PointIn(sidebar()->row_at(2), 80));
  EXPECT_FALSE(sidebar()->dragging());
}

// The empty Pinned and Favorites zones float over the list: no row moves when
// a drag starts.
IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, StartingADragDoesNotMoveAnyRow) {
  OpenTabs(3);
  std::vector<int> ys;
  for (size_t i = 0; i < 3; ++i) {
    ys.push_back(sidebar()->row_at(i)->y());
  }
  views::View* row = sidebar()->row_at(0);
  Press(row);
  Lift(row);
  ASSERT_TRUE(sidebar()->dragging());
  ASSERT_FALSE(sidebar()->drag_host_for_testing()->PinnedZone().IsEmpty());
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(sidebar()->row_at(i)->y(), ys[i]) << i;
  }
  Release(row, PointIn(row, 50));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, DraggingARowDownReordersTodayInTheModelAndTheList) {
  OpenTabs(3);
  const std::string a = IdAt(0), b = IdAt(1), c = IdAt(2);
  ASSERT_EQ(sidebar()->tab_id_at(0), a);
  DragAndDrop(a, [&] { return sidebar()->row_at(2); }, 80);
  EXPECT_EQ(Children(Section::kToday), (std::vector<std::string>{b, c, a}));
  EXPECT_EQ(sidebar()->tab_id_at(0), b);
  EXPECT_EQ(sidebar()->tab_id_at(1), c);
  EXPECT_EQ(sidebar()->tab_id_at(2), a);
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(model()->GetTab(sidebar()->tab_id_at(i))->position, i);
  }
  EXPECT_LT(sidebar()->row_at(0)->y(), sidebar()->row_at(1)->y());
  EXPECT_LT(sidebar()->row_at(1)->y(), sidebar()->row_at(2)->y());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, DraggingARowUpMovesItAboveTheOthers) {
  OpenTabs(3);
  const std::string a = IdAt(0), b = IdAt(1), c = IdAt(2);
  DragAndDrop(c, [&] { return sidebar()->row_at(0); }, 20);
  EXPECT_EQ(Children(Section::kToday), (std::vector<std::string>{c, a, b}));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, ADropOnItsOwnSlotChangesNothing) {
  OpenTabs(3);
  const std::string a = IdAt(0), b = IdAt(1), c = IdAt(2);
  const int64_t before = model()->next_change();
  DragAndDrop(a, [&] { return sidebar()->row_at(0); }, 85);
  EXPECT_EQ(Children(Section::kToday), (std::vector<std::string>{a, b, c}));
  EXPECT_EQ(model()->next_change(), before);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, APressThatDoesNotMoveFarStillSelectsTheTab) {
  OpenTabs(3);
  ASSERT_EQ(browser()->tab_strip_model()->active_index(), 2);
  views::View* row = sidebar()->row_at(0);
  Press(row);
  gfx::Point near = PointIn(row);
  near.Offset(0, 2);
  DragTo(row, near);
  EXPECT_FALSE(sidebar()->dragging());
  Release(row, near);
  EXPECT_EQ(browser()->tab_strip_model()->active_index(), 0);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, ADragDoesNotSelectTheTab) {
  OpenTabs(3);
  tabs::TabInterface* active = browser()->tab_strip_model()->GetActiveTab();
  DragAndDrop(IdAt(0), [&] { return sidebar()->row_at(2); }, 80);
  EXPECT_EQ(browser()->tab_strip_model()->GetActiveTab(), active);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, ADropOnTheMiddleOfAFolderPutsTheTabInIt) {
  OpenTabs(2);
  const std::string a = IdAt(0), b = IdAt(1);
  const std::string folder = NewFolder(Section::kToday, "", "Trips");
  WaitForRows();
  // The folder is the first row now (new folders go to the top).
  views::View* folder_row = sidebar()->RowForId(folder);
  ASSERT_TRUE(folder_row);
  views::View* source = sidebar()->RowForId(a);
  Press(source);
  Lift(source);
  const gfx::Point middle = PointIn(folder_row, 50);
  DragTo(source, middle);
  ASSERT_TRUE(controller());
  EXPECT_EQ(controller()->target().kind, aerie_sidebar::DropKind::kInto);
  EXPECT_EQ(controller()->indicator()->mode(),
            AerieSidebarDropIndicator::Mode::kOutline);
  Release(source, middle);
  EXPECT_EQ(model()->GetTab(a)->parent, folder);
  EXPECT_EQ(model()->GetTab(b)->parent, "");
  EXPECT_EQ(Children(Section::kToday, folder), (std::vector<std::string>{a}));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, TheEdgesOfAFolderRowMeanBeforeAndAfter) {
  OpenTabs(2);
  const std::string a = IdAt(0);
  const std::string folder = NewFolder(Section::kToday, "", "Trips");
  WaitForRows();
  views::View* folder_row = sidebar()->RowForId(folder);
  views::View* source = sidebar()->RowForId(a);
  Press(source);
  Lift(source);
  DragTo(source, PointIn(folder_row, 5));
  EXPECT_EQ(controller()->target().kind, aerie_sidebar::DropKind::kBefore);
  DragTo(source, PointIn(folder_row, 95));
  // An open folder's bottom edge is "first child".
  EXPECT_EQ(controller()->target().kind, aerie_sidebar::DropKind::kFirstChild);
  Release(source, PointIn(folder_row, 95));
  EXPECT_EQ(model()->GetTab(a)->parent, folder);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, ATabDroppedOnAnEmptyPinnedSectionIsPinned) {
  OpenTabs(2);
  const std::string a = IdAt(0);
  views::View* source = sidebar()->RowForId(a);
  Press(source);
  Lift(source);
  // Dragging reveals room for the empty Pinned list.
  gfx::Point probe = PointIn(source);
  probe.Offset(0, -10);
  DragTo(source, probe, 2);
  ASSERT_TRUE(controller() && controller()->active());
  const gfx::Rect zone = sidebar()->drag_host_for_testing()->PinnedZone();
  ASSERT_FALSE(zone.IsEmpty());
  const gfx::Point pinned = zone.CenterPoint();
  DragTo(source, pinned);
  EXPECT_EQ(controller()->target().section, Section::kPinned);
  Release(source, pinned);
  EXPECT_EQ(model()->GetTab(a)->section, Section::kPinned);
  EXPECT_TRUE(model()->GetTab(a)->pinned_url.has_value());
  ASSERT_TRUE(WaitFor([&] { return sidebar()->pinned_row_count() == 1u; }));
  EXPECT_EQ(sidebar()->pinned_tab_id_at(0), a);
  EXPECT_EQ(sidebar()->row_count(), 1u);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, APinnedTabDraggedToTodayIsUnpinned) {
  OpenTabs(2);
  const std::string a = IdAt(0);
  aerie_tab_model::PinTab(*model(), a, "https://a.test/");
  ASSERT_TRUE(WaitFor([&] { return sidebar()->pinned_row_count() == 1u; }));
  WaitForRows();
  DragAndDrop(a, [&] { return sidebar()->row_at(0); }, 80);
  EXPECT_EQ(model()->GetTab(a)->section, Section::kToday);
  EXPECT_FALSE(model()->GetTab(a)->pinned_url.has_value());
  EXPECT_EQ(sidebar()->pinned_row_count(), 0u);
  EXPECT_EQ(sidebar()->row_count(), 2u);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, ATabDroppedOnFavoritesBecomesATileAndComesBack) {
  OpenTabs(2);
  const std::string a = IdAt(0);
  views::View* source = sidebar()->RowForId(a);
  Press(source);
  Lift(source);
  gfx::Point probe = PointIn(source);
  probe.Offset(0, -10);
  DragTo(source, probe, 2);
  const gfx::Rect zone = sidebar()->drag_host_for_testing()->FavoritesZone();
  ASSERT_FALSE(zone.IsEmpty());
  const gfx::Point grid = zone.origin() + gfx::Vector2d(5, zone.height() / 2);
  DragTo(source, grid);
  EXPECT_EQ(controller()->target().kind, aerie_sidebar::DropKind::kFavoritesSlot);
  Release(source, grid);
  EXPECT_EQ(model()->GetTab(a)->section, Section::kFavorite);
  ASSERT_TRUE(WaitFor([&] { return sidebar()->favorite_tile_count() == 1u; }));

  // Out of Favorites, back into the list as a row.
  WaitForRows();
  views::View* tile = ViewFor(a);
  ASSERT_TRUE(tile);
  Press(tile);
  Lift(tile);
  const gfx::Point row = PointIn(sidebar()->row_at(0), 80);
  DragTo(tile, row);
  Release(tile, row);
  EXPECT_EQ(model()->GetTab(a)->section, Section::kToday);
  EXPECT_EQ(sidebar()->favorite_tile_count(), 0u);
  EXPECT_EQ(sidebar()->row_count(), 2u);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, TheThirteenthFavoriteIsRefusedWithTheMessageOfTheMenu) {
  OpenTabs(2);
  // Twelve Favorites that have no tab in this window are enough for the cap.
  for (int i = 0; i < 12; ++i) {
    aerie_tab_model::TabRecord record;
    record.section = Section::kFavorite;
    record.position = i;
    ASSERT_FALSE(model()->AddTab(std::move(record)).empty());
  }
  const std::string a = IdAt(0);
  views::View* source = sidebar()->RowForId(a);
  Press(source);
  Lift(source);
  gfx::Point probe = PointIn(source);
  probe.Offset(0, -10);
  DragTo(source, probe, 2);
  const gfx::Rect zone = sidebar()->drag_host_for_testing()->FavoritesZone();
  ASSERT_FALSE(zone.IsEmpty());
  const gfx::Point grid = zone.origin() + gfx::Vector2d(5, zone.height() / 2);
  DragTo(source, grid);
  EXPECT_EQ(controller()->target().kind, aerie_sidebar::DropKind::kRefused);
  EXPECT_EQ(controller()->target().reason,
            aerie_sidebar::RefusalReason::kFavoritesFull);
  EXPECT_EQ(controller()->indicator()->mode(),
            AerieSidebarDropIndicator::Mode::kRefused);
  Release(source, grid);
  EXPECT_EQ(model()->GetTab(a)->section, Section::kToday);
  EXPECT_TRUE(sidebar()->toast_visible());
  EXPECT_EQ(sidebar()->toast()->GetText(),
            u"Favorites are full. Remove one to add another.");
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, ADropPastFiveLevelsShowsTheOutlineTheCursorAndTheHintAndChangesNothing) {
  OpenTabs(2);
  std::string parent;
  for (int i = 0; i < 5; ++i) {
    parent = NewFolder(Section::kToday, parent, "L" + std::to_string(i + 1));
  }
  const std::string level5 = parent;
  const std::string loose = NewFolder(Section::kToday, "", "Loose");
  WaitForRows();
  const int64_t before = model()->next_change();
  views::View* source = sidebar()->RowForId(loose);
  views::View* deep = sidebar()->RowForId(level5);
  ASSERT_TRUE(source && deep);
  Press(source);
  Lift(source);
  const gfx::Point middle = PointIn(deep, 50);
  DragTo(source, middle);
  EXPECT_EQ(controller()->target().kind, aerie_sidebar::DropKind::kRefused);
  EXPECT_EQ(controller()->target().reason, aerie_sidebar::RefusalReason::kTooDeep);
  EXPECT_EQ(controller()->indicator()->mode(),
            AerieSidebarDropIndicator::Mode::kRefused);
  EXPECT_TRUE(controller()->indicator()->shown());
  ui::MouseEvent probe = Mouse(ui::EventType::kMouseDragged, source, middle);
  EXPECT_EQ(source->GetCursor(probe), ui::Cursor(ui::mojom::CursorType::kNotAllowed));
  EXPECT_TRUE(sidebar()->toast_visible());
  EXPECT_EQ(sidebar()->toast()->GetText(), u"Can't nest deeper than 5 levels");
  Release(source, middle);
  EXPECT_EQ(model()->GetFolder(loose)->parent, "");
  EXPECT_EQ(model()->next_change(), before);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, AFolderDroppedIntoItsOwnChildIsRefused) {
  OpenTabs(2);
  const std::string outer = NewFolder(Section::kToday, "", "Outer");
  const std::string inner = NewFolder(Section::kToday, outer, "Inner");
  WaitForRows();
  views::View* source = sidebar()->RowForId(outer);
  Press(source);
  Lift(source);
  const gfx::Point middle = PointIn(sidebar()->RowForId(inner), 50);
  DragTo(source, middle);
  EXPECT_EQ(controller()->target().kind, aerie_sidebar::DropKind::kRefused);
  EXPECT_EQ(controller()->target().reason,
            aerie_sidebar::RefusalReason::kIntoItself);
  Release(source, middle);
  EXPECT_EQ(model()->GetFolder(outer)->parent, "");
  EXPECT_EQ(model()->GetFolder(inner)->parent, outer);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, AFolderTakesItsTabsToPinned) {
  OpenTabs(2);
  const std::string a = IdAt(0);
  const std::string folder = NewFolder(Section::kToday, "", "Trips");
  aerie_tab_model::MoveTabToFolder(*model(), a, folder);
  WaitForRows();
  views::View* source = sidebar()->RowForId(folder);
  Press(source);
  Lift(source);
  gfx::Point probe = PointIn(source);
  probe.Offset(0, 3);
  DragTo(source, probe, 2);
  // Not a drag yet: too close. Move on up.
  probe.Offset(0, -12);
  DragTo(source, probe, 2);
  ASSERT_TRUE(controller() && controller()->active());
  const gfx::Point pinned =
      sidebar()->drag_host_for_testing()->PinnedZone().CenterPoint();
  DragTo(source, pinned);
  Release(source, pinned);
  EXPECT_EQ(model()->GetFolder(folder)->section, Section::kPinned);
  EXPECT_EQ(model()->GetTab(a)->section, Section::kPinned);
  EXPECT_EQ(model()->GetTab(a)->parent, folder);
  EXPECT_TRUE(model()->GetTab(a)->pinned_url.has_value());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, LosingTheMouseCaptureCancelsTheDrag) {
  OpenTabs(3);
  const std::string a = IdAt(0);
  views::View* row = sidebar()->row_at(0);
  Press(row);
  Lift(row);
  DragTo(row, PointIn(sidebar()->row_at(2), 80));
  ASSERT_TRUE(sidebar()->dragging());
  row->OnMouseCaptureLost();
  EXPECT_FALSE(sidebar()->dragging());
  EXPECT_EQ(Children(Section::kToday).front(), a);
  EXPECT_FALSE(controller()->hidden_ids().empty());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, EscCancelsTheDragAndNothingMoves) {
  OpenTabs(3);
  const std::string a = IdAt(0);
  tabs::TabInterface* active = browser()->tab_strip_model()->GetActiveTab();
  views::View* row = sidebar()->row_at(0);
  Press(row);
  Lift(row);
  DragTo(row, PointIn(sidebar()->row_at(2), 80));
  ASSERT_TRUE(sidebar()->dragging());
  EXPECT_TRUE(row->GetWidget()->GetFocusManager()->ProcessAccelerator(
      ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE)));
  EXPECT_FALSE(sidebar()->dragging());
  EXPECT_EQ(Children(Section::kToday).front(), a);
  Release(row, PointIn(sidebar()->row_at(2), 80));
  EXPECT_EQ(Children(Section::kToday).front(), a);
  EXPECT_EQ(browser()->tab_strip_model()->GetActiveTab(), active);
  EXPECT_FALSE(row->AcceleratorPressed(
      ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE)));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, TheDraggedTabClosingEndsTheDragWithoutADanglingRow) {
  OpenTabs(3);
  const std::string a = IdAt(0);
  views::View* row = sidebar()->row_at(0);
  Press(row);
  Lift(row);
  DragTo(row, PointIn(sidebar()->row_at(2), 80));
  ASSERT_TRUE(sidebar()->dragging());
  browser()->tab_strip_model()->CloseWebContentsAt(0, TabCloseTypes::CLOSE_NONE);
  ASSERT_TRUE(WaitFor([&] { return !sidebar()->dragging(); }));
  ASSERT_TRUE(WaitFor([&] { return sidebar()->row_count() == 2u; }));
  EXPECT_EQ(controller()->ghost(), nullptr);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, AnotherTabClosingMidDragEndsTheDrag) {
  OpenTabs(3);
  views::View* row = sidebar()->row_at(0);
  Press(row);
  Lift(row);
  DragTo(row, PointIn(sidebar()->row_at(2), 80));
  ASSERT_TRUE(sidebar()->dragging());
  browser()->tab_strip_model()->CloseWebContentsAt(2, TabCloseTypes::CLOSE_NONE);
  ASSERT_TRUE(WaitFor([&] { return !sidebar()->dragging(); }));
  EXPECT_EQ(controller()->ghost(), nullptr);
  Release(row, PointIn(sidebar()->row_at(0), 50));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, TheTargetFolderBeingDeletedMidDragEndsTheDrag) {
  OpenTabs(2);
  const std::string folder = NewFolder(Section::kToday, "", "Trips");
  WaitForRows();
  views::View* source = sidebar()->row_at(0)->GetVisible()
                            ? sidebar()->RowForId(IdAt(0))
                            : nullptr;
  ASSERT_TRUE(source);
  Press(source);
  Lift(source);
  DragTo(source, PointIn(sidebar()->RowForId(folder), 50));
  ASSERT_TRUE(sidebar()->dragging());
  sidebar()->DeleteFolderNow(folder);
  ASSERT_TRUE(WaitFor([&] { return !sidebar()->dragging(); }));
  EXPECT_EQ(controller()->ghost(), nullptr);
  Release(source, PointIn(source, 50));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, NoLayoutRunsWhileThePointerMoves) {
  OpenTabs(8);
  views::View* row = sidebar()->row_at(0);
  Press(row);
  Lift(row);
  DragTo(row, PointIn(sidebar()->row_at(1), 80), 2);
  ASSERT_TRUE(sidebar()->dragging());
  sidebar()->GetWidget()->LayoutRootViewIfNecessary();
  ASSERT_FALSE(sidebar()->needs_layout());
  for (size_t i = 2; i < 8; ++i) {
    gfx::Point p = PointIn(sidebar()->row_at(i), 80);
    ui::MouseEvent event = Mouse(ui::EventType::kMouseDragged, row, p);
    row->OnMouseDragged(event);
    EXPECT_FALSE(sidebar()->needs_layout()) << i;
    EXPECT_FALSE(sidebar()->today_tree()->needs_layout()) << i;
  }
  Release(row, PointIn(sidebar()->row_at(7), 80));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, ADropOutsideTheWindowMovesTheTabToANewWindow) {
  OpenTabs(3);
  const std::string a = IdAt(0);
  ASSERT_EQ(GlobalBrowserCollection::GetInstance()->GetSize(), 1u);
  views::View* row = sidebar()->row_at(0);
  Press(row);
  Lift(row);
  DragTo(row, PointIn(sidebar()->row_at(1), 50));
  Release(row, gfx::Point(-3000, 100));
  EXPECT_TRUE(WaitFor([&] {
    return GlobalBrowserCollection::GetInstance()->GetSize() == 2u;
  }));
  EXPECT_EQ(browser()->tab_strip_model()->count(), 2);
  EXPECT_TRUE(model()->GetTab(a));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, AltArrowsMoveTheFocusedRowAndOutdentAndIndent) {
  OpenTabs(3);
  const std::string a = IdAt(0), b = IdAt(1), c = IdAt(2);
  sidebar()->MoveKeyForTesting(a, aerie_sidebar::MoveKey::kDown, /*today=*/true);
  EXPECT_EQ(Children(Section::kToday), (std::vector<std::string>{b, a, c}));
  sidebar()->MoveKeyForTesting(a, aerie_sidebar::MoveKey::kUp, true);
  EXPECT_EQ(Children(Section::kToday), (std::vector<std::string>{a, b, c}));
  // Nothing before the first row; nothing happens, nothing is written.
  const int64_t before = model()->next_change();
  sidebar()->MoveKeyForTesting(a, aerie_sidebar::MoveKey::kUp, true);
  EXPECT_EQ(model()->next_change(), before);

  const std::string folder = NewFolder(Section::kToday, "", "Trips");
  WaitForRows();
  // The folder is first; b indents into the folder only when it is directly
  // after it, so first move a, b, c below it.
  sidebar()->MoveKeyForTesting(a, aerie_sidebar::MoveKey::kIndent, true);
  EXPECT_EQ(model()->GetTab(a)->parent, folder);
  sidebar()->MoveKeyForTesting(a, aerie_sidebar::MoveKey::kOutdent, true);
  EXPECT_EQ(model()->GetTab(a)->parent, "");
  EXPECT_EQ(Children(Section::kToday).front(), folder);
  EXPECT_EQ(Children(Section::kToday)[1], a);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, AltRightRefusesPastFiveLevels) {
  OpenTabs(2);
  std::string parent;
  for (int i = 0; i < 4; ++i) {
    parent = NewFolder(Section::kToday, parent, "L" + std::to_string(i + 1));
  }
  const std::string level5 = NewFolder(Section::kToday, parent, "L5");
  const std::string mover = NewFolder(Section::kToday, parent, "Mover");
  WaitForRows();
  // Mover is the first child of L4 (new folders go to the top), L5 after it.
  // Put it after L5, then indent: L5 is level 5, Mover has height 1.
  aerie_tab_model::TabModel* m = model();
  m->MoveFolder(mover, Section::kToday, parent, 5);
  WaitForRows();
  const int64_t before = m->next_change();
  sidebar()->MoveKeyForTesting(mover, aerie_sidebar::MoveKey::kIndent, true);
  EXPECT_EQ(m->GetFolder(mover)->parent, parent);
  EXPECT_EQ(m->next_change(), before);
  EXPECT_TRUE(sidebar()->toast_visible());
  EXPECT_EQ(sidebar()->toast()->GetText(), u"Can't nest deeper than 5 levels");
  (void)level5;
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, TheOrderOfADropSurvivesTheModelBeingWrittenAndRead) {
  OpenTabs(3);
  const std::string a = IdAt(0), b = IdAt(1), c = IdAt(2);
  DragAndDrop(a, [&] { return sidebar()->row_at(2); }, 80);
  // What state.json would hold: the same records and positions.
  const base::DictValue json = model()->ToValue();
  auto parsed = aerie_tab_model::ParseState(json);
  ASSERT_EQ(parsed.status, aerie_tab_model::LoadStatus::kOk);
  std::map<int, std::string> by_position;
  for (const auto& tab : parsed.state->tabs) {
    if (tab.id == a || tab.id == b || tab.id == c) {
      by_position[tab.position] = tab.id;
    }
  }
  std::vector<std::string> order;
  for (const auto& [position, id] : by_position) {
    order.push_back(id);
  }
  EXPECT_EQ(order, (std::vector<std::string>{b, c, a}));
}

// --- Frame trace of a 50-row drag (issue #182 acceptance) ------------------

namespace {

// Counts the main frames the compositor starts while the drag runs, and the
// frames it missed (a gap of N frame times misses N - 1 frames).
class FrameCounter : public ui::CompositorObserver {
 public:
  explicit FrameCounter(ui::Compositor* compositor) : compositor_(compositor) {
    compositor_->AddObserver(this);
  }
  ~FrameCounter() override { compositor_->RemoveObserver(this); }
  void OnDidBeginMainFrame(ui::Compositor*) override {
    starts_.push_back(base::TimeTicks::Now());
  }
  int frames() const { return static_cast<int>(starts_.size()); }
  int missed() const {
    constexpr double kFrameMs = 1000.0 / 60.0;
    int missed = 0;
    for (size_t i = 1; i < starts_.size(); ++i) {
      const double gap = (starts_[i] - starts_[i - 1]).InMillisecondsF();
      missed += std::max(0, static_cast<int>(gap / kFrameMs + 0.5) - 1);
    }
    return missed;
  }
  double longest_gap_ms() const {
    double longest = 0;
    for (size_t i = 1; i < starts_.size(); ++i) {
      longest = std::max(longest,
                         (starts_[i] - starts_[i - 1]).InMillisecondsF());
    }
    return longest;
  }

 private:
  raw_ptr<ui::Compositor> compositor_;
  std::vector<base::TimeTicks> starts_;
};

}  // namespace

class AerieSidebarDragTraceBrowserTest : public AerieSidebarDragBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    AerieSidebarDragBrowserTest::SetUpOnMainThread();
    // Motion on: the lift and the shifts are what is traced.
    motion_.emplace(
        gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(false));
  }

 private:
  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter> motion_;
};

// 50 rows, the pointer sweeping down and up the list for about two seconds, a
// move every 8 ms. No row is laid out again while it runs (bounds and
// needs_layout are the layout counter), and the frames are logged ("FRAMES
// ...") for the PR.
IN_PROC_BROWSER_TEST_F(AerieSidebarDragTraceBrowserTest, DraggingAmong50RowsKeepsTheFramesAndLaysOutNothing) {
  OpenTabs(50);
  // Rows opened a moment ago may still be growing; the trace starts at rest.
  ASSERT_TRUE(WaitFor([&] {
    sidebar()->GetWidget()->LayoutRootViewIfNecessary();
    for (size_t i = 0; i < sidebar()->row_count(); ++i) {
      if (sidebar()->row_at(i)->height() != 34) {
        return false;
      }
    }
    return !sidebar()->needs_layout();
  }));
  ui::Compositor* compositor = BrowserView::GetBrowserViewForBrowser(browser())
                                   ->GetWidget()
                                   ->GetCompositor();
  views::View* source = sidebar()->row_at(0);
  Press(source);
  Lift(source);
  ASSERT_TRUE(sidebar()->dragging());
  // Starting the drag adds the ghost, the indicator and the drop bands to the
  // sidebar, which asks for one layout. That one is the drag's start, not a
  // frame of it, so it runs before the trace begins.
  // Late title and icon updates of the 50 new tabs also ask for layouts; let
  // them land first so the count below is the drag's alone.
  for (int i = 0; i < 3; ++i) {
    base::RunLoop loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), base::Milliseconds(100));
    loop.Run();
    sidebar()->GetWidget()->LayoutRootViewIfNecessary();
  }
  std::vector<gfx::Rect> bounds;
  for (size_t i = 0; i < sidebar()->row_count(); ++i) {
    bounds.push_back(sidebar()->row_at(i)->bounds());
  }
  const gfx::Rect viewport = sidebar()->today_scroll()->GetBoundsInScreen();
  gfx::Point top = PointIn(sidebar()->today_scroll(), 0);
  const int height = sidebar()->today_scroll()->height();
  int layouts = 0;
  FrameCounter counter(compositor);
  const base::TimeTicks begin = base::TimeTicks::Now();
  constexpr int kMoves = 250;
  for (int i = 0; i < kMoves; ++i) {
    // A triangle wave over the middle of the viewport, away from the edges
    // that scroll.
    const int phase = i % 100;
    const int span = height - 120;
    const int dy = 60 + (phase < 50 ? phase : 100 - phase) * span / 50;
    gfx::Point p(top.x(), top.y() + dy);
    ui::MouseEvent event = Mouse(ui::EventType::kMouseDragged, source, p);
    source->OnMouseDragged(event);
    if (sidebar()->needs_layout() || sidebar()->today_tree()->needs_layout()) {
      ++layouts;
    }
    base::RunLoop loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), base::Milliseconds(8));
    loop.Run();
  }
  const double ms = (base::TimeTicks::Now() - begin).InMillisecondsF();
  int moved_rows = 0;
  for (size_t i = 0; i < sidebar()->row_count(); ++i) {
    if (sidebar()->row_at(i)->bounds() != bounds[i]) {
      if (moved_rows++ < 3) {
        LOG(INFO) << "ROW " << i << " " << bounds[i].ToString() << " -> "
                  << sidebar()->row_at(i)->bounds().ToString();
      }
    }
  }
  LOG(INFO) << "FRAMES drag among 50 rows: " << kMoves << " moves in " << ms
            << " ms, frames " << counter.frames() << " main frames, missed "
            << counter.missed() << ", longest gap " << counter.longest_gap_ms()
            << " ms, pending layouts " << layouts << ", rows whose bounds changed "
            << moved_rows << ", viewport " << viewport.height() << " px";
  EXPECT_EQ(layouts, 0);
  EXPECT_EQ(moved_rows, 0);
  EXPECT_GT(counter.frames(), 0);
  // Esc puts the row back.
  EXPECT_TRUE(source->GetWidget()->GetFocusManager()->ProcessAccelerator(
      ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE)));
  EXPECT_FALSE(sidebar()->dragging());
}
