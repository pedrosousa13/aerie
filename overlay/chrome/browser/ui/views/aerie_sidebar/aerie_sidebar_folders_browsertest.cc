// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of folders in Pinned and Today (issue #181): the rows follow
// the Tab Model, folders open and shut, the keys, what a screen reader hears,
// the menu items, delete, the motion that is read from the group, and the time
// to build a tree of 300 tabs in 20 folders.

#include <string>
#include <vector>

#include "base/logging.h"
#include "base/test/run_until.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_folder_group.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/pinned_tabs.h"
#include "components/aerie_tab_model/tab_model.h"
#include "content/public/test/browser_test.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/accessibility/view_accessibility.h"
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

ui::AXNodeData DataOf(views::View* view) {
  ui::AXNodeData data;
  view->GetViewAccessibility().GetAccessibleNodeData(&data);
  return data;
}

}  // namespace

class AerieSidebarFoldersBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    reduced_motion_.emplace(
        gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(true));
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
  TabStripModel* strip() { return browser()->tab_strip_model(); }

  std::string OpenBlankTab() {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), GURL("about:blank"),
        WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    const std::string id = service()->adapter()->RecordIdForTab(
        strip()->GetTabAtIndex(strip()->active_index()));
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return sidebar()->RowForId(id) != nullptr; }));
    return id;
  }

  void WaitForRows(Section section, size_t count) {
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return sidebar()->tree_rows(section).size() == count; }))
        << "rows: " << sidebar()->tree_rows(section).size();
  }

  void Key(AerieSidebarRowView* row, ui::KeyboardCode code) {
    ui::KeyEvent event(ui::EventType::kKeyPressed, code, ui::EF_NONE);
    row->OnKeyPressed(event);
  }

  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

IN_PROC_BROWSER_TEST_F(AerieSidebarFoldersBrowserTest,
                       AFolderHoldsATabAndShutsAndOpens) {
  const std::string tab = OpenBlankTab();
  const std::string folder =
      sidebar()->CreateFolderIn(Section::kToday, std::string());
  ASSERT_FALSE(folder.empty());
  ASSERT_TRUE(aerie_tab_model::MoveTabToFolder(*model(), tab, folder));
  // Folder row, then the tab at level 1 (the start-page tab is a row too).
  ASSERT_TRUE(base::test::RunUntil([&] {
    AerieSidebarRowView* row = sidebar()->RowForId(tab);
    return row && row->level() == 1;
  }));
  AerieSidebarRowView* folder_row = sidebar()->RowForId(folder);
  ASSERT_TRUE(folder_row);
  EXPECT_TRUE(folder_row->is_folder());
  EXPECT_TRUE(folder_row->expanded());

  sidebar()->ToggleFolder(folder);
  EXPECT_FALSE(model()->GetFolder(folder)->expanded);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->RowForId(tab) == nullptr; }));
  sidebar()->ToggleFolder(folder);
  EXPECT_TRUE(model()->GetFolder(folder)->expanded);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->RowForId(tab) != nullptr; }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarFoldersBrowserTest, FoldersNestFiveLevels) {
  std::string parent;
  for (int level = 1; level <= 5; ++level) {
    parent = sidebar()->CreateFolderIn(Section::kToday, parent);
    ASSERT_FALSE(parent.empty()) << level;
  }
  // The sixth level is refused.
  EXPECT_TRUE(sidebar()->CreateFolderIn(Section::kToday, parent).empty());
  EXPECT_EQ(aerie_tab_model::Depth(*model(), parent), 5);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarFoldersBrowserTest, KeysOpenShutAndMove) {
  const std::string tab = OpenBlankTab();
  const std::string folder =
      sidebar()->CreateFolderIn(Section::kToday, std::string());
  ASSERT_TRUE(aerie_tab_model::MoveTabToFolder(*model(), tab, folder));
  ASSERT_TRUE(base::test::RunUntil([&] {
    AerieSidebarRowView* row = sidebar()->RowForId(tab);
    return row && row->level() == 1;
  }));
  // A new folder opens its name for editing; the keys are the field's while it
  // is open, so leave it first.
  ASSERT_TRUE(base::test::RunUntil([&] {
    AerieSidebarRowView* row = sidebar()->RowForId(folder);
    return row && row->renaming();
  }));
  sidebar()->RowForId(folder)->CancelRename();
  AerieSidebarRowView* folder_row = sidebar()->RowForId(folder);
  Key(folder_row, ui::VKEY_LEFT);
  EXPECT_FALSE(model()->GetFolder(folder)->expanded);
  Key(sidebar()->RowForId(folder), ui::VKEY_RIGHT);
  EXPECT_TRUE(model()->GetFolder(folder)->expanded);
  Key(sidebar()->RowForId(folder), ui::VKEY_RETURN);
  EXPECT_FALSE(model()->GetFolder(folder)->expanded);
  Key(sidebar()->RowForId(folder), ui::VKEY_RETURN);
  EXPECT_TRUE(model()->GetFolder(folder)->expanded);
  // Right on an open folder moves to its first child, Left back to the folder.
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->RowForId(tab) != nullptr; }));
  Key(sidebar()->RowForId(folder), ui::VKEY_RIGHT);
  EXPECT_TRUE(sidebar()->RowForId(tab)->HasFocus());
  Key(sidebar()->RowForId(tab), ui::VKEY_LEFT);
  EXPECT_TRUE(sidebar()->RowForId(folder)->HasFocus());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarFoldersBrowserTest, AccessibilityTree) {
  const std::string tab = OpenBlankTab();
  const std::string folder =
      sidebar()->CreateFolderIn(Section::kToday, std::string());
  ASSERT_TRUE(aerie_tab_model::MoveTabToFolder(*model(), tab, folder));
  ASSERT_TRUE(base::test::RunUntil([&] {
    AerieSidebarRowView* row = sidebar()->RowForId(tab);
    return row && row->level() == 1;
  }));
  EXPECT_EQ(DataOf(sidebar()->today_tree()).role, ax::mojom::Role::kTree);
  const ui::AXNodeData folder_data = DataOf(sidebar()->RowForId(folder));
  EXPECT_EQ(folder_data.role, ax::mojom::Role::kTreeItem);
  EXPECT_TRUE(folder_data.HasState(ax::mojom::State::kExpanded));
  EXPECT_EQ(folder_data.GetIntAttribute(ax::mojom::IntAttribute::kHierarchicalLevel), 1);
  EXPECT_NE(folder_data.GetStringAttribute(ax::mojom::StringAttribute::kName)
                .find("folder, expanded, 1 item"),
            std::string::npos)
      << folder_data.GetStringAttribute(ax::mojom::StringAttribute::kName);
  const ui::AXNodeData tab_data = DataOf(sidebar()->RowForId(tab));
  EXPECT_EQ(tab_data.role, ax::mojom::Role::kTreeItem);
  EXPECT_EQ(tab_data.GetIntAttribute(ax::mojom::IntAttribute::kHierarchicalLevel), 2);
  sidebar()->ToggleFolder(folder);
  EXPECT_TRUE(DataOf(sidebar()->RowForId(folder))
                  .HasState(ax::mojom::State::kCollapsed));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarFoldersBrowserTest,
                       DeleteAskOnlyWhenTheFolderHoldsItems) {
  const std::string empty =
      sidebar()->CreateFolderIn(Section::kToday, std::string());
  EXPECT_FALSE(sidebar()->RequestDeleteFolder(empty));
  EXPECT_EQ(model()->GetFolder(empty), nullptr);

  const std::string tab = OpenBlankTab();
  const std::string full =
      sidebar()->CreateFolderIn(Section::kToday, std::string());
  ASSERT_TRUE(aerie_tab_model::MoveTabToFolder(*model(), tab, full));
  EXPECT_TRUE(sidebar()->RequestDeleteFolder(full));
  EXPECT_TRUE(sidebar()->delete_question_open());
  EXPECT_NE(model()->GetFolder(full), nullptr);
  sidebar()->DeleteFolderNow(full);
  EXPECT_EQ(model()->GetFolder(full), nullptr);
  // The tab went out next to where the folder was.
  EXPECT_NE(model()->GetTab(tab), nullptr);
  EXPECT_TRUE(model()->GetTab(tab)->parent.empty());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarFoldersBrowserTest,
                       FolderMotionUsesTheSettledValues) {
  // Reduced motion off for this read of the group.
  reduced_motion_.reset();
  reduced_motion_.emplace(
      gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(false));
  const std::string a = OpenBlankTab();
  const std::string b = OpenBlankTab();
  const std::string folder =
      sidebar()->CreateFolderIn(Section::kToday, std::string());
  ASSERT_TRUE(aerie_tab_model::MoveTabToFolder(*model(), a, folder));
  ASSERT_TRUE(aerie_tab_model::MoveTabToFolder(*model(), b, folder));
  ASSERT_TRUE(base::test::RunUntil([&] {
    AerieSidebarRowView* row = sidebar()->RowForId(b);
    return row && row->level() == 1;
  }));
  sidebar()->ToggleFolder(folder);
  ASSERT_TRUE(sidebar()->folder_group());
  EXPECT_FALSE(sidebar()->folder_group()->reveal());
  ASSERT_TRUE(sidebar()->folder_group()->height_animation());
  EXPECT_EQ(sidebar()->folder_group()->height_animation()->GetSlideDuration()
                .InMilliseconds(),
            180);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->collapsing_row_count() == 0u; }));
  sidebar()->ToggleFolder(folder);
  ASSERT_TRUE(sidebar()->folder_group());
  EXPECT_TRUE(sidebar()->folder_group()->reveal());
  EXPECT_EQ(sidebar()->folder_group()->height_animation()->GetSlideDuration()
                .InMilliseconds(),
            200);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarFoldersBrowserTest, BuildOf300TabsIn20Folders) {
  aerie_tab_model::TabModel* m = model();
  std::vector<std::string> folders;
  for (int i = 0; i < 20; ++i) {
    folders.push_back(aerie_tab_model::CreateFolder(
        *m, Section::kPinned, std::string(), "F" + std::to_string(i)));
  }
  for (int i = 0; i < 300; ++i) {
    aerie_tab_model::TabRecord record;
    record.section = Section::kPinned;
    record.parent = folders[i % 20];
    record.position = i;
    m->AddTab(record);
  }
  base::TimeTicks start = base::TimeTicks::Now();
  const auto rows = aerie_tab_model::BuildVisibleRows(
      *m, Section::kPinned, [](const std::string&) { return true; });
  const double ms = (base::TimeTicks::Now() - start).InMillisecondsF();
  LOG(INFO) << "PERF build of " << rows.size() << " rows: " << ms << " ms";
  EXPECT_EQ(rows.size(), 320u);
  EXPECT_LT(ms, 16.0);
}
