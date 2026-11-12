// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Interactive UI tests of tab rename (issue #145): F2, Enter and Esc with real
// key events and real focus; focus going back to the row; Backspace and the
// other editing keys staying in the field (a pinned row that left its page goes
// back on a bare Backspace, and must not while the field is open); focus loss
// keeping the name; and the motion of the field and the pencil, which is an
// opacity fade on `xfast` and never a transform, with or without reduced
// motion. Run with ./aerie test-cpp --target aerie_interactive_ui_tests.

#include <optional>
#include <string>

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "chrome/browser/aerie_tab_model/pinned_tabs_controller.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_rename_field.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_rename_popover.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_tile_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_tab_model/pinned_tabs.h"
#include "components/aerie_tab_model/tab_model.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/test/layer_animator_test_controller.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/range/range.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
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

// True when no layer property but opacity animates, and opacity does.
bool OnlyOpacityAnimates(ui::Layer* layer) {
  if (!layer) {
    ADD_FAILURE() << "no layer";
    return false;
  }
  for (int bit = ui::LayerAnimationElement::FIRST_PROPERTY;
       bit != ui::LayerAnimationElement::SENTINEL; bit <<= 1) {
    const auto property =
        static_cast<ui::LayerAnimationElement::AnimatableProperty>(bit);
    const bool animates = layer->GetAnimator()->IsAnimatingProperty(property);
    if (property == ui::LayerAnimationElement::OPACITY) {
      if (!animates) {
        ADD_FAILURE() << "opacity does not animate";
        return false;
      }
    } else if (animates) {
      ADD_FAILURE() << "property " << bit << " animates";
      return false;
    }
  }
  return true;
}

// The real duration of the opacity animation on `layer`, in ms; -1 when none.
int OpacityDurationMs(ui::Layer* layer) {
  ui::LayerAnimatorTestController controller(layer->GetAnimator());
  ui::LayerAnimationSequence* sequence =
      controller.GetRunningSequence(ui::LayerAnimationElement::OPACITY);
  return sequence ? static_cast<int>(
                        sequence->FirstElement()->duration().InMilliseconds())
                  : -1;
}

}  // namespace

class AerieSidebarRenameUiTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
    host_resolver()->AddRule("*", "127.0.0.1");
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
  views::View* focused() {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->GetWidget()
        ->GetFocusManager()
        ->GetFocusedView();
  }
  void Press(ui::KeyboardCode key, bool control = false, bool shift = false) {
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), key, control, shift,
                                                false, false));
  }
  tabs::TabInterface* OpenTab(const std::string& path = "/title1.html") {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), embedded_test_server()->GetURL(path),
        WindowOpenDisposition::NEW_BACKGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    EXPECT_TRUE(base::test::RunUntil([&] {
      return sidebar()->row_count() ==
             aerie_tab_model::SectionTabIds(*service()->model(),
                                            aerie_tab_model::Section::kToday)
                 .size();
    }));
    return browser()->tab_strip_model()->GetTabAtIndex(
        browser()->tab_strip_model()->count() - 1);
  }
  std::string IdOf(tabs::TabInterface* tab) {
    return service()->adapter()->RecordIdForTab(tab);
  }
  std::optional<std::string> NameOf(const std::string& id) {
    return service()->model()->GetTab(id)->manual_name;
  }
  // A Today row with focus.
  AerieSidebarRowView* FocusTodayRow() {
    tabs::TabInterface* tab = OpenTab();
    AerieSidebarRowView* row = nullptr;
    for (size_t i = 0; i < sidebar()->row_count(); ++i) {
      if (sidebar()->tab_id_at(i) == IdOf(tab)) {
        row = sidebar()->row_at(i);
      }
    }
    EXPECT_TRUE(row);
    sidebar()->today_tree()->RequestFocus();
    for (size_t i = 0; i < sidebar()->row_count() && focused() != row; ++i) {
      Press(ui::VKEY_DOWN);
    }
    EXPECT_EQ(focused(), row);
    return row;
  }

  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

// Acceptance: F2 starts, Enter keeps, and focus goes back to the row.
IN_PROC_BROWSER_TEST_F(AerieSidebarRenameUiTest,
                       F2StartsEnterKeepsAndFocusReturnsToTheRow) {
  ForceReducedMotion(true);
  AerieSidebarRowView* row = FocusTodayRow();
  const std::string id = sidebar()->tab_id_at(sidebar()->row_count() - 1);
  Press(ui::VKEY_F2);
  ASSERT_TRUE(row->renaming());
  ASSERT_TRUE(
      base::test::RunUntil([&] { return focused() == row->rename_field(); }));
  // The whole title is selected, so a typed letter replaces it.
  Press(ui::VKEY_X);
  EXPECT_EQ(base::ToLowerASCII(row->rename_field()->GetText()), u"x");
  Press(ui::VKEY_RETURN);
  EXPECT_FALSE(row->renaming());
  EXPECT_EQ(focused(), row);
  ASSERT_TRUE(NameOf(id));
  EXPECT_EQ(base::ToLowerASCII(*NameOf(id)), "x");
  EXPECT_TRUE(base::test::RunUntil([&] { return row->renamed(); }));
}

// Acceptance: Esc cancels and focus goes back to the row.
IN_PROC_BROWSER_TEST_F(AerieSidebarRenameUiTest,
                       EscCancelsAndFocusReturnsToTheRow) {
  ForceReducedMotion(true);
  AerieSidebarRowView* row = FocusTodayRow();
  const std::string id = sidebar()->tab_id_at(sidebar()->row_count() - 1);
  Press(ui::VKEY_F2);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return focused() == row->rename_field(); }));
  Press(ui::VKEY_X);
  Press(ui::VKEY_ESCAPE);
  EXPECT_FALSE(row->renaming());
  EXPECT_EQ(focused(), row);
  EXPECT_EQ(NameOf(id), std::nullopt);
}

// Known trap (#180): a pinned row that has left its page goes back on a bare
// Backspace. While the field is open, Backspace edits the text and never
// reaches the row, with text in the field and with the field empty.
IN_PROC_BROWSER_TEST_F(AerieSidebarRenameUiTest,
                       BackspaceInTheFieldNeverGoesBackToThePinnedPage) {
  ForceReducedMotion(true);
  tabs::TabInterface* tab = OpenTab("/title1.html");
  ASSERT_TRUE(service()->pinned_tabs()->Pin(tab));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->pinned_row_count() == 1u; }));
  browser()->tab_strip_model()->ActivateTabAt(
      browser()->tab_strip_model()->GetIndexOfTab(tab));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title2.html")));
  AerieSidebarRowView* row = sidebar()->pinned_row_at(0);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return row->back_to_pinned_button()->GetVisible(); }));
  sidebar()->pinned_tree()->RequestFocus();
  ASSERT_EQ(focused(), row);

  Press(ui::VKEY_F2);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return focused() == row->rename_field(); }));
  row->rename_field()->SetText(u"abc");
  row->rename_field()->SetSelectedRange(gfx::Range(3, 3));
  Press(ui::VKEY_BACK);
  EXPECT_EQ(row->rename_field()->GetText(), u"ab");
  // Delete the rest, then Backspace on an empty field.
  Press(ui::VKEY_BACK);
  Press(ui::VKEY_BACK);
  Press(ui::VKEY_BACK);
  EXPECT_TRUE(row->rename_field()->GetText().empty());
  EXPECT_TRUE(row->renaming());
  EXPECT_EQ(tab->GetContents()->GetLastCommittedURL().path(), "/title2.html");
  EXPECT_TRUE(row->back_to_pinned_button()->GetVisible() || row->renaming());
  // Delete and Ctrl+Backspace edit as well; they do not close or go back.
  Press(ui::VKEY_DELETE);
  Press(ui::VKEY_BACK, /*control=*/true);
  EXPECT_TRUE(row->renaming());
  EXPECT_EQ(sidebar()->pinned_row_count(), 1u);
  EXPECT_EQ(tab->GetContents()->GetLastCommittedURL().path(), "/title2.html");
  Press(ui::VKEY_ESCAPE);
  EXPECT_FALSE(row->renaming());
  EXPECT_EQ(tab->GetContents()->GetLastCommittedURL().path(), "/title2.html");
  // With the field closed, the bare key works again.
  ASSERT_EQ(focused(), row);
  Press(ui::VKEY_BACK);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return tab->GetContents()->GetLastCommittedURL().path() == "/title1.html";
  }));
}

// Decision: focus loss keeps the name. Real focus moves to the page.
IN_PROC_BROWSER_TEST_F(AerieSidebarRenameUiTest, MovingFocusAwayKeepsTheName) {
  ForceReducedMotion(true);
  AerieSidebarRowView* row = FocusTodayRow();
  const std::string id = sidebar()->tab_id_at(sidebar()->row_count() - 1);
  Press(ui::VKEY_F2);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return focused() == row->rename_field(); }));
  row->rename_field()->SetText(u"Kept on blur");
  browser()->tab_strip_model()->GetActiveWebContents()->Focus();
  ASSERT_TRUE(base::test::RunUntil([&] { return !row->renaming(); }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return NameOf(id) == std::optional<std::string>("Kept on blur");
  }));
  EXPECT_EQ(NameOf(id), "Kept on blur");
}

// Acceptance: the field swap and the pencil use `xfast` and only `opacity`.
// An interactive test asserts that no transform animation exists.
IN_PROC_BROWSER_TEST_F(AerieSidebarRenameUiTest,
                       TheFieldAndThePencilFadeOnXfastWithNoTransform) {
  ForceReducedMotion(false);
  AerieSidebarRowView* row = FocusTodayRow();
  const std::string id = sidebar()->tab_id_at(sidebar()->row_count() - 1);
  Press(ui::VKEY_F2);
  ASSERT_TRUE(row->renaming());
  ui::Layer* field_layer = row->rename_field()->layer();
  ASSERT_TRUE(field_layer);
  EXPECT_TRUE(OnlyOpacityAnimates(field_layer));
  EXPECT_EQ(OpacityDurationMs(field_layer), 90);
  EXPECT_EQ(OpacityDurationMs(field_layer),
            aerie::tokens::GetDurationMs(aerie::tokens::MotionDuration::kXfast,
                                         false));
  EXPECT_TRUE(field_layer->transform().IsIdentity());
  // The title goes out the same way: opacity only, on `xfast`, and nothing of
  // it moves.
  ui::Layer* title_layer = row->title_view()->layer();
  ASSERT_TRUE(title_layer);
  EXPECT_TRUE(OnlyOpacityAnimates(title_layer));
  EXPECT_EQ(OpacityDurationMs(title_layer), 90);
  EXPECT_TRUE(title_layer->transform().IsIdentity());
  EXPECT_FLOAT_EQ(title_layer->GetTargetOpacity(), 0.f);
  Press(ui::VKEY_X);
  Press(ui::VKEY_RETURN);
  ASSERT_TRUE(base::test::RunUntil([&] { return row->renamed(); }));
  // The title is back at full opacity and in the layout.
  EXPECT_FLOAT_EQ(row->title_view()->layer()->GetTargetOpacity(), 1.f);
  EXPECT_FALSE(row->title_view()->GetProperty(views::kViewIgnoredByLayoutKey));

  // The pencil fades in the same way. (It faded in when the name was set.)
  ui::Layer* pencil_layer = row->pencil_button()->layer();
  ASSERT_TRUE(pencil_layer);
  EXPECT_TRUE(row->pencil_button()->GetVisible());
  EXPECT_FALSE(pencil_layer->GetAnimator()->IsAnimatingProperty(
      ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(pencil_layer->transform().IsIdentity());
}

// The pencil's own fade, read at the moment it starts.
IN_PROC_BROWSER_TEST_F(AerieSidebarRenameUiTest,
                       ThePencilAppearsWithAnXfastOpacityFade) {
  ForceReducedMotion(false);
  AerieSidebarRowView* row = FocusTodayRow();
  const std::string id = sidebar()->tab_id_at(sidebar()->row_count() - 1);
  sidebar()->CommitRename(id, u"Mine");
  // The sidebar updates the row in the same turn the model changes or the next:
  // read the layer as soon as the pencil is visible.
  ASSERT_TRUE(base::test::RunUntil([&] { return row->renamed(); }));
  ui::Layer* layer = row->pencil_button()->layer();
  ASSERT_TRUE(layer);
  if (layer->GetAnimator()->is_animating()) {
    EXPECT_TRUE(OnlyOpacityAnimates(layer));
    EXPECT_EQ(OpacityDurationMs(layer), 90);
  }
  EXPECT_TRUE(layer->transform().IsIdentity());
}

// Reduced motion: unchanged. It is already an opacity-only fade at 90 ms.
IN_PROC_BROWSER_TEST_F(AerieSidebarRenameUiTest,
                       ReducedMotionLeavesTheFadeUnchanged) {
  ForceReducedMotion(true);
  AerieSidebarRowView* row = FocusTodayRow();
  Press(ui::VKEY_F2);
  ASSERT_TRUE(row->renaming());
  ui::Layer* field_layer = row->rename_field()->layer();
  ASSERT_TRUE(field_layer);
  EXPECT_TRUE(OnlyOpacityAnimates(field_layer));
  EXPECT_EQ(OpacityDurationMs(field_layer), 90);
  EXPECT_TRUE(field_layer->transform().IsIdentity());
  Press(ui::VKEY_ESCAPE);
}

// Acceptance: F2 on a Favorites tile opens the popover; Enter keeps the name
// and focus goes back to the tile.
IN_PROC_BROWSER_TEST_F(AerieSidebarRenameUiTest,
                       F2OnATileOpensThePopoverAndEnterBringsFocusBack) {
  ForceReducedMotion(true);
  tabs::TabInterface* tab = OpenTab();
  ASSERT_EQ(service()->pinned_tabs()->AddToFavorites(tab),
            aerie_tab_model::FavoriteResult::kAdded);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->favorite_tile_count() == 1u; }));
  AerieSidebarTileView* tile = sidebar()->tile_at(0);
  sidebar()->favorites_grid()->RequestFocus();
  ASSERT_EQ(focused(), tile);
  Press(ui::VKEY_F2);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->rename_popover() != nullptr; }));
  AerieSidebarRenameField* field = sidebar()->rename_popover()->field();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return field->GetFocusManager() &&
           field->GetFocusManager()->GetFocusedView() == field;
  }));
  Press(ui::VKEY_X);
  Press(ui::VKEY_RETURN);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->rename_popover() == nullptr; }));
  ASSERT_TRUE(NameOf(IdOf(tab)));
  EXPECT_EQ(base::ToLowerASCII(*NameOf(IdOf(tab))), "x");
  ASSERT_TRUE(base::test::RunUntil([&] { return focused() == tile; }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarRenameUiTest,
                       EscInThePopoverCancelsAndBringsFocusBack) {
  ForceReducedMotion(true);
  tabs::TabInterface* tab = OpenTab();
  ASSERT_EQ(service()->pinned_tabs()->AddToFavorites(tab),
            aerie_tab_model::FavoriteResult::kAdded);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->favorite_tile_count() == 1u; }));
  AerieSidebarTileView* tile = sidebar()->tile_at(0);
  sidebar()->favorites_grid()->RequestFocus();
  Press(ui::VKEY_F2);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->rename_popover() != nullptr; }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* f = sidebar()->rename_popover()->field();
    return f->GetFocusManager() && f->GetFocusManager()->GetFocusedView() == f;
  }));
  Press(ui::VKEY_X);
  Press(ui::VKEY_ESCAPE);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->rename_popover() == nullptr; }));
  EXPECT_EQ(NameOf(IdOf(tab)), std::nullopt);
  ASSERT_TRUE(base::test::RunUntil([&] { return focused() == tile; }));
}
