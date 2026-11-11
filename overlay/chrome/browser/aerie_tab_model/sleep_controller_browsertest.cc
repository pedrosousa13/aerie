// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of putting tabs to sleep (issue #179): the idle timer with a
// controllable clock, "never", Sleep now, selecting a sleeping tab, a discard
// that Chromium makes itself, and a tab that cannot sleep.

#include <memory>
#include <string>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/time/time.h"
#include "chrome/browser/aerie_tab_model/sleep_controller.h"
#include "chrome/browser/aerie_tab_model/sleep_controller_factory.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/resource_coordinator/lifecycle_unit_state.mojom.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_prefs.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "url/gurl.h"

namespace aerie_tab_model {

class AerieSleepControllerBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    service_ = TabModelServiceFactory::GetForProfile(browser()->GetProfile());
    ASSERT_TRUE(service_);
    // The test owns the clock; the controller is built on it, as it is built
    // lazily in the browser.
    clock_.SetNowTicks(base::TimeTicks::Now());
    controller_ = static_cast<SleepController*>(
        SleepControllerFactory::GetInstance()->SetTestingFactoryAndUse(
            browser()->GetProfile(),
            base::BindRepeating(
                [](base::SimpleTestTickClock* clock,
                   content::BrowserContext* context)
                    -> std::unique_ptr<KeyedService> {
                  Profile* profile = Profile::FromBrowserContext(context);
                  return std::make_unique<SleepController>(
                      profile, TabModelServiceFactory::GetForProfile(profile),
                      clock);
                },
                &clock_)));
    ASSERT_TRUE(controller_);
  }

  void TearDownOnMainThread() override {
    controller_ = nullptr;
    InProcessBrowserTest::TearDownOnMainThread();
  }

  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }
  TabStripModel* strip() { return browser()->tab_strip_model(); }

  // Opens a loaded tab behind the selected one and returns its index.
  int OpenBackgroundTab() {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), embedded_test_server()->GetURL("/title1.html"),
        WindowOpenDisposition::NEW_BACKGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    return strip()->count() - 1;
  }

  const TabRecord* RecordOf(int index) {
    const std::string id =
        service_->adapter()->RecordIdForTab(strip()->GetTabAtIndex(index));
    return id.empty() ? nullptr : service_->model()->GetTab(id);
  }

  bool Asleep(int index) {
    content::WebContents* contents = strip()->GetWebContentsAt(index);
    const TabRecord* record = RecordOf(index);
    auto* lifecycle =
        resource_coordinator::TabLifecycleUnitExternal::FromWebContents(
            contents);
    return record && record->sleeping && contents->WasDiscarded() &&
           lifecycle &&
           lifecycle->GetTabState() == ::mojom::LifecycleUnitState::DISCARDED;
  }

  // Moves the controllable clock and lets the schedule look at it.
  void AdvanceClock(base::TimeDelta by) {
    clock_.Advance(by);
    controller_->schedule()->RunDueNow();
  }

  template <typename Predicate>
  bool WaitFor(Predicate done) {
    const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(10);
    while (!done() && base::TimeTicks::Now() < deadline) {
      base::RunLoop loop;
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE, loop.QuitClosure(), base::Milliseconds(20));
      loop.Run();
    }
    return done();
  }

  base::SimpleTestTickClock clock_;
  raw_ptr<TabModelService> service_ = nullptr;
  raw_ptr<SleepController> controller_ = nullptr;
};

IN_PROC_BROWSER_TEST_F(AerieSleepControllerBrowserTest,
                       AfterFifteenMinutesABackgroundTabSleepsAndTheSelectedOneDoesNot) {
  prefs()->SetInteger(kSleepAfterMinutes, 15);
  const int background = OpenBackgroundTab();
  ASSERT_EQ(background, 1);

  AdvanceClock(base::Minutes(14));
  EXPECT_FALSE(RecordOf(background)->sleeping);
  EXPECT_FALSE(strip()->GetWebContentsAt(background)->WasDiscarded());

  AdvanceClock(base::Minutes(1));
  EXPECT_TRUE(WaitFor([&] { return Asleep(background); }));
  // The selected tab never sleeps.
  EXPECT_FALSE(RecordOf(0)->sleeping);
  EXPECT_FALSE(strip()->GetWebContentsAt(0)->WasDiscarded());
  EXPECT_FALSE(controller_->schedule()->IsTracked(
      strip()->GetTabAtIndex(0)->GetHandle().raw_value()));
}

IN_PROC_BROWSER_TEST_F(AerieSleepControllerBrowserTest,
                       NeverStopsSleepingAndAChangeRearmsTheTimerWithoutARestart) {
  prefs()->SetInteger(kSleepAfterMinutes, 0);
  const int background = OpenBackgroundTab();
  EXPECT_FALSE(controller_->schedule()->timer_pending());
  AdvanceClock(base::Hours(12));
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(RecordOf(background)->sleeping);
  EXPECT_FALSE(strip()->GetWebContentsAt(background)->WasDiscarded());

  // Back to 15 minutes while the browser runs: the timer is armed at once, and
  // the tab, idle for 12 hours, is due.
  prefs()->SetInteger(kSleepAfterMinutes, 15);
  EXPECT_TRUE(controller_->schedule()->timer_pending());
  AdvanceClock(base::Minutes(1));
  EXPECT_TRUE(WaitFor([&] { return Asleep(background); }));
}

IN_PROC_BROWSER_TEST_F(AerieSleepControllerBrowserTest,
                       OneTimerServesManyTabs) {
  prefs()->SetInteger(kSleepAfterMinutes, 60);
  OpenBackgroundTab();
  OpenBackgroundTab();
  OpenBackgroundTab();
  EXPECT_EQ(controller_->schedule()->tracked_count(), 3u);
  EXPECT_TRUE(controller_->schedule()->timer_pending());
}

IN_PROC_BROWSER_TEST_F(AerieSleepControllerBrowserTest,
                       SleepNowDiscardsAtOnceAndSelectingTheTabWakesIt) {
  const int background = OpenBackgroundTab();
  tabs::TabInterface* tab = strip()->GetTabAtIndex(background);
  ASSERT_EQ(controller_->GetEligibility(tab).state,
            SleepController::Eligibility::State::kCanSleep);

  EXPECT_TRUE(controller_->Sleep(tab));
  EXPECT_TRUE(WaitFor([&] { return Asleep(background); }));
  EXPECT_EQ(controller_->GetEligibility(strip()->GetTabAtIndex(background)).state,
            SleepController::Eligibility::State::kAlreadyAsleep);

  strip()->ActivateTabAt(background);
  EXPECT_TRUE(WaitFor([&] {
    return !RecordOf(background)->sleeping &&
           !Asleep(background) &&
           !strip()->GetWebContentsAt(background)->GetController()
                .NeedsReload();
  }));
}

IN_PROC_BROWSER_TEST_F(AerieSleepControllerBrowserTest,
                       WakeSelectsTheTabAndTheRecordIsAwakeAgain) {
  const int background = OpenBackgroundTab();
  ASSERT_TRUE(controller_->Sleep(strip()->GetTabAtIndex(background)));
  ASSERT_TRUE(WaitFor([&] { return Asleep(background); }));
  controller_->Wake(strip()->GetTabAtIndex(background));
  EXPECT_EQ(strip()->active_index(), background);
  EXPECT_FALSE(RecordOf(background)->sleeping);
  // The tab that was selected is tracked now.
  EXPECT_TRUE(controller_->schedule()->IsTracked(
      strip()->GetTabAtIndex(0)->GetHandle().raw_value()));
}

IN_PROC_BROWSER_TEST_F(AerieSleepControllerBrowserTest,
                       ATabThatChromiumDiscardsItselfIsShownAsAsleep) {
  const int background = OpenBackgroundTab();
  content::WebContents* contents = strip()->GetWebContentsAt(background);
  auto* lifecycle =
      resource_coordinator::TabLifecycleUnitExternal::FromWebContents(contents);
  ASSERT_TRUE(lifecycle);
  // The same call the memory-pressure policy makes, not ours.
  ASSERT_TRUE(lifecycle->DiscardTab(::mojom::LifecycleUnitDiscardReason::URGENT));
  EXPECT_TRUE(WaitFor([&] { return Asleep(background); }));
  EXPECT_FALSE(controller_->schedule()->IsTracked(
      strip()->GetTabAtIndex(background)->GetHandle().raw_value()));
}

IN_PROC_BROWSER_TEST_F(AerieSleepControllerBrowserTest,
                       TheSelectedTabCannotSleepKeepsItsRecordAndSaysWhy) {
  tabs::TabInterface* selected = strip()->GetTabAtIndex(0);
  const SleepController::Eligibility eligibility =
      controller_->GetEligibility(selected);
  EXPECT_EQ(eligibility.state, SleepController::Eligibility::State::kCannotSleep);
  EXPECT_EQ(eligibility.cause, SleepController::Eligibility::Cause::kSelectedTab);
  EXPECT_FALSE(controller_->Sleep(selected));
  EXPECT_FALSE(RecordOf(0)->sleeping);
  EXPECT_FALSE(strip()->GetWebContentsAt(0)->WasDiscarded());
}

IN_PROC_BROWSER_TEST_F(AerieSleepControllerBrowserTest,
                       AerieTabsDoNotCarryTheChromiumPinnedBitSoTheyCanSleep) {
  // Pinned and Favorites tabs are Tab Model sections, not Chromium's pinned
  // bit, which Chromium refuses to discard (#176).
  const int background = OpenBackgroundTab();
  EXPECT_FALSE(strip()->IsTabPinned(background));
  EXPECT_EQ(controller_->GetEligibility(strip()->GetTabAtIndex(background)).state,
            SleepController::Eligibility::State::kCanSleep);
}

}  // namespace aerie_tab_model
