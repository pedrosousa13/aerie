// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of auto-filing folders (issue #139): a new tab that matches a
// rule goes into its folder, the toast and its two actions, the switches, the
// folder editor sheet and what a screen reader hears. The timing of the move
// (the 280 ms pause, the 10 s window, a tab moved by hand) is tested with a fake
// clock in auto_file_controller_unittest.cc; these tests run on real time, and
// the pause under reduced motion is 120 ms.

#include <string>
#include <vector>

#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_context_menu.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_folder_sheet.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_folder_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_switch.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_tab_model/folder_rules.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_prefs.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace {

using aerie_tab_model::FolderKind;
using aerie_tab_model::RuleType;
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

void Click(views::Button* button) {
  views::test::ButtonTestApi(button).NotifyClick(
      ui::MouseEvent(ui::EventType::kMousePressed, gfx::Point(1, 1),
                     gfx::Point(1, 1), base::TimeTicks::Now(),
                     ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON));
}

void WaitMs(int ms) {
  base::RunLoop loop;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, loop.QuitClosure(), base::Milliseconds(ms));
  loop.Run();
}

}  // namespace

class AerieSidebarAutoFileBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
        embedded_test_server()->ServeFilesFromSourceDirectory(
        "chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return sidebar() && sidebar()->row_count() == 1u; }));
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
  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }

  GURL UrlOn(const std::string& host) {
    return embedded_test_server()->GetURL(host, "/title1.html");
  }

  // An auto-filing folder in `section` with one rule.
  std::string MakeAutoFolder(const std::string& name,
                             RuleType type,
                             const std::string& value,
                             Section section = Section::kToday) {
    aerie_tab_model::FolderRecord folder;
    folder.name = name;
    folder.section = section;
    // In the order they are made, as the sidebar makes them.
    folder.position = next_folder_position_++;
    const std::string id = model()->AddFolder(std::move(folder));
    EXPECT_TRUE(aerie_tab_model::SetFolderRuleKind(*model(), id,
                                                   FolderKind::kAuto));
    EXPECT_FALSE(aerie_tab_model::AddRule(*model(), id, type, value).empty());
    return id;
  }

  int next_folder_position_ = 0;

  std::string OpenTabAt(const GURL& url) {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    const std::string id = service()->adapter()->RecordIdForTab(
        browser()->tab_strip_model()->GetActiveTab());
    return id;
  }

  std::string ParentOf(const std::string& tab_id) {
    const aerie_tab_model::TabRecord* tab = model()->GetTab(tab_id);
    return tab ? tab->parent : std::string("<gone>");
  }

  // True once the tab is in `folder_id`.
  bool WaitFiledInto(const std::string& tab_id, const std::string& folder_id) {
    return base::test::RunUntil(
        [&] { return ParentOf(tab_id) == folder_id; });
  }
  // The tab stays where it is for longer than the pause and the window's first
  // moments.
  void ExpectNotFiled(const std::string& tab_id) {
    WaitMs(700);
    EXPECT_EQ(ParentOf(tab_id), "");
  }
};

IN_PROC_BROWSER_TEST_F(AerieSidebarAutoFileBrowserTest,
                       ANewMatchingTabIsFiledAndTheToastOffersUndo) {
  const std::string folder =
      MakeAutoFolder("Trips", RuleType::kDomain, "trips.localhost");
  const std::string tab = OpenTabAt(UrlOn("trips.localhost"));
  ASSERT_TRUE(WaitFiledInto(tab, folder));
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->toast_visible(); }));
  EXPECT_EQ(base::UTF16ToUTF8(sidebar()->toast()->GetText()),
            "Filed into Trips");
  EXPECT_EQ(DataOf(sidebar()->toast()).role, ax::mojom::Role::kStatus);
  ASSERT_TRUE(sidebar()->toast_undo_button()->GetVisible());
  ASSERT_TRUE(sidebar()->toast_dont_file_button()->GetVisible());
  // The folder row pulses.
  AerieSidebarFolderRowView* row = sidebar()->FolderRowForId(folder);
  ASSERT_TRUE(row);
  ASSERT_TRUE(row->pulse_view()->layer());
  // Undo returns the tab to Today.
  Click(sidebar()->toast_undo_button());
  EXPECT_EQ(ParentOf(tab), "");
  EXPECT_EQ(model()->GetTab(tab)->section, Section::kToday);
  EXPECT_FALSE(model()->GetTab(tab)->filed_by.has_value());
}

// Ctrl+T, then typing an address, is the commonest way to open a tab: the New
// Tab page that the tab shows first must not use up the one chance to file it.
IN_PROC_BROWSER_TEST_F(AerieSidebarAutoFileBrowserTest,
                       ATabOpenedWithCtrlTAndThenNavigatedIsFiled) {
  const std::string folder =
      MakeAutoFolder("Trips", RuleType::kDomain, "trips.localhost");
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL("chrome://newtab/"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  const std::string tab = service()->adapter()->RecordIdForTab(
      browser()->tab_strip_model()->GetActiveTab());
  ASSERT_FALSE(tab.empty());
  EXPECT_EQ(ParentOf(tab), "");
  // The user types an address into the New Tab page's tab.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), UrlOn("trips.localhost")));
  ASSERT_TRUE(WaitFiledInto(tab, folder));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarAutoFileBrowserTest,
                       ATabThatStaysOnTheNewTabPageIsNotFiled) {
  MakeAutoFolder("Trips", RuleType::kDomain, "trips.localhost");
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL("chrome://newtab/"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  const std::string tab = service()->adapter()->RecordIdForTab(
      browser()->tab_strip_model()->GetActiveTab());
  ASSERT_FALSE(tab.empty());
  ExpectNotFiled(tab);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarAutoFileBrowserTest,
                       DontFileThisSiteStopsFilingThatHost) {
  const std::string folder =
      MakeAutoFolder("Trips", RuleType::kDomain, "trips.localhost");
  const std::string first = OpenTabAt(UrlOn("trips.localhost"));
  ASSERT_TRUE(WaitFiledInto(first, folder));
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->toast_visible(); }));
  Click(sidebar()->toast_dont_file_button());
  const auto rules = aerie_tab_model::ReadFolderRules(*model()->GetFolder(folder));
  ASSERT_EQ(rules.excluded_hosts.size(), 1u);
  EXPECT_EQ(rules.excluded_hosts[0], "trips.localhost");
  const std::string second = OpenTabAt(UrlOn("trips.localhost"));
  ExpectNotFiled(second);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarAutoFileBrowserTest,
                       ALaterNavigationOrAHandMovedTabIsNotFiled) {
  const std::string folder =
      MakeAutoFolder("Trips", RuleType::kDomain, "trips.localhost");
  // A tab whose first page does not match stays out, whatever it opens next.
  const std::string tab = OpenTabAt(UrlOn("other.localhost"));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), UrlOn("trips.localhost")));
  ExpectNotFiled(tab);
  // A tab that was filed and then moved out by hand is not filed again.
  const std::string second = OpenTabAt(UrlOn("trips.localhost"));
  ASSERT_TRUE(WaitFiledInto(second, folder));
  ASSERT_TRUE(aerie_tab_model::MoveTabToFolder(*model(), second, ""));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), UrlOn("trips.localhost")));
  ExpectNotFiled(second);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarAutoFileBrowserTest,
                       TheFirstFolderWinsAndPinnedBeatsToday) {
  const std::string first =
      MakeAutoFolder("First", RuleType::kDomain, "trips.localhost");
  const std::string second =
      MakeAutoFolder("Second", RuleType::kUrlContains, "title1");
  const std::string tab = OpenTabAt(UrlOn("trips.localhost"));
  ASSERT_TRUE(WaitFiledInto(tab, first));
  // A matching Pinned folder beats both.
  const std::string pinned = MakeAutoFolder("Pinned one", RuleType::kDomain,
                                            "trips.localhost", Section::kPinned);
  const std::string next = OpenTabAt(UrlOn("trips.localhost"));
  ASSERT_TRUE(WaitFiledInto(next, pinned));
  EXPECT_EQ(model()->GetTab(next)->section, Section::kPinned);
  EXPECT_NE(ParentOf(next), second);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarAutoFileBrowserTest,
                       TheThreeSwitchesEachStopFiling) {
  const std::string folder =
      MakeAutoFolder("Trips", RuleType::kDomain, "trips.localhost");
  const std::string rule_id =
      aerie_tab_model::ReadFolderRules(*model()->GetFolder(folder)).rules[0].id;

  ASSERT_TRUE(aerie_tab_model::SetRuleEnabled(*model(), folder, rule_id, false));
  ExpectNotFiled(OpenTabAt(UrlOn("trips.localhost")));
  ASSERT_TRUE(aerie_tab_model::SetRuleEnabled(*model(), folder, rule_id, true));

  ASSERT_TRUE(aerie_tab_model::SetFolderRulesEnabled(*model(), folder, false));
  ExpectNotFiled(OpenTabAt(UrlOn("trips.localhost")));
  ASSERT_TRUE(aerie_tab_model::SetFolderRulesEnabled(*model(), folder, true));

  prefs()->SetBoolean(aerie_tab_model::kFolderRulesEnabled, false);
  ExpectNotFiled(OpenTabAt(UrlOn("trips.localhost")));
  prefs()->SetBoolean(aerie_tab_model::kFolderRulesEnabled, true);

  // With all three on again, it files.
  const std::string tab = OpenTabAt(UrlOn("trips.localhost"));
  EXPECT_TRUE(WaitFiledInto(tab, folder));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarAutoFileBrowserTest,
                       StopThisRuleMatchingThisTabAddsAnExceptionAndMovesTheTabOut) {
  const std::string folder =
      MakeAutoFolder("Trips", RuleType::kDomain, "trips.localhost");
  const std::string tab = OpenTabAt(UrlOn("trips.localhost"));
  ASSERT_TRUE(WaitFiledInto(tab, folder));
  sidebar()->ContextCommandForTesting(
      tab, AerieSidebarContextMenu::kCommandStopRuleMatching);
  EXPECT_EQ(ParentOf(tab), "");
  EXPECT_EQ(aerie_tab_model::ReadFolderRules(*model()->GetFolder(folder))
                .excluded_tabs.size(),
            1u);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarAutoFileBrowserTest,
                       TheFolderRowNamesItsKindAndShowsTheFunnel) {
  const std::string manual = [&] {
    aerie_tab_model::FolderRecord folder;
    folder.name = "Plain";
    folder.section = Section::kToday;
    return model()->AddFolder(std::move(folder));
  }();
  const std::string folder =
      MakeAutoFolder("Trips", RuleType::kDomain, "trips.localhost");
  ASSERT_TRUE(base::test::RunUntil([&] {
    return sidebar()->RowForId(folder) && sidebar()->RowForId(manual);
  }));
  AerieSidebarFolderRowView* row = sidebar()->FolderRowForId(folder);
  EXPECT_NE(DataOf(row).GetStringAttribute(ax::mojom::StringAttribute::kName)
                .find("Trips, auto-filing folder"),
            std::string::npos);
  EXPECT_TRUE(row->funnel_button()->GetVisible());
  EXPECT_FALSE(sidebar()->FolderRowForId(manual)->funnel_button()->GetVisible());
  EXPECT_EQ(DataOf(sidebar()->RowForId(manual))
                .GetStringAttribute(ax::mojom::StringAttribute::kName)
                .find("auto-filing"),
            std::string::npos);
}

class AerieSidebarFolderSheetBrowserTest
    : public AerieSidebarAutoFileBrowserTest {
 protected:
  AerieSidebarFolderSheet* OpenSheet(const std::string& folder) {
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return sidebar()->RowForId(folder) != nullptr; }));
    sidebar()->OpenFolderSheet(folder);
    AerieSidebarFolderSheet* sheet = sidebar()->folder_sheet();
    EXPECT_TRUE(sheet);
    return sheet;
  }
  void CloseSheetAndWait() {
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return sidebar()->folder_sheet() == nullptr; }));
  }
  static void PressKey(views::Widget* widget, ui::KeyboardCode code) {
    ui::KeyEvent event(ui::EventType::kKeyPressed, code, ui::EF_NONE);
    widget->GetFocusManager()->OnKeyEvent(event);
  }
};

IN_PROC_BROWSER_TEST_F(AerieSidebarFolderSheetBrowserTest,
                       TheSheetOpensFromTheThreeEntryPoints) {
  const std::string folder =
      MakeAutoFolder("Trips", RuleType::kDomain, "trips.localhost");
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->RowForId(folder) != nullptr; }));
  AerieSidebarFolderRowView* row = sidebar()->FolderRowForId(folder);

  // The funnel.
  Click(row->funnel_button());
  ASSERT_TRUE(sidebar()->folder_sheet());
  sidebar()->folder_sheet()->Dismiss(false);
  CloseSheetAndWait();

  // The menu item "Edit folder...".
  sidebar()->FolderContextCommandForTesting(
      folder, AerieSidebarContextMenu::kCommandEditFolder);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->folder_sheet() != nullptr; }));
  sidebar()->folder_sheet()->Dismiss(false);
  CloseSheetAndWait();

  // A double-click on the folder icon.
  gfx::Point center = row->favicon_view()->GetLocalBounds().CenterPoint();
  views::View::ConvertPointToTarget(row->favicon_view(), row, &center);
  ui::MouseEvent press(ui::EventType::kMousePressed, center, center,
                       base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
                       ui::EF_LEFT_MOUSE_BUTTON);
  press.SetClickCount(2);
  row->OnMousePressed(press);
  EXPECT_TRUE(sidebar()->folder_sheet());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarFolderSheetBrowserTest,
                       EditsApplyLiveAndTheSwitchesWork) {
  aerie_tab_model::FolderRecord record;
  record.name = "Trips";
  record.section = Section::kToday;
  const std::string folder = model()->AddFolder(std::move(record));
  AerieSidebarFolderSheet* sheet = OpenSheet(folder);

  // Kind: Auto-filing.
  EXPECT_TRUE(sheet->kind_checked(0));
  Click(sheet->kind_button(1));
  EXPECT_TRUE(aerie_tab_model::IsAutoFiling(*model()->GetFolder(folder)));
  sheet->FlushForTesting();
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->folder_sheet(); }));

  // A rule is added with the type and the text.
  sheet->type_combobox()->SetSelectedIndex(0);
  sheet->value_field()->SetText(u"trips.localhost");
  Click(sheet->add_button());
  auto rules = aerie_tab_model::ReadFolderRules(*model()->GetFolder(folder));
  ASSERT_EQ(rules.rules.size(), 1u);
  EXPECT_EQ(rules.rules[0].value, "trips.localhost");
  sheet->FlushForTesting();
  ASSERT_EQ(sheet->rules_list()->children().size(), 1u);

  // The chip: a list item, a switch and a remove button with a name.
  EXPECT_EQ(DataOf(sheet->rules_list()).role, ax::mojom::Role::kList);
  views::View* chip = sheet->rules_list()->children()[0];
  EXPECT_EQ(DataOf(chip).role, ax::mojom::Role::kListItem);
  auto* chip_switch = FindDescendant<AerieSidebarSwitch>(chip);
  ASSERT_TRUE(chip_switch);
  EXPECT_EQ(DataOf(chip_switch).role, ax::mojom::Role::kSwitch);
  views::Button* remove = nullptr;
  for (views::View* child : chip->children()) {
    if (auto* button = views::AsViewClass<views::ImageButton>(child)) {
      remove = button;
    }
  }
  ASSERT_TRUE(remove);
  EXPECT_EQ(DataOf(remove).GetStringAttribute(ax::mojom::StringAttribute::kName),
            "Remove rule Domain: trips.localhost");

  // The rule's own switch, the folder's "All" switch and the global switch.
  ui::MouseEvent click(ui::EventType::kMouseReleased, gfx::Point(1, 1),
                       gfx::Point(1, 1), base::TimeTicks::Now(),
                       ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  chip_switch->OnMousePressed(click);
  chip_switch->OnMouseReleased(click);
  EXPECT_FALSE(aerie_tab_model::ReadFolderRules(*model()->GetFolder(folder))
                   .rules[0].enabled);
  sheet->all_switch()->OnMousePressed(click);
  sheet->all_switch()->OnMouseReleased(click);
  EXPECT_FALSE(
      aerie_tab_model::ReadFolderRules(*model()->GetFolder(folder)).enabled);
  sheet->global_switch()->OnMousePressed(click);
  sheet->global_switch()->OnMouseReleased(click);
  EXPECT_FALSE(aerie_tab_model::AreFolderRulesEnabled(*prefs()));
  // The same preference as Settings: set from elsewhere, the switch follows.
  prefs()->SetBoolean(aerie_tab_model::kFolderRulesEnabled, true);
  sheet->FlushForTesting();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sheet->global_switch()->on(); }));

  // Remove the rule.
  Click(remove);
  EXPECT_TRUE(aerie_tab_model::ReadFolderRules(*model()->GetFolder(folder))
                  .rules.empty());

  // The name applies live.
  sheet->name_field()->SetText(u"Holiday");
  sheet->ContentsChanged(sheet->name_field(), u"Holiday");
  EXPECT_EQ(model()->GetFolder(folder)->name, "Holiday");
}

IN_PROC_BROWSER_TEST_F(AerieSidebarFolderSheetBrowserTest,
                       TheSheetIsADialogTrapsFocusAndClosesOnEscAndDone) {
  const std::string folder =
      MakeAutoFolder("Trips", RuleType::kDomain, "trips.localhost");
  AerieSidebarFolderSheet* sheet = OpenSheet(folder);
  views::Widget* widget = sheet->GetWidget();
  EXPECT_EQ(sheet->GetAccessibleWindowRole(), ax::mojom::Role::kDialog);
  EXPECT_EQ(sheet->GetAccessibleWindowTitle(), u"Edit folder Trips");

  // Tab never leaves the sheet: it comes back to the first field after one lap.
  views::FocusManager* focus = widget->GetFocusManager();
  ASSERT_TRUE(focus);
  views::View* first = focus->GetFocusedView();
  ASSERT_TRUE(first);
  bool returned = false;
  for (int i = 0; i < 60 && !returned; ++i) {
    focus->AdvanceFocus(false);
    views::View* now = focus->GetFocusedView();
    ASSERT_TRUE(now);
    ASSERT_EQ(now->GetWidget(), widget) << "focus left the sheet";
    returned = (now == first);
  }
  EXPECT_TRUE(returned);

  // Esc closes it, and focus goes back to the folder row.
  sheet->done_button()->RequestFocus();
  PressKey(widget, ui::VKEY_ESCAPE);
  CloseSheetAndWait();
  AerieSidebarFolderRowView* row = sidebar()->FolderRowForId(folder);
  ASSERT_TRUE(row);
  // The browser window may not be the active window in this environment; then
  // the focus manager keeps the view to focus when it is.
  EXPECT_TRUE(base::test::RunUntil([&] {
    views::FocusManager* manager = row->GetFocusManager();
    return row->HasFocus() || (manager && manager->GetStoredFocusView() == row);
  }));

  // Done closes it too.
  sheet = OpenSheet(folder);
  Click(sheet->done_button());
  CloseSheetAndWait();
}

IN_PROC_BROWSER_TEST_F(AerieSidebarFolderSheetBrowserTest,
                       TheAccessibilityTreeNamesTheSheetTheChipsAndTheToast) {
  const std::string folder =
      MakeAutoFolder("Trips", RuleType::kDomain, "trips.localhost");
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->RowForId(folder) != nullptr; }));
  // The row: "<name>, auto-filing folder, ...".
  EXPECT_EQ(DataOf(sidebar()->RowForId(folder))
                .GetStringAttribute(ax::mojom::StringAttribute::kName)
                .rfind("Trips, auto-filing folder", 0),
            0u);

  AerieSidebarFolderSheet* sheet = OpenSheet(folder);
  EXPECT_EQ(sheet->GetAccessibleWindowRole(), ax::mojom::Role::kDialog);
  EXPECT_EQ(sheet->GetAccessibleWindowTitle(), u"Edit folder Trips");
  sheet->FlushForTesting();

  // The three switches: a role, a name and a state each.
  for (AerieSidebarSwitch* sw : {sheet->all_switch(), sheet->global_switch()}) {
    const ui::AXNodeData data = DataOf(sw);
    EXPECT_EQ(data.role, ax::mojom::Role::kSwitch);
    EXPECT_FALSE(data.GetStringAttribute(ax::mojom::StringAttribute::kName).empty());
    EXPECT_EQ(data.GetCheckedState(), ax::mojom::CheckedState::kTrue);
  }
  // The chips are the items of a named list, each with a switch and a remove
  // button that says which rule it removes.
  EXPECT_EQ(DataOf(sheet->rules_list()).role, ax::mojom::Role::kList);
  EXPECT_FALSE(DataOf(sheet->rules_list())
                   .GetStringAttribute(ax::mojom::StringAttribute::kName)
                   .empty());
  ASSERT_EQ(sheet->rules_list()->children().size(), 1u);
  views::View* chip = sheet->rules_list()->children()[0];
  EXPECT_EQ(DataOf(chip).role, ax::mojom::Role::kListItem);
  auto* chip_switch = FindDescendant<AerieSidebarSwitch>(chip);
  ASSERT_TRUE(chip_switch);
  EXPECT_EQ(DataOf(chip_switch).role, ax::mojom::Role::kSwitch);
  EXPECT_EQ(DataOf(chip_switch).GetCheckedState(),
            ax::mojom::CheckedState::kTrue);
  bool found_remove = false;
  for (views::View* child : chip->children()) {
    if (auto* button = views::AsViewClass<views::ImageButton>(child)) {
      EXPECT_EQ(
          DataOf(button).GetStringAttribute(ax::mojom::StringAttribute::kName),
          "Remove rule Domain: trips.localhost");
      found_remove = true;
    }
  }
  EXPECT_TRUE(found_remove);
  sheet->Dismiss(false);
  CloseSheetAndWait();

  // The toast is a status (a polite live region), not an alert.
  const std::string tab = OpenTabAt(UrlOn("trips.localhost"));
  ASSERT_TRUE(WaitFiledInto(tab, folder));
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->toast_visible(); }));
  EXPECT_EQ(DataOf(sidebar()->toast()).role, ax::mojom::Role::kStatus);
  EXPECT_NE(DataOf(sidebar()->toast()).role, ax::mojom::Role::kAlert);
}

// The row is in the Sidebar section of Settings (patch 0038) and follows the
// pref. Settings nests shadow roots, so the script walks them.
IN_PROC_BROWSER_TEST_F(AerieSidebarFolderSheetBrowserTest,
                       SettingsShowsTheFolderRulesRowAndItFollowsThePref) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("chrome://settings/appearance")));
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  const std::string script = R"JS(
    (async (expectChecked) => {
      const find = (root, selector) => {
        const hit = root.querySelector(selector);
        if (hit) return hit;
        for (const el of root.querySelectorAll('*')) {
          if (el.shadowRoot) {
            const found = find(el.shadowRoot, selector);
            if (found) return found;
          }
        }
        return null;
      };
      for (let i = 0; i < 100; ++i) {
        const row = find(document, 'settings-aerie-folder-rules-row');
        const toggle = row && row.shadowRoot &&
            find(row.shadowRoot, 'settings-toggle-button');
        if (toggle && !!toggle.checked === expectChecked) {
          return {label: toggle.label, checked: !!toggle.checked,
                  visible: toggle.getBoundingClientRect().height > 0};
        }
        await new Promise(r => setTimeout(r, 100));
      }
      return null;
    })
  )JS";
  EXPECT_TRUE(prefs()->GetBoolean(aerie_tab_model::kFolderRulesEnabled));
  auto on = content::EvalJs(contents, "(" + script + ")(true)");
  ASSERT_TRUE(on.is_ok()) << on.ExtractError();
  const base::DictValue& on_dict = on.ExtractDict();
  EXPECT_EQ(*on_dict.FindString("label"), "Folder rules everywhere");
  EXPECT_TRUE(*on_dict.FindBool("visible"));
  prefs()->SetBoolean(aerie_tab_model::kFolderRulesEnabled, false);
  auto off = content::EvalJs(contents, "(" + script + ")(false)");
  ASSERT_TRUE(off.is_ok()) << off.ExtractError();
  EXPECT_FALSE(*off.ExtractDict().FindBool("checked"));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarFolderSheetBrowserTest,
                       TheSheetKeepsEmptySlotsForSmartAndBehaviour) {
  const std::string folder =
      MakeAutoFolder("Trips", RuleType::kDomain, "trips.localhost");
  AerieSidebarFolderSheet* sheet = OpenSheet(folder);
  ASSERT_TRUE(sheet->smart_body_slot());
  ASSERT_TRUE(sheet->behaviour_slot());
  EXPECT_TRUE(sheet->smart_body_slot()->children().empty());
  EXPECT_TRUE(sheet->behaviour_slot()->children().empty());
  EXPECT_FALSE(sheet->smart_body_slot()->GetVisible());
  EXPECT_FALSE(sheet->behaviour_slot()->GetVisible());
  sheet->Dismiss(false);
  CloseSheetAndWait();
}

IN_PROC_BROWSER_TEST_F(AerieSidebarFolderSheetBrowserTest,
                       TheRulesSurviveARestartOfTheModel) {
  const std::string folder =
      MakeAutoFolder("Trips", RuleType::kTitleContains, "Café");
  ASSERT_TRUE(aerie_tab_model::AddExcludedHost(*model(), folder, "x.test"));
  // The rules are in the folder's own record, which the model saves; reading it
  // back through the dictionary is what a restart does.
  const aerie_tab_model::FolderRules rules =
      aerie_tab_model::ReadFolderRules(*model()->GetFolder(folder));
  const base::DictValue value = aerie_tab_model::FolderRulesToValue(rules);
  const aerie_tab_model::FolderRules again = aerie_tab_model::ReadFolderRules(
      [&] {
        aerie_tab_model::FolderRecord record;
        record.kind = FolderKind::kAuto;
        record.rules = value.Clone();
        return record;
      }());
  EXPECT_EQ(again.rules.size(), 1u);
  EXPECT_EQ(again.rules[0].value, "Café");
  EXPECT_EQ(again.excluded_hosts.size(), 1u);
}
