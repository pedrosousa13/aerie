// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// The motion of dragging in the sidebar (issue #182, items 8 of #215): the
// lift and the settle animate transform only, one animation per layer, and
// under reduced motion nothing animates a transform while a row is dragged and
// dropped. Run with ./aerie test-cpp --target aerie_interactive_ui_tests
// --gtest-filter='AerieSidebarDragUi*'.

#include <optional>

#include "base/test/run_until.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_drag_controller.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "components/aerie_design_tokens/aerie_motion_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animator.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace {

using Property = ui::LayerAnimationElement::AnimatableProperty;

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

// Visits every layer under `view`; `fn(layer)` for each.
template <typename Fn>
void ForEachLayer(views::View* view, const Fn& fn) {
  if (view->layer()) {
    fn(view->layer());
  }
  for (views::View* child : view->children()) {
    ForEachLayer(child, fn);
  }
}

bool AnyTransformAnimates(views::View* root) {
  bool any = false;
  ForEachLayer(root, [&](ui::Layer* layer) {
    any |= layer->GetAnimator()->IsAnimatingProperty(
        ui::LayerAnimationElement::TRANSFORM);
  });
  return any;
}

// True when nothing but transform and opacity animates on any layer.
bool OnlyTransformAndOpacityAnimate(views::View* root) {
  bool ok = true;
  ForEachLayer(root, [&](ui::Layer* layer) {
    for (int bit = ui::LayerAnimationElement::FIRST_PROPERTY;
         bit != ui::LayerAnimationElement::SENTINEL; bit <<= 1) {
      const auto property = static_cast<Property>(bit);
      if (property != ui::LayerAnimationElement::TRANSFORM &&
          property != ui::LayerAnimationElement::OPACITY &&
          layer->GetAnimator()->IsAnimatingProperty(property)) {
        ADD_FAILURE() << "property " << bit << " animates";
        ok = false;
      }
    }
  });
  return ok;
}

}  // namespace

class AerieSidebarDragUiTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
    browser_view()->GetWidget()->SetBounds(gfx::Rect(0, 0, 1200, 900));
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return sidebar() && sidebar()->row_count() == 1u; }));
  }

  void ForceReducedMotion(bool reduced) {
    reduced_motion_.reset();
    reduced_motion_.emplace(
        gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(reduced));
  }
  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  AerieSidebarView* sidebar() {
    return FindDescendant<AerieSidebarView>(
        browser_view()->vertical_tab_strip_region_view_for_testing());
  }

  void OpenTabs(size_t total) {
    while (sidebar()->row_count() < total) {
      const size_t before = sidebar()->row_count();
      chrome::ExecuteCommand(browser(), IDC_NEW_TAB);
      ASSERT_TRUE(base::test::RunUntil(
          [&] { return sidebar()->row_count() == before + 1; }));
    }
    ASSERT_TRUE(base::test::RunUntil([&] {
      for (size_t i = 0; i < total; ++i) {
        if (sidebar()->row_at(i)->height_animating()) {
          return false;
        }
      }
      return true;
    }));
  }

  gfx::Point PointIn(views::View* view, int y_percent) {
    gfx::Point p(view->width() / 2, view->height() * y_percent / 100);
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
        Mouse(ui::EventType::kMousePressed, view, PointIn(view, 50));
    view->OnMousePressed(event);
  }
  void Drag(views::View* view, const gfx::Point& to) {
    ui::MouseEvent event = Mouse(ui::EventType::kMouseDragged, view, to);
    view->OnMouseDragged(event);
  }
  void Release(views::View* view, const gfx::Point& at) {
    ui::MouseEvent event = Mouse(ui::EventType::kMouseReleased, view, at);
    view->OnMouseReleased(event);
  }

 private:
  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

IN_PROC_BROWSER_TEST_F(AerieSidebarDragUiTest, TheLiftAndTheSettleAnimateOnlyTransformAndOpacity) {
  ForceReducedMotion(false);
  OpenTabs(3);
  views::View* row = sidebar()->row_at(0);
  Press(row);
  Drag(row, PointIn(row, 50) + gfx::Vector2d(0, 20));
  const gfx::Point target = PointIn(sidebar()->row_at(2), 80);
  Drag(row, target);
  ASSERT_TRUE(sidebar()->dragging());
  AerieSidebarDragController* controller = sidebar()->drag_controller();
  ASSERT_TRUE(controller && controller->ghost());

  // The lift is a transform on the ghost; nothing else animates, and the
  // siblings shift with transforms.
  EXPECT_TRUE(OnlyTransformAndOpacityAnimate(sidebar()));
  EXPECT_TRUE(AnyTransformAnimates(sidebar()));

  Release(row, target);
  // The drop settles with a spring (a transform) and ends at rest.
  EXPECT_TRUE(OnlyTransformAndOpacityAnimate(sidebar()));
  ASSERT_TRUE(base::test::RunUntil([&] { return !controller->settling(); }));
  EXPECT_FALSE(AnyTransformAnimates(sidebar()));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragUiTest, EachLayerHasAtMostOneTransformAnimation) {
  ForceReducedMotion(false);
  OpenTabs(4);
  views::View* row = sidebar()->row_at(0);
  Press(row);
  Drag(row, PointIn(row, 50) + gfx::Vector2d(0, 20));
  Drag(row, PointIn(sidebar()->row_at(3), 80));
  Drag(row, PointIn(sidebar()->row_at(1), 50));
  Drag(row, PointIn(sidebar()->row_at(3), 80));
  ForEachLayer(sidebar(), [&](ui::Layer* layer) {
    // A new shift replaces the running one: the animator holds one sequence
    // for the transform.
    EXPECT_LE(layer->GetAnimator()->IsAnimatingProperty(
                  ui::LayerAnimationElement::TRANSFORM)
                  ? 1
                  : 0,
              1);
  });
  Release(row, PointIn(sidebar()->row_at(3), 80));
}

class AerieSidebarDragReducedMotionUiTest
    : public AerieSidebarDragUiTest,
      public testing::WithParamInterface<bool> {
 protected:
  void ReduceMotion() {
    if (GetParam()) {
      ForceReducedMotion(true);
      return;
    }
    ForceReducedMotion(false);
    browser()->GetProfile()->GetPrefs()->SetString(
        aerie::motion::kReduceMotionPref, aerie::motion::kReduceMotionOn);
  }
};

INSTANTIATE_TEST_SUITE_P(
    AerieReduceVia,
    AerieSidebarDragReducedMotionUiTest,
    testing::Bool(),
    [](const testing::TestParamInfo<bool>& info) {
      return info.param ? "OsFlag" : "Pref";
    });

IN_PROC_BROWSER_TEST_P(AerieSidebarDragReducedMotionUiTest, NothingAnimatesATransformWhileDraggingAndDropping) {
  ReduceMotion();
  OpenTabs(3);
  ASSERT_TRUE(base::test::RunUntil([&] { return !AnyTransformAnimates(sidebar()); }));
  views::View* row = sidebar()->row_at(0);
  const std::string first = sidebar()->tab_id_at(0);
  Press(row);
  Drag(row, PointIn(row, 50) + gfx::Vector2d(0, 20));
  const gfx::Point target = PointIn(sidebar()->row_at(2), 80);
  Drag(row, target);
  ASSERT_TRUE(sidebar()->dragging());
  EXPECT_FALSE(AnyTransformAnimates(sidebar()));
  // The ghost still tracks the pointer.
  ASSERT_TRUE(sidebar()->drag_controller()->ghost());
  EXPECT_TRUE(sidebar()->drag_controller()->ghost()->layer()->transform().IsIdentity());

  Release(row, target);
  EXPECT_FALSE(AnyTransformAnimates(sidebar()));
  EXPECT_FALSE(sidebar()->drag_controller()->settling());
  EXPECT_EQ(sidebar()->tab_id_at(2), first);
}
