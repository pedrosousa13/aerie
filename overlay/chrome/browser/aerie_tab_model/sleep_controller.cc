// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_tab_model/sleep_controller.h"

#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/performance_manager/policies/cannot_discard_reason.h"
#include "chrome/browser/performance_manager/policies/discard_eligibility_policy.h"
#include "chrome/browser/performance_manager/public/user_tuning/user_tuning_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/resource_coordinator/lifecycle_unit_state.mojom.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/tab_list/tab_removed_reason.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_prefs.h"
#include "components/performance_manager/public/graph/page_node.h"
#include "components/performance_manager/public/performance_manager.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"

namespace aerie_tab_model {

namespace {

using DiscardEligibilityPolicy =
    performance_manager::policies::DiscardEligibilityPolicy;

// "Asleep" is a discarded page that has not been loaded again. Chromium's
// discard does not set "needs reload" (the tab loads when it is selected), and a
// discarded tab keeps WasDiscarded() after it loads, so the lifecycle state is
// what tells.
bool IsAsleep(content::WebContents* contents) {
  if (!contents || !contents->WasDiscarded()) {
    return false;
  }
  if (auto* lifecycle =
          resource_coordinator::TabLifecycleUnitExternal::FromWebContents(
              contents)) {
    return lifecycle->GetTabState() ==
           ::mojom::LifecycleUnitState::DISCARDED;
  }
  return contents->GetController().NeedsReload();
}

DiscardEligibilityPolicy* GetPolicy() {
  if (!performance_manager::PerformanceManager::IsAvailable()) {
    return nullptr;
  }
  return DiscardEligibilityPolicy::GetFromGraph(
      performance_manager::PerformanceManager::GetGraph());
}

}  // namespace

SleepController::SleepController(Profile* profile,
                                 TabModelService* service,
                                 const base::TickClock* clock)
    : profile_(profile),
      service_(service),
      clock_(clock),
      schedule_(clock,
                base::BindRepeating(&SleepController::OnDue,
                                    base::Unretained(this))) {
  schedule_.SetDelay(service_->sleep_after());
  pref_registrar_.Init(profile_->GetPrefs());
  pref_registrar_.Add(kSleepAfterMinutes,
                      base::BindRepeating(&SleepController::OnSleepAfterChanged,
                                          base::Unretained(this)));
  // Windows that exist already, then the ones that open later.
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [this](BrowserWindowInterface* browser) {
        OnBrowserCreated(browser);
        return true;
      });
  browser_collection_observation_.Observe(GlobalBrowserCollection::GetInstance());
}

SleepController::~SleepController() = default;

void SleepController::Shutdown() {
  browser_collection_observation_.Reset();
  pref_registrar_.RemoveAll();
}

bool SleepController::Watches(BrowserWindowInterface* browser) const {
  return browser->GetProfile() == profile_ &&
         browser->GetType() == BrowserWindowInterface::TYPE_NORMAL;
}

void SleepController::OnBrowserCreated(BrowserWindowInterface* browser) {
  if (!Watches(browser)) {
    return;
  }
  TabStripModel* strip = browser->GetTabStripModel();
  strip->AddObserver(this);
  for (int i = 0; i < strip->count(); ++i) {
    Track(strip->GetTabAtIndex(i));
  }
}

void SleepController::OnBrowserClosed(BrowserWindowInterface* browser) {
  if (!Watches(browser)) {
    return;
  }
  browser->GetTabStripModel()->RemoveObserver(this);
}

void SleepController::Track(tabs::TabInterface* tab) {
  const SleepSchedule::Key key = tab->GetHandle().raw_value();
  content::WebContents* contents = tab->GetContents();
  if (!contents || tab->IsActivated()) {
    schedule_.Untrack(key);
    return;
  }
  if (IsAsleep(contents)) {
    schedule_.Untrack(key);
    SetSleeping(tab, true);
    return;
  }
  // A tab that is tracked already keeps the time it has idled.
  if (!schedule_.IsTracked(key)) {
    schedule_.Touch(key);
  }
}

void SleepController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  switch (change.type()) {
    case TabStripModelChange::kInserted:
      for (const auto& inserted : change.GetInsert()->contents) {
        if (inserted.tab == selection.new_tab) {
          schedule_.Untrack(inserted.tab->GetHandle().raw_value());
        } else {
          Track(inserted.tab);
        }
      }
      break;
    case TabStripModelChange::kRemoved:
      for (const auto& removed : change.GetRemove()->contents) {
        const tabs::TabHandle handle = removed.tab->GetHandle();
        // A tab that goes to another window is tracked again by that window.
        schedule_.Untrack(handle.raw_value());
        if (TabRemoveReasonUtils::WillDeleteTab(removed.remove_reason)) {
          pending_estimates_.erase(handle);
          estimates_.erase(handle);
        }
      }
      break;
    case TabStripModelChange::kReplaced: {
      // A discard (ours, or Chromium's for memory) replaces the contents of the
      // same tab with a page that is not loaded.
      const auto* replace = change.GetReplace();
      if (IsAsleep(replace->new_contents)) {
        schedule_.Untrack(replace->tab->GetHandle().raw_value());
        SetSleeping(replace->tab, true);
      }
      break;
    }
    case TabStripModelChange::kMoved:
    case TabStripModelChange::kSelectionOnly:
      break;
  }

  if (selection.active_tab_changed() && selection.old_tab != selection.new_tab) {
    // The tab that was selected starts to idle, unless it is gone.
    bool old_removed = false;
    if (change.type() == TabStripModelChange::kRemoved) {
      for (const auto& removed : change.GetRemove()->contents) {
        old_removed |= removed.tab == selection.old_tab;
      }
    }
    if (selection.old_tab && !old_removed) {
      Track(selection.old_tab);
    }
    // Selecting a tab wakes it: Chromium reloads a discarded tab that gets
    // selected, and the record follows.
    if (selection.new_tab) {
      schedule_.Untrack(selection.new_tab->GetHandle().raw_value());
      SetSleeping(selection.new_tab, false);
    }
  }
}

void SleepController::OnTabChangedAt(tabs::TabInterface* tab,
                                     TabChangeType change_type) {
  // Audio resets the idle time of a tab that is tracked. A tab that plays audio
  // is not discarded either (the eligibility check below says so), so a missed
  // event costs one more delay and no wrong sleep.
  const SleepSchedule::Key key = tab->GetHandle().raw_value();
  content::WebContents* contents = tab->GetContents();
  if (schedule_.IsTracked(key) && contents && contents->IsCurrentlyAudible()) {
    schedule_.Touch(key);
  }
  // Chromium tells the strip once its discard is done (and a discard that keeps
  // the contents sends no replace at all): this is the one place that sees both.
  if (IsAsleep(contents)) {
    schedule_.Untrack(key);
    SetSleeping(tab, true);
  }
}

bool SleepController::OnDue(SleepSchedule::Key key) {
  tabs::TabInterface* tab = tabs::TabHandle(key).Get();
  if (!tab) {
    return true;
  }
  return Sleep(tab);
}

void SleepController::OnSleepAfterChanged() {
  schedule_.SetDelay(service_->sleep_after());
}

SleepController::Eligibility SleepController::GetEligibility(
    tabs::TabInterface* tab) const {
  Eligibility result;
  content::WebContents* contents = tab->GetContents();
  if (!contents) {
    return result;
  }
  if (IsAsleep(contents)) {
    result.state = Eligibility::State::kAlreadyAsleep;
    result.cause = Eligibility::Cause::kNone;
    return result;
  }
  if (tab->IsActivated()) {
    result.cause = Eligibility::Cause::kSelectedTab;
    return result;
  }
  DiscardEligibilityPolicy* policy = GetPolicy();
  base::WeakPtr<performance_manager::PageNode> page_node =
      performance_manager::PerformanceManager::GetPrimaryPageNodeForWebContents(
          contents);
  if (!policy || !page_node) {
    return result;
  }
  // The same question as Helium's "Hibernate" menu item: eligible, with the
  // recent-visibility protection ignored (the user asked, or the idle time is
  // ours). Chromium's "protected" results count as no.
  std::vector<performance_manager::policies::CannotDiscardReason> reasons;
  const auto can_discard = policy->CanDiscard(
      page_node.get(), ::mojom::LifecycleUnitDiscardReason::EXTERNAL,
      /*ignore_recent_visibility=*/true, &reasons);
  if (can_discard ==
      performance_manager::policies::CanDiscardResult::kEligible) {
    result.state = Eligibility::State::kCanSleep;
    result.cause = Eligibility::Cause::kNone;
    return result;
  }
  result.cause = Eligibility::Cause::kChromium;
  for (auto reason : reasons) {
    result.reasons.push_back(
        performance_manager::policies::CannotDiscardReasonToString(reason));
  }
  return result;
}

bool SleepController::Sleep(tabs::TabInterface* tab) {
  const Eligibility eligibility = GetEligibility(tab);
  if (eligibility.state == Eligibility::State::kAlreadyAsleep) {
    SetSleeping(tab, true);
    return true;
  }
  if (eligibility.state != Eligibility::State::kCanSleep) {
    return false;
  }
  const tabs::TabHandle handle = tab->GetHandle();
  base::WeakPtr<performance_manager::PageNode> page_node =
      performance_manager::PerformanceManager::GetPrimaryPageNodeForWebContents(
          tab->GetContents());
  if (!page_node) {
    return false;
  }
  pending_estimates_[handle] = static_cast<int64_t>(
      page_node->EstimatePrivateFootprintSize().InBytes());
  performance_manager::user_tuning::DiscardPage(
      page_node.get(), ::mojom::LifecycleUnitDiscardReason::EXTERNAL,
      /*ignore_minimum_time_in_background=*/true);
  // The tab strip tells the replaced contents (OnTabStripModelChanged), which
  // marks the record. A discard that finished already is marked here too.
  if (IsAsleep(tab->GetContents())) {
    schedule_.Untrack(handle.raw_value());
    SetSleeping(tab, true);
    return true;
  }
  pending_estimates_.erase(handle);
  return false;
}

void SleepController::Wake(tabs::TabInterface* tab) {
  // Chromium reloads a discarded tab when it is selected; there is no other
  // path to wake it, so waking is selecting.
  BrowserWindowInterface* browser = tab->GetBrowserWindowInterface();
  if (!browser) {
    return;
  }
  TabStripModel* strip = browser->GetTabStripModel();
  strip->ActivateTabAt(strip->GetIndexOfTab(tab));
}

int64_t SleepController::sleeping_memory_estimate_bytes() const {
  int64_t total = 0;
  for (const auto& [handle, bytes] : estimates_) {
    total += bytes;
  }
  return total;
}

void SleepController::SetSleeping(tabs::TabInterface* tab, bool sleeping) {
  const tabs::TabHandle handle = tab->GetHandle();
  if (sleeping) {
    auto pending = pending_estimates_.find(handle);
    int64_t bytes = 0;
    if (pending != pending_estimates_.end()) {
      bytes = pending->second;
      pending_estimates_.erase(pending);
    }
    if (!estimates_.contains(handle) || bytes > 0) {
      estimates_[handle] = bytes;
    }
  } else {
    estimates_.erase(handle);
    pending_estimates_.erase(handle);
  }
  TabStripAdapter* adapter = service_->adapter();
  if (!adapter) {
    return;
  }
  const std::string id = adapter->RecordIdForTab(tab);
  const TabRecord* record = id.empty() ? nullptr : service_->model()->GetTab(id);
  if (record && record->sleeping != sleeping) {
    service_->model()->UpdateTab(
        id, [sleeping](TabRecord& tab) { tab.sleeping = sleeping; });
  }
}

}  // namespace aerie_tab_model
