// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"

#include <algorithm>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/aerie_tab_model/session_record_id_hook.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/tab_list/tab_removed_reason.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_types.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/web_contents.h"

namespace aerie_tab_model {

TabStripAdapter::TabStripAdapter(Profile* profile,
                                 TabModel* model,
                                 const base::Clock* clock)
    : profile_(profile), model_(model), clock_(clock) {
  model_observation_.Observe(model_);
  // Windows that exist already, then the ones that open later.
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [this](BrowserWindowInterface* browser) {
        OnBrowserCreated(browser);
        return true;
      });
  browser_collection_observation_.Observe(GlobalBrowserCollection::GetInstance());
}

TabStripAdapter::~TabStripAdapter() = default;

std::string TabStripAdapter::RecordIdForTab(
    const tabs::TabInterface* tab) const {
  auto it = ids_.find(tab->GetHandle());
  return it == ids_.end() ? std::string() : it->second;
}

std::string TabStripAdapter::SessionRecordIdForTab(
    const tabs::TabInterface* tab) const {
  const std::string id = RecordIdForTab(tab);
  const TabRecord* record = id.empty() ? nullptr : model_->GetTab(id);
  return record && record->section == Section::kToday ? id : std::string();
}

tabs::TabInterface* TabStripAdapter::TabForRecord(
    const std::string& record_id) const {
  for (const auto& [handle, id] : ids_) {
    if (id == record_id) {
      return handle.Get();
    }
  }
  return nullptr;
}

void TabStripAdapter::SetPendingRestoreRecord(std::string record_id) {
  pending_restore_record_ = std::move(record_id);
}

void TabStripAdapter::SetPinnedTabGoneCallback(
    base::RepeatingClosure callback) {
  pinned_tab_gone_ = std::move(callback);
}

void TabStripAdapter::SetTabChangedCallback(TabChangedCallback callback) {
  tab_changed_ = std::move(callback);
}

void TabStripAdapter::SetTabInsertedCallback(TabInsertedCallback callback) {
  tab_inserted_ = std::move(callback);
}

void TabStripAdapter::SetRecordKeptCallback(base::RepeatingClosure callback) {
  record_kept_ = std::move(callback);
}

bool TabStripAdapter::Watches(BrowserWindowInterface* browser) const {
  return browser->GetProfile() == profile_ &&
         browser->GetType() == BrowserWindowInterface::TYPE_NORMAL;
}

void TabStripAdapter::OnBrowserCreated(BrowserWindowInterface* browser) {
  if (!Watches(browser)) {
    return;
  }
  TabStripModel* strip = browser->GetTabStripModel();
  strip->AddObserver(this);
  for (int i = 0; i < strip->count(); ++i) {
    TabInserted(strip->GetTabAtIndex(i), i);
  }
}

void TabStripAdapter::OnBrowserClosed(BrowserWindowInterface* browser) {
  if (!Watches(browser)) {
    return;
  }
  browser->GetTabStripModel()->RemoveObserver(this);
}

void TabStripAdapter::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  switch (change.type()) {
    case TabStripModelChange::kInserted:
      for (const auto& inserted : change.GetInsert()->contents) {
        TabInserted(inserted.tab, inserted.index);
      }
      break;
    case TabStripModelChange::kRemoved:
      for (const auto& removed : change.GetRemove()->contents) {
        // A tab that goes to another window keeps its record.
        if (!TabRemoveReasonUtils::WillDeleteTab(removed.remove_reason)) {
          continue;
        }
        deferred_claims_.erase(removed.tab->GetHandle());
        auto it = ids_.find(removed.tab->GetHandle());
        if (it == ids_.end()) {
          continue;
        }
        const TabRecord* record = model_->GetTab(it->second);
        if (record && record->section != Section::kToday) {
          // Pinned and Favorites outlive their tab (issue #180). When the
          // window is not closing, the tab comes back.
          ids_.erase(it);
          if (!tab_strip_model->closing_all() && pinned_tab_gone_) {
            pinned_tab_gone_.Run();
          }
          continue;
        }
        if (record && KeepRecordWithClosingWindow(tab_strip_model, *record)) {
          // Chromium's session keeps this tab and brings it back, and the tab
          // then claims this record (issue #223). Until then the record waits
          // like a stored one, and has no tab, so no row and no count shows it.
          // If the process lives on and a window opens that is not a restore,
          // the next restore pass removes it.
          model_->MarkAwaitingRestore(record->id);
          ids_.erase(it);
          if (record_kept_) {
            record_kept_.Run();
          }
          continue;
        }
        model_->RemoveTab(it->second);
        ids_.erase(it);
      }
      break;
    case TabStripModelChange::kMoved: {
      // The moved tab and every tab it passed changed index: each record's
      // position follows its tab, so the Today list (ordered by position) shows
      // the order of the strip (issue #215, tab drag and drop).
      const TabStripModelChange::Move* move = change.GetMove();
      const int first = std::min(move->from_index, move->to_index);
      const int last = std::max(move->from_index, move->to_index);
      for (int index = first; index <= last; ++index) {
        tabs::TabInterface* tab = tab_strip_model->GetTabAtIndex(index);
        auto it = tab ? ids_.find(tab->GetHandle()) : ids_.end();
        const TabRecord* record =
            it == ids_.end() ? nullptr : model_->GetTab(it->second);
        // Only Today follows the strip (issue #180).
        if (record && record->section == Section::kToday &&
            record->position != index) {
          model_->MoveTab(record->id, record->section, record->parent, index);
        }
      }
      break;
    }
    case TabStripModelChange::kReplaced:
      // The tab is the same (prerender swap or discard): same record.
      break;
    case TabStripModelChange::kSelectionOnly:
      break;
  }
  if (selection.active_tab_changed() && selection.new_tab) {
    TouchActive(selection.new_tab);
  }
}

bool TabStripAdapter::KeepRecordWithClosingWindow(
    TabStripModel* tab_strip_model,
    const TabRecord& record) const {
  // Only an awake Today record: sleeping Today records go with their tab, as
  // they always did (the Tab Model restores sleeping tabs its own way).
  if (!tab_strip_model->closing_all() || record.section != Section::kToday ||
      record.sleeping) {
    return false;
  }
  // The browser is quitting: session restore brings every window back.
  if (browser_shutdown::IsTryingToQuit()) {
    return true;
  }
  // Else only the last window of the profile: this is the test that
  // SessionService::WindowClosing makes (has_open_trackable_browsers_), and it
  // is also true when the last window is closed with its X button, which does
  // not set IsTryingToQuit() until the window is gone. Any other window that is
  // closed is not restored, so its records go with it.
  bool other_window = false;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* browser) {
        if (Watches(browser) && browser->GetTabStripModel() != tab_strip_model) {
          other_window = true;
          return false;
        }
        return true;
      });
  return !other_window;
}

void TabStripAdapter::TabInserted(tabs::TabInterface* tab, int index) {
  auto it = ids_.find(tab->GetHandle());
  if (it != ids_.end()) {
    // Came from another window.
    if (const TabRecord* record = model_->GetTab(it->second);
        record && record->section == Section::kToday) {
      model_->MoveTab(record->id, record->section, record->parent, index);
    }
    return;
  }
  if (!pending_restore_record_.empty()) {
    // The unloaded tab of a Pinned or Favorites record that was saved.
    std::string id = std::move(pending_restore_record_);
    pending_restore_record_.clear();
    if (model_->GetTab(id)) {
      ids_.emplace(tab->GetHandle(), std::move(id));
      return;
    }
  }
  // A tab that session restore brought back carries the id its record had in
  // the run that saved the session (issue #223). The value is untrusted: the
  // model checks it and only looks it up.
  const std::string restored_id = TakeRestoredRecordId(tab->GetContents());
  if (!restored_id.empty() && model_->loaded()) {
    std::string claimed = model_->ClaimRestoredTab(restored_id);
    if (!claimed.empty()) {
      // Section, folder, position and name are in the record already.
      ids_.emplace(tab->GetHandle(), std::move(claimed));
      SaveRecordIdSoon(tab);
      return;
    }
  }
  TabRecord record;
  record.section = Section::kToday;
  record.position = index;
  record.last_active = clock_->Now();
  std::string id = model_->AddTab(std::move(record));
  if (!id.empty()) {
    ids_.emplace(tab->GetHandle(), id);
    if (!restored_id.empty() && !model_->loaded()) {
      // The stored records are not here yet: try again when they are.
      deferred_claims_.emplace(tab->GetHandle(), restored_id);
    }
    SaveRecordIdSoon(tab);
    if (tab_inserted_) {
      tab_inserted_.Run(id, tab);
    }
  }
}

void TabStripAdapter::OnModelLoaded() {
  // Tabs that came in before the read: the ones that carried an id swap their
  // fresh record for the stored one.
  std::map<tabs::TabHandle, std::string> claims;
  claims.swap(deferred_claims_);
  for (const auto& [handle, restored_id] : claims) {
    tabs::TabInterface* tab = handle.Get();
    auto it = ids_.find(handle);
    if (!tab || it == ids_.end()) {
      continue;
    }
    std::string claimed =
        model_->ClaimRestoredTabInPlaceOf(it->second, restored_id);
    if (!claimed.empty()) {
      it->second = std::move(claimed);
      SaveRecordIdSoon(tab);
    }
  }
}

void TabStripAdapter::OnTabMoved(const TabRecord& tab, const Placement& from) {
  // A record that enters or leaves Today: the session saves the id for Today
  // only.
  if (from.section == tab.section) {
    return;
  }
  if (tabs::TabInterface* live = TabForRecord(tab.id)) {
    SaveRecordIdSoon(live);
  }
}

void TabStripAdapter::SaveRecordIdSoon(tabs::TabInterface* tab) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&TabStripAdapter::SaveRecordId,
                                weak_factory_.GetWeakPtr(), tab->GetHandle()));
}

void TabStripAdapter::SaveRecordId(tabs::TabHandle handle) {
  tabs::TabInterface* tab = handle.Get();
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  SessionService* service =
      SessionServiceFactory::GetForProfileIfExisting(profile_);
  if (!contents || !service) {
    return;
  }
  const SessionID window_id =
      sessions::SessionTabHelper::IdForWindowContainingTab(contents);
  const SessionID tab_id = sessions::SessionTabHelper::IdForTab(contents);
  if (!window_id.is_valid() || !tab_id.is_valid()) {
    return;
  }
  // An empty value clears an id that an earlier save left (the record left
  // Today).
  service->AddTabExtraData(window_id, tab_id, kSessionRecordIdKey,
                           SessionRecordIdForTab(tab));
}

void TabStripAdapter::TouchActive(tabs::TabInterface* tab) {
  auto it = ids_.find(tab->GetHandle());
  if (it == ids_.end()) {
    return;
  }
  const base::Time now = clock_->Now();
  // The selected tab is awake: selecting a tab that was restored unloaded (issue
  // #180) loads it, and the Chromium discard that SleepController watches does
  // not cover that case.
  model_->UpdateTab(it->second, [now](TabRecord& record) {
    record.last_active = now;
    record.sleeping = false;
  });
}

void TabStripAdapter::OnTabChangedAt(tabs::TabInterface* tab,
                                     TabChangeType change_type) {
  // A Pinned or Favorites tab remembers its title, for the row that is shown
  // before the tab loads again.
  auto it = ids_.find(tab->GetHandle());
  if (it != ids_.end() && tab_changed_) {
    tab_changed_.Run(it->second, tab);
  }
  const TabRecord* record =
      it == ids_.end() ? nullptr : model_->GetTab(it->second);
  if (!record || record->section == Section::kToday) {
    return;
  }
  const std::string title = base::UTF16ToUTF8(tab->GetTitle());
  if (title.empty() || record->last_title == title) {
    return;
  }
  model_->UpdateTab(it->second,
                    [&title](TabRecord& tab_record) { tab_record.last_title = title; });
}

}  // namespace aerie_tab_model
