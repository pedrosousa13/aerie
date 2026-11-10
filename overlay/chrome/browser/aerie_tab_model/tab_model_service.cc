// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_tab_model/tab_model_service.h"

#include "base/files/file_path.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/time/default_clock.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "components/aerie_tab_model/tab_model_prefs.h"
#include "components/prefs/pref_service.h"

namespace aerie_tab_model {

namespace {

base::FilePath StatePath(Profile* profile) {
  // Off the record: nothing is written, the model lives in memory.
  if (profile->IsOffTheRecord()) {
    return base::FilePath();
  }
  return profile->GetPath()
      .AppendASCII(kStateDirectory)
      .AppendASCII(kStateFile);
}

}  // namespace

TabModelService::TabModelService(Profile* profile)
    : profile_(profile),
      model_(base::DefaultClock::GetInstance()),
      persistence_(&model_,
                   StatePath(profile),
                   base::ThreadPool::CreateSequencedTaskRunner(
                       {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
                        base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {
  // The read runs on the pool; only the merge comes back to this sequence.
  persistence_.Load();
  adapter_ = std::make_unique<TabStripAdapter>(
      profile_, &model_, base::DefaultClock::GetInstance());
}

TabModelService::~TabModelService() = default;

std::optional<base::TimeDelta> TabModelService::archive_after() const {
  return GetArchiveAfter(*profile_->GetPrefs());
}

std::optional<base::TimeDelta> TabModelService::sleep_after() const {
  return GetSleepAfter(*profile_->GetPrefs());
}

void TabModelService::Shutdown() {
  adapter_.reset();
  persistence_.FlushPendingWrite();
}

}  // namespace aerie_tab_model
