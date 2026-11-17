// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_tab_model/horizontal_order_mirror.h"

#include <string>
#include <utility>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/aerie_sidebar/aerie_sidebar_layout.h"
#include "components/aerie_sidebar/aerie_sidebar_prefs.h"
#include "components/aerie_tab_model/horizontal_order.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"

namespace aerie_tab_model {

HorizontalOrderMirror::HorizontalOrderMirror(Profile* profile,
                                             TabModel* model,
                                             TabStripAdapter* adapter)
    : profile_(profile), model_(model), adapter_(adapter) {
  pref_registrar_.Init(profile_->GetPrefs());
  pref_registrar_.Add(
      aerie_sidebar::kLayoutPref,
      base::BindRepeating(&HorizontalOrderMirror::ScheduleMirror,
                          base::Unretained(this)));
  model_observation_.Observe(model_);
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [this](BrowserWindowInterface* browser) {
        OnBrowserCreated(browser);
        return true;
      });
  browser_collection_observation_.Observe(GlobalBrowserCollection::GetInstance());
  if (active()) {
    ScheduleMirror();
  }
}

HorizontalOrderMirror::~HorizontalOrderMirror() = default;

bool HorizontalOrderMirror::active() const {
  return !aerie_sidebar::IsSidebarLayout(*profile_->GetPrefs());
}

bool HorizontalOrderMirror::Watches(BrowserWindowInterface* browser) const {
  return browser->GetProfile() == profile_ &&
         browser->GetType() == BrowserWindowInterface::TYPE_NORMAL;
}

void HorizontalOrderMirror::OnBrowserCreated(BrowserWindowInterface* browser) {
  if (!Watches(browser)) {
    return;
  }
  browser->GetTabStripModel()->AddObserver(this);
  if (active()) {
    ScheduleMirror();
  }
}

void HorizontalOrderMirror::OnBrowserClosed(BrowserWindowInterface* browser) {
  if (!Watches(browser)) {
    return;
  }
  browser->GetTabStripModel()->RemoveObserver(this);
}

void HorizontalOrderMirror::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (!active() || mirroring_) {
    return;
  }
  switch (change.type()) {
    case TabStripModelChange::kMoved:
      // The user moved a tab in the strip: write it down, and put right what
      // the model does not allow.
      ScheduleMirror();
      break;
    case TabStripModelChange::kInserted:
      // A new tab is a Today tab at the end of the model's order.
      ScheduleMirror();
      break;
    default:
      break;
  }
}

void HorizontalOrderMirror::OnModelLoaded() {
  if (active()) {
    ScheduleMirror();
  }
}

void HorizontalOrderMirror::OnModelChanged() {
  if (active() && !mirroring_) {
    ScheduleMirror();
  }
}

void HorizontalOrderMirror::ScheduleMirror() {
  if (!active() || mirror_pending_) {
    return;
  }
  mirror_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&HorizontalOrderMirror::FlushForTesting,
                                weak_factory_.GetWeakPtr()));
}

void HorizontalOrderMirror::FlushForTesting() {
  if (!mirror_pending_) {
    return;
  }
  mirror_pending_ = false;
  MirrorAll();
}

std::vector<std::string> HorizontalOrderMirror::StripIds(
    TabStripModel* strip) const {
  std::vector<std::string> ids;
  for (int i = 0; i < strip->count(); ++i) {
    ids.push_back(adapter_->RecordIdForTab(strip->GetTabAtIndex(i)));
  }
  return ids;
}

void HorizontalOrderMirror::MirrorAll() {
  if (!active()) {
    return;
  }
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [this](BrowserWindowInterface* browser) {
        if (Watches(browser)) {
          WriteBack(browser->GetTabStripModel());
          MirrorStrip(browser->GetTabStripModel());
        }
        return true;
      });
}

// A reorder of the strip becomes positions in the model, for Pinned and
// Favorites tabs. Today follows the strip already (TabStripAdapter), so the
// model's order and the strip agree for it.
void HorizontalOrderMirror::WriteBack(TabStripModel* strip) {
  const std::vector<std::string> ids = StripIds(strip);
  for (const PositionChange& change : PositionsFromStripOrder(*model_, ids)) {
    const TabRecord* tab = model_->GetTab(change.id);
    if (tab) {
      model_->MoveTab(change.id, tab->section, tab->parent, change.position);
    }
  }
}

void HorizontalOrderMirror::MirrorStrip(TabStripModel* strip) {
  const std::vector<std::string> current = StripIds(strip);
  const std::vector<std::string> target = HorizontalStripOrder(
      *model_, [&](const std::string& id) {
        return adapter_->TabForRecord(id) != nullptr;
      });
  const std::vector<StripMove> moves = PlanStripMoves(current, target);
  if (moves.empty()) {
    return;
  }
  base::AutoReset<bool> mirroring(&mirroring_, true);
  for (const StripMove& move : moves) {
    strip->MoveWebContentsAt(move.from, move.to, /*select_after_move=*/false);
  }
}

}  // namespace aerie_tab_model
