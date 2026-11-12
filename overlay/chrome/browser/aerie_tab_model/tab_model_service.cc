// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_tab_model/tab_model_service.h"

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/time/default_clock.h"
#include "base/time/default_tick_clock.h"
#include "chrome/browser/aerie_tab_model/pinned_tabs_controller.h"
#include "chrome/browser/aerie_tab_model/restore_pass.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_restore.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_tab_model/auto_file_controller.h"
#include "components/aerie_tab_model/tab_model_prefs.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"

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
  pinned_tabs_ = std::make_unique<PinnedTabsController>(profile_, &model_,
                                                        adapter_.get());
  // Auto-filing (issue #139). Nothing runs here: the controller listens for
  // new tabs and keeps the few seconds of state each one needs.
  auto_file_ = std::make_unique<AutoFileController>(
      &model_, base::DefaultTickClock::GetInstance(),
      base::BindRepeating(
          [](Profile* profile) {
            return AreFolderRulesEnabled(*profile->GetPrefs());
          },
          base::Unretained(profile_.get())),
      base::BindRepeating(
          [](Profile* profile) {
            return AutoFileDelay(
                aerie::motion::ShouldReduceMotion(profile->GetPrefs()));
          },
          base::Unretained(profile_.get())));
  restore_pass_ = std::make_unique<RestorePass>(profile_, &model_);
  adapter_->SetTabInsertedCallback(base::BindRepeating(
      &TabModelService::OnTabInserted, base::Unretained(this)));
  adapter_->SetTabChangedCallback(base::BindRepeating(
      &TabModelService::OnTabChanged, base::Unretained(this)));
}

void TabModelService::OnTabInserted(const std::string& record_id,
                                    tabs::TabInterface* tab) {
  if (!auto_file_) {
    return;
  }
  // Nothing is watched while session restore runs: those tabs are not new, and
  // no auto-file work should run at startup.
  if (SessionRestore::IsRestoring(profile_)) {
    return;
  }
  // A tab that Reopen closed tab or Duplicate made has history already. Only a
  // tab that has not committed a page of its own yet is new.
  content::WebContents* contents = tab->GetContents();
  if (!contents || !contents->GetController().IsInitialBlankNavigation()) {
    return;
  }
  auto_file_->Watch(record_id);
}

void TabModelService::OnTabChanged(const std::string& record_id,
                                   tabs::TabInterface* tab) {
  if (!auto_file_ || !auto_file_->IsWatching(record_id)) {
    return;
  }
  content::WebContents* contents = tab->GetContents();
  if (!contents) {
    return;
  }
  auto_file_->OnTabPage(record_id, contents->GetLastCommittedURL(),
                        base::UTF16ToUTF8(tab->GetTitle()));
}

TabModelService::~TabModelService() = default;

std::optional<base::TimeDelta> TabModelService::archive_after() const {
  return GetArchiveAfter(*profile_->GetPrefs());
}

std::optional<base::TimeDelta> TabModelService::sleep_after() const {
  return GetSleepAfter(*profile_->GetPrefs());
}

void TabModelService::Shutdown() {
  restore_pass_.reset();
  auto_file_.reset();
  pinned_tabs_.reset();
  adapter_.reset();
  persistence_.FlushPendingWrite();
}

}  // namespace aerie_tab_model
