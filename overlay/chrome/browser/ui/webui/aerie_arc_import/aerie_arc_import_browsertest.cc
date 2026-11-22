// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of "Import from Arc" (issue #171): the Settings handler, driven
// through its messages and the real file-chooser path (a fake dialog picks the
// file), and the Tab Model and Chromium tabs it leaves behind. The fixtures are
// the hand-written files of components/aerie_tab_model/testdata/arc
// (docs/arc-import.md).

#include "chrome/browser/ui/webui/aerie_arc_import/aerie_arc_import_handler.h"

#include "build/build_config.h"

#if BUILDFLAG(IS_POSIX)
#include <sys/stat.h>
#endif

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/test/bind.h"
#include "base/task/current_thread.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/aerie_tab_model/pinned_tabs_controller.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/aerie_tab_model/arc_import.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/pinned_tabs.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_web_ui.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "url/gurl.h"
#include "ui/shell_dialogs/fake_select_file_dialog.h"

namespace aerie_arc_import {

namespace {

using aerie_tab_model::Section;
using aerie_tab_model::TabModel;
using aerie_tab_model::TabRecord;
using testing::ElementsAre;

// "t:<title>" for a tab, "f:<name>" for a folder, in order.
std::vector<std::string> Children(const TabModel& model,
                                  Section section,
                                  const std::string& folder) {
  std::vector<std::string> out;
  for (const aerie_tab_model::ChildRef& child : aerie_tab_model::OrderedChildren(
           model, section, folder, /*listed_only=*/false,
           [](const std::string&) { return true; })) {
    if (child.is_folder) {
      out.push_back("f:" + model.GetFolder(child.id)->name);
    } else {
      out.push_back("t:" + model.GetTab(child.id)->last_title.value_or("?"));
    }
  }
  return out;
}

std::string FolderIdByName(const TabModel& model, const std::string& name) {
  for (const auto& [id, folder] : model.folders()) {
    if (folder.name == name) {
      return id;
    }
  }
  return std::string();
}

int IntOf(const base::DictValue& dict, std::string_view key) {
  return dict.FindInt(key).value_or(-1);
}

int SkippedCount(const base::DictValue& answer, std::string_view reason) {
  const base::ListValue* skipped = answer.FindList("skipped");
  if (!skipped) {
    return -1;
  }
  for (const base::Value& entry : *skipped) {
    const std::string* name = entry.GetDict().FindString("reason");
    if (name && *name == reason) {
      return entry.GetDict().FindInt("count").value_or(-1);
    }
  }
  return 0;
}

// A file with one Space whose Pinned tabs are "https://example.test/<n>" for n
// in [0, count). The titles are "T<n>".
std::string SpaceWithTabs(int count) {
  std::string ids;
  std::string items;
  for (int i = 0; i < count; ++i) {
    const std::string n = base::NumberToString(i);
    ids += (i ? ",\"t" : "\"t") + n + "\"";
    items += "\"t" + n + "\",{\"id\":\"t" + n +
             "\",\"parentID\":\"p\",\"data\":{\"tab\":{\"savedURL\":"
             "\"https://example.test/" + n + "\",\"savedTitle\":\"T" + n +
             "\"}}},";
  }
  return base::StrCat(
      {R"({"sidebar":{"containers":[{"global":{}},{"items":[)", items,
       R"("p",{"id":"p","childrenIds":[)", ids,
       R"(],"data":{"itemContainer":{}}}],
        "spaces":["s",{"id":"s","title":"S","containerIDs":["pinned","p"]}],
        "topAppsContainerIDs":[]}]}})"});
}

// Records how long each task that runs on this thread takes, from the moment
// the observer is added. The restore of Pinned tabs is a chain of
// posted tasks (PinnedTabsController::RestoreBatch), so the longest of them is
// the longest time the UI thread was blocked by it. A task that was already
// running when the observer was added is not counted (its start is unseen).
class LongestTaskObserver : public base::TaskObserver {
 public:
  LongestTaskObserver() { base::CurrentThread::Get()->AddTaskObserver(this); }
  ~LongestTaskObserver() override {
    base::CurrentThread::Get()->RemoveTaskObserver(this);
  }
  void WillProcessTask(const base::PendingTask&, bool) override {
    starts_.push_back(base::TimeTicks::Now());
  }
  void DidProcessTask(const base::PendingTask&) override {
    if (starts_.empty()) {
      return;
    }
    const base::TimeDelta took = base::TimeTicks::Now() - starts_.back();
    starts_.pop_back();
    ++tasks_;
    longest_ = std::max(longest_, took);
  }
  base::TimeDelta longest() const { return longest_; }
  int tasks() const { return tasks_; }

 private:
  std::vector<base::TimeTicks> starts_;
  base::TimeDelta longest_;
  int tasks_ = 0;
};

}  // namespace

class AerieArcImportBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    factory_ = ui::FakeSelectFileDialog::RegisterFactory();
    factory_->SetOpenCallback(base::DoNothing());
    service_ = aerie_tab_model::TabModelServiceFactory::GetForProfile(
        browser()->GetProfile());
    ASSERT_TRUE(service_);
    ASSERT_TRUE(base::test::RunUntil([&] { return service_->loaded(); }));
    web_ui_ = std::make_unique<content::TestWebUI>();
    web_ui_->set_web_contents(
        browser()->tab_strip_model()->GetActiveWebContents());
    handler_ = std::make_unique<AerieArcImportHandler>(browser()->GetProfile());
    handler_->set_web_ui(web_ui_.get());
    handler_->RegisterMessages();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  }

  void TearDownOnMainThread() override {
    handler_->set_web_ui(nullptr);
    handler_.reset();
    web_ui_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  TabModel& model() { return *service_->model(); }

  base::FilePath Fixture(const std::string& name) {
    base::FilePath root;
    EXPECT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
    return root.AppendASCII("components/aerie_tab_model/testdata/arc")
        .AppendASCII(name);
  }

  // Sends `name` with a callback id and what follows, and waits for the answer.
  base::DictValue Send(const std::string& name,
                       std::vector<base::Value> args = {}) {
    const std::string id = "cb" + std::to_string(++calls_);
    base::ListValue list;
    list.Append(id);
    for (base::Value& arg : args) {
      list.Append(std::move(arg));
    }
    web_ui_->HandleReceivedMessage(name, list);
    return Await(id);
  }

  // How many responses (resolve or reject) the page got for `id`.
  int Responses(const std::string& id) {
    int count = 0;
    for (const auto& call : web_ui_->call_data()) {
      if (call->function_name() == "cr.webUIResponse" && call->arg1() &&
          call->arg1()->is_string() && call->arg1()->GetString() == id) {
        ++count;
      }
    }
    return count;
  }
  bool Answered(const std::string& id) { return Responses(id) > 0; }

  // The text the page got when it rejected `id`; empty if it did not.
  std::string Rejection(const std::string& id) {
    for (const auto& call : web_ui_->call_data()) {
      if (call->function_name() == "cr.webUIResponse" && call->arg1() &&
          call->arg1()->is_string() && call->arg1()->GetString() == id &&
          call->arg2() && call->arg2()->is_bool() && !call->arg2()->GetBool() &&
          call->arg3() && call->arg3()->is_string()) {
        return call->arg3()->GetString();
      }
    }
    return std::string();
  }

  // Waits for the answer of `id` and returns a copy.
  base::DictValue Await(const std::string& id) {
    base::DictValue answer;
    EXPECT_TRUE(base::test::RunUntil([&] {
      for (const auto& call : web_ui_->call_data()) {
        const base::Value* arg1 = call->arg1();
        if (call->function_name() == "cr.webUIResponse" && arg1 &&
            arg1->is_string() && arg1->GetString() == id) {
          const base::Value* result = call->arg3();
          if (result && result->is_dict()) {
            answer = result->GetDict().Clone();
          }
          return true;
        }
      }
      return false;
    }));
    return answer;
  }

  // The user presses "Import from Arc" and picks `path` in the chooser.
  base::DictValue Choose(const base::FilePath& path) {
    const std::string id = "cb" + std::to_string(++calls_);
    base::ListValue list;
    list.Append(id);
    web_ui_->HandleReceivedMessage("aerieArcImport.choose", list);
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return factory_->GetLastDialog() != nullptr; }));
    ui::FakeSelectFileDialog* dialog = factory_->GetLastDialog();
    if (dialog) {
      // The chooser is for one .json file.
      EXPECT_EQ(dialog->file_types().extensions.size(), 1u);
      EXPECT_THAT(dialog->file_types().extensions[0],
                  ElementsAre(FILE_PATH_LITERAL("json")));
      EXPECT_TRUE(dialog->CallFileSelected(path, "json"));
    }
    return Await(id);
  }

  // The import message alone. The handler answers when the records are in the
  // Tab Model; the Chromium tabs follow in batches (issue #276).
  base::DictValue ImportSpaceNoWait(int index) {
    std::vector<base::Value> args;
    args.emplace_back(index);
    return Send("aerieArcImport.import", std::move(args));
  }

  // Waits until every record has its tab. Runs at once if none is missing.
  void WaitForRestore() {
    base::test::TestFuture<void> done;
    service_->pinned_tabs()->RestoreMissingTabs(done.GetCallback());
    EXPECT_TRUE(done.Wait());
  }

  // The import, and the wait for its tabs.
  base::DictValue ImportSpace(int index) {
    base::DictValue answer = ImportSpaceNoWait(index);
    WaitForRestore();
    return answer;
  }

  // What the Tab Model holds, to see that a failed import left it alone.
  struct Snapshot {
    size_t tabs;
    size_t folders;
    int64_t next_change;
    int chromium_tabs;
    bool operator==(const Snapshot&) const = default;
  };
  Snapshot Take() {
    return {model().tabs().size(), model().folders().size(),
            model().next_change(), browser()->tab_strip_model()->count()};
  }

  // The records the user arranged: Pinned and Favorites (the window's own
  // first tab is a Today record).
  std::vector<std::pair<std::string, TabRecord>> NonToday() {
    std::vector<std::pair<std::string, TabRecord>> out;
    for (const auto& [id, tab] : model().tabs()) {
      if (tab.section != Section::kToday) {
        out.emplace_back(id, tab);
      }
    }
    return out;
  }

  // Every imported Chromium tab is unloaded: discarded, with no navigation
  // under way.
  void ExpectUnloaded(const std::string& record_id) {
    tabs::TabInterface* tab = service_->adapter()->TabForRecord(record_id);
    ASSERT_TRUE(tab) << record_id;
    content::WebContents* contents = tab->GetContents();
    // Restored with kDeferUntilActivation: it has an entry and has not loaded it.
    EXPECT_TRUE(contents->GetController().NeedsReload()) << record_id;
    EXPECT_FALSE(contents->IsLoading()) << record_id;
    EXPECT_FALSE(contents->GetController().GetPendingEntry()) << record_id;
  }

  raw_ptr<ui::FakeSelectFileDialog::Factory, DanglingUntriaged> factory_ =
      nullptr;
  raw_ptr<aerie_tab_model::TabModelService, DanglingUntriaged> service_ =
      nullptr;
  std::unique_ptr<content::TestWebUI> web_ui_;
  std::unique_ptr<AerieArcImportHandler> handler_;
  base::ScopedTempDir temp_dir_;
  int calls_ = 0;
};

IN_PROC_BROWSER_TEST_F(AerieArcImportBrowserTest,
                       ImportsPinnedFoldersAndFavoritesAfterWhatIsThere) {
  // The user has one Pinned tab already.
  TabRecord existing;
  existing.section = Section::kPinned;
  existing.pinned_url = "https://existing.example/";
  existing.last_title = "existing";
  const std::string existing_id = model().AddTab(std::move(existing));
  ASSERT_FALSE(existing_id.empty());

  base::DictValue chosen = Choose(Fixture("one_space.json"));
  ASSERT_EQ(*chosen.FindString("status"), "ok");
  const base::ListValue* spaces = chosen.FindList("spaces");
  ASSERT_TRUE(spaces);
  ASSERT_EQ(spaces->size(), 1u);
  const base::DictValue& space = (*spaces)[0].GetDict();
  EXPECT_EQ(*space.FindString("name"), "Work Space");
  EXPECT_EQ(*space.FindString("icon"), "\xF0\x9F\xA7\xAA");
  EXPECT_EQ(IntOf(space, "pinnedTabs"), 4);
  EXPECT_EQ(IntOf(space, "folders"), 1);
  EXPECT_EQ(IntOf(space, "favorites"), 2);
  // Choosing changed nothing.
  EXPECT_EQ(NonToday().size(), 1u);

  base::DictValue done = ImportSpace(0);
  ASSERT_EQ(*done.FindString("status"), "ok");
  EXPECT_EQ(IntOf(done, "pinnedTabs"), 4);
  EXPECT_EQ(IntOf(done, "folders"), 1);
  EXPECT_EQ(IntOf(done, "favorites"), 2);

  // Pinned: what was there first, then the import, nested and in order.
  EXPECT_THAT(Children(model(), Section::kPinned, ""),
              ElementsAre("t:existing", "t:Example", "f:Work", "t:Four"));
  EXPECT_THAT(Children(model(), Section::kPinned,
                       FolderIdByName(model(), "Work")),
              ElementsAre("t:Two", "t:Three"));
  // Favorites.
  std::vector<std::string> favorites;
  for (const std::string& id :
       aerie_tab_model::SectionTabIds(model(), Section::kFavorite)) {
    favorites.push_back(*model().GetTab(id)->last_title);
    EXPECT_EQ(model().GetTab(id)->section, Section::kFavorite);
  }
  EXPECT_THAT(favorites, ElementsAre("Calendar", "Mail"));
  // What was there is untouched.
  EXPECT_EQ(model().GetTab(existing_id)->position, 0);

  // Every imported record has an unloaded tab.
  int imported = 0;
  for (const auto& [id, tab] : NonToday()) {
    if (id != existing_id) {
      ++imported;
      EXPECT_TRUE(tab.sleeping);
      ExpectUnloaded(id);
    }
  }
  EXPECT_EQ(imported, 6);
}

IN_PROC_BROWSER_TEST_F(AerieArcImportBrowserTest, CustomNamesBecomeCustomNames) {
  ASSERT_EQ(*Choose(Fixture("custom_titles.json")).FindString("status"), "ok");
  base::DictValue done = ImportSpace(0);
  ASSERT_EQ(*done.FindString("status"), "ok");
  EXPECT_EQ(IntOf(done, "customNames"), 1);
  int named = 0;
  for (const auto& [id, tab] : NonToday()) {
    if (tab.last_title == "Inbox (3) - Mail") {
      EXPECT_EQ(tab.manual_name, "My Mail");
      ++named;
    } else {
      EXPECT_FALSE(tab.manual_name.has_value()) << *tab.last_title;
    }
  }
  EXPECT_EQ(named, 1);
}

IN_PROC_BROWSER_TEST_F(AerieArcImportBrowserTest, FavoritesStopAtTheCap) {
  ASSERT_EQ(*Choose(Fixture("favorites.json")).FindString("status"), "ok");
  base::DictValue done = ImportSpace(0);
  ASSERT_EQ(*done.FindString("status"), "ok");
  EXPECT_EQ(IntOf(done, "favorites"), 12);
  EXPECT_EQ(SkippedCount(done, "favorites_full"), 2);
  EXPECT_EQ(aerie_tab_model::SectionTabIds(model(), Section::kFavorite).size(),
            aerie_tab_model::kFavoritesCap);

  // The Workspace is full now: a second import adds no Favorite and says so.
  ASSERT_EQ(*Choose(Fixture("favorites.json")).FindString("status"), "ok");
  base::DictValue again = ImportSpace(0);
  ASSERT_EQ(*again.FindString("status"), "ok");
  EXPECT_EQ(IntOf(again, "favorites"), 0);
  EXPECT_EQ(SkippedCount(again, "favorites_full"), 14);
  EXPECT_EQ(aerie_tab_model::SectionTabIds(model(), Section::kFavorite).size(),
            aerie_tab_model::kFavoritesCap);
}

IN_PROC_BROWSER_TEST_F(AerieArcImportBrowserTest,
                       OnlyHttpAndHttpsAddressesAreImported) {
  ASSERT_EQ(*Choose(Fixture("bad_schemes.json")).FindString("status"), "ok");
  base::DictValue done = ImportSpace(0);
  ASSERT_EQ(*done.FindString("status"), "ok");
  EXPECT_EQ(IntOf(done, "pinnedTabs"), 3);
  EXPECT_EQ(IntOf(done, "favorites"), 1);
  EXPECT_EQ(SkippedCount(done, "unsupported_scheme"), 8);
  for (const auto& [id, tab] : NonToday()) {
    ASSERT_TRUE(tab.pinned_url.has_value());
    const GURL url(*tab.pinned_url);
    EXPECT_TRUE(url.SchemeIsHTTPOrHTTPS()) << *tab.pinned_url;
    EXPECT_FALSE(url.has_username());
    // The tab's own address is the same.
    ExpectUnloaded(id);
    EXPECT_EQ(
        service_->adapter()->TabForRecord(id)->GetContents()->GetVisibleURL(),
        url);
  }
}

IN_PROC_BROWSER_TEST_F(AerieArcImportBrowserTest, TitlesWithMarkupStayText) {
  ASSERT_EQ(*Choose(Fixture("markup_titles.json")).FindString("status"), "ok");
  base::DictValue done = ImportSpace(0);
  ASSERT_EQ(*done.FindString("status"), "ok");
  // The record holds the text as it is, with the control characters gone.
  EXPECT_EQ(FolderIdByName(model(), "<img src=x onerror=alert(1)>").empty(),
            false);
  bool found = false;
  for (const auto& [id, tab] : NonToday()) {
    EXPECT_EQ(tab.manual_name, "<script>alert('name')</script>");
    EXPECT_EQ(tab.last_title, "<b onclick=alert(2)>bold</b>&amp; end");
    found = true;
  }
  EXPECT_TRUE(found);
}

IN_PROC_BROWSER_TEST_F(AerieArcImportBrowserTest,
                       TabsOverTheCapOfOneImportAreNamedInTheResult) {
  // 260 pinned tabs: 250 come in, and the page is told that 10 did not.
  const std::string json = SpaceWithTabs(260);
  const base::FilePath path = temp_dir_.GetPath().AppendASCII("many.json");
  ASSERT_TRUE(base::WriteFile(path, json));

  ASSERT_EQ(*Choose(path).FindString("status"), "ok");
  base::DictValue done = ImportSpace(0);
  ASSERT_EQ(*done.FindString("status"), "ok");
  EXPECT_EQ(IntOf(done, "pinnedTabs"), 250);
  EXPECT_EQ(SkippedCount(done, "over_cap"), 10);
}

IN_PROC_BROWSER_TEST_F(AerieArcImportBrowserTest,
                       RestoringAFullImportNeverBlocksTheUiThreadFor100Ms) {
  const base::FilePath path = temp_dir_.GetPath().AppendASCII("full.json");
  ASSERT_TRUE(base::WriteFile(path, SpaceWithTabs(250)));
  ASSERT_EQ(*Choose(path).FindString("status"), "ok");
  const int tabs_before = browser()->tab_strip_model()->count();

  base::DictValue done;
  base::TimeDelta longest;
  int tasks = 0;
  {
    // Measures every task of the UI thread from the answer of the import to the
    // last tab: the answer comes first, and the tabs follow in batches.
    LongestTaskObserver observer;
    done = ImportSpaceNoWait(0);
    WaitForRestore();
    longest = observer.longest();
    tasks = observer.tasks();
  }
  ASSERT_EQ(*done.FindString("status"), "ok");
  EXPECT_EQ(IntOf(done, "pinnedTabs"), 250);
  EXPECT_EQ(browser()->tab_strip_model()->count(), tabs_before + 250);
  // More than a few tasks: the restore was in batches and not in one.
  EXPECT_GT(tasks, 10);
  EXPECT_LT(longest, base::Milliseconds(100)) << longest;
}

IN_PROC_BROWSER_TEST_F(AerieArcImportBrowserTest,
                       A300TabSpaceBringsIn250AndReportsTheRest) {
  const base::FilePath path = temp_dir_.GetPath().AppendASCII("three.json");
  ASSERT_TRUE(base::WriteFile(path, SpaceWithTabs(300)));
  ASSERT_EQ(*Choose(path).FindString("status"), "ok");
  const int tabs_before = browser()->tab_strip_model()->count();
  base::DictValue done = ImportSpace(0);
  ASSERT_EQ(*done.FindString("status"), "ok");
  EXPECT_EQ(IntOf(done, "pinnedTabs"), 250);
  EXPECT_EQ(SkippedCount(done, "over_cap"), 50);
  EXPECT_EQ(browser()->tab_strip_model()->count(), tabs_before + 250);
}

IN_PROC_BROWSER_TEST_F(AerieArcImportBrowserTest,
                       AWorkspaceWith1000PinnedTabsTakesNoMoreFromAnImport) {
  // Records only: an address that does not parse gets no Chromium tab, so the
  // test does not make 1000 of them.
  for (int i = 0; i < 1000; ++i) {
    TabRecord existing;
    existing.section = Section::kPinned;
    existing.position = i;
    existing.pinned_url = "not an address " + base::NumberToString(i);
    ASSERT_FALSE(model().AddTab(std::move(existing)).empty());
  }
  const base::FilePath path = temp_dir_.GetPath().AppendASCII("some.json");
  ASSERT_TRUE(base::WriteFile(path, SpaceWithTabs(20)));
  ASSERT_EQ(*Choose(path).FindString("status"), "ok");
  const size_t records_before = model().tabs().size();
  const int tabs_before = browser()->tab_strip_model()->count();

  base::DictValue done = ImportSpace(0);
  ASSERT_EQ(*done.FindString("status"), "ok");
  EXPECT_EQ(IntOf(done, "pinnedTabs"), 0);
  EXPECT_EQ(SkippedCount(done, "over_cap"), 20);
  EXPECT_EQ(model().tabs().size(), records_before);
  EXPECT_EQ(browser()->tab_strip_model()->count(), tabs_before);
}

IN_PROC_BROWSER_TEST_F(AerieArcImportBrowserTest,
                       ASecondImportOfTheSameSpaceAddsNoDuplicates) {
  ASSERT_EQ(*Choose(Fixture("one_space.json")).FindString("status"), "ok");
  base::DictValue first = ImportSpace(0);
  ASSERT_EQ(*first.FindString("status"), "ok");
  const Snapshot after_first = Take();
  const std::vector<std::string> pinned_before =
      Children(model(), Section::kPinned, "");

  // The file is read again: a repeat of the same press.
  ASSERT_EQ(*Choose(Fixture("one_space.json")).FindString("status"), "ok");
  base::DictValue second = ImportSpace(0);
  ASSERT_EQ(*second.FindString("status"), "ok");
  EXPECT_EQ(IntOf(second, "pinnedTabs"), 0);
  EXPECT_EQ(IntOf(second, "folders"), 0);
  EXPECT_EQ(IntOf(second, "favorites"), 0);
  EXPECT_EQ(SkippedCount(second, "already_there"), 6);
  EXPECT_EQ(Children(model(), Section::kPinned, ""), pinned_before);
  EXPECT_EQ(Take().tabs, after_first.tabs);
  EXPECT_EQ(Take().folders, after_first.folders);
  EXPECT_EQ(Take().chromium_tabs, after_first.chromium_tabs);
}

IN_PROC_BROWSER_TEST_F(AerieArcImportBrowserTest,
                       NothingLoadsAndNoRequestIsMade) {
  std::vector<std::string> requests;
  embedded_test_server()->RegisterRequestMonitor(base::BindLambdaForTesting(
      [&](const net::test_server::HttpRequest& request) {
        requests.push_back(request.relative_url);
      }));
  ASSERT_TRUE(embedded_test_server()->Start());
  const std::string a = embedded_test_server()->GetURL("a.test", "/a").spec();
  const std::string b = embedded_test_server()->GetURL("b.test", "/b").spec();
  const std::string fav = embedded_test_server()->GetURL("c.test", "/c").spec();
  const std::string json = base::StrCat(
      {R"({"sidebar":{"containers":[{"global":{}},{"items":[
        "top",{"id":"top","childrenIds":["fav"],"data":{"itemContainer":{}}},
        "fav",{"id":"fav","parentID":"top","data":{"tab":{"savedURL":")",
       fav, R"(","savedTitle":"Fav"}}},
        "p",{"id":"p","childrenIds":["a","b"],"data":{"itemContainer":{}}},
        "a",{"id":"a","parentID":"p","data":{"tab":{"savedURL":")",
       a, R"(","savedTitle":"A"}}},
        "b",{"id":"b","parentID":"p","data":{"tab":{"savedURL":")",
       b, R"(","savedTitle":"B"}}}],
        "spaces":["s",{"id":"s","title":"S","containerIDs":["pinned","p"]}],
        "topAppsContainerIDs":[{"default":{}},"top"]}]}})"});
  const base::FilePath path = temp_dir_.GetPath().AppendASCII("sidebar.json");
  ASSERT_TRUE(base::WriteFile(path, json));

  ASSERT_EQ(*Choose(path).FindString("status"), "ok");
  base::DictValue done = ImportSpace(0);
  ASSERT_EQ(*done.FindString("status"), "ok");
  EXPECT_EQ(IntOf(done, "pinnedTabs"), 2);
  EXPECT_EQ(IntOf(done, "favorites"), 1);
  for (const auto& [id, tab] : NonToday()) {
    ExpectUnloaded(id);
  }
  // Let anything that was going to start start.
  content::RunAllTasksUntilIdle();
  EXPECT_TRUE(requests.empty()) << requests.size() << " request(s)";
}

IN_PROC_BROWSER_TEST_F(AerieArcImportBrowserTest, AFileThatFailsChangesNothing) {
  const Snapshot before = Take();
  const std::map<std::string, std::string> expected = {
      {"bad_json.json", "invalid_json"},
      {"not_an_object.json", "not_arc"},
      {"no_containers.json", "not_arc"},
      {"missing_keys.json", "no_spaces"},
  };
  for (const auto& [name, error] : expected) {
    base::DictValue answer = Choose(Fixture(name));
    EXPECT_EQ(*answer.FindString("status"), "error") << name;
    EXPECT_EQ(*answer.FindString("error"), error) << name;
    // Nothing to import after an error.
    base::DictValue import = ImportSpace(0);
    EXPECT_EQ(*import.FindString("error"), "no_file") << name;
    EXPECT_EQ(Take(), before) << name;
  }
  // A file that is not there, and a folder.
  EXPECT_EQ(*Choose(temp_dir_.GetPath().AppendASCII("none.json"))
                 .FindString("error"),
            "unreadable");
  EXPECT_EQ(*Choose(temp_dir_.GetPath()).FindString("error"), "not_a_file");
  EXPECT_EQ(Take(), before);
}

#if BUILDFLAG(IS_POSIX)
// Opening a FIFO for reading waits for a writer. The handler must refuse it
// by its type before it opens anything: this test finishes only if it does.
IN_PROC_BROWSER_TEST_F(AerieArcImportBrowserTest, AFifoIsRefusedWithoutBlocking) {
  const base::FilePath fifo = temp_dir_.GetPath().AppendASCII("pipe.json");
  ASSERT_EQ(mkfifo(fifo.value().c_str(), 0600), 0);
  const Snapshot before = Take();
  base::DictValue answer = Choose(fifo);
  EXPECT_EQ(*answer.FindString("status"), "error");
  EXPECT_EQ(*answer.FindString("error"), "not_a_file");
  EXPECT_EQ(Take(), before);
}
#endif

IN_PROC_BROWSER_TEST_F(AerieArcImportBrowserTest, APressWhileTheChooserIsOpenIsRejectedAsBusy) {
  // First press: the chooser opens.
  const std::string first = "cb" + std::to_string(++calls_);
  base::ListValue first_args;
  first_args.Append(first);
  web_ui_->HandleReceivedMessage("aerieArcImport.choose", first_args);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return factory_->GetLastDialog() != nullptr; }));
  ui::FakeSelectFileDialog* dialog = factory_->GetLastDialog();

  // Second press while it is open: rejected at once, and the chooser is the
  // same one.
  const std::string second = "cb" + std::to_string(++calls_);
  base::ListValue second_args;
  second_args.Append(second);
  web_ui_->HandleReceivedMessage("aerieArcImport.choose", second_args);
  EXPECT_EQ(Rejection(second), "busy");
  EXPECT_EQ(factory_->GetLastDialog(), dialog);
  EXPECT_FALSE(Answered(first));

  // The answer of the open chooser is the reply to the first press only.
  ASSERT_TRUE(dialog->CallFileSelected(Fixture("one_space.json"), "json"));
  base::DictValue answer = Await(first);
  EXPECT_EQ(*answer.FindString("status"), "ok");
  EXPECT_EQ(Responses(second), 1);
  EXPECT_EQ(Responses(first), 1);

  // With the chooser closed, a press works again.
  EXPECT_EQ(*Choose(Fixture("one_space.json")).FindString("status"), "ok");
}

IN_PROC_BROWSER_TEST_F(AerieArcImportBrowserTest, EveryMalformedCallWithAnIdIsRejected) {
  for (const std::vector<base::Value>& extra :
       {std::vector<base::Value>(), [] {
          std::vector<base::Value> v;
          v.emplace_back("not a number");
          return v;
        }(), [] {
          std::vector<base::Value> v;
          v.emplace_back(0);
          v.emplace_back(1);
          return v;
        }()}) {
    const std::string id = "cb" + std::to_string(++calls_);
    base::ListValue list;
    list.Append(id);
    for (const base::Value& arg : extra) {
      list.Append(arg.Clone());
    }
    web_ui_->HandleReceivedMessage("aerieArcImport.import", list);
    EXPECT_EQ(Rejection(id), "bad_argument") << extra.size();
  }
  // A choose that carries more than its id is rejected too.
  const std::string id = "cb" + std::to_string(++calls_);
  base::ListValue list;
  list.Append(id);
  list.Append("extra");
  web_ui_->HandleReceivedMessage("aerieArcImport.choose", list);
  EXPECT_EQ(Rejection(id), "bad_argument");
  EXPECT_EQ(factory_->GetLastDialog(), nullptr);
}

IN_PROC_BROWSER_TEST_F(AerieArcImportBrowserTest, AFileOver32MiBIsRefused) {
  const base::FilePath path = temp_dir_.GetPath().AppendASCII("big.json");
  {
    base::File file(path, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    ASSERT_TRUE(file.IsValid());
    // A sparse file one byte over the cap: refused from its size alone.
    ASSERT_TRUE(file.SetLength(aerie_tab_model::kArcMaxFileBytes + 1));
  }
  const Snapshot before = Take();
  base::DictValue answer = Choose(path);
  EXPECT_EQ(*answer.FindString("status"), "error");
  EXPECT_EQ(*answer.FindString("error"), "too_large");
  EXPECT_EQ(Take(), before);
}

IN_PROC_BROWSER_TEST_F(AerieArcImportBrowserTest, ACanceledChooserChangesNothing) {
  const Snapshot before = Take();
  const std::string id = "cb-cancel";
  base::ListValue list;
  list.Append(id);
  web_ui_->HandleReceivedMessage("aerieArcImport.choose", list);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return factory_->GetLastDialog() != nullptr; }));
  factory_->GetLastDialog()->CallFileSelectionCanceled();
  EXPECT_EQ(*Await(id).FindString("status"), "cancelled");
  EXPECT_EQ(Take(), before);
}

IN_PROC_BROWSER_TEST_F(AerieArcImportBrowserTest, AFailedWriteIsAllOrNothing) {
  TabRecord existing;
  existing.section = Section::kPinned;
  existing.pinned_url = "https://existing.example/";
  const std::string existing_id = model().AddTab(std::move(existing));
  WaitForRestore();
  const Snapshot before = Take();

  ASSERT_EQ(*Choose(Fixture("one_space.json")).FindString("status"), "ok");
  handler_->set_fail_at_record_for_testing(3);
  base::DictValue failed = ImportSpace(0);
  EXPECT_EQ(*failed.FindString("status"), "error");
  EXPECT_EQ(*failed.FindString("error"), "apply_failed");
  // No record, no Chromium tab, and what was there is still there.
  EXPECT_EQ(model().tabs().size(), before.tabs);
  EXPECT_EQ(model().folders().size(), before.folders);
  EXPECT_EQ(browser()->tab_strip_model()->count(), before.chromium_tabs);
  EXPECT_TRUE(model().GetTab(existing_id));

  // The file is still chosen: the same import works when the write does.
  handler_->set_fail_at_record_for_testing(-1);
  base::DictValue done = ImportSpace(0);
  EXPECT_EQ(*done.FindString("status"), "ok");
  EXPECT_EQ(model().tabs().size(), before.tabs + 6);
}

IN_PROC_BROWSER_TEST_F(AerieArcImportBrowserTest, ABadSpaceIndexImportsNothing) {
  ASSERT_EQ(*Choose(Fixture("three_spaces.json")).FindString("status"), "ok");
  const Snapshot before = Take();
  EXPECT_EQ(*ImportSpace(3).FindString("error"), "no_file");
  EXPECT_EQ(*ImportSpace(-1).FindString("error"), "no_file");
  EXPECT_EQ(Take(), before);
  // The second of three Spaces brings its own pinned tab and its own Favorite.
  base::DictValue done = ImportSpace(1);
  ASSERT_EQ(*done.FindString("status"), "ok");
  EXPECT_EQ(IntOf(done, "pinnedTabs"), 1);
  EXPECT_EQ(IntOf(done, "favorites"), 1);
  // The Little Arc sidebar was left out and counted.
  EXPECT_EQ(SkippedCount(done, "little_arc"), 1);
}

}  // namespace aerie_arc_import
