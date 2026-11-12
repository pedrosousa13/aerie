// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/auto_file_controller.h"

#include <utility>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_tab_model/folder_rules.h"
#include "components/aerie_tab_model/tab_model.h"

namespace aerie_tab_model {

namespace {

// An address that is not a navigation the user or a page made: the empty
// document that a tab starts with.
bool IsBlank(const GURL& url) {
  return !url.is_valid() || url.is_empty() || url.spec() == "about:blank";
}

bool AnyAutoFilingFolder(const TabModel& model) {
  for (const auto& [id, folder] : model.folders()) {
    if (IsAutoFiling(folder)) {
      return true;
    }
  }
  return false;
}

}  // namespace

base::TimeDelta AutoFileDelay(bool reduced) {
  return base::Milliseconds(reduced
                                ? aerie::tokens::kMotionTimingAutofileDelayReduced
                                : aerie::tokens::kMotionTimingAutofileDelay);
}

AutoFileController::AutoFileController(TabModel* model,
                                       const base::TickClock* clock,
                                       BoolProvider rules_enabled,
                                       DelayProvider delay,
                                       base::TimeDelta window)
    : model_(model),
      clock_(clock),
      rules_enabled_(std::move(rules_enabled)),
      delay_(std::move(delay)),
      window_(window) {
  model_->AddObserver(this);
}

AutoFileController::~AutoFileController() {
  observers_.Notify(&Observer::OnAutoFileControllerDestroyed);
  model_->RemoveObserver(this);
}

void AutoFileController::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void AutoFileController::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

bool AutoFileController::IsWatching(const std::string& tab_id) const {
  auto it = watched_.find(tab_id);
  return it != watched_.end() && !it->second.pending &&
         clock_->NowTicks() < it->second.deadline;
}

void AutoFileController::PurgeExpired() {
  const base::TimeTicks now = clock_->NowTicks();
  for (auto it = watched_.begin(); it != watched_.end();) {
    // A pending entry's deadline is its pause plus a grace, so one whose move
    // never ran does not stay.
    if (now >= it->second.deadline) {
      titles_.erase(it->first);
      it = watched_.erase(it);
    } else {
      ++it;
    }
  }
}

void AutoFileController::Watch(const std::string& tab_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  PurgeExpired();
  const TabRecord* tab = model_->GetTab(tab_id);
  // Only a new Today tab can be filed, and only when some folder could take it.
  if (!tab || tab->section != Section::kToday || !tab->parent.empty() ||
      !AnyAutoFilingFolder(*model_)) {
    return;
  }
  watched_[tab_id].deadline = clock_->NowTicks() + window_;
}

void AutoFileController::OnTabMoved(const TabRecord& tab,
                                    const Placement& from) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // A move to another section or folder is the user's (or another feature's)
  // choice: the tab is theirs now. A change of position in Today is the tab
  // strip following the window, and does not count.
  if (!filing_ &&
      (tab.section != from.section || tab.parent != from.parent)) {
    Forget(tab.id);
  }
}

void AutoFileController::OnTabRemoved(const TabRecord& tab) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Forget(tab.id);
}

void AutoFileController::Forget(const std::string& tab_id) {
  watched_.erase(tab_id);
  titles_.erase(tab_id);
}

void AutoFileController::OnTabPage(const std::string& tab_id,
                                   const GURL& url,
                                   std::string_view title) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = watched_.find(tab_id);
  if (it == watched_.end() || it->second.pending) {
    return;
  }
  Watched& watched = it->second;
  if (clock_->NowTicks() >= watched.deadline) {
    Forget(tab_id);
    return;
  }
  // The first navigation that commits fixes the address. Later ones are the
  // user moving on inside the tab, which never files it.
  if (watched.url.is_empty()) {
    if (IsBlank(url)) {
      return;
    }
    watched.url = url;
  }
  const TabRecord* record = model_->GetTab(tab_id);
  if (!record || record->section != Section::kToday || !record->parent.empty()) {
    Forget(tab_id);
    return;
  }
  titles_[tab_id] = std::string(title);
  if (!FindAutoFileTarget(*model_, watched.url, title, tab_id,
                          rules_enabled_.Run())) {
    return;
  }
  watched.pending = true;
  const base::TimeDelta delay = delay_.Run();
  watched.deadline = clock_->NowTicks() + delay + kAutoFilePendingGrace;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&AutoFileController::FileNow, weak_factory_.GetWeakPtr(),
                     tab_id),
      delay);
}

void AutoFileController::FileNow(const std::string& tab_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = watched_.find(tab_id);
  // Moved by the user, or closed, while it waited.
  if (it == watched_.end() || !it->second.pending) {
    return;
  }
  const GURL url = it->second.url;
  const std::string title = titles_[tab_id];
  Forget(tab_id);

  const TabRecord* record = model_->GetTab(tab_id);
  if (!record || record->section != Section::kToday || !record->parent.empty()) {
    return;
  }
  // The rules may have changed in the pause: ask again.
  std::optional<AutoFileTarget> target = FindAutoFileTarget(
      *model_, url, title, tab_id, rules_enabled_.Run());
  if (!target) {
    return;
  }
  {
    base::AutoReset<bool> filing(&filing_, true);
    if (!FileTabIntoFolder(*model_, tab_id, *target, url.spec())) {
      return;
    }
  }
  Filed filed{tab_id, target->folder_id, target->rule_id,
              url.has_host() ? std::string(url.host()) : std::string()};
  observers_.Notify(&Observer::OnTabFiled, filed);
}

}  // namespace aerie_tab_model
