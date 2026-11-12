// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_TAB_MODEL_RESTORE_PASS_H_
#define CHROME_BROWSER_AERIE_TAB_MODEL_RESTORE_PASS_H_

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_observer.h"

class BrowserWindowInterface;
class GlobalBrowserCollection;
class Profile;

namespace aerie_tab_model {

// Ends the restore pass of one profile (issue #223): the start of a run, in
// which Chromium's session restore brings tabs back and each one claims its
// stored Today record (TabStripAdapter). A stored Today record that is still
// unclaimed when the pass ends is removed (TabModel::RemoveUnclaimedRestoredTabs);
// empty Today folders stay.
//
// Startup order (checked in the source): SessionRestore::RestoreSession makes
// its SessionRestoreImpl, which registers itself as "restoring" in its
// constructor (session_restore.cc, GetActiveSessionRestorers().insert), before
// it creates any window. So a window never exists first with IsRestoring() still
// false on the startup path.
//
// The pass ends once, at the first moment when the Tab Model has read
// state.json, session restore is not running for the profile, and a normal
// window exists. If session restore is running at that moment, the pass ends
// when it reports that it is done. A session that is restored later (a crash
// bubble "Restore" much after startup) finds no waiting records, and its tabs
// get fresh ones.
class RestorePass : public TabModelObserver, public BrowserCollectionObserver {
 public:
  RestorePass(Profile* profile, TabModel* model);
  RestorePass(const RestorePass&) = delete;
  RestorePass& operator=(const RestorePass&) = delete;
  ~RestorePass() override;

  bool finished() const { return finished_; }

  // Ends the pass now if the conditions hold. Safe to call again.
  void MaybeFinish();

  // Starts the pass again, because records of closed tabs now wait for a claim
  // (TabModel::MarkAwaitingRestore): the last window closed while the process
  // lives on. It ends by the same rule when the next window opens (and never
  // while the browser is quitting, when the records must stay for the next
  // launch). It does not look at the windows now: the closing window is still
  // in the list for a moment.
  void Rearm();

  // TabModelObserver:
  void OnModelLoaded() override;

  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* browser) override;

 private:
  void ScheduleCheck();
  bool HasNormalWindow() const;
  void OnSessionRestored(Profile* profile, int tab_count);

  const raw_ptr<Profile> profile_;
  const raw_ptr<TabModel> model_;
  bool finished_ = false;
  bool check_scheduled_ = false;
  bool restore_reported_ = false;
  base::CallbackListSubscription restored_subscription_;
  base::ScopedObservation<TabModel, TabModelObserver> model_observation_{this};
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};
  base::WeakPtrFactory<RestorePass> weak_factory_{this};
};

}  // namespace aerie_tab_model

#endif  // CHROME_BROWSER_AERIE_TAB_MODEL_RESTORE_PASS_H_
