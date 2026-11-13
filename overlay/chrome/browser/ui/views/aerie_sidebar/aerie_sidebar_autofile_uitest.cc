// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Interactive UI tests of the motion of auto-filing (issue #139): the folder
// editor sheet opens and closes on the named tokens and moves only `transform`
// and `opacity`; a filed tab pulses its folder on `opacity` alone; and under
// reduced motion (the operating system's flag and the in-app setting) the sheet
// is opacity-only at 90 ms and no transform animation exists anywhere in the
// sidebar. Run with ./aerie test-cpp --target aerie_interactive_ui_tests.

#include <optional>
#include <set>
#include <string>

#include "base/test/run_until.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_folder_sheet.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_folder_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_switch.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_design_tokens/aerie_motion_prefs.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_tab_model/auto_file_controller.h"
#include "components/aerie_tab_model/folder_rules.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/test/layer_animator_test_controller.h"
#include "ui/gfx/animation/animation_test_api.h"
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

bool Animates(ui::Layer* layer, ui::LayerAnimationElement::AnimatableProperty p) {
  return layer && layer->GetAnimator()->IsAnimatingProperty(p);
}

// The class of the first view in the subtree whose layer animates a transform,
// or an empty string.
//
// Animations that were already running when the test began (the first row's
// opening at start-up, which no frame has played yet) are in `ignored`.
std::string AnyTransformAnimation(views::View* root,
                                  const std::set<ui::Layer*>& ignored = {}) {
  if (root->layer() && !ignored.count(root->layer()) &&
      Animates(root->layer(), ui::LayerAnimationElement::TRANSFORM)) {
    std::string path(root->GetClassName());
    {
      ui::LayerAnimatorTestController controller(root->layer()->GetAnimator());
      ui::LayerAnimationSequence* sequence =
          controller.GetRunningSequence(ui::LayerAnimationElement::TRANSFORM);
      if (sequence) {
        path += " [" +
                std::to_string(
                    sequence->FirstElement()->duration().InMilliseconds()) +
                " ms, target " +
                root->layer()->GetTargetTransform().ToString() +
                ", opacity target " +
                std::to_string(root->layer()->GetTargetOpacity()) + "]";
      }
    }
    for (views::View* up = root->parent(); up; up = up->parent()) {
      path += "<" + std::string(up->GetClassName());
    }
    return path;
  }
  for (views::View* child : root->children()) {
    std::string found = AnyTransformAnimation(child, ignored);
    if (!found.empty()) {
      return found;
    }
  }
  return std::string();
}

void CollectTransformAnimating(views::View* root, std::set<ui::Layer*>* out) {
  if (root->layer() &&
      Animates(root->layer(), ui::LayerAnimationElement::TRANSFORM)) {
    out->insert(root->layer());
  }
  for (views::View* child : root->children()) {
    CollectTransformAnimating(child, out);
  }
}

// No property other than transform and opacity runs on the layer.
bool OnlyTransformAndOpacity(ui::Layer* layer) {
  for (auto property : {ui::LayerAnimationElement::BOUNDS,
                        ui::LayerAnimationElement::VISIBILITY,
                        ui::LayerAnimationElement::BRIGHTNESS,
                        ui::LayerAnimationElement::GRAYSCALE,
                        ui::LayerAnimationElement::COLOR}) {
    if (Animates(layer, property)) {
      return false;
    }
  }
  return true;
}

int OpacityDurationMs(ui::Layer* layer) {
  ui::LayerAnimatorTestController controller(layer->GetAnimator());
  ui::LayerAnimationSequence* sequence =
      controller.GetRunningSequence(ui::LayerAnimationElement::OPACITY);
  return sequence ? static_cast<int>(
                        sequence->FirstElement()->duration().InMilliseconds())
                  : -1;
}

}  // namespace

class AerieSidebarAutoFileUiTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
        embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return sidebar() && sidebar()->row_count() == 1u; }));
  }
  void TearDownOnMainThread() override {
    reduced_motion_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }
  void ForceReducedMotion(bool reduced) {
    reduced_motion_.reset();
    reduced_motion_.emplace(
        gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(reduced));
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
  std::string MakeAutoFolder() {
    aerie_tab_model::FolderRecord folder;
    folder.name = "Trips";
    folder.section = aerie_tab_model::Section::kToday;
    const std::string id = service()->model()->AddFolder(std::move(folder));
    EXPECT_TRUE(aerie_tab_model::SetFolderRuleKind(
        *service()->model(), id, aerie_tab_model::FolderKind::kAuto));
    EXPECT_FALSE(aerie_tab_model::AddRule(*service()->model(), id,
                                          aerie_tab_model::RuleType::kDomain,
                                          "trips.localhost")
                     .empty());
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return sidebar()->RowForId(id) != nullptr; }));
    return id;
  }

  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

IN_PROC_BROWSER_TEST_F(AerieSidebarAutoFileUiTest,
                       TheSheetSlidesAndFadesInOnTheNamedTokens) {
  ForceReducedMotion(false);
  const std::string folder = MakeAutoFolder();
  sidebar()->OpenFolderSheet(folder);
  AerieSidebarFolderSheet* sheet = sidebar()->folder_sheet();
  ASSERT_TRUE(sheet);
  ui::Layer* slide = sheet->slide_layer();
  ui::Layer* fade = sheet->fade_layer();
  ASSERT_TRUE(slide);
  ASSERT_TRUE(fade);
  // `sheet-enter` (180 ms), the slide is a transform, the fade an opacity.
  EXPECT_TRUE(Animates(slide, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(Animates(fade, ui::LayerAnimationElement::OPACITY));
  EXPECT_EQ(OpacityDurationMs(fade),
            aerie::tokens::GetDurationMs(
                aerie::tokens::MotionDuration::kSheetEnter, false));
  EXPECT_TRUE(slide->GetTargetTransform().IsIdentity());
  EXPECT_FLOAT_EQ(fade->GetTargetOpacity(), 1.f);
  EXPECT_TRUE(OnlyTransformAndOpacity(slide));
  EXPECT_TRUE(OnlyTransformAndOpacity(fade));
  // It closes by fading on `sheet-exit`, with no transform.
  sheet->Dismiss(false);
  EXPECT_FLOAT_EQ(fade->GetTargetOpacity(), 0.f);
  EXPECT_EQ(OpacityDurationMs(fade),
            aerie::tokens::GetDurationMs(
                aerie::tokens::MotionDuration::kSheetExit, false));
  EXPECT_TRUE(OnlyTransformAndOpacity(fade));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarAutoFileUiTest,
                       FilingPulsesTheFolderOnOpacityAlone) {
  ForceReducedMotion(false);
  const std::string folder = MakeAutoFolder();
  AerieSidebarFolderRowView* row = sidebar()->FolderRowForId(folder);
  ASSERT_TRUE(row);
  row->PlayFilingPulse(browser()->GetProfile()->GetPrefs());
  ui::Layer* layer = row->pulse_view()->layer();
  ASSERT_TRUE(layer);
  EXPECT_TRUE(Animates(layer, ui::LayerAnimationElement::OPACITY));
  EXPECT_FALSE(Animates(layer, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(OnlyTransformAndOpacity(layer));
  EXPECT_FLOAT_EQ(layer->GetTargetOpacity(),
                  aerie::tokens::kMotionOpacityAutofilePulse);
}

enum class ReduceVia { kOsFlag, kPref };

class AerieSidebarAutoFileReducedUiTest
    : public AerieSidebarAutoFileUiTest,
      public testing::WithParamInterface<ReduceVia> {
 protected:
  void Reduce() {
    if (GetParam() == ReduceVia::kOsFlag) {
      ForceReducedMotion(true);
    } else {
      ForceReducedMotion(false);
      browser()->GetProfile()->GetPrefs()->SetString(
          aerie::motion::kReduceMotionPref, aerie::motion::kReduceMotionOn);
    }
  }
};

INSTANTIATE_TEST_SUITE_P(
    AerieReduceVia,
    AerieSidebarAutoFileReducedUiTest,
    testing::Values(ReduceVia::kOsFlag, ReduceVia::kPref),
    [](const testing::TestParamInfo<ReduceVia>& info) {
      return info.param == ReduceVia::kOsFlag ? "OsFlag" : "Pref";
    });

IN_PROC_BROWSER_TEST_P(AerieSidebarAutoFileReducedUiTest,
                       TheSheetIsOpacityOnlyAndNinetyMilliseconds) {
  Reduce();
  const std::string folder = MakeAutoFolder();
  sidebar()->OpenFolderSheet(folder);
  AerieSidebarFolderSheet* sheet = sidebar()->folder_sheet();
  ASSERT_TRUE(sheet);
  EXPECT_FALSE(Animates(sheet->slide_layer(), ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(sheet->slide_layer()->transform().IsIdentity());
  EXPECT_TRUE(Animates(sheet->fade_layer(), ui::LayerAnimationElement::OPACITY));
  EXPECT_EQ(OpacityDurationMs(sheet->fade_layer()), 90);
  // A switch toggled in the sheet moves no transform either.
  sheet->all_switch()->SetOn(!sheet->all_switch()->on(),
                             browser()->GetProfile()->GetPrefs());
  EXPECT_EQ(AnyTransformAnimation(sheet->GetContentsView()), "");
  sheet->Dismiss(false);
  EXPECT_EQ(OpacityDurationMs(sheet->fade_layer()), 90);
  EXPECT_EQ(AnyTransformAnimation(sheet->GetContentsView()), "");
}

IN_PROC_BROWSER_TEST_P(AerieSidebarAutoFileReducedUiTest,
                       FilingMakesNoTransformAnimationAndThePulseIsShort) {
  Reduce();
  const std::string folder = MakeAutoFolder();
  AerieSidebarFolderRowView* row = sidebar()->FolderRowForId(folder);
  ASSERT_TRUE(row);
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL("trips.localhost", "/title1.html"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  const std::string tab = service()->adapter()->RecordIdForTab(
      browser()->tab_strip_model()->GetActiveTab());
  // While the tab is filed, the pulse runs and the toast shows, nothing in the
  // sidebar animates a transform.
  std::set<ui::Layer*> ignored;
  CollectTransformAnimating(sidebar(), &ignored);
  std::string saw_transform;
  bool saw_pulse_opacity_animation = false;
  ASSERT_TRUE(base::test::RunUntil([&] {
    if (row->pulse_view()->layer() &&
        Animates(row->pulse_view()->layer(),
                 ui::LayerAnimationElement::OPACITY)) {
      saw_pulse_opacity_animation = true;
    }
    if (saw_transform.empty()) {
      saw_transform = AnyTransformAnimation(sidebar(), ignored);
      const auto* now = service()->model()->GetTab(tab);
      if (!saw_transform.empty()) {
        saw_transform += now && now->parent == folder ? " (after filing)"
                                                       : " (before filing)";
      }
    }
    const auto* record = service()->model()->GetTab(tab);
    return record && record->parent == folder && sidebar()->toast_visible();
  }));
  if (saw_transform.empty()) {
    saw_transform = AnyTransformAnimation(sidebar(), ignored);
  }
  EXPECT_EQ(saw_transform, "");
  ui::Layer* pulse = row->pulse_view()->layer();
  ASSERT_TRUE(pulse);
  // The pause before the move is the short one, and the folder shows a fade
  // (opacity) instead of the motion.
  EXPECT_EQ(aerie_tab_model::AutoFileDelay(aerie::motion::ShouldReduceMotion(
                browser()->GetProfile()->GetPrefs())),
            base::Milliseconds(120));
  EXPECT_TRUE(saw_pulse_opacity_animation);
  EXPECT_FALSE(Animates(pulse, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_FALSE(Animates(sidebar()->toast_layer(),
                        ui::LayerAnimationElement::TRANSFORM));
}
