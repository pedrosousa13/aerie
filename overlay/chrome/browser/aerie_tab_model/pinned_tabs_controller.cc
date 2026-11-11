// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_tab_model/pinned_tabs_controller.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/aerie_tab_model/pinned_close_hook.h"
#include "chrome/browser/aerie_tab_model/sleep_controller.h"
#include "chrome/browser/aerie_tab_model/sleep_controller_factory.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_tabrestore.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_prefs.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/core/serialized_navigation_entry.h"
#include "components/sessions/core/session_types.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace aerie_tab_model {

namespace {

// How long a reset waits for the pinned page to finish loading before the tab
// is put to sleep anyway.
constexpr base::TimeDelta kResetTimeout = base::Seconds(20);

}  // namespace

// Navigates a tab to its pinned URL, replacing the history entry, and puts it
// to sleep when the page has stopped loading (the first press of "Reset, sleep,
// switch"). It deletes itself through the controller.
class PinnedTabsController::ResetThenSleep : public content::WebContentsObserver {
 public:
  ResetThenSleep(PinnedTabsController* owner,
                 tabs::TabInterface* tab,
                 const GURL& url)
      : content::WebContentsObserver(tab->GetContents()),
        owner_(owner),
        handle_(tab->GetHandle()) {
    timeout_.Start(FROM_HERE, kResetTimeout,
                   base::BindOnce(&ResetThenSleep::Finish,
                                  base::Unretained(this)));
    content::NavigationController::LoadURLParams params(url);
    params.should_replace_current_entry = true;
    params.transition_type = ui::PAGE_TRANSITION_AUTO_BOOKMARK;
    web_contents()->GetController().LoadURLWithParams(params);
  }

  tabs::TabHandle handle() const { return handle_; }

  // content::WebContentsObserver:
  void DidStopLoading() override { Finish(); }
  void WebContentsDestroyed() override { Finish(); }

 private:
  void Finish() {
    if (finished_) {
      return;
    }
    finished_ = true;
    timeout_.Stop();
    owner_->FinishReset(this);
  }

  const raw_ptr<PinnedTabsController> owner_;
  const tabs::TabHandle handle_;
  bool finished_ = false;
  base::OneShotTimer timeout_;
};

PinnedTabsController::PinnedTabsController(Profile* profile,
                                           TabModel* model,
                                           TabStripAdapter* adapter)
    : profile_(profile), model_(model), adapter_(adapter) {
  model_observation_.Observe(model_);
  browser_collection_observation_.Observe(GlobalBrowserCollection::GetInstance());
  // A Pinned or Favorites tab that went away for a reason other than the
  // window closing comes back (the record is still there).
  adapter_->SetPinnedTabGoneCallback(base::BindRepeating(
      &PinnedTabsController::ScheduleRestore, weak_factory_.GetWeakPtr()));
}

PinnedTabsController::~PinnedTabsController() {
  adapter_->SetPinnedTabGoneCallback(base::RepeatingClosure());
}

const TabRecord* PinnedTabsController::RecordFor(tabs::TabInterface* tab) const {
  const std::string id = adapter_->RecordIdForTab(tab);
  return id.empty() ? nullptr : model_->GetTab(id);
}

bool PinnedTabsController::IsPinnedOrFavorite(tabs::TabInterface* tab) const {
  const TabRecord* record = RecordFor(tab);
  return record && (record->section == Section::kPinned ||
                    record->section == Section::kFavorite);
}

GURL PinnedTabsController::CommittedUrl(tabs::TabInterface* tab) const {
  content::WebContents* contents = tab->GetContents();
  return contents ? contents->GetLastCommittedURL() : GURL();
}

PinnedCloseMode PinnedTabsController::Mode() const {
  return ParsePinnedCloseMode(profile_->GetPrefs()->GetString(kPinnedClose));
}

void PinnedTabsController::OnModelLoaded() {
  loaded_ = true;
  ScheduleRestore();
}

void PinnedTabsController::OnBrowserCreated(BrowserWindowInterface*) {
  ScheduleRestore();
}

void PinnedTabsController::ScheduleRestore() {
  if (restore_scheduled_) {
    return;
  }
  restore_scheduled_ = true;
  // After the call that made the window or removed the tab has returned: a
  // window that is being created has no web contents size yet.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<PinnedTabsController> self) {
                       if (self) {
                         self->restore_scheduled_ = false;
                         self->RestoreMissingTabs();
                       }
                     },
                     weak_factory_.GetWeakPtr()));
}

BrowserWindowInterface* PinnedTabsController::RestoreTarget() const {
  BrowserWindowInterface* target = nullptr;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* browser) {
        if (browser->GetProfile() == profile_ &&
            browser->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
            browser->GetWindow()) {
          target = browser;
          return false;
        }
        return true;
      });
  return target;
}

std::vector<std::string> PinnedTabsController::RecordsWithoutTab() const {
  std::vector<std::string> ids;
  for (Section section : {Section::kFavorite, Section::kPinned}) {
    for (const std::string& id : SectionTabIds(*model_, section)) {
      const TabRecord* record = model_->GetTab(id);
      if (record && record->pinned_url && !adapter_->TabForRecord(id)) {
        ids.push_back(id);
      }
    }
  }
  return ids;
}

void PinnedTabsController::RestoreMissingTabs() {
  if (!loaded_) {
    return;
  }
  const std::vector<std::string> ids = RecordsWithoutTab();
  if (ids.empty()) {
    return;
  }
  BrowserWindowInterface* browser = RestoreTarget();
  if (!browser) {
    return;
  }
  for (const std::string& id : ids) {
    const TabRecord* record = model_->GetTab(id);
    const GURL url(*record->pinned_url);
    if (!url.is_valid()) {
      continue;
    }
    sessions::SerializedNavigationEntry navigation;
    navigation.set_index(0);
    navigation.set_virtual_url(url);
    navigation.set_original_request_url(url);
    if (record->last_title) {
      navigation.set_title(base::UTF8ToUTF16(*record->last_title));
    }
    // The adapter joins the tab that this call inserts to the record, instead
    // of making it a new Today tab.
    adapter_->SetPendingRestoreRecord(id);
    chrome::AddRestoredTab(
        browser, base::span_from_ref(navigation),
        browser->GetTabStripModel()->count(), /*selected_navigation=*/0,
        /*extension_app_id=*/std::string(), /*group=*/std::nullopt,
        /*select=*/false, /*pin=*/false,
        /*last_active_time_ticks=*/base::TimeTicks(),
        /*last_active_time=*/base::Time(), /*storage_namespace=*/nullptr,
        sessions::SerializedUserAgentOverride(), /*extra_data=*/{},
        /*from_session_restore=*/false, /*is_active_browser=*/std::nullopt,
        chrome::RestoredTabLoadPolicy::kDeferUntilActivation);
    adapter_->SetPendingRestoreRecord(std::string());
    // Not loaded: the sidebar shows it as asleep until it is selected (the
    // decision of issue #180: one look for "not loaded", the moon badge).
    model_->UpdateTab(id, [](TabRecord& tab) { tab.sleeping = true; });
  }
}

PinnedCloseAction PinnedTabsController::ActionFor(tabs::TabInterface* tab) const {
  const TabRecord* record = RecordFor(tab);
  if (!record) {
    return PinnedCloseAction::kUnpinClose;
  }
  const bool at_pinned =
      !record->pinned_url ||
      IsAtPinnedUrl(CommittedUrl(tab), GURL(*record->pinned_url));
  return DecidePinnedClose(Mode(), record->sleeping, at_pinned);
}

bool PinnedTabsController::HandleClose(tabs::TabInterface* tab) {
  if (!IsPinnedOrFavorite(tab)) {
    return false;
  }
  const std::string id = adapter_->RecordIdForTab(tab);
  const TabRecord* record = model_->GetTab(id);
  switch (ActionFor(tab)) {
    case PinnedCloseAction::kUnpinClose:
      // The record goes to Today first, so that the adapter removes it with the
      // tab. Once the archive exists (slice F) this is where the tab lands in
      // it; until then it closes.
      UnpinTab(*model_, id);
      tab->Close();
      return true;
    case PinnedCloseAction::kSleepSwitch:
      SwitchAwayFrom(tab);
      SleepTab(tab);
      return true;
    case PinnedCloseAction::kResetSleepSwitch: {
      const bool at_pinned =
          !record->pinned_url ||
          IsAtPinnedUrl(CommittedUrl(tab), GURL(*record->pinned_url));
      SwitchAwayFrom(tab);
      if (at_pinned) {
        SleepTab(tab);
      } else {
        resets_.push_back(std::make_unique<ResetThenSleep>(
            this, tab, GURL(*record->pinned_url)));
      }
      return true;
    }
  }
  return true;
}

void PinnedTabsController::FinishReset(ResetThenSleep* reset) {
  auto it = std::ranges::find(resets_, reset,
                              &std::unique_ptr<ResetThenSleep>::get);
  if (it == resets_.end()) {
    return;
  }
  std::unique_ptr<ResetThenSleep> owned = std::move(*it);
  resets_.erase(it);
  // Sleeping destroys the page's WebContents, so it must not run inside the
  // WebContentsObserver notification that got us here: it runs in a task.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<PinnedTabsController> self, tabs::TabHandle handle) {
            tabs::TabInterface* tab = handle.Get();
            // The selected tab cannot sleep; the user came back to it.
            if (self && tab && !tab->IsActivated()) {
              self->SleepTab(tab);
            }
          },
          weak_factory_.GetWeakPtr(), owned->handle()));
  // Not inside the observer's own callback.
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                             std::move(owned));
}

void PinnedTabsController::SleepTab(tabs::TabInterface* tab) {
  if (auto* sleep = SleepControllerFactory::GetForProfile(profile_)) {
    sleep->Sleep(tab);
  }
}

void PinnedTabsController::SwitchAwayFrom(tabs::TabInterface* tab) {
  if (!tab->IsActivated()) {
    return;
  }
  BrowserWindowInterface* browser = tab->GetBrowserWindowInterface();
  if (!browser) {
    return;
  }
  TabStripModel* strip = browser->GetTabStripModel();
  const std::string id = adapter_->RecordIdForTab(tab);
  // The sidebar order, limited to the tabs this window has.
  std::vector<std::string> order;
  for (const std::string& candidate : SidebarTabOrder(*model_)) {
    tabs::TabInterface* other =
        candidate == id ? tab : adapter_->TabForRecord(candidate);
    if (other && strip->GetIndexOfTab(other) != TabStripModel::kNoTab) {
      order.push_back(candidate);
    }
  }
  const std::string next = PickNextTab(order, id);
  if (!next.empty()) {
    strip->ActivateTabAt(strip->GetIndexOfTab(adapter_->TabForRecord(next)));
    return;
  }
  // The only tab of the window: a new tab to switch to.
  browser->OpenGURL(GURL("chrome://newtab/"),
                    WindowOpenDisposition::NEW_FOREGROUND_TAB);
}

bool PinnedTabsController::IsAwayFromPinnedPage(tabs::TabInterface* tab) const {
  const TabRecord* record = RecordFor(tab);
  if (!record || record->section != Section::kPinned || !record->pinned_url) {
    return false;
  }
  return !IsAtPinnedUrl(CommittedUrl(tab), GURL(*record->pinned_url));
}

void PinnedTabsController::GoBackToPinnedPage(tabs::TabInterface* tab) {
  const TabRecord* record = RecordFor(tab);
  if (!record || !record->pinned_url) {
    return;
  }
  if (BrowserWindowInterface* browser = tab->GetBrowserWindowInterface()) {
    TabStripModel* strip = browser->GetTabStripModel();
    strip->ActivateTabAt(strip->GetIndexOfTab(tab));
  }
  content::NavigationController::LoadURLParams params{GURL(*record->pinned_url)};
  params.transition_type = ui::PAGE_TRANSITION_AUTO_BOOKMARK;
  tab->GetContents()->GetController().LoadURLWithParams(params);
}

bool PinnedTabsController::Pin(tabs::TabInterface* tab) {
  const std::string id = adapter_->RecordIdForTab(tab);
  if (id.empty()) {
    return false;
  }
  GURL url = CommittedUrl(tab);
  if (url.is_empty()) {
    url = tab->GetURL();
  }
  return PinTab(*model_, id, url.spec());
}

bool PinnedTabsController::Unpin(tabs::TabInterface* tab) {
  const std::string id = adapter_->RecordIdForTab(tab);
  return !id.empty() && UnpinTab(*model_, id);
}

FavoriteResult PinnedTabsController::AddToFavorites(tabs::TabInterface* tab) {
  const std::string id = adapter_->RecordIdForTab(tab);
  if (id.empty()) {
    return FavoriteResult::kUnknownTab;
  }
  GURL url = CommittedUrl(tab);
  if (url.is_empty()) {
    url = tab->GetURL();
  }
  return aerie_tab_model::AddToFavorites(*model_, id, url.spec());
}

bool PinnedTabsController::RemoveFromFavorites(tabs::TabInterface* tab) {
  const std::string id = adapter_->RecordIdForTab(tab);
  return !id.empty() && aerie_tab_model::RemoveFromFavorites(*model_, id);
}

bool HandlePinnedClose(tabs::TabInterface* tab) {
  Profile* profile = tab->GetProfile();
  auto* service = profile ? TabModelServiceFactory::GetForProfile(profile)
                          : nullptr;
  if (!service || !service->pinned_tabs()) {
    return false;
  }
  return service->pinned_tabs()->HandleClose(tab);
}

}  // namespace aerie_tab_model
