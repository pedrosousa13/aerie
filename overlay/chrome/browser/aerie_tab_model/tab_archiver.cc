// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_tab_model/tab_archiver.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/power_monitor/power_monitor.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_tabrestore.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/aerie_tab_model/archive.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_prefs.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/core/serialized_navigation_entry.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"

namespace aerie_tab_model {

namespace {

// The page the tab shows, its title, and the last entries of its history up to
// the one shown. Forward entries are dropped: the restored tab opens on the page
// that was shown.
ArchiveData Capture(tabs::TabInterface* tab) {
  ArchiveData data;
  content::WebContents* contents = tab->GetContents();
  data.title = base::UTF16ToUTF8(tab->GetTitle());
  content::NavigationController& controller = contents->GetController();
  const int current = controller.GetCurrentEntryIndex();
  if (current >= 0) {
    const int first = std::max(
        0, current - static_cast<int>(kMaxArchivedNavigationEntries) + 1);
    for (int i = first; i <= current; ++i) {
      content::NavigationEntry* entry = controller.GetEntryAtIndex(i);
      if (!entry) {
        continue;
      }
      data.entries.push_back({entry->GetVirtualURL().spec(),
                              base::UTF16ToUTF8(entry->GetTitleForDisplay())});
    }
  }
  if (data.entries.empty()) {
    data.entries.push_back({contents->GetVisibleURL().spec(), data.title});
  }
  data.url = data.entries.back().url;
  // The favicon service looks a favicon up by the address of its page.
  data.favicon_key = data.url;
  if (data.title.empty()) {
    data.title = data.entries.back().title;
  }
  return data;
}

}  // namespace

TabArchiver::TabArchiver(Profile* profile,
                         TabModelService* service,
                         const base::Clock* clock,
                         const base::TickClock* tick_clock)
    : profile_(profile),
      service_(service),
      clock_(clock),
      schedule_(service->model(),
                clock,
                tick_clock,
                base::BindRepeating(&TabArchiver::OnDue, base::Unretained(this)),
                base::BindRepeating(&TabArchiver::IsExempt,
                                    base::Unretained(this))),
      created_(tick_clock->NowTicks()),
      tick_clock_(tick_clock) {
  pref_registrar_.Init(profile_->GetPrefs());
  pref_registrar_.Add(kArchiveAfter,
                      base::BindRepeating(&TabArchiver::OnSettingsChanged,
                                          base::Unretained(this)));
  pref_registrar_.Add(kArchiveKeep,
                      base::BindRepeating(&TabArchiver::OnSettingsChanged,
                                          base::Unretained(this)));
  schedule_.SetKeep(GetArchiveKeep(*profile_->GetPrefs()));
  // The first arming: the delay may be the one that was already overdue.
  schedule_.SetArchiveAfter(service_->archive_after());
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [this](BrowserWindowInterface* browser) {
        OnBrowserCreated(browser);
        return true;
      });
  browser_collection_observation_.Observe(GlobalBrowserCollection::GetInstance());
  if (base::PowerMonitor::GetInstance()->IsInitialized()) {
    base::PowerMonitor::GetInstance()->AddPowerSuspendObserver(this);
  }
}

TabArchiver::~TabArchiver() = default;

void TabArchiver::Shutdown() {
  browser_collection_observation_.Reset();
  pref_registrar_.RemoveAll();
  if (base::PowerMonitor::GetInstance()->IsInitialized()) {
    base::PowerMonitor::GetInstance()->RemovePowerSuspendObserver(this);
  }
}

void TabArchiver::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void TabArchiver::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

bool TabArchiver::Watches(BrowserWindowInterface* browser) const {
  return browser->GetProfile() == profile_ &&
         browser->GetType() == BrowserWindowInterface::TYPE_NORMAL;
}

void TabArchiver::OnBrowserCreated(BrowserWindowInterface* browser) {
  if (!Watches(browser)) {
    return;
  }
  TabStripModel* strip = browser->GetTabStripModel();
  strip->AddObserver(this);
  for (int i = 0; i < strip->count(); ++i) {
    tabs::TabInterface* tab = strip->GetTabAtIndex(i);
    if (tab->GetContents() && tab->GetContents()->IsCurrentlyAudible()) {
      audible_[tab->GetHandle()] = true;
    }
  }
}

void TabArchiver::OnBrowserClosed(BrowserWindowInterface* browser) {
  if (!Watches(browser)) {
    return;
  }
  browser->GetTabStripModel()->RemoveObserver(this);
}

void TabArchiver::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (change.type() == TabStripModelChange::kRemoved) {
    for (const auto& removed : change.GetRemove()->contents) {
      audible_.erase(removed.tab->GetHandle());
    }
  }
  // A tab that is selected, or stops being, changes the exemption. The
  // adapter writes `last_active` for both, which also arms the schedule; this
  // covers a selection change that writes nothing (no record yet).
  if (selection.active_tab_changed()) {
    schedule_.Rearm();
  }
}

void TabArchiver::OnTabChangedAt(tabs::TabInterface* tab,
                                 TabChangeType change_type) {
  NoteAudio(tab);
}

void TabArchiver::NoteAudio(tabs::TabInterface* tab) {
  content::WebContents* contents = tab->GetContents();
  const bool audible = contents && contents->IsCurrentlyAudible();
  auto it = audible_.find(tab->GetHandle());
  const bool was_audible = it != audible_.end() && it->second;
  if (audible == was_audible) {
    return;
  }
  if (audible) {
    audible_[tab->GetHandle()] = true;
    schedule_.Rearm();
    return;
  }
  audible_.erase(tab->GetHandle());
  // The sound stopped: the tab was in use until now.
  TabStripAdapter* adapter = service_->adapter();
  const std::string id = adapter ? adapter->RecordIdForTab(tab) : std::string();
  if (id.empty() || !service_->model()->GetTab(id)) {
    return;
  }
  const base::Time now = clock_->Now();
  service_->model()->UpdateTab(
      id, [now](TabRecord& record) { record.last_active = now; });
  schedule_.Rearm();
}

void TabArchiver::OnResume() {
  // The timer does not count the time asleep; the records' wall-clock times do.
  schedule_.Rearm();
}

void TabArchiver::OnSettingsChanged() {
  schedule_.SetKeep(GetArchiveKeep(*profile_->GetPrefs()));
  schedule_.SetArchiveAfter(service_->archive_after());
}

bool TabArchiver::IsExempt(const std::string& record_id) const {
  TabStripAdapter* adapter = service_->adapter();
  tabs::TabInterface* tab =
      adapter ? adapter->TabForRecord(record_id) : nullptr;
  if (!tab) {
    // No tab to close: the record waits for session restore.
    return true;
  }
  content::WebContents* contents = tab->GetContents();
  return tab->IsActivated() || !contents || contents->IsCurrentlyAudible();
}

bool TabArchiver::OnDue(const std::string& record_id) {
  TabStripAdapter* adapter = service_->adapter();
  tabs::TabInterface* tab =
      adapter ? adapter->TabForRecord(record_id) : nullptr;
  if (!tab) {
    return false;
  }
  // The tabs that were overdue when the browser started are archived without
  // motion.
  return ArchiveWithMotion(
      tab, tick_clock_->NowTicks() - created_ > kArchiveCatchUpWindow);
}

bool TabArchiver::Archive(tabs::TabInterface* tab) {
  return ArchiveWithMotion(tab, /*animate=*/true);
}

bool TabArchiver::ArchiveWithMotion(tabs::TabInterface* tab, bool animate) {
  TabStripAdapter* adapter = service_->adapter();
  TabModel* model = service_->model();
  content::WebContents* contents = tab->GetContents();
  const std::string id = adapter ? adapter->RecordIdForTab(tab) : std::string();
  const TabRecord* record = id.empty() ? nullptr : model->GetTab(id);
  if (!contents || !record || record->section != Section::kToday ||
      record->archived_at) {
    return false;
  }
  // A page that asks to confirm leaving is closed by the normal path, where
  // the user can answer. An automatic archive must never raise a dialog or
  // lose what the user was typing.
  if (contents->NeedToFireBeforeUnloadOrUnloadEvents()) {
    return false;
  }
  for (Observer& observer : observers_) {
    observer.OnTabArchiving(id);
  }
  const tabs::TabHandle handle = tab->GetHandle();
  if (!ArchiveTab(*model, id, Capture(tab), clock_->Now())) {
    for (Observer& observer : observers_) {
      observer.OnTabArchiveFailed(id);
    }
    return false;
  }
  // The record is written, so the row leaves Today; now the tab goes. Close()
  // is CLOSE_NONE: nothing is added to "Recently closed".
  tab->Close();
  if (handle.Get()) {
    // The tab did not close. Put the record back as it was.
    model->UpdateTab(id, [](TabRecord& tab_record) {
      tab_record.archived_at.reset();
      tab_record.archive.reset();
    });
    for (Observer& observer : observers_) {
      observer.OnTabArchiveFailed(id);
    }
    return false;
  }
  for (Observer& observer : observers_) {
    observer.OnTabArchived(id, animate);
  }
  return true;
}

content::WebContents* TabArchiver::Restore(const std::string& record_id,
                                           BrowserWindowInterface* browser) {
  TabModel* model = service_->model();
  TabStripAdapter* adapter = service_->adapter();
  const TabRecord* record = model->GetTab(record_id);
  if (!adapter || !browser || !record || !record->archived_at ||
      !record->archive || record->archive->entries.empty()) {
    return nullptr;
  }
  std::vector<sessions::SerializedNavigationEntry> navigations;
  for (const ArchivedNavigation& entry : record->archive->entries) {
    const GURL url(entry.url);
    sessions::SerializedNavigationEntry navigation;
    navigation.set_index(static_cast<int>(navigations.size()));
    navigation.set_virtual_url(url);
    navigation.set_original_request_url(url);
    navigation.set_title(base::UTF8ToUTF16(entry.title));
    navigations.push_back(std::move(navigation));
  }
  const int selected = static_cast<int>(navigations.size()) - 1;
  if (!RestoreArchivedTab(*model, record_id, clock_->Now())) {
    return nullptr;
  }
  // The strip slot: before the first Today tab of this window, so that the
  // strip and the sidebar list agree on "top of Today".
  TabStripModel* strip = browser->GetTabStripModel();
  int index = strip->count();
  for (int i = 0; i < strip->count(); ++i) {
    const std::string other = adapter->RecordIdForTab(strip->GetTabAtIndex(i));
    const TabRecord* other_record = other.empty() ? nullptr : model->GetTab(other);
    if (other_record && other_record->section == Section::kToday) {
      index = i;
      break;
    }
  }
  // The adapter joins the tab that this call inserts to the record, instead of
  // making it a new Today tab.
  adapter->SetPendingRestoreRecord(record_id);
  content::WebContents* contents = chrome::AddRestoredTab(
      browser, navigations, index, selected,
      /*extension_app_id=*/std::string(), /*group=*/std::nullopt,
      /*select=*/true, /*pin=*/false,
      /*last_active_time_ticks=*/base::TimeTicks(),
      /*last_active_time=*/base::Time(), /*storage_namespace=*/nullptr,
      sessions::SerializedUserAgentOverride(), /*extra_data=*/{},
      /*from_session_restore=*/false, /*is_active_browser=*/std::nullopt);
  adapter->SetPendingRestoreRecord(std::string());
  return contents;
}

}  // namespace aerie_tab_model
