// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_tab_model/restore_pass.h"

#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"

namespace aerie_tab_model {

RestorePass::RestorePass(Profile* profile, TabModel* model)
    : profile_(profile), model_(model) {
  model_observation_.Observe(model_);
  browser_collection_observation_.Observe(
      GlobalBrowserCollection::GetInstance());
}

RestorePass::~RestorePass() = default;

void RestorePass::Rearm() {
  finished_ = false;
  restore_reported_ = false;
}

void RestorePass::OnModelLoaded() {
  ScheduleCheck();
}

void RestorePass::OnBrowserCreated(BrowserWindowInterface*) {
  ScheduleCheck();
}

void RestorePass::ScheduleCheck() {
  if (finished_ || check_scheduled_) {
    return;
  }
  check_scheduled_ = true;
  // After the call that created the window, or merged the read, has returned:
  // the tabs that this call inserted have claimed their records by then.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<RestorePass> self) {
                       if (self) {
                         self->check_scheduled_ = false;
                         self->MaybeFinish();
                       }
                     },
                     weak_factory_.GetWeakPtr()));
}

bool RestorePass::HasNormalWindow() const {
  bool found = false;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* browser) {
        if (browser->GetProfile() == profile_ &&
            browser->GetType() == BrowserWindowInterface::TYPE_NORMAL) {
          found = true;
          return false;
        }
        return true;
      });
  return found;
}

void RestorePass::MaybeFinish() {
  if (finished_ || !model_->loaded()) {
    return;
  }
  if (browser_shutdown::IsTryingToQuit()) {
    // Windows are closing one by one: what waits stays for the next launch.
    return;
  }
  if (!restore_reported_ && SessionRestore::IsRestoring(profile_)) {
    // The tabs are still coming. Wait for the end of the restore.
    if (!restored_subscription_) {
      restored_subscription_ = SessionRestore::RegisterOnSessionRestoredCallback(
          base::BindRepeating(&RestorePass::OnSessionRestored,
                              weak_factory_.GetWeakPtr()));
    }
    return;
  }
  if (!HasNormalWindow()) {
    // Nothing has started yet. A window will say so.
    return;
  }
  finished_ = true;
  restored_subscription_ = {};
  model_->RemoveUnclaimedRestoredTabs();
}

void RestorePass::OnSessionRestored(Profile* profile, int tab_count) {
  if (profile == profile_) {
    // All the tabs are in. The restorer may still be counted as running inside
    // this notification, so look again after it has returned.
    restore_reported_ = true;
    ScheduleCheck();
  }
}

}  // namespace aerie_tab_model
