// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_audio_watcher.h"

#include <algorithm>

#include "chrome/browser/ui/tabs/alert/tab_alert_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_alert.h"

AerieSidebarAudioWatcher::AerieSidebarAudioWatcher(
    TabStripModel* model,
    base::RepeatingClosure changed)
    : model_(model), changed_(std::move(changed)) {
  model_->AddObserver(this);
  Sync();
}

AerieSidebarAudioWatcher::~AerieSidebarAudioWatcher() {
  model_->RemoveObserver(this);
}

// static
AerieSidebarAudioWatcher::State AerieSidebarAudioWatcher::StateOf(
    tabs::TabInterface* tab) {
  const tabs::TabAlertController* alerts =
      tab ? tabs::TabAlertController::From(tab) : nullptr;
  if (!alerts) {
    return State::kNone;
  }
  if (alerts->IsAlertActive(tabs::TabAlert::kAudioMuting)) {
    return State::kMuted;
  }
  return alerts->IsAlertActive(tabs::TabAlert::kAudioPlaying) ? State::kPlaying
                                                               : State::kNone;
}

void AerieSidebarAudioWatcher::OnTabStripModelChanged(
    TabStripModel*,
    const TabStripModelChange&,
    const TabStripSelectionChange&) {
  Sync();
}

void AerieSidebarAudioWatcher::Sync() {
  std::erase_if(subs_, [](const auto& entry) { return !entry.first.Get(); });
  for (int i = 0; i < model_->count(); ++i) {
    tabs::TabInterface* tab = model_->GetTabAtIndex(i);
    tabs::TabAlertController* alerts =
        tab ? tabs::TabAlertController::From(tab) : nullptr;
    if (!alerts) {
      continue;
    }
    const tabs::TabHandle handle = tab->GetHandle();
    if (std::ranges::any_of(subs_, [&](const auto& entry) {
          return entry.first == handle;
        })) {
      continue;
    }
    // The callback fires when the alert to show changes: playing to muted and
    // back, and either to none.
    subs_.emplace_back(
        handle, alerts->AddAlertToShowChangedCallback(base::BindRepeating(
                    [](AerieSidebarAudioWatcher* self,
                       std::optional<tabs::TabAlert>) { self->changed_.Run(); },
                    base::Unretained(this))));
  }
}
