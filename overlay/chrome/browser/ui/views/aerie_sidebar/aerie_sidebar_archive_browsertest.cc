// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of the Archive in the sidebar (issue #183): the button, the
// panel (a dialog named "Archive" with a searchbox and a listbox), the groups,
// the search, the header menu, restoring by click and by Enter with
// "Restored <title>", the keys, the focus that returns to the button, and the
// animators of the archive moment, the restore and the popover. Reduced motion
// is tested in aerie_sidebar_archive_uitest.cc.

#include <optional>
#include <string>
#include <vector>

#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
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
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "components/aerie_tab_model/archive.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_prefs.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/test/layer_animator_test_controller.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/test/event_generator.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/focus/focus_manager.h"
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

ui::AXNodeData DataOf(views::View* view) {
  ui::AXNodeData data;
  view->GetViewAccessibility().GetAccessibleNodeData(&data);
  return data;
}

std::string NameOf(views::View* view) {
  return DataOf(view).GetStringAttribute(ax::mojom::StringAttribute::kName);
}

bool Animates(ui::Layer* layer,
              ui::LayerAnimationElement::AnimatableProperty property) {
  return layer && layer->GetAnimator()->IsAnimatingProperty(property);
}

// The length of the running animation of `property`, or -1.
int RunningMs(ui::Layer* layer,
              ui::LayerAnimationElement::AnimatableProperty property) {
  ui::LayerAnimatorTestController controller(layer->GetAnimator());
  ui::LayerAnimationSequence* sequence = controller.GetRunningSequence(property);
  return sequence ? static_cast<int>(
                        sequence->FirstElement()->duration().InMilliseconds())
                  : -1;
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

}  // namespace

class AerieSidebarArchiveBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    service_ = aerie_tab_model::TabModelServiceFactory::GetForProfile(
        browser()->GetProfile());
    ASSERT_TRUE(service_);
    archiver_ = aerie_tab_model::TabArchiverFactory::GetForProfile(
        browser()->GetProfile());
    ASSERT_TRUE(archiver_);
    sidebar()->archive()->Attach();
    ForceReducedMotion(true);
  }

  void TearDownOnMainThread() override {
    if (sidebar() && sidebar()->archive()->panel()) {
      sidebar()->archive()->panel()->ClosePanel(false);
    }
    archiver_ = nullptr;
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
  AerieSidebarArchiveController* controller() { return sidebar()->archive(); }
  AerieSidebarArchivePanel* panel() { return controller()->panel(); }
  aerie_tab_model::TabModel* model() { return service_->model(); }
  TabStripModel* strip() { return browser()->tab_strip_model(); }
  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }

  std::string IdAt(int index) {
    return service_->adapter()->RecordIdForTab(strip()->GetTabAtIndex(index));
  }

  // A loaded tab behind the selected one; returns its record id.
  std::string OpenBackgroundTab(const char* path = "/title1.html") {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), embedded_test_server()->GetURL(path),
        WindowOpenDisposition::NEW_BACKGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    const std::string id = IdAt(strip()->count() - 1);
    EXPECT_FALSE(id.empty());
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return sidebar()->RowForId(id) != nullptr; }));
    return id;
  }

  // Waits for the row of `id` (and the list) to stop moving: the same bounds
  // on several checks in a row. Geometry read earlier races the open motion.
  void WaitForRowToRest(const std::string& id) {
    gfx::Rect last;
    int same = 0;
    ASSERT_TRUE(base::test::RunUntil([&] {
      AerieSidebarRowView* row = sidebar()->RowForId(id);
      if (!row || row->height() == 0) {
        return false;
      }
      same = row->bounds() == last ? same + 1 : 0;
      last = row->bounds();
      return same >= 10;
    }));
  }

  // Archives the tab of `id` through the archiver, as the timer does.
  void ArchiveTab(const std::string& id) {
    for (int i = 0; i < strip()->count(); ++i) {
      if (IdAt(i) == id) {
        ASSERT_TRUE(archiver_->Archive(strip()->GetTabAtIndex(i)));
        break;
      }
    }
    const auto* record = model()->GetTab(id);
    ASSERT_TRUE(record);
    ASSERT_TRUE(record->archived_at);
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return sidebar()->RowForId(id) == nullptr; }));
  }

  // A record in the Archive with no tab behind it, archived at `when`.
  std::string AddArchived(const std::string& title,
                          const std::string& url,
                          base::Time when) {
    aerie_tab_model::TabRecord tab;
    tab.workspace_id = model()->workspace().id;
    tab.section = aerie_tab_model::Section::kToday;
    tab.archived_at = when;
    aerie_tab_model::ArchiveData data;
    data.url = url;
    data.title = title;
    data.favicon_key = url;
    tab.archive = data;
    return model()->AddTab(std::move(tab));
  }

  void OpenPanel() {
    ASSERT_FALSE(panel());
    views::test::ButtonTestApi(sidebar()->archive_button()).NotifyClick(ui::MouseEvent(
        ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
        base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
        ui::EF_LEFT_MOUSE_BUTTON));
    ASSERT_TRUE(panel());
    ASSERT_TRUE(panel()->GetWidget());
  }

  void Press(ui::KeyboardCode code, views::View* target) {
    ui::KeyEvent event(ui::EventType::kKeyPressed, code, ui::EF_NONE);
    if (auto* field = views::AsViewClass<views::Textfield>(target)) {
      ASSERT_TRUE(panel()->HandleKeyEvent(field, event));
    } else {
      target->OnKeyPressed(event);
    }
  }

  // Types into the search field and tells the panel, as typing does.
  void Search(const std::u16string& text) {
    panel()->search()->SetText(text);
    panel()->ContentsChanged(panel()->search(), text);
  }

  void ClickRow(size_t index) {
    views::test::ButtonTestApi(panel()->rows()[index])
        .NotifyClick(ui::MouseEvent(
            ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
            base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
            ui::EF_LEFT_MOUSE_BUTTON));
  }

  raw_ptr<aerie_tab_model::TabModelService> service_ = nullptr;
  raw_ptr<aerie_tab_model::TabArchiver> archiver_ = nullptr;

 private:
  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

// ---- the button -------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(AerieSidebarArchiveBrowserTest,
                       TheButtonIsInTheBottomBarFocusableAndNamed) {
  AerieSidebarArchiveButton* button = sidebar()->archive_button();
  ASSERT_TRUE(button);
  EXPECT_TRUE(button->IsFocusable());
  EXPECT_EQ(NameOf(button),
            l10n_util::GetStringUTF8(IDS_AERIE_SIDEBAR_ARCHIVE_BUTTON));
  EXPECT_EQ(DataOf(button).role, ax::mojom::Role::kButton);
  EXPECT_EQ(base::UTF16ToUTF8(button->GetRenderedTooltipText(gfx::Point())),
            l10n_util::GetStringUTF8(IDS_AERIE_SIDEBAR_ARCHIVE_BUTTON));
}

// ---- the panel --------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(AerieSidebarArchiveBrowserTest,
                       ThePanelIsADialogNamedArchiveWithASearchboxAndAListbox) {
  AddArchived("Alpha docs", "https://example.com/alpha", base::Time::Now());
  OpenPanel();
  EXPECT_EQ(panel()->GetAccessibleWindowTitle(), u"Archive");
  EXPECT_EQ(panel()->GetAccessibleWindowRole(), ax::mojom::Role::kDialog);
  EXPECT_EQ(DataOf(panel()->search()).role, ax::mojom::Role::kSearchBox);
  EXPECT_FALSE(NameOf(panel()->search()).empty());
  EXPECT_EQ(DataOf(panel()->list()).role, ax::mojom::Role::kListBox);
  EXPECT_FALSE(NameOf(panel()->list()).empty());
  EXPECT_NE(panel()->timeout_button(), nullptr);
  EXPECT_FALSE(NameOf(panel()->timeout_button()).empty());
  ASSERT_EQ(panel()->rows().size(), 1u);
  EXPECT_EQ(DataOf(panel()->rows()[0]).role, ax::mojom::Role::kListBoxOption);
  // The search has focus when the panel opens.
  EXPECT_TRUE(panel()->search()->HasFocus());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarArchiveBrowserTest,
                       RowsAreGroupedByDayAndShowTheirWorkspace) {
  const base::Time midnight = base::Time::Now().LocalMidnight();
  AddArchived("Earlier page", "https://example.com/earlier",
              midnight - base::Days(20));
  AddArchived("Last week page", "https://example.com/week",
              midnight - base::Days(3) - base::Hours(12));
  AddArchived("Yesterday page", "https://example.com/yesterday",
              midnight - base::Hours(12));
  AddArchived("Today page", "https://example.com/today", base::Time::Now());
  OpenPanel();
  EXPECT_EQ(panel()->group_titles(),
            (std::vector<std::u16string>{u"Today", u"Yesterday", u"Last week",
                                         u"Earlier"}));
  const auto rows = panel()->rows();
  ASSERT_EQ(rows.size(), 4u);
  EXPECT_EQ(rows[0]->title(), u"Today page");
  EXPECT_EQ(rows[1]->title(), u"Yesterday page");
  EXPECT_EQ(rows[2]->title(), u"Last week page");
  EXPECT_EQ(rows[3]->title(), u"Earlier page");
  const std::u16string workspace =
      base::UTF8ToUTF16(model()->workspace().name);
  for (auto* row : rows) {
    EXPECT_EQ(row->workspace(), workspace);
    EXPECT_NE(NameOf(row).find(base::UTF16ToUTF8(workspace)),
              std::string::npos);
    EXPECT_NE(NameOf(row).find(base::UTF16ToUTF8(row->title())),
              std::string::npos);
  }
}

IN_PROC_BROWSER_TEST_F(AerieSidebarArchiveBrowserTest,
                       AGroupWithNoRowHasNoHeadingAndAnEmptyArchiveSaysSo) {
  OpenPanel();
  EXPECT_TRUE(panel()->group_titles().empty());
  EXPECT_TRUE(panel()->rows().empty());
  panel()->ClosePanel(true);
  AddArchived("Only today", "https://example.com/a", base::Time::Now());
  OpenPanel();
  EXPECT_EQ(panel()->group_titles(), (std::vector<std::u16string>{u"Today"}));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarArchiveBrowserTest,
                       SearchMatchesTitleAndAddressIgnoringCase) {
  AddArchived("Alpha docs", "https://example.com/one", base::Time::Now());
  AddArchived("Beta notes", "https://Example.org/Gamma", base::Time::Now());
  AddArchived("Delta", "https://example.net/delta", base::Time::Now());
  OpenPanel();
  ASSERT_EQ(panel()->rows().size(), 3u);
  Search(u"ALPHA");
  ASSERT_EQ(panel()->rows().size(), 1u);
  EXPECT_EQ(panel()->rows()[0]->title(), u"Alpha docs");
  Search(u"gamma");
  ASSERT_EQ(panel()->rows().size(), 1u);
  EXPECT_EQ(panel()->rows()[0]->title(), u"Beta notes");
  Search(u"nothing like it");
  EXPECT_TRUE(panel()->rows().empty());
  Search(u"");
  EXPECT_EQ(panel()->rows().size(), 3u);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarArchiveBrowserTest,
                       TheHeaderMenuHasTheFourValuesAndWritesThePref) {
  OpenPanel();
  EXPECT_EQ(prefs()->GetString(aerie_tab_model::kArchiveAfter), "12h");
  EXPECT_TRUE(panel()->IsCommandIdChecked(1));
  panel()->ExecuteCommand(2, 0);
  EXPECT_EQ(prefs()->GetString(aerie_tab_model::kArchiveAfter), "24h");
  EXPECT_EQ(panel()->timeout_button()->GetText(), u"24 hours");
  EXPECT_TRUE(panel()->IsCommandIdChecked(2));
  panel()->ExecuteCommand(3, 0);
  EXPECT_EQ(prefs()->GetString(aerie_tab_model::kArchiveAfter), "7d");
  panel()->ExecuteCommand(4, 0);
  EXPECT_EQ(prefs()->GetString(aerie_tab_model::kArchiveAfter), "never");
  EXPECT_EQ(panel()->timeout_button()->GetText(), u"Never");
  panel()->ExecuteCommand(1, 0);
  EXPECT_EQ(prefs()->GetString(aerie_tab_model::kArchiveAfter), "12h");
  // The menu is a real menu of four checkable items.
  panel()->ShowTimeoutMenu();
  EXPECT_EQ(NameOf(panel()->timeout_button()),
            "Archive Today tabs after 12 hours");
}

// ---- restoring --------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(AerieSidebarArchiveBrowserTest,
                       AClickRestoresTheTabAtTheTopOfTodayAndAnnouncesIt) {
  const std::string other = OpenBackgroundTab("/title2.html");
  const std::string id = OpenBackgroundTab("/title1.html");
  ArchiveTab(id);
  EXPECT_EQ(strip()->count(), 2);
  OpenPanel();
  ASSERT_EQ(panel()->rows().size(), 1u);
  const std::u16string title = panel()->rows()[0]->title();
  ClickRow(0);
  EXPECT_FALSE(panel());
  ASSERT_TRUE(base::test::RunUntil([&] { return strip()->count() == 3; }));
  EXPECT_EQ(IdAt(strip()->active_index()), id);
  const auto* record = model()->GetTab(id);
  ASSERT_TRUE(record);
  EXPECT_FALSE(record->archived_at);
  EXPECT_EQ(record->section, aerie_tab_model::Section::kToday);
  EXPECT_LT(record->position, model()->GetTab(other)->position);
  EXPECT_EQ(controller()->last_announcement(), u"Restored " + title);
  // The restored tab is selected, so focus goes to its page, not the button.
  EXPECT_FALSE(panel());
  EXPECT_TRUE(aerie_tab_model::ArchivedRows(*model()).empty());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarArchiveBrowserTest,
                       EnterInTheSearchRestoresTheFirstMatch) {
  const std::string id = OpenBackgroundTab("/title1.html");
  ArchiveTab(id);
  AddArchived("Unrelated", "https://example.com/unrelated", base::Time::Now());
  OpenPanel();
  Search(u"title1");
  ASSERT_EQ(panel()->rows().size(), 1u);
  Press(ui::VKEY_RETURN, panel()->search());
  EXPECT_FALSE(panel());
  ASSERT_TRUE(base::test::RunUntil([&] {
    const auto* record = model()->GetTab(id);
    return record && !record->archived_at;
  }));
  EXPECT_NE(controller()->last_announcement().find(u"Restored "),
            std::u16string::npos);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarArchiveBrowserTest,
                       UpAndDownMoveBetweenTheSearchAndTheRowsAndEscCloses) {
  AddArchived("One", "https://example.com/1", base::Time::Now());
  AddArchived("Two", "https://example.com/2", base::Time::Now());
  OpenPanel();
  views::FocusManager* focus = panel()->GetWidget()->GetFocusManager();
  ASSERT_TRUE(panel()->search()->HasFocus());
  Press(ui::VKEY_DOWN, panel()->search());
  EXPECT_EQ(focus->GetFocusedView(), panel()->rows()[0]);
  Press(ui::VKEY_DOWN, panel()->rows()[0]);
  EXPECT_EQ(focus->GetFocusedView(), panel()->rows()[1]);
  Press(ui::VKEY_DOWN, panel()->rows()[1]);
  EXPECT_EQ(focus->GetFocusedView(), panel()->rows()[1]);
  Press(ui::VKEY_UP, panel()->rows()[1]);
  EXPECT_EQ(focus->GetFocusedView(), panel()->rows()[0]);
  Press(ui::VKEY_UP, panel()->rows()[0]);
  EXPECT_TRUE(panel()->search()->HasFocus());
  Press(ui::VKEY_ESCAPE, panel()->search());
  EXPECT_FALSE(panel());
  EXPECT_TRUE(sidebar()->archive_button()->HasFocus());
  // Esc from a row works too.
  OpenPanel();
  Press(ui::VKEY_DOWN, panel()->search());
  Press(ui::VKEY_ESCAPE, panel()->rows()[0]);
  EXPECT_FALSE(panel());
  EXPECT_TRUE(sidebar()->archive_button()->HasFocus());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarArchiveBrowserTest,
                       TheButtonOpensAndClosesThePanel) {
  OpenPanel();
  views::test::ButtonTestApi(sidebar()->archive_button()).NotifyClick(ui::MouseEvent(
      ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
      base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
      ui::EF_LEFT_MOUSE_BUTTON));
  EXPECT_FALSE(panel());
}

// ---- motion -----------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(AerieSidebarArchiveBrowserTest,
                       TheArchiveMomentFliesACopyOnTheNamedTokensAndPulsesTheButton) {
  ForceReducedMotion(false);
  OpenBackgroundTab("/title2.html");
  const std::string id = OpenBackgroundTab("/title1.html");
  // Both rows have come to rest before the archive moment starts.
  WaitForRowToRest(id);
  for (int i = 0; i < strip()->count(); ++i) {
    if (IdAt(i) == id) {
      ASSERT_TRUE(archiver_->Archive(strip()->GetTabAtIndex(i)));
      break;
    }
  }
  ui::Layer* copy = controller()->FlightLayer("archive:" + id);
  ASSERT_TRUE(copy);
  // The copy shrinks to `motion-scale-archive-collapse` (0.25) and flies on
  // `archive-collapse` (320 ms); it fades as it lands. Only transform and
  // opacity animate.
  EXPECT_TRUE(Animates(copy, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_EQ(RunningMs(copy, ui::LayerAnimationElement::TRANSFORM),
            aerie::tokens::GetDurationMs(
                aerie::tokens::MotionDuration::kArchiveCollapse, false));
  EXPECT_EQ(RunningMs(copy, ui::LayerAnimationElement::TRANSFORM), 320);
  EXPECT_TRUE(OnlyTransformAndOpacity(copy));
  const gfx::Transform target = copy->GetTargetTransform();
  EXPECT_FLOAT_EQ(target.rc(0, 0), aerie::tokens::kMotionScaleArchiveCollapse);
  EXPECT_FLOAT_EQ(target.rc(1, 1), 0.25f);
  EXPECT_FLOAT_EQ(copy->GetTargetOpacity(), 0.f);

  // When the copy lands the button pulses to `motion-scale-archive-pulse`
  // (1.25) and back over `archive-pulse` (360 ms): a transform of its layer.
  AerieSidebarArchiveButton* button = sidebar()->archive_button();
  ASSERT_TRUE(base::test::RunUntil([&] { return button->pulsing(); }));
  ui::Layer* pulse = button->layer();
  ASSERT_TRUE(pulse);
  EXPECT_TRUE(Animates(pulse, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(OnlyTransformAndOpacity(pulse));
  EXPECT_FLOAT_EQ(pulse->GetTargetTransform().rc(0, 0),
                  aerie::tokens::kMotionScaleArchivePulse);
  EXPECT_FLOAT_EQ(pulse->GetTargetTransform().rc(0, 0), 1.25f);
  EXPECT_EQ(RunningMs(pulse, ui::LayerAnimationElement::TRANSFORM),
            aerie::tokens::GetDurationMs(
                aerie::tokens::MotionDuration::kArchivePulse, false) /
                2);
  // It comes to rest: no layer, no scale.
  ASSERT_TRUE(base::test::RunUntil([&] { return !button->pulsing(); }));
  EXPECT_EQ(button->layer(), nullptr);
  EXPECT_EQ(controller()->flights_in_progress(), 0u);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarArchiveBrowserTest,
                       RestoreFliesACopyOnTheSpringOverArchiveRestore) {
  ForceReducedMotion(false);
  const std::string id = OpenBackgroundTab("/title1.html");
  ArchiveTab(id);
  OpenPanel();
  ClickRow(0);
  ui::Layer* copy = controller()->FlightLayer("restore:" + id);
  ASSERT_TRUE(copy);
  EXPECT_TRUE(Animates(copy, ui::LayerAnimationElement::TRANSFORM));
  // `archive-restore` (340 ms) on the spring: the plan names the tokens, and
  // the spring is played as several short linear pieces of one sequence.
  const aerie_sidebar::ArchiveRestoreMotion plan =
      aerie_sidebar::RestoreTabMotion(false);
  EXPECT_EQ(plan.duration, aerie::tokens::MotionDuration::kArchiveRestore);
  EXPECT_EQ(plan.duration_ms, 340);
  EXPECT_EQ(plan.easing, aerie::tokens::MotionEasing::kSpring);
  EXPECT_LT(RunningMs(copy, ui::LayerAnimationElement::TRANSFORM), 340);
  EXPECT_TRUE(copy->GetTargetTransform().IsIdentity());
  EXPECT_TRUE(OnlyTransformAndOpacity(copy));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return controller()->flights_in_progress() == 0u; }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarArchiveBrowserTest,
                       ThePopoverDropsFourPixelsAndFadesInOnFastAndDecelerate) {
  ForceReducedMotion(false);
  OpenPanel();
  ui::Layer* layer = panel()->GetContentsView()->layer();
  ASSERT_TRUE(layer);
  EXPECT_TRUE(Animates(layer, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(Animates(layer, ui::LayerAnimationElement::OPACITY));
  EXPECT_TRUE(OnlyTransformAndOpacity(layer));
  const int fast = aerie::tokens::GetDurationMs(
      aerie::tokens::MotionDuration::kFast, false);
  EXPECT_EQ(RunningMs(layer, ui::LayerAnimationElement::TRANSFORM), fast);
  EXPECT_EQ(RunningMs(layer, ui::LayerAnimationElement::OPACITY), fast);
  EXPECT_TRUE(layer->GetTargetTransform().IsIdentity());
  EXPECT_FLOAT_EQ(layer->GetTargetOpacity(), 1.f);
  // It starts 4 px up.
  EXPECT_FLOAT_EQ(layer->transform().To2dTranslation().y(), -4.f);
  // There is no closing animation: the widget goes at once.
  panel()->ClosePanel(true);
  EXPECT_FALSE(panel());
}
