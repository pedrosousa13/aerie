// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of archiving unused Today tabs (issue #183): the timeout with a
// controllable clock, the tabs that never archive, the setting without a
// restart, restoring a tab with its history, the pinned-close hook, and that
// closing for archive leaves "Recently closed" alone.

#include "chrome/browser/aerie_tab_model/tab_archiver.h"

#include <memory>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "base/test/simple_test_clock.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/power_monitor/power_observer.h"
#include "base/time/time.h"
#include "chrome/browser/aerie_tab_model/pinned_tabs_controller.h"
#include "chrome/browser/aerie_tab_model/tab_archiver_factory.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_tab_model/archive.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_prefs.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/core/serialized_navigation_entry.h"
#include "components/sessions/core/tab_restore_service.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "url/gurl.h"

namespace aerie_tab_model {

class AerieTabArchiverBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    service_ = TabModelServiceFactory::GetForProfile(browser()->GetProfile());
    ASSERT_TRUE(service_);
    // The test owns both clocks; the archiver is built on them, as it is built
    // lazily in the browser.
    clock_.SetNow(base::Time::Now());
    tick_clock_.SetNowTicks(base::TimeTicks::Now());
    archiver_ = static_cast<TabArchiver*>(
        TabArchiverFactory::GetInstance()->SetTestingFactoryAndUse(
            browser()->GetProfile(),
            base::BindRepeating(
                [](base::SimpleTestClock* clock,
                   base::SimpleTestTickClock* tick_clock,
                   content::BrowserContext* context)
                    -> std::unique_ptr<KeyedService> {
                  Profile* profile = Profile::FromBrowserContext(context);
                  return std::make_unique<TabArchiver>(
                      profile, TabModelServiceFactory::GetForProfile(profile),
                      clock, tick_clock);
                },
                &clock_, &tick_clock_)));
    ASSERT_TRUE(archiver_);
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitchASCII("autoplay-policy",
                                    "no-user-gesture-required");
  }

  void TearDownOnMainThread() override {
    archiver_ = nullptr;
    InProcessBrowserTest::TearDownOnMainThread();
  }

  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }
  TabStripModel* strip() { return browser()->tab_strip_model(); }
  TabModel* model() { return service_->model(); }

  // Opens a loaded tab behind the selected one, marks it as last used now (on
  // the test clock) and returns its record id.
  std::string OpenBackgroundTab(const char* path = "/title1.html") {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), embedded_test_server()->GetURL(path),
        WindowOpenDisposition::NEW_BACKGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    const std::string id = IdAt(strip()->count() - 1);
    EXPECT_FALSE(id.empty());
    UseNow(id);
    return id;
  }

  std::string IdAt(int index) {
    return service_->adapter()->RecordIdForTab(strip()->GetTabAtIndex(index));
  }

  void UseNow(const std::string& id) {
    const base::Time now = clock_.Now();
    model()->UpdateTab(id, [now](TabRecord& tab) { tab.last_active = now; });
  }

  bool IsArchived(const std::string& id) {
    const TabRecord* tab = model()->GetTab(id);
    return tab && tab->archived_at.has_value();
  }

  int IndexOf(const std::string& id) {
    for (int i = 0; i < strip()->count(); ++i) {
      if (IdAt(i) == id) {
        return i;
      }
    }
    return -1;
  }

  // Moves both controllable clocks and lets the schedule look at them.
  void AdvanceClock(base::TimeDelta by) {
    clock_.Advance(by);
    tick_clock_.Advance(by);
    archiver_->schedule()->RunDueNow();
  }

  size_t RecentlyClosedCount() {
    sessions::TabRestoreService* restore =
        TabRestoreServiceFactory::GetForProfile(browser()->GetProfile());
    return restore ? restore->entries().size() : 0;
  }

  // Runs the message loop until `done` is true. It waits for the event, with
  // no sleep and no real-time poll; it fails after the test timeout.
  template <typename Predicate>
  bool WaitFor(Predicate done) {
    return base::test::RunUntil(done);
  }

  base::SimpleTestClock clock_;
  base::SimpleTestTickClock tick_clock_;
  raw_ptr<TabModelService> service_ = nullptr;
  raw_ptr<TabArchiver> archiver_ = nullptr;
};

IN_PROC_BROWSER_TEST_F(AerieTabArchiverBrowserTest,
                       AnUnusedTodayTabIsArchivedAtTwelveHoursAndNotBefore) {
  const std::string id = OpenBackgroundTab();
  const size_t recently_closed = RecentlyClosedCount();
  AdvanceClock(base::Hours(11) + base::Minutes(59));
  EXPECT_EQ(strip()->count(), 2);
  EXPECT_FALSE(IsArchived(id));
  AdvanceClock(base::Minutes(1));
  EXPECT_EQ(strip()->count(), 1);
  ASSERT_TRUE(IsArchived(id));
  // It is listed in the Archive with its page.
  bool listed = false;
  for (const ArchivedRow& row : ArchivedRows(*model())) {
    listed |= row.id == id &&
              row.url == embedded_test_server()->GetURL("/title1.html").spec();
  }
  EXPECT_TRUE(listed);
  // Closing for archive added nothing to "Recently closed".
  EXPECT_EQ(RecentlyClosedCount(), recently_closed);
}

IN_PROC_BROWSER_TEST_F(AerieTabArchiverBrowserTest,
                       TheSelectedTabIsNeverArchivedAndNeitherIsAnAudibleOne) {
  const std::string selected = IdAt(0);
  UseNow(selected);
  const std::string audible = OpenBackgroundTab();
  content::WebContents* contents =
      strip()->GetWebContentsAt(IndexOf(audible));
  ASSERT_TRUE(content::ExecJs(contents, R"(
      globalThis.ctx = new AudioContext();
      const osc = ctx.createOscillator();
      osc.connect(ctx.destination);
      osc.start();
      ctx.resume();)"));
  ASSERT_TRUE(WaitFor([&] { return contents->IsCurrentlyAudible(); }));
  AdvanceClock(base::Days(2));
  EXPECT_FALSE(IsArchived(selected));
  EXPECT_FALSE(IsArchived(audible));
  EXPECT_EQ(strip()->count(), 2);

  // When the sound stops the tab was in use until then: its time starts again.
  ASSERT_TRUE(content::ExecJs(contents, "ctx.close();"));
  ASSERT_TRUE(WaitFor([&] { return !contents->IsCurrentlyAudible(); }));
  base::RunLoop().RunUntilIdle();
  AdvanceClock(base::Hours(11) + base::Minutes(59));
  EXPECT_FALSE(IsArchived(audible));
  AdvanceClock(base::Minutes(1));
  EXPECT_TRUE(IsArchived(audible));
  EXPECT_FALSE(IsArchived(selected));
}

IN_PROC_BROWSER_TEST_F(AerieTabArchiverBrowserTest,
                       PinnedFavoritePinnedFolderTabsNeverArchiveButATodayFolderTabDoes) {
  const std::string pinned = OpenBackgroundTab("/title1.html");
  const std::string favorite = OpenBackgroundTab("/title2.html");
  const std::string in_pinned_folder = OpenBackgroundTab("/title3.html");
  const std::string in_today_folder = OpenBackgroundTab("/simple.html");
  PinnedTabsController* controller = service_->pinned_tabs();
  ASSERT_TRUE(controller->Pin(strip()->GetTabAtIndex(IndexOf(pinned))));
  ASSERT_EQ(controller->AddToFavorites(strip()->GetTabAtIndex(IndexOf(favorite))),
            FavoriteResult::kAdded);
  ASSERT_TRUE(controller->Pin(strip()->GetTabAtIndex(IndexOf(in_pinned_folder))));
  const std::string pinned_folder =
      CreateFolder(*model(), Section::kPinned, std::string(), "Pinned folder");
  ASSERT_TRUE(MoveTabToFolder(*model(), in_pinned_folder, pinned_folder));
  const std::string today_folder =
      CreateFolder(*model(), Section::kToday, std::string(), "Today folder");
  ASSERT_TRUE(MoveTabToFolder(*model(), in_today_folder, today_folder));
  for (const std::string& id :
       {pinned, favorite, in_pinned_folder, in_today_folder}) {
    UseNow(id);
  }
  AdvanceClock(base::Days(3));
  EXPECT_FALSE(IsArchived(pinned));
  EXPECT_FALSE(IsArchived(favorite));
  EXPECT_FALSE(IsArchived(in_pinned_folder));
  EXPECT_TRUE(IsArchived(in_today_folder));
  EXPECT_EQ(model()->GetTab(in_today_folder)->archive->folder, today_folder);
}

IN_PROC_BROWSER_TEST_F(AerieTabArchiverBrowserTest,
                       TheSettingChangesTheBehaviourWithoutARestart) {
  const std::string id = OpenBackgroundTab();
  prefs()->SetString(kArchiveAfter, "24h");
  AdvanceClock(base::Hours(13));
  EXPECT_FALSE(IsArchived(id));
  prefs()->SetString(kArchiveAfter, "7d");
  AdvanceClock(base::Days(6));
  EXPECT_FALSE(IsArchived(id));
  prefs()->SetString(kArchiveAfter, "never");
  AdvanceClock(base::Days(30));
  EXPECT_FALSE(IsArchived(id));
  EXPECT_FALSE(archiver_->schedule()->timer_pending());
  prefs()->SetString(kArchiveAfter, "24h");
  AdvanceClock(base::Seconds(1));
  EXPECT_TRUE(IsArchived(id));
}

IN_PROC_BROWSER_TEST_F(AerieTabArchiverBrowserTest,
                       RestoringReopensTheTabAtTheTopOfTodayWithItsHistory) {
  const std::string other = OpenBackgroundTab("/title2.html");
  const std::string id = OpenBackgroundTab("/title1.html");
  // A second page in the tab's history, loaded while it is selected.
  strip()->ActivateTabAt(IndexOf(id));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/simple.html")));
  strip()->ActivateTabAt(0);
  UseNow(other);
  UseNow(id);
  // Only the tab that is left alone for the whole time is archived: `other` is
  // used again, a moment before the end.
  AdvanceClock(base::Hours(11));
  UseNow(other);
  AdvanceClock(base::Hours(1));
  ASSERT_TRUE(IsArchived(id));
  ASSERT_FALSE(IsArchived(other));
  const size_t recently_closed = RecentlyClosedCount();

  content::WebContents* restored = archiver_->Restore(id, browser());
  ASSERT_TRUE(restored);
  EXPECT_EQ(strip()->GetActiveWebContents(), restored);
  ASSERT_TRUE(WaitFor([&] { return !restored->IsLoading(); }));
  EXPECT_EQ(restored->GetLastCommittedURL(),
            embedded_test_server()->GetURL("/simple.html"));
  EXPECT_TRUE(restored->GetController().CanGoBack());
  EXPECT_EQ(restored->GetController().GetEntryCount(), 2);
  // Same record, back in Today, above the other tab.
  EXPECT_EQ(IdAt(strip()->active_index()), id);
  const TabRecord* record = model()->GetTab(id);
  ASSERT_TRUE(record);
  EXPECT_FALSE(record->archived_at);
  EXPECT_EQ(record->section, Section::kToday);
  EXPECT_LT(record->position, model()->GetTab(other)->position);
  EXPECT_EQ(RecentlyClosedCount(), recently_closed);
  // Restoring a record that is not archived does nothing.
  EXPECT_EQ(archiver_->Restore(id, browser()), nullptr);
}

IN_PROC_BROWSER_TEST_F(AerieTabArchiverBrowserTest,
                       RestoringGoesBackIntoTheSameTodayFolderWhenItStillExists) {
  const std::string id = OpenBackgroundTab();
  const std::string folder =
      CreateFolder(*model(), Section::kToday, std::string(), "Reading");
  ASSERT_TRUE(MoveTabToFolder(*model(), id, folder));
  UseNow(id);
  AdvanceClock(base::Hours(12));
  ASSERT_TRUE(IsArchived(id));
  SetFolderExpanded(*model(), folder, false);
  ASSERT_TRUE(archiver_->Restore(id, browser()));
  EXPECT_EQ(model()->GetTab(id)->parent, folder);
  EXPECT_TRUE(model()->GetFolder(folder)->expanded);
}

IN_PROC_BROWSER_TEST_F(AerieTabArchiverBrowserTest,
                       APageThatConfirmsLeavingIsNotArchived) {
  const std::string id = OpenBackgroundTab();
  content::WebContents* contents = strip()->GetWebContentsAt(IndexOf(id));
  ASSERT_TRUE(content::ExecJs(contents, "window.onbeforeunload = () => 'x';"));
  ASSERT_TRUE(WaitFor([&] { return contents->NeedToFireBeforeUnloadOrUnloadEvents(); }));
  AdvanceClock(base::Hours(12));
  EXPECT_FALSE(IsArchived(id));
  EXPECT_EQ(strip()->count(), 2);
  // It is tried again after another full timeout.
  EXPECT_EQ(archiver_->schedule()->next_fire_in(), base::Hours(12));
  // Let the window close without a prompt.
  ASSERT_TRUE(content::ExecJs(contents, "window.onbeforeunload = null;"));
}

IN_PROC_BROWSER_TEST_F(AerieTabArchiverBrowserTest,
                       TheSecondPressOfAPinnedCloseSendsTheTabToTheArchive) {
  prefs()->SetString(kPinnedClose, kPinnedCloseClose);
  const std::string id = OpenBackgroundTab();
  PinnedTabsController* controller = service_->pinned_tabs();
  tabs::TabInterface* tab = strip()->GetTabAtIndex(IndexOf(id));
  ASSERT_TRUE(controller->Pin(tab));
  const size_t recently_closed = RecentlyClosedCount();
  ASSERT_TRUE(controller->HandleClose(tab));
  EXPECT_EQ(strip()->count(), 1);
  const TabRecord* record = model()->GetTab(id);
  ASSERT_TRUE(record);
  EXPECT_TRUE(record->archived_at);
  EXPECT_EQ(record->section, Section::kToday);
  EXPECT_FALSE(record->pinned_url);
  EXPECT_EQ(RecentlyClosedCount(), recently_closed);
}

IN_PROC_BROWSER_TEST_F(AerieTabArchiverBrowserTest,
                       AClosedTabThatIsNotArchivedStillLosesItsRecord) {
  const std::string id = OpenBackgroundTab();
  strip()->CloseWebContentsAt(IndexOf(id), TabCloseTypes::CLOSE_NONE);
  EXPECT_FALSE(model()->GetTab(id));
}

IN_PROC_BROWSER_TEST_F(AerieTabArchiverBrowserTest,
                       ATabThatCannotBeReopenedStaysInTheArchive) {
  const std::string id = OpenBackgroundTab();
  AdvanceClock(base::Hours(12));
  ASSERT_TRUE(IsArchived(id));
  const int count = strip()->count();
  // The tab does not open (nothing is returned).
  archiver_->SetTabAdderForTesting(base::BindRepeating(
      [](BrowserWindowInterface*,
         const std::vector<sessions::SerializedNavigationEntry>&, int,
         int) -> content::WebContents* { return nullptr; }));
  EXPECT_EQ(archiver_->Restore(id, browser()), nullptr);
  EXPECT_EQ(strip()->count(), count);
  // The record is still an archived record, listed, not a Today record with no
  // tab.
  const TabRecord* record = model()->GetTab(id);
  ASSERT_TRUE(record);
  EXPECT_TRUE(record->archived_at);
  ASSERT_TRUE(record->archive);
  EXPECT_EQ(record->archive->url,
            embedded_test_server()->GetURL("/title1.html").spec());
  EXPECT_EQ(ArchivedRows(*model()).size(), 1u);
  // And it can still be restored afterwards.
  archiver_->SetTabAdderForTesting(TabArchiver::TabAdder());
  EXPECT_TRUE(archiver_->Restore(id, browser()));
  EXPECT_FALSE(IsArchived(id));
}

IN_PROC_BROWSER_TEST_F(AerieTabArchiverBrowserTest,
                       AHostileAddressInTheRecordIsNeverHandedBackToTheBrowser) {
  const std::string id = OpenBackgroundTab();
  AdvanceClock(base::Hours(12));
  ASSERT_TRUE(IsArchived(id));
  model()->UpdateTab(id, [](TabRecord& tab) {
    tab.archive->entries = {{"javascript:alert(document.cookie)", "x"}};
  });
  const int count = strip()->count();
  EXPECT_EQ(archiver_->Restore(id, browser()), nullptr);
  EXPECT_EQ(strip()->count(), count);
  EXPECT_TRUE(IsArchived(id));
}

IN_PROC_BROWSER_TEST_F(AerieTabArchiverBrowserTest,
                       AWakeFromSleepArmsTheTimerAgainByTheWallClock) {
  const std::string id = OpenBackgroundTab();
  // The computer sleeps 13 hours: the wall clock moves, the monotonic clock
  // (the timer's) does not.
  clock_.Advance(base::Hours(13));
  EXPECT_FALSE(IsArchived(id));
  ASSERT_TRUE(archiver_->schedule()->next_fire_in());
  EXPECT_GT(*archiver_->schedule()->next_fire_in(), base::Hours(11));
  // The resume reaches the archiver as the PowerMonitor delivers it, through
  // base::PowerSuspendObserver (the monitor itself is the browser's own here and
  // takes no test events).
  static_cast<base::PowerSuspendObserver*>(archiver_)->OnResume();
  ASSERT_TRUE(archiver_->schedule()->next_fire_in());
  EXPECT_LE(*archiver_->schedule()->next_fire_in(), base::TimeDelta());
  ASSERT_TRUE(WaitFor([&] { return IsArchived(id); }));
}

IN_PROC_BROWSER_TEST_F(AerieTabArchiverBrowserTest,
                       AudioStartingAndStoppingArmTheTimerThroughTheObserver) {
  const std::string audible = OpenBackgroundTab();
  ASSERT_TRUE(archiver_->schedule()->timer_pending());
  EXPECT_EQ(archiver_->schedule()->next_fire_in(), base::Hours(12));
  content::WebContents* contents = strip()->GetWebContentsAt(IndexOf(audible));
  ASSERT_TRUE(content::ExecJs(contents, R"(
      globalThis.ctx = new AudioContext();
      const osc = ctx.createOscillator();
      osc.connect(ctx.destination);
      osc.start();
      ctx.resume();)"));
  ASSERT_TRUE(WaitFor([&] { return contents->IsCurrentlyAudible(); }));
  // The only other candidate is the selected tab, also exempt: nothing is due.
  ASSERT_TRUE(WaitFor([&] { return !archiver_->schedule()->timer_pending(); }));
  // Some time passes while it plays; then the sound stops.
  clock_.Advance(base::Hours(5));
  tick_clock_.Advance(base::Hours(5));
  ASSERT_TRUE(content::ExecJs(contents, "ctx.close();"));
  ASSERT_TRUE(WaitFor([&] { return !contents->IsCurrentlyAudible(); }));
  ASSERT_TRUE(WaitFor([&] { return archiver_->schedule()->timer_pending(); }));
  // Its time starts again from the end of the sound.
  EXPECT_EQ(archiver_->schedule()->next_fire_in(), base::Hours(12));
}

IN_PROC_BROWSER_TEST_F(AerieTabArchiverBrowserTest,
                       ShuttingTheArchiverDownStopsTheTimerForGood) {
  const std::string id = OpenBackgroundTab();
  ASSERT_TRUE(archiver_->schedule()->timer_pending());
  archiver_->Shutdown();
  EXPECT_FALSE(archiver_->schedule()->timer_pending());
  AdvanceClock(base::Days(2));
  EXPECT_FALSE(IsArchived(id));
  EXPECT_EQ(strip()->count(), 2);
}

class AerieTabArchiverStartupBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(AerieTabArchiverStartupBrowserTest,
                       NoArchiverAndNoTimerExistUntilTheSidebarAsksForIt) {
  Profile* profile = browser()->GetProfile();
  // The window is open and the tab model is there, but nothing has asked for
  // the archiver (the sidebar does after a few seconds): it costs nothing at
  // startup and no timer is armed.
  EXPECT_TRUE(TabModelServiceFactory::GetForProfile(profile));
  EXPECT_EQ(TabArchiverFactory::GetForProfileIfExists(profile), nullptr);
  TabArchiver* archiver = TabArchiverFactory::GetForProfile(profile);
  ASSERT_TRUE(archiver);
  EXPECT_EQ(TabArchiverFactory::GetForProfileIfExists(profile), archiver);
}

}  // namespace aerie_tab_model
