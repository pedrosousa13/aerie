// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/archive_schedule.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/check.h"
#include "components/aerie_tab_model/archive.h"

namespace aerie_tab_model {

std::optional<base::TimeDelta> EffectiveArchiveTimeout(
    const TabModel& model,
    const TabRecord& tab,
    std::optional<base::TimeDelta> global) {
  return global;
}

ArchiveSchedule::ArchiveSchedule(TabModel* model,
                                 const base::Clock* clock,
                                 const base::TickClock* tick_clock,
                                 ArchiveCallback archive,
                                 ExemptCallback exempt)
    : model_(model),
      clock_(clock),
      tick_clock_(tick_clock),
      archive_(std::move(archive)),
      exempt_(std::move(exempt)),
      timer_(tick_clock) {
  model_observation_.Observe(model_);
}

ArchiveSchedule::~ArchiveSchedule() = default;

void ArchiveSchedule::SetArchiveAfter(std::optional<base::TimeDelta> delay) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  archive_after_ = delay;
  Rearm();
}

void ArchiveSchedule::SetKeep(std::optional<base::TimeDelta> keep) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  keep_ = keep;
  Rearm();
}

std::optional<base::TimeDelta> ArchiveSchedule::next_fire_in() const {
  if (!timer_.IsRunning()) {
    return std::nullopt;
  }
  return timer_.desired_run_time() - tick_clock_->NowTicks();
}

std::optional<base::Time> ArchiveSchedule::DueTime(const TabRecord& tab) const {
  if (tab.section != Section::kToday || tab.archived_at ||
      model_->IsAwaitingRestore(tab.id) || exempt_.Run(tab.id)) {
    return std::nullopt;
  }
  std::optional<base::TimeDelta> timeout =
      EffectiveArchiveTimeout(*model_, tab, archive_after_);
  if (!timeout) {
    return std::nullopt;
  }
  base::Time used = tab.last_active;
  if (auto retry = retry_from_.find(tab.id); retry != retry_from_.end()) {
    used = std::max(used, retry->second);
  }
  return used + *timeout;
}

void ArchiveSchedule::Rearm() {
  if (running_) {
    return;
  }
  std::optional<base::Time> earliest = NextPurgeTime(*model_, keep_);
  for (const auto& [id, tab] : model_->tabs()) {
    std::optional<base::Time> due = DueTime(tab);
    if (due && (!earliest || *due < *earliest)) {
      earliest = due;
    }
  }
  if (!earliest) {
    timer_.Stop();
    return;
  }
  const base::TimeDelta wait =
      std::max(*earliest - clock_->Now(), base::TimeDelta());
  // Start() on a running timer replaces its task: still one pending task.
  timer_.Start(FROM_HERE, wait,
               base::BindOnce(&ArchiveSchedule::RunDueNow,
                              base::Unretained(this)));
}

void ArchiveSchedule::RunDueNow() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  running_ = true;
  PurgeArchive(*model_, clock_->Now(), keep_);
  const base::Time now = clock_->Now();
  std::vector<std::string> due;
  for (const auto& [id, tab] : model_->tabs()) {
    std::optional<base::Time> when = DueTime(tab);
    if (when && *when <= now) {
      due.push_back(id);
    }
  }
  for (const std::string& id : due) {
    // An earlier archive may have changed the model; ask again.
    const TabRecord* tab = model_->GetTab(id);
    if (!tab) {
      continue;
    }
    std::optional<base::Time> when = DueTime(*tab);
    if (!when || *when > now) {
      continue;
    }
    if (archive_.Run(id)) {
      retry_from_.erase(id);
    } else {
      retry_from_[id] = now;
    }
  }
  // Forget retries of records that are gone.
  std::erase_if(retry_from_, [this](const auto& entry) {
    return !model_->GetTab(entry.first);
  });
  running_ = false;
  Rearm();
}

}  // namespace aerie_tab_model
