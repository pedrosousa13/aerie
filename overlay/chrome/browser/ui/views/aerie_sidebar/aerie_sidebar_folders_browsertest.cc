// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of folders in Pinned and Today (issue #181): the rows follow
// the Tab Model, folders open and shut, the keys, what a screen reader hears,
// the menu items, delete, the motion that is read from the group, and the time
// to build a tree of 300 tabs in 20 folders.

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "base/logging.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_folder_group.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_folder_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/pinned_tabs.h"
#include "components/aerie_tab_model/tab_model.h"
#include "content/public/test/browser_test.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/compositor_observer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/accessibility/view_accessibility.h"
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

  void ForceReducedMotion(bool reduced) {
    reduced_motion_.reset();
    reduced_motion_.emplace(
        gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(reduced));
  }

  // Opens `tab_count` blank tabs in the background, makes `folder_count`
  // folders in Today and files the tabs into them round-robin. Returns the
  // folder ids.
  std::vector<std::string> MakeTabsInFolders(int tab_count, int folder_count) {
    for (int i = 0; i < tab_count; ++i) {
      chrome::AddTabAt(browser(), GURL("about:blank"), -1,
                       /*foreground=*/false);
    }
    EXPECT_TRUE(base::test::RunUntil([&] {
      return aerie_tab_model::SectionTabIds(*model(), Section::kToday).size() ==
             static_cast<size_t>(strip()->count());
    }));
    std::vector<std::string> folders;
    for (int i = 0; i < folder_count; ++i) {
      folders.push_back(aerie_tab_model::CreateFolder(
          *model(), Section::kToday, std::string(), "F" + std::to_string(i)));
    }
    int filed = 0;
    for (int i = 0; i < strip()->count() && filed < tab_count; ++i) {
      const std::string id =
          service()->adapter()->RecordIdForTab(strip()->GetTabAtIndex(i));
      if (aerie_tab_model::MoveTabToFolder(*model(), id,
                                           folders[filed % folder_count])) {
        ++filed;
      }
    }
    return folders;
  }

  void WaitForQuiet() {
    EXPECT_TRUE(base::test::RunUntil([&] {
      return (!sidebar()->folder_group() ||
              !sidebar()->folder_group()->running()) &&
             sidebar()->collapsing_row_count() == 0u;
    }));
  }

  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

// Counts the frames the compositor starts while a motion plays, and the frames
// it missed: a gap of N frame times between two frames misses N - 1 frames.
class FrameCounter : public ui::CompositorObserver {
 public:
  explicit FrameCounter(ui::Compositor* compositor) : compositor_(compositor) {
    compositor_->AddObserver(this);
  }
  ~FrameCounter() override { compositor_->RemoveObserver(this); }
  // A main frame is one tick of the animations and the layout; the browser test
  // compositor starts no frame of its own that could be counted instead.
  void OnDidBeginMainFrame(ui::Compositor*) override {
    starts_.push_back(base::TimeTicks::Now());
  }
  void OnCompositingDidCommit(ui::Compositor*) override { ++commits_; }
  int commits() const { return commits_; }
  int frames() const { return static_cast<int>(starts_.size()); }
  // At 60 Hz.
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
      longest = std::max(longest, (starts_[i] - starts_[i - 1]).InMillisecondsF());
    }
    return longest;
  }

 private:
  raw_ptr<ui::Compositor> compositor_;
  std::vector<base::TimeTicks> starts_;
  int commits_ = 0;
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
  AerieSidebarFolderRowView* folder_row =
      sidebar()->FolderRowForId(folder);
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
    AerieSidebarFolderRowView* row = sidebar()->FolderRowForId(folder);
    return row && row->renaming();
  }));
  sidebar()->RowForId(folder)->CancelRename();
  AerieSidebarFolderRowView* folder_row =
      sidebar()->FolderRowForId(folder);
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
  // In sequence: the fade takes `xfast`, then the height takes the rest of
  // `folder-collapse` (90 + 90 = 180 ms).
  EXPECT_TRUE(base::test::RunUntil([&] {
    return sidebar()->folder_group()->height_animation()
               ->GetSlideDuration().InMilliseconds() == 90;
  }));
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

// Frame trace of a reveal (issue #181 acceptance: 300 tabs in 20 folders, no
// dropped frames on expand). The numbers are logged ("FRAMES ...") for the PR.
IN_PROC_BROWSER_TEST_F(AerieSidebarFoldersBrowserTest,
                       ExpandingAFolderInA300TabTreeKeepsTheFrames) {
  ForceReducedMotion(false);
  const std::vector<std::string> folders = MakeTabsInFolders(300, 20);
  for (const std::string& folder : folders) {
    sidebar()->SetFolderExpanded(folder, false);
    WaitForQuiet();
  }
  ui::Compositor* compositor = BrowserView::GetBrowserViewForBrowser(browser())
                                   ->GetWidget()
                                   ->GetCompositor();
  auto expand = [&](const std::string& folder, const char* what,
                    bool big_case) {
    FrameCounter counter(compositor);
    const base::TimeTicks begin = base::TimeTicks::Now();
    sidebar()->SetFolderExpanded(folder, true);
    const double sync_ms = (base::TimeTicks::Now() - begin).InMillisecondsF();
    EXPECT_TRUE(base::test::RunUntil([&] {
      return sidebar()->folder_group() && !sidebar()->folder_group()->running();
    }));
    const double total_ms = (base::TimeTicks::Now() - begin).InMillisecondsF();
    LOG(INFO) << "FRAMES " << what << ": toggle " << sync_ms
              << " ms on the main thread, motion " << total_ms
              << " ms, frames " << counter.frames() << " main frames (" << counter.commits()
              << " commits), missed "
              << counter.missed() << ", longest gap " << counter.longest_gap_ms()
              << " ms";
    // The work that one toggle does on the main thread has to fit in a frame.
    if (!big_case) {
      EXPECT_LT(sync_ms, 16.0) << what;
    }
    EXPECT_GT(counter.frames(), 0) << what;
  };
  expand(folders[0], "15 children, 300 tabs in 20 folders", false);

  // The worst case: one folder that holds all 300 tabs.
  aerie_tab_model::TabModel* m = model();
  const std::string big = aerie_tab_model::CreateFolder(
      *m, Section::kToday, std::string(), "All");
  std::vector<std::string> ids;
  for (const auto& [id, tab] : m->tabs()) {
    ids.push_back(id);
  }
  for (const std::string& id : ids) {
    aerie_tab_model::MoveTabToFolder(*m, id, big);
  }
  sidebar()->SetFolderExpanded(big, false);
  WaitForQuiet();
  // Reported, not asserted: 300 rows are made at once.
  expand(big, "one folder with all the tabs", true);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarFoldersBrowserTest,
                       ShuttingTwoFoldersInARowEndsTheFirstMotionCleanly) {
  ForceReducedMotion(false);
  const std::vector<std::string> folders = MakeTabsInFolders(6, 2);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return sidebar()->tree_rows(Section::kToday).size() >= 8u;
  }));
  sidebar()->SetFolderExpanded(folders[0], false);
  sidebar()->SetFolderExpanded(folders[1], false);
  WaitForQuiet();
  for (const aerie_tab_model::TreeRow& row :
       sidebar()->tree_rows(Section::kToday)) {
    EXPECT_TRUE(row.is_folder || row.level == 0);
  }
}

// The motion that is destroyed in the middle puts every row at rest and the
// sidebar's done callback runs, with no call to Finish.
IN_PROC_BROWSER_TEST_F(AerieSidebarFoldersBrowserTest,
                       DestroyingAGroupMidRunEndsEveryRowsMotion) {
  ForceReducedMotion(false);
  const std::string a = OpenBlankTab();
  const std::string b = OpenBlankTab();
  AerieSidebarRowView* row_a = sidebar()->RowForId(a);
  AerieSidebarRowView* row_b = sidebar()->RowForId(b);
  ASSERT_TRUE(row_a && row_b);
  // Let the open motion of the new rows end: it owns a layer too.
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !row_a->layer() && !row_b->layer(); }));
  bool done = false;
  auto group = std::make_unique<AerieSidebarFolderGroup>(
      "f", std::vector<raw_ptr<AerieSidebarRowView>>{row_a, row_b},
      row_a->parent(), browser()->GetProfile()->GetPrefs(),
      base::BindLambdaForTesting([&] { done = true; }));
  group->Start(/*reveal=*/false);
  EXPECT_TRUE(row_a->folder_fading());
  group.reset();
  EXPECT_TRUE(done);
  for (AerieSidebarRowView* row : {row_a, row_b}) {
    EXPECT_FALSE(row->folder_fading());
    EXPECT_EQ(row->height_fraction(), 1.0);
    EXPECT_EQ(row->layer(), nullptr);
  }
}

// Reduced motion changes in the middle of a motion: the slide and the timers
// are not the same in the two modes, so the rows go to the end state at once.
IN_PROC_BROWSER_TEST_F(AerieSidebarFoldersBrowserTest,
                       RetargetAcrossReducedAndFullMotionSnapsToTheEnd) {
  const std::string a = OpenBlankTab();
  AerieSidebarRowView* row = sidebar()->RowForId(a);
  ASSERT_TRUE(row);
  for (bool start_reduced : {false, true}) {
    ForceReducedMotion(start_reduced);
    bool done = false;
    AerieSidebarFolderGroup group(
        "f", {row}, row->parent(), browser()->GetProfile()->GetPrefs(),
        base::BindLambdaForTesting([&] { done = true; }));
    group.Start(/*reveal=*/false);
    ASSERT_TRUE(group.running());
    ForceReducedMotion(!start_reduced);
    group.Retarget(/*reveal=*/true);
    EXPECT_FALSE(group.running()) << start_reduced;
    EXPECT_TRUE(done);
    EXPECT_FALSE(row->folder_fading());
    EXPECT_EQ(row->height_fraction(), 1.0);
  }
}

// Same setting on both sides: the motion turns around and goes on.
IN_PROC_BROWSER_TEST_F(AerieSidebarFoldersBrowserTest,
                       RetargetWithTheSameSettingTurnsTheMotionAround) {
  ForceReducedMotion(false);
  const std::string a = OpenBlankTab();
  AerieSidebarRowView* row = sidebar()->RowForId(a);
  ASSERT_TRUE(row);
  AerieSidebarFolderGroup group("f", {row}, row->parent(),
                                browser()->GetProfile()->GetPrefs(),
                                base::DoNothing());
  group.Start(/*reveal=*/false);
  group.Retarget(/*reveal=*/true);
  EXPECT_TRUE(group.running());
  EXPECT_TRUE(group.reveal());
  group.Finish();
  EXPECT_FALSE(group.running());
}

// Delete asks only when it can show the question: with no window it asks
// nothing and the flag stays clear.
IN_PROC_BROWSER_TEST_F(AerieSidebarFoldersBrowserTest,
                       DeleteQuestionNeedsAWindowAndTheFlagFollowsTheDialog) {
  const std::string tab = OpenBlankTab();
  const std::string folder =
      sidebar()->CreateFolderIn(Section::kToday, std::string());
  ASSERT_TRUE(aerie_tab_model::MoveTabToFolder(*model(), tab, folder));
  AerieSidebarView* view = sidebar();
  views::View* parent = view->parent();
  const size_t index = parent->GetIndexOf(view).value();
  std::unique_ptr<AerieSidebarView> detached = parent->RemoveChildViewT(view);
  EXPECT_EQ(view->GetWidget(), nullptr);
  EXPECT_FALSE(view->RequestDeleteFolder(folder));
  EXPECT_FALSE(view->delete_question_open());
  EXPECT_NE(model()->GetFolder(folder), nullptr);
  parent->AddChildViewAt(std::move(detached), index);
  // With a window the question shows and the flag is set.
  EXPECT_TRUE(sidebar()->RequestDeleteFolder(folder));
  EXPECT_TRUE(sidebar()->delete_question_open());
}
