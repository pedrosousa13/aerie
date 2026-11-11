// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"

#include <algorithm>

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_removed_reason.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_types.h"

namespace aerie_tab_model {

TabStripAdapter::TabStripAdapter(Profile* profile,
                                 TabModel* model,
                                 const base::Clock* clock)
    : profile_(profile), model_(model), clock_(clock) {
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
  TabRecord record;
  record.section = Section::kToday;
  record.position = index;
  record.last_active = clock_->Now();
  std::string id = model_->AddTab(std::move(record));
  if (!id.empty()) {
    ids_.emplace(tab->GetHandle(), std::move(id));
  }
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
