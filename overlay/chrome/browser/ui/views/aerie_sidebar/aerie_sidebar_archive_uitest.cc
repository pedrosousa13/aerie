// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Interactive UI tests of reduced motion in the Archive (issue #183). With the
// operating system's flag and with the in-app setting alike: the copy of an
// archived row fades in place in 120 ms and no transform animation is created
// anywhere in the sidebar, the Archive button flashes its colour for 140 ms
// and does not scale, a restore fades in 120 ms, and the panel opens by opacity
// alone in 90 ms. Run with ./aerie test-cpp --target aerie_interactive_ui_tests.

#include <optional>
#include <set>
#include <string>

#include "base/test/run_until.h"
#include "chrome/browser/aerie_tab_model/tab_archiver.h"
#include "chrome/browser/aerie_tab_model/tab_archiver_factory.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_archive_button.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_archive_controller.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_archive_panel.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_design_tokens/aerie_motion_prefs.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/test/ui_controls.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/test/layer_animator_test_controller.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace {

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

bool Animates(ui::Layer* layer,
              ui::LayerAnimationElement::AnimatableProperty property) {
  return layer && layer->GetAnimator()->IsAnimatingProperty(property);
}

// The class of the first view in the subtree whose layer animates a transform,
// or an empty string.
std::string AnyTransformAnimation(views::View* root) {
  if (root->layer() &&
      Animates(root->layer(), ui::LayerAnimationElement::TRANSFORM)) {
    return std::string(root->GetClassName()) + " layer=" +
           root->layer()->name() + " bounds=" + root->bounds().ToString() +
           " parent=" +
           std::string(root->parent() ? root->parent()->GetClassName() : "");
  }
  for (views::View* child : root->children()) {
    std::string found = AnyTransformAnimation(child);
    if (!found.empty()) {
      return found;
    }
  }
  return std::string();
}

int OpacityMs(ui::Layer* layer) {
  ui::LayerAnimatorTestController controller(layer->GetAnimator());
  ui::LayerAnimationSequence* sequence =
      controller.GetRunningSequence(ui::LayerAnimationElement::OPACITY);
  return sequence ? static_cast<int>(
                        sequence->FirstElement()->duration().InMilliseconds())
                  : -1;
}

}  // namespace

enum class ReduceVia { kOsFlag, kPref };

class AerieSidebarArchiveReducedUiTest
    : public InProcessBrowserTest,
      public testing::WithParamInterface<ReduceVia> {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return sidebar() && sidebar()->row_count() == 1u; }));
    archiver_ = aerie_tab_model::TabArchiverFactory::GetForProfile(
        browser()->GetProfile());
    ASSERT_TRUE(archiver_);
    sidebar()->archive()->Attach();
    if (GetParam() == ReduceVia::kOsFlag) {
      reduced_motion_.emplace(
          gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(true));
    } else {
      reduced_motion_.emplace(
          gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(false));
      browser()->GetProfile()->GetPrefs()->SetString(
          aerie::motion::kReduceMotionPref, aerie::motion::kReduceMotionOn);
    }
    ASSERT_TRUE(aerie::motion::ShouldReduceMotion(
        browser()->GetProfile()->GetPrefs()));
  }

  void TearDownOnMainThread() override {
    if (sidebar() && sidebar()->archive()->panel()) {
      sidebar()->archive()->panel()->ClosePanel(false);
    }
    archiver_ = nullptr;
    reduced_motion_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  AerieSidebarView* sidebar() {
    return FindDescendant<AerieSidebarView>(
        BrowserView::GetBrowserViewForBrowser(browser())
            ->vertical_tab_strip_region_view_for_testing());
  }
  AerieSidebarArchiveController* controller() { return sidebar()->archive(); }
  TabStripModel* strip() { return browser()->tab_strip_model(); }

  std::string OpenBackgroundTab() {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), embedded_test_server()->GetURL("/title1.html"),
        WindowOpenDisposition::NEW_BACKGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    const std::string id =
        aerie_tab_model::TabModelServiceFactory::GetForProfile(
            browser()->GetProfile())
            ->adapter()
            ->RecordIdForTab(strip()->GetTabAtIndex(strip()->count() - 1));
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return sidebar()->RowForId(id) != nullptr; }));
    // Let the row come to rest before the archive moment starts.
    gfx::Rect last;
    int same = 0;
    EXPECT_TRUE(base::test::RunUntil([&] {
      AerieSidebarRowView* row = sidebar()->RowForId(id);
      same = row->bounds() == last ? same + 1 : 0;
      last = row->bounds();
      return row->height() > 0 && same >= 10;
    }));
    // And every animation the new row started has finished, so that a
    // transform seen later belongs to the archive moment. Under reduced motion
    // none should run at all; this ends at once then, and times out if one is
    // stuck.
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return AnyTransformAnimation(sidebar()).empty(); }));
    return id;
  }

  void ArchiveBackgroundTab(const std::string& id) {
    aerie_tab_model::TabModelService* service =
        aerie_tab_model::TabModelServiceFactory::GetForProfile(
            browser()->GetProfile());
    for (int i = 0; i < strip()->count(); ++i) {
      if (service->adapter()->RecordIdForTab(strip()->GetTabAtIndex(i)) == id) {
        ASSERT_TRUE(archiver_->Archive(strip()->GetTabAtIndex(i)));
        return;
      }
    }
    FAIL() << "no tab for " << id;
  }

  raw_ptr<aerie_tab_model::TabArchiver> archiver_ = nullptr;
  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

INSTANTIATE_TEST_SUITE_P(
    AerieReduceVia,
    AerieSidebarArchiveReducedUiTest,
    testing::Values(ReduceVia::kOsFlag, ReduceVia::kPref),
    [](const testing::TestParamInfo<ReduceVia>& info) {
      return info.param == ReduceVia::kOsFlag ? "OsFlag" : "Pref";
    });

IN_PROC_BROWSER_TEST_P(AerieSidebarArchiveReducedUiTest,
                       TheCopyFadesInPlaceInOneHundredTwentyMsAndNothingScales) {
  const std::string id = OpenBackgroundTab();
  ArchiveBackgroundTab(id);
  ui::Layer* copy = controller()->FlightLayer("archive:" + id);
  ASSERT_TRUE(copy);
  // The copy fades (opacity, 120 ms at the most) and does not move or scale.
  EXPECT_TRUE(Animates(copy, ui::LayerAnimationElement::OPACITY));
  EXPECT_FLOAT_EQ(copy->GetTargetOpacity(), 0.f);
  EXPECT_LE(OpacityMs(copy), 120);
  EXPECT_EQ(OpacityMs(copy), 120);
  EXPECT_FALSE(Animates(copy, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(copy->GetTargetTransform().IsIdentity());
  // No transform animation is created anywhere in the sidebar while the row
  // closes and the button answers.
  EXPECT_EQ(AnyTransformAnimation(sidebar()), "");

  // The button flashes its colour for 140 ms with no scale.
  AerieSidebarArchiveButton* button = sidebar()->archive_button();
  const aerie_sidebar::ArchiveMotion plan = aerie_sidebar::ArchiveTabMotion(true);
  EXPECT_EQ(plan.pulse_ms, 140);
  EXPECT_FALSE(plan.animate_scale);
  bool saw_flash = false;
  ASSERT_TRUE(base::test::RunUntil([&] {
    saw_flash |= button->flashing();
    EXPECT_EQ(button->layer(), nullptr);
    EXPECT_FALSE(button->pulse_motion().IsAnimating());
    EXPECT_EQ(AnyTransformAnimation(sidebar()), "");
    return saw_flash && !button->pulsing();
  }));
  EXPECT_TRUE(saw_flash);
  EXPECT_FALSE(button->flashing());
}

// Decided for now (the maintainer may change it): after a restore the page of
// the restored tab has the focus, not the Archive button or the sidebar.
IN_PROC_BROWSER_TEST_P(AerieSidebarArchiveReducedUiTest,
                       AfterARestoreTheRestoredPageHasTheFocus) {
  const std::string id = OpenBackgroundTab();
  ArchiveBackgroundTab(id);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return controller()->flights_in_progress() == 0u; }));
  controller()->TogglePanel();
  AerieSidebarArchivePanel* panel = controller()->panel();
  ASSERT_TRUE(panel);
  ASSERT_EQ(panel->rows().size(), 1u);
  panel->ChooseRow(0);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return controller()->flights_in_progress() == 0u; }));
  EXPECT_EQ(controller()->panel(), nullptr);
  EXPECT_EQ(browser()->tab_strip_model()->active_index(), 0);
  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_TAB_CONTAINER));
}

IN_PROC_BROWSER_TEST_P(AerieSidebarArchiveReducedUiTest,
                       RestoreFadesInOneHundredTwentyMsWithoutMoving) {
  const std::string id = OpenBackgroundTab();
  ArchiveBackgroundTab(id);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return controller()->flights_in_progress() == 0u; }));
  controller()->TogglePanel();
  AerieSidebarArchivePanel* panel = controller()->panel();
  ASSERT_TRUE(panel);
  ASSERT_EQ(panel->rows().size(), 1u);
  panel->ChooseRow(0);
  ui::Layer* copy = controller()->FlightLayer("restore:" + id);
  ASSERT_TRUE(copy);
  EXPECT_TRUE(Animates(copy, ui::LayerAnimationElement::OPACITY));
  EXPECT_EQ(OpacityMs(copy), 120);
  EXPECT_FALSE(Animates(copy, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(copy->GetTargetTransform().IsIdentity());
  EXPECT_EQ(AnyTransformAnimation(sidebar()), "");
}

IN_PROC_BROWSER_TEST_P(AerieSidebarArchiveReducedUiTest,
                       ThePanelOpensByOpacityAloneInNinetyMilliseconds) {
  controller()->TogglePanel();
  AerieSidebarArchivePanel* panel = controller()->panel();
  ASSERT_TRUE(panel);
  ui::Layer* layer = panel->GetContentsView()->layer();
  ASSERT_TRUE(layer);
  EXPECT_TRUE(Animates(layer, ui::LayerAnimationElement::OPACITY));
  EXPECT_EQ(OpacityMs(layer), 90);
  EXPECT_FALSE(Animates(layer, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(layer->transform().IsIdentity());
  EXPECT_EQ(AnyTransformAnimation(panel->GetContentsView()), "");
}
