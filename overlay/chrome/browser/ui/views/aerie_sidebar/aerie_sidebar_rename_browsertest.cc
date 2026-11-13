// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of tab rename (issue #145): the four ways to start
// (double-click, F2, the context menu), on a Today row, a Pinned row and a
// Favorites tile; Enter, Esc, an empty name and the page title typed back;
// focus loss and the 200-character limit; the name across navigation and a
// restart; the page's title, its history entry and the window title unchanged;
// the pencil and its tooltip; the two menu items; and what a screen reader
// hears. PRE_ tests are the first run of a pair; the next run is the restart.

#include <memory>
#include <optional>
#include <string>
#include <tuple>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "chrome/browser/aerie_tab_model/pinned_tabs_controller.h"
#include "chrome/browser/aerie_tab_model/sleep_controller.h"
#include "chrome/browser/aerie_tab_model/sleep_controller_factory.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_context_menu.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_rename_field.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_rename_popover.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_tile_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_tab_model/pinned_tabs.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/title_resolver.h"
#include "components/history/core/browser/history_service.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace {

using aerie_tab_model::Section;
using M = AerieSidebarContextMenu;

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

std::string DescriptionOf(views::View* view) {
  return DataOf(view).GetStringAttribute(
      ax::mojom::StringAttribute::kDescription);
}

// Serves /title1.html and /title2.html with titles of their own: the test
// server has no document root here, so the pages would be 404s titled with the
// host name.
std::unique_ptr<net::test_server::HttpResponse> ServeTitles(
    const net::test_server::HttpRequest& request) {
  std::string title;
  if (request.relative_url == "/title1.html") {
    title = "Rename test page one";
  } else if (request.relative_url == "/title2.html") {
    title = "Rename test page two";
  } else {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  response->set_content("<!doctype html><title>" + title + "</title><p>" +
                        title + "</p>");
  return response;
}

// The ways to start a rename.
enum class Way { kDoubleClick, kF2, kMenu };

const char* WayName(Way way) {
  switch (way) {
    case Way::kDoubleClick:
      return "DoubleClick";
    case Way::kF2:
      return "F2";
    case Way::kMenu:
      return "Menu";
  }
}

const char* SectionName(Section section) {
  switch (section) {
    case Section::kToday:
      return "Today";
    case Section::kPinned:
      return "Pinned";
    case Section::kFavorite:
      return "Favorite";
  }
}

}  // namespace

class AerieSidebarRenameBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&ServeTitles));
    ASSERT_TRUE(embedded_test_server()->Start());
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
  TabStripModel* strip() { return browser()->tab_strip_model(); }
  aerie_tab_model::TabModelService* service() {
    return aerie_tab_model::TabModelServiceFactory::GetForProfile(
        browser()->GetProfile());
  }
  aerie_tab_model::TabModel* model() { return service()->model(); }
  aerie_tab_model::PinnedTabsController* pinned() {
    return service()->pinned_tabs();
  }
  GURL Url(const std::string& path) {
    return embedded_test_server()->GetURL(path);
  }
  std::string IdOf(tabs::TabInterface* tab) {
    return service()->adapter()->RecordIdForTab(tab);
  }

  void WaitForRowsSynced() {
    EXPECT_TRUE(base::test::RunUntil([&] {
      return sidebar()->row_count() ==
             aerie_tab_model::SectionTabIds(*model(), Section::kToday).size();
    }));
  }
  tabs::TabInterface* OpenTab(const std::string& path) {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), Url(path), WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    WaitForRowsSynced();
    return strip()->GetTabAtIndex(strip()->active_index());
  }

  // A tab in `section`, with the view that shows it.
  struct Target {
    tabs::TabInterface* tab = nullptr;
    std::string id;
    AerieSidebarRowView* row = nullptr;
    AerieSidebarTileView* tile = nullptr;
  };
  Target MakeTarget(Section section, const std::string& path = "/title1.html") {
    Target target;
    target.tab = OpenTab(path);
    target.id = IdOf(target.tab);
    if (section == Section::kPinned) {
      EXPECT_TRUE(pinned()->Pin(target.tab));
      EXPECT_TRUE(base::test::RunUntil(
          [&] { return sidebar()->pinned_row_count() == 1; }));
      target.row = sidebar()->pinned_row_at(0);
    } else if (section == Section::kFavorite) {
      EXPECT_EQ(pinned()->AddToFavorites(target.tab),
                aerie_tab_model::FavoriteResult::kAdded);
      EXPECT_TRUE(base::test::RunUntil(
          [&] { return sidebar()->favorite_tile_count() == 1; }));
      target.tile = sidebar()->tile_at(0);
    } else {
      for (size_t i = 0; i < sidebar()->row_count(); ++i) {
        if (sidebar()->tab_id_at(i) == target.id) {
          target.row = sidebar()->row_at(i);
        }
      }
    }
    EXPECT_TRUE(target.row || target.tile);
    if (target.row) {
      LayOut(target.row);
    }
    return target;
  }

  // The headless window has no real size: give the row one and lay it out, so
  // that a click on its title has a title to hit.
  void LayOut(AerieSidebarRowView* row) {
    row->SetSize(gfx::Size(260, row->GetPreferredSize().height()));
    row->DeprecatedLayoutImmediately();
  }

  std::u16string PageTitle(const Target& target) {
    return target.tab->GetTitle();
  }
  std::optional<std::string> ManualName(const Target& target) {
    return model()->GetTab(target.id)->manual_name;
  }

  void KeyDown(views::View* view,
               ui::KeyboardCode key,
               int flags = ui::EF_NONE) {
    ui::KeyEvent event(ui::EventType::kKeyPressed, key, flags);
    view->OnKeyPressed(event);
  }

  void DoubleClick(views::View* view, const gfx::Point& point) {
    ui::MouseEvent event(ui::EventType::kMousePressed, point, point,
                         base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
                         ui::EF_LEFT_MOUSE_BUTTON);
    event.SetClickCount(2);
    view->OnMousePressed(event);
    ui::MouseEvent release(ui::EventType::kMouseReleased, point, point,
                           base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
                           ui::EF_LEFT_MOUSE_BUTTON);
    view->OnMouseReleased(release);
  }

  // Starts a rename with `way` and waits for the editor.
  void Start(const Target& target, Way way) {
    switch (way) {
      case Way::kDoubleClick:
        if (target.row) {
          views::Label* title =
              FindDescendant<views::Label>(target.row->content());
          ASSERT_TRUE(title);
          DoubleClick(target.row, title->bounds().CenterPoint());
        } else {
          DoubleClick(target.tile, gfx::Point(5, 5));
        }
        break;
      case Way::kF2:
        KeyDown(target.row ? static_cast<views::View*>(target.row)
                           : static_cast<views::View*>(target.tile),
                ui::VKEY_F2);
        break;
      case Way::kMenu: {
        views::View* view = target.row ? static_cast<views::View*>(target.row)
                                       : static_cast<views::View*>(target.tile);
        view->ShowContextMenu(gfx::Point(5, 5),
                              ui::mojom::MenuSourceType::kMouse);
        ASSERT_TRUE(sidebar()->context_menu());
        views::MenuItemView* item =
            sidebar()->context_menu()->item(M::Command::kRename);
        ASSERT_TRUE(item);
        EXPECT_TRUE(item->GetEnabled());
        EXPECT_EQ(base::UTF16ToUTF8(item->title()),
                  l10n_util::GetStringUTF8(IDS_AERIE_SIDEBAR_RENAME_TAB));
        sidebar()->context_menu()->ExecuteCommand(M::Command::kRename);
        sidebar()->context_menu()->Close();
        break;
      }
    }
    ASSERT_TRUE(
        base::test::RunUntil([&] { return Editor(target) != nullptr; }));
  }

  // The open editor of the target, or null.
  AerieSidebarRenameField* Editor(const Target& target) {
    if (target.row) {
      return target.row->renaming() ? target.row->rename_field() : nullptr;
    }
    return sidebar()->rename_popover() ? sidebar()->rename_popover()->field()
                                       : nullptr;
  }

  // Types like the user: through the field's edit path, so that the field's
  // controller sees the change (SetText does not tell it).
  void Type(AerieSidebarRenameField* field, const std::u16string& text) {
    field->SelectAll(false);
    field->InsertOrReplaceText(text);
  }
  void Enter(AerieSidebarRenameField* field) {
    KeyDown(field, ui::VKEY_RETURN);
  }
  void Esc(AerieSidebarRenameField* field) { KeyDown(field, ui::VKEY_ESCAPE); }

  // Waits for the editor to be gone.
  void WaitClosed(const Target& target) {
    ASSERT_TRUE(
        base::test::RunUntil([&] { return Editor(target) == nullptr; }));
    // The commit of a rename runs in a task after the editor is gone.
    base::RunLoop().RunUntilIdle();
  }

  // The shown title of the target's view.
  // What a screen reader hears for the row or tile.
  std::u16string Shown(const Target& target) {
    return base::UTF8ToUTF16(
        NameOf(target.row ? static_cast<views::View*>(target.row)
                          : static_cast<views::View*>(target.tile)));
  }

  void Click(views::Button* button) {
    views::test::ButtonTestApi(button).NotifyClick(ui::MouseEvent(
        ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
        base::TimeTicks(), ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON));
  }

  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

// --- Start: three ways, three kinds of view -------------------------------

class AerieSidebarRenameStartBrowserTest
    : public AerieSidebarRenameBrowserTest,
      public testing::WithParamInterface<std::tuple<Section, Way>> {};

INSTANTIATE_TEST_SUITE_P(
    AerieRenameStart,
    AerieSidebarRenameStartBrowserTest,
    testing::Combine(
        testing::Values(Section::kToday, Section::kPinned, Section::kFavorite),
        testing::Values(Way::kDoubleClick, Way::kF2, Way::kMenu)),
    [](const testing::TestParamInfo<std::tuple<Section, Way>>& info) {
      return std::string(SectionName(std::get<0>(info.param))) +
             WayName(std::get<1>(info.param));
    });

// Acceptance: double-click, F2 and the menu start a rename on any tab in
// Today, Pinned and a Favorites tile. The editor opens with the page title,
// all of it selected.
IN_PROC_BROWSER_TEST_P(AerieSidebarRenameStartBrowserTest, StartsARename) {
  const auto [section, way] = GetParam();
  Target target = MakeTarget(section);
  EXPECT_EQ(Editor(target), nullptr);
  Start(target, way);
  AerieSidebarRenameField* field = Editor(target);
  ASSERT_TRUE(field);
  EXPECT_EQ(field->GetText(), PageTitle(target));
  EXPECT_EQ(field->GetSelectedText(), PageTitle(target));
  // Nothing was written by starting.
  EXPECT_EQ(ManualName(target), std::nullopt);
}

// --- The four endings, per kind of view -----------------------------------

class AerieSidebarRenameEndBrowserTest
    : public AerieSidebarRenameBrowserTest,
      public testing::WithParamInterface<Section> {};

INSTANTIATE_TEST_SUITE_P(AerieRenameEnd,
                         AerieSidebarRenameEndBrowserTest,
                         testing::Values(Section::kToday,
                                         Section::kPinned,
                                         Section::kFavorite),
                         [](const testing::TestParamInfo<Section>& info) {
                           return std::string(SectionName(info.param));
                         });

// Acceptance: Enter keeps the name.
IN_PROC_BROWSER_TEST_P(AerieSidebarRenameEndBrowserTest, EnterKeepsTheName) {
  Target target = MakeTarget(GetParam());
  Start(target, Way::kF2);
  Type(Editor(target), u"  My inbox ");
  Enter(Editor(target));
  WaitClosed(target);
  EXPECT_EQ(ManualName(target), "My inbox");
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return Shown(target) == u"My inbox, renamed"; }));
}

// Acceptance: Esc cancels.
IN_PROC_BROWSER_TEST_P(AerieSidebarRenameEndBrowserTest, EscCancels) {
  Target target = MakeTarget(GetParam());
  Start(target, Way::kF2);
  Type(Editor(target), u"Never kept");
  Esc(Editor(target));
  WaitClosed(target);
  EXPECT_EQ(ManualName(target), std::nullopt);
  // Esc on a tab that has a name leaves that name.
  model()->UpdateTab(target.id, [](aerie_tab_model::TabRecord& tab) {
    tab.manual_name = "Old";
  });
  Start(target, Way::kF2);
  Type(Editor(target), u"Other");
  Esc(Editor(target));
  WaitClosed(target);
  EXPECT_EQ(ManualName(target), "Old");
}

// Acceptance: an empty name resets.
IN_PROC_BROWSER_TEST_P(AerieSidebarRenameEndBrowserTest, AnEmptyNameResets) {
  Target target = MakeTarget(GetParam());
  model()->UpdateTab(target.id, [](aerie_tab_model::TabRecord& tab) {
    tab.manual_name = "Old";
  });
  Start(target, Way::kF2);
  EXPECT_EQ(Editor(target)->GetText(), u"Old");
  Type(Editor(target), u"   ");
  Enter(Editor(target));
  WaitClosed(target);
  EXPECT_EQ(ManualName(target), std::nullopt);
}

// Acceptance: typing the page title back resets (case-sensitive).
IN_PROC_BROWSER_TEST_P(AerieSidebarRenameEndBrowserTest,
                       TheRealPageTitleResets) {
  Target target = MakeTarget(GetParam());
  model()->UpdateTab(target.id, [](aerie_tab_model::TabRecord& tab) {
    tab.manual_name = "Old";
  });
  Start(target, Way::kF2);
  Type(Editor(target), PageTitle(target));
  Enter(Editor(target));
  WaitClosed(target);
  EXPECT_EQ(ManualName(target), std::nullopt);

  // A different case is a name of its own.
  Start(target, Way::kF2);
  Type(Editor(target), base::ToLowerASCII(PageTitle(target)));
  Enter(Editor(target));
  WaitClosed(target);
  EXPECT_EQ(ManualName(target),
            base::UTF16ToUTF8(base::ToLowerASCII(PageTitle(target))));
}

// Decision: focus loss keeps the name.
IN_PROC_BROWSER_TEST_P(AerieSidebarRenameEndBrowserTest,
                       FocusLossKeepsTheName) {
  Target target = MakeTarget(GetParam());
  Start(target, Way::kF2);
  Type(Editor(target), u"Kept on blur");
  if (target.row) {
    target.row->rename_field()->OnBlur();
  } else {
    // A click elsewhere deactivates the popover, which closes it.
    sidebar()->rename_popover()->GetWidget()->Close();
  }
  WaitClosed(target);
  EXPECT_EQ(ManualName(target), "Kept on blur");
}

// Decision: the field holds at most 200 characters.
IN_PROC_BROWSER_TEST_P(AerieSidebarRenameEndBrowserTest,
                       TheFieldHoldsTwoHundredCharacters) {
  Target target = MakeTarget(GetParam());
  Start(target, Way::kF2);
  Type(Editor(target), std::u16string(300, u'x'));
  EXPECT_EQ(Editor(target)->GetText().size(),
            aerie_tab_model::kMaxManualNameLength);
  Enter(Editor(target));
  WaitClosed(target);
  EXPECT_EQ(ManualName(target)->size(), 200u);
}

// A paste that ends in a pair of UTF-16 units at the limit never keeps half of
// it: the field holds 199 characters and the saved name has no U+FFFD.
IN_PROC_BROWSER_TEST_P(AerieSidebarRenameEndBrowserTest,
                       APasteNeverLeavesHalfASurrogatePair) {
  Target target = MakeTarget(GetParam());
  Start(target, Way::kF2);
  std::u16string text(199, u'a');
  text += u"\U0001F600tail";
  Type(Editor(target), text);
  EXPECT_EQ(Editor(target)->GetText(), std::u16string(199, u'a'));
  Enter(Editor(target));
  WaitClosed(target);
  ASSERT_TRUE(ManualName(target));
  EXPECT_EQ(*ManualName(target), std::string(199, 'a'));
  EXPECT_EQ(ManualName(target)->find("\xEF\xBF\xBD"), std::string::npos);
}

// Acceptance: Enter or Esc (not a blur) puts focus on the view again; a rename
// of one tab leaves the others alone.
IN_PROC_BROWSER_TEST_P(AerieSidebarRenameEndBrowserTest, OnlyThatTabIsRenamed) {
  Target other = MakeTarget(Section::kToday, "/title2.html");
  Target target = MakeTarget(GetParam());
  Start(target, Way::kF2);
  Type(Editor(target), u"Mine");
  Enter(Editor(target));
  WaitClosed(target);
  EXPECT_EQ(ManualName(target), "Mine");
  EXPECT_EQ(model()->GetTab(other.id)->manual_name, std::nullopt);
}

// --- Persistence and the real title ---------------------------------------

// Acceptance: the name survives navigation in the tab, and the page's
// document.title, its history entry and the window title do not change.
IN_PROC_BROWSER_TEST_F(AerieSidebarRenameBrowserTest,
                       TheNameSurvivesNavigationAndTheRealTitlesStay) {
  Target target = MakeTarget(Section::kPinned, "/title1.html");
  const std::u16string real_title = PageTitle(target);
  ASSERT_FALSE(real_title.empty());
  sidebar()->CommitRename(target.id, u"Renamed");
  ASSERT_EQ(ManualName(target), "Renamed");

  content::WebContents* contents = target.tab->GetContents();
  EXPECT_EQ(content::EvalJs(contents, "document.title").ExtractString(),
            base::UTF16ToUTF8(real_title));
  EXPECT_EQ(contents->GetTitle(), real_title);
  EXPECT_EQ(contents->GetController().GetLastCommittedEntry()->GetTitle(),
            real_title);
  // The window title is the real title (plus the product name).
  strip()->ActivateTabAt(strip()->GetIndexOfTab(target.tab));
  const std::u16string window_title =
      BrowserView::GetBrowserViewForBrowser(browser())->GetWindowTitle();
  EXPECT_NE(window_title.find(real_title), std::u16string::npos)
      << window_title;
  EXPECT_EQ(window_title.find(u"Renamed"), std::u16string::npos);

  // The history entry keeps the real title.
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      browser()->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  std::u16string history_title;
  bool answered = false;
  base::CancelableTaskTracker tracker;
  auto query = [&] {
    answered = false;
    history->QueryURL(
        Url("/title1.html"),
        base::BindLambdaForTesting([&](history::QueryURLResult result) {
          history_title =
              result.success ? result.row.title() : std::u16string();
          answered = true;
        }),
        &tracker);
  };
  query();
  // The history is written after the load; ask again until it has the title.
  EXPECT_TRUE(base::test::RunUntil([&] {
    if (!answered) {
      return false;
    }
    if (!history_title.empty()) {
      return true;
    }
    query();
    return false;
  }));
  EXPECT_EQ(history_title, real_title);

  // Navigation never touches the name, and the row keeps showing it.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("/title2.html")));
  EXPECT_EQ(ManualName(target), "Renamed");
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return Shown(target) == u"Renamed, renamed"; }));
  // The page behind it has its own title.
  EXPECT_NE(target.tab->GetTitle(), u"Renamed");
}

// Acceptance: the name survives a browser restart (Pinned and Favorites come
// back from the Tab Model).
IN_PROC_BROWSER_TEST_F(AerieSidebarRenameBrowserTest,
                       PRE_TheNameSurvivesARestart) {
  Target pinned_target = MakeTarget(Section::kPinned, "/title1.html");
  sidebar()->CommitRename(pinned_target.id, u"Kept pinned");
  Target favorite_target = MakeTarget(Section::kFavorite, "/title2.html");
  sidebar()->CommitRename(favorite_target.id, u"Kept favorite");
  EXPECT_EQ(ManualName(pinned_target), "Kept pinned");
  EXPECT_EQ(ManualName(favorite_target), "Kept favorite");
}

IN_PROC_BROWSER_TEST_F(AerieSidebarRenameBrowserTest, TheNameSurvivesARestart) {
  ASSERT_TRUE(base::test::RunUntil([&] {
    return sidebar()->pinned_row_count() == 1 &&
           sidebar()->favorite_tile_count() == 1;
  }));
  AerieSidebarRowView* row = sidebar()->pinned_row_at(0);
  AerieSidebarTileView* tile = sidebar()->tile_at(0);
  EXPECT_EQ(model()->GetTab(sidebar()->pinned_tab_id_at(0))->manual_name,
            "Kept pinned");
  EXPECT_EQ(model()->GetTab(sidebar()->favorite_tab_id_at(0))->manual_name,
            "Kept favorite");
  EXPECT_TRUE(base::test::RunUntil([&] {
    return row->renamed() && tile->renamed() &&
           NameOf(row) == "Kept pinned, asleep, renamed";
  })) << NameOf(row);
  EXPECT_EQ(tile->title(), u"Kept favorite");
  EXPECT_TRUE(row->pencil_button()->GetVisible());
}

// A tab in a folder keeps a name as well. The sidebar draws no folder rows yet
// (docs/sidebar-design.md), so there is no editor to open: the commit is what
// the rename of such a tab will call.
IN_PROC_BROWSER_TEST_F(AerieSidebarRenameBrowserTest, ATabInAFolderCanBeNamed) {
  tabs::TabInterface* tab = OpenTab("/title1.html");
  const std::string id = IdOf(tab);
  aerie_tab_model::FolderRecord folder;
  folder.name = "Work";
  const std::string folder_id = model()->AddFolder(std::move(folder));
  ASSERT_FALSE(folder_id.empty());
  ASSERT_TRUE(model()->MoveTab(id, Section::kToday, folder_id, 0));
  sidebar()->CommitRename(id, u"In a folder");
  EXPECT_EQ(model()->GetTab(id)->manual_name, "In a folder");
  EXPECT_EQ(model()->GetTab(id)->parent, folder_id);
}

// --- The pencil ------------------------------------------------------------

class AerieSidebarRenamePencilBrowserTest
    : public AerieSidebarRenameBrowserTest,
      public testing::WithParamInterface<Section> {};

INSTANTIATE_TEST_SUITE_P(AerieRenamePencil,
                         AerieSidebarRenamePencilBrowserTest,
                         testing::Values(Section::kToday, Section::kPinned),
                         [](const testing::TestParamInfo<Section>& info) {
                           return std::string(SectionName(info.param));
                         });

// Acceptance: the pencil shows for a renamed tab only, resets on click, and
// its tooltip has the real page title and the order.
IN_PROC_BROWSER_TEST_P(AerieSidebarRenamePencilBrowserTest,
                       ThePencilShowsForARenamedTabAndResetsOnClick) {
  Target target = MakeTarget(GetParam());
  EXPECT_FALSE(target.row->pencil_button()->GetVisible());
  EXPECT_FALSE(target.row->renamed());
  sidebar()->CommitRename(target.id, u"Mine");
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return target.row->pencil_button()->GetVisible(); }));
  EXPECT_TRUE(target.row->renamed());
  EXPECT_EQ(NameOf(target.row->pencil_button()),
            l10n_util::GetStringUTF8(IDS_AERIE_SIDEBAR_RESET_NAME));
  const std::u16string tooltip = target.row->pencil_button()->GetTooltipText();
  EXPECT_EQ(tooltip, l10n_util::GetStringFUTF16(
                         IDS_AERIE_SIDEBAR_RENAMED_TOOLTIP, PageTitle(target)) +
                         u"\n" +
                         l10n_util::GetStringUTF16(
                             IDS_AERIE_SIDEBAR_RENAMED_PRECEDENCE));
  EXPECT_NE(tooltip.find(PageTitle(target)), std::u16string::npos);
  EXPECT_NE(tooltip.find(u"Shown: your name."), std::u16string::npos);
  EXPECT_NE(tooltip.find(u"page title"), std::u16string::npos);

  Click(target.row->pencil_button());
  EXPECT_EQ(ManualName(target), std::nullopt);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return !target.row->pencil_button()->GetVisible(); }));
  EXPECT_FALSE(target.row->renamed());
}

// --- The context menu -------------------------------------------------------

// Acceptance: "Reset name" is in the menu only when a name is set; "Rename" is
// always there and both are enabled.
IN_PROC_BROWSER_TEST_F(AerieSidebarRenameBrowserTest,
                       ResetNameIsInTheMenuOnlyWhenANameIsSet) {
  for (Section section :
       {Section::kToday, Section::kPinned, Section::kFavorite}) {
    AerieSidebarContextMenu::Options options;
    options.section = section;
    M without(options, base::DoNothing());
    ASSERT_TRUE(without.item(M::Command::kRename)) << SectionName(section);
    EXPECT_TRUE(without.item(M::Command::kRename)->GetEnabled());
    EXPECT_EQ(without.item(M::Command::kResetName), nullptr);
    EXPECT_FALSE(without.IsCommandEnabled(M::Command::kResetName));
    options.has_manual_name = true;
    M with(options, base::DoNothing());
    ASSERT_TRUE(with.item(M::Command::kResetName)) << SectionName(section);
    EXPECT_TRUE(with.item(M::Command::kResetName)->GetEnabled());
    EXPECT_EQ(base::UTF16ToUTF8(with.item(M::Command::kResetName)->title()),
              l10n_util::GetStringUTF8(IDS_AERIE_SIDEBAR_RESET_NAME));
    EXPECT_TRUE(with.IsCommandEnabled(M::Command::kResetName));
  }
}

// The one menu of a row (not a second menu) offers the items, and "Reset name"
// in it clears the name.
IN_PROC_BROWSER_TEST_F(AerieSidebarRenameBrowserTest, TheRowMenuResetsTheName) {
  Target target = MakeTarget(Section::kToday);
  target.row->ShowContextMenu(gfx::Point(5, 5),
                              ui::mojom::MenuSourceType::kMouse);
  ASSERT_TRUE(sidebar()->context_menu());
  EXPECT_EQ(sidebar()->context_menu()->item(M::Command::kResetName), nullptr);
  EXPECT_TRUE(sidebar()->context_menu()->item(M::Command::kRename));
  sidebar()->context_menu()->Close();

  sidebar()->CommitRename(target.id, u"Mine");
  target.row->ShowContextMenu(gfx::Point(5, 5),
                              ui::mojom::MenuSourceType::kMouse);
  ASSERT_TRUE(sidebar()->context_menu());
  ASSERT_TRUE(sidebar()->context_menu()->item(M::Command::kResetName));
  sidebar()->context_menu()->ExecuteCommand(M::Command::kResetName);
  sidebar()->context_menu()->Close();
  EXPECT_EQ(ManualName(target), std::nullopt);
}

// --- Accessibility ----------------------------------------------------------

// Acceptance: the editor is a textbox named "Tab name" and described by the
// caption; the row name is the shown title, ", asleep", ", renamed".
IN_PROC_BROWSER_TEST_F(AerieSidebarRenameBrowserTest,
                       TheEditorIsATextboxNamedTabNameDescribedByTheCaption) {
  Target target = MakeTarget(Section::kToday);
  Start(target, Way::kF2);
  AerieSidebarRenameField* field = Editor(target);
  ASSERT_TRUE(field);
  EXPECT_EQ(DataOf(field).role, ax::mojom::Role::kTextField);
  EXPECT_EQ(NameOf(field), "Tab name");
  EXPECT_EQ(NameOf(field),
            l10n_util::GetStringUTF8(IDS_AERIE_SIDEBAR_RENAME_FIELD_NAME));
  EXPECT_EQ(DescriptionOf(field),
            l10n_util::GetStringUTF8(IDS_AERIE_SIDEBAR_RENAME_CAPTION));
  EXPECT_TRUE(target.row->rename_caption()->GetVisible());
  EXPECT_EQ(target.row->rename_caption()->GetText(),
            l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_RENAME_CAPTION));
  // The caption is under the field, inside the row, and the row is taller.
  EXPECT_GT(target.row->GetPreferredSize().height(), 34);
  LayOut(target.row);
  views::Label* caption = target.row->rename_caption();
  EXPECT_GT(caption->height(), 0);
  EXPECT_EQ(caption->height(), caption->GetHeightForWidth(caption->width()));
  EXPECT_LE(caption->bounds().bottom(), target.row->height());
  EXPECT_GE(caption->y(), 34);
  // The caption can wrap, and the row grows by what the wrapped text needs.
  EXPECT_TRUE(caption->GetMultiLine());
  Esc(field);
  WaitClosed(target);
  EXPECT_FALSE(target.row->rename_caption()->GetVisible());
  EXPECT_EQ(target.row->GetPreferredSize().height(), 34);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarRenameBrowserTest,
                       TheRowNameEndsWithAsleepThenRenamed) {
  Target target = MakeTarget(Section::kToday);
  OpenTab("/title2.html");
  const std::string page = base::UTF16ToUTF8(PageTitle(target));
  EXPECT_EQ(NameOf(target.row), page);
  sidebar()->CommitRename(target.id, u"Mine");
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return NameOf(target.row) == "Mine, renamed"; }));
  // Put the tab to sleep: ", asleep" comes before ", renamed".
  strip()->ActivateTabAt(strip()->count() - 1);
  ASSERT_TRUE(aerie_tab_model::SleepControllerFactory::GetForProfile(
                  browser()->GetProfile())
                  ->Sleep(target.tab));
  ASSERT_TRUE(base::test::RunUntil([&] { return target.row->sleeping(); }));
  EXPECT_EQ(NameOf(target.row), "Mine, asleep, renamed");
  // The row is a tree item; resetting takes the suffix away.
  EXPECT_EQ(DataOf(target.row).role, ax::mojom::Role::kTreeItem);
  sidebar()->ResetName(target.id);
  ASSERT_TRUE(base::test::RunUntil([&] { return !target.row->renamed(); }));
  EXPECT_EQ(NameOf(target.row), page + ", asleep");
}

// A Favorites tile names the same way.
IN_PROC_BROWSER_TEST_F(AerieSidebarRenameBrowserTest,
                       TheTileNameEndsWithRenamed) {
  Target target = MakeTarget(Section::kFavorite);
  sidebar()->CommitRename(target.id, u"Mine");
  ASSERT_TRUE(base::test::RunUntil([&] { return target.tile->renamed(); }));
  EXPECT_EQ(NameOf(target.tile), "Mine, renamed");
  EXPECT_EQ(DataOf(target.tile).role, ax::mojom::Role::kButton);
  const std::u16string tooltip =
      target.tile->GetRenderedTooltipText(gfx::Point());
  EXPECT_NE(tooltip.find(u"Shown: your name."), std::u16string::npos);
}

// The popover of a tile: a textbox named "Tab name", described by the caption.
IN_PROC_BROWSER_TEST_F(AerieSidebarRenameBrowserTest,
                       ThePopoverOfATileHasTheSameField) {
  Target target = MakeTarget(Section::kFavorite);
  Start(target, Way::kF2);
  AerieSidebarRenameField* field = Editor(target);
  ASSERT_TRUE(field);
  EXPECT_EQ(DataOf(field).role, ax::mojom::Role::kTextField);
  EXPECT_EQ(NameOf(field), "Tab name");
  EXPECT_EQ(DescriptionOf(field),
            l10n_util::GetStringUTF8(IDS_AERIE_SIDEBAR_RENAME_CAPTION));
  EXPECT_EQ(sidebar()->rename_popover()->caption()->GetText(),
            l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_RENAME_CAPTION));
}

// The popover of a tile closes, and keeps nothing, when its tile goes away.
IN_PROC_BROWSER_TEST_F(AerieSidebarRenameBrowserTest,
                       TheTilePopoverClosesWhenItsTabCloses) {
  OpenTab("/title2.html");
  Target target = MakeTarget(Section::kFavorite);
  Start(target, Way::kF2);
  ASSERT_TRUE(sidebar()->rename_popover());
  Type(sidebar()->rename_popover()->field(), u"Never saved");
  strip()->CloseWebContentsAt(strip()->GetIndexOfTab(target.tab),
                              TabCloseTypes::CLOSE_NONE);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->rename_popover() == nullptr; }));
  base::RunLoop().RunUntilIdle();
  const auto* record = model()->GetTab(target.id);
  EXPECT_TRUE(!record || record->manual_name == std::nullopt);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarRenameBrowserTest,
                       TheTilePopoverClosesWhenItsTabLeavesFavorites) {
  Target target = MakeTarget(Section::kFavorite);
  Start(target, Way::kF2);
  ASSERT_TRUE(sidebar()->rename_popover());
  Type(sidebar()->rename_popover()->field(), u"Never saved");
  ASSERT_TRUE(pinned()->RemoveFromFavorites(target.tab));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->rename_popover() == nullptr; }));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(ManualName(target), std::nullopt);
}

// A rename of a tab whose page then goes away does not leave an editor behind.
IN_PROC_BROWSER_TEST_F(AerieSidebarRenameBrowserTest,
                       ClosingTheTabDropsTheEditor) {
  OpenTab("/title2.html");
  Target target = MakeTarget(Section::kToday);
  Start(target, Way::kF2);
  ASSERT_TRUE(Editor(target));
  strip()->CloseWebContentsAt(strip()->GetIndexOfTab(target.tab),
                              TabCloseTypes::CLOSE_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return sidebar()->closing_row_count() == 0 && sidebar()->row_count() == 2;
  }));
}
