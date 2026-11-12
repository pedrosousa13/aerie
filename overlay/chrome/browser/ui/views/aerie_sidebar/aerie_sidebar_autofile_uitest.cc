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
#include <string>

#include "base/test/run_until.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_folder_sheet.h"
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
#include "components/aerie_tab_model/folder_rules.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
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

// True when any layer in the subtree animates a transform.
bool AnyTransformAnimation(views::View* root) {
  if (root->layer() &&
      Animates(root->layer(), ui::LayerAnimationElement::TRANSFORM)) {
    return true;
  }
  for (views::View* child : root->children()) {
    if (AnyTransformAnimation(child)) {
      return true;
    }
  }
  return false;
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
    host_resolver()->AddRule("*", "127.0.0.1");
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
                                          "trips.test")
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
  AerieSidebarRowView* row = sidebar()->RowForId(folder);
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
  EXPECT_FALSE(AnyTransformAnimation(sheet->GetContentsView()));
  sheet->Dismiss(false);
  EXPECT_EQ(OpacityDurationMs(sheet->fade_layer()), 90);
  EXPECT_FALSE(AnyTransformAnimation(sheet->GetContentsView()));
}

IN_PROC_BROWSER_TEST_P(AerieSidebarAutoFileReducedUiTest,
                       FilingMakesNoTransformAnimationAndThePulseIsShort) {
  Reduce();
  const std::string folder = MakeAutoFolder();
  AerieSidebarRowView* row = sidebar()->RowForId(folder);
  ASSERT_TRUE(row);
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL("trips.test", "/title1.html"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  const std::string tab = service()->adapter()->RecordIdForTab(
      browser()->tab_strip_model()->GetActiveTab());
  // While the tab is filed, the pulse runs and the toast shows, nothing in the
  // sidebar animates a transform.
  bool saw_transform = false;
  ASSERT_TRUE(base::test::RunUntil([&] {
    saw_transform = saw_transform || AnyTransformAnimation(sidebar());
    const auto* record = service()->model()->GetTab(tab);
    return record && record->parent == folder && sidebar()->toast_visible();
  }));
  saw_transform = saw_transform || AnyTransformAnimation(sidebar());
  EXPECT_FALSE(saw_transform);
  ui::Layer* pulse = row->pulse_view()->layer();
  ASSERT_TRUE(pulse);
  EXPECT_FALSE(Animates(pulse, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_FALSE(Animates(sidebar()->toast_layer(),
                        ui::LayerAnimationElement::TRANSFORM));
}
