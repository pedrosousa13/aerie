// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_FILTER_LIST_MANAGER_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_FILTER_LIST_MANAGER_H_

#include <memory>
#include <string>
#include <map>
#include <set>
#include <string_view>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/aerie_adblock/browser/filter_engine_service.h"
#include "components/aerie_adblock/browser/filter_list_fetcher.h"
#include "components/aerie_adblock/browser/filter_list_state.h"
#include "url/gurl.h"

namespace network {
class SharedURLLoaderFactory;
}

namespace aerie_adblock {

// Lets the user manage the filter lists (issue #10): add, remove and update
// lists, per-site exceptions, and the scheduled update. It keeps the
// FilterListState, saves it (filter_list_storage.h), downloads lists
// (filter_list_fetcher.h), checks them (CheckList) and tells the
// FilterEngineService what to block with.
//
// Rules:
//   - A list that fails the download or the check never replaces the copy in
//     use. Its error is shown, the old rules keep blocking.
//   - Nothing is fetched except from a list's own address: a built-in list's
//     publisher address, or the address the user typed. There is no other
//     server. The scheduled update runs when automatic updates are on and a
//     day has passed since the last check; a fresh install waits one day.
//   - The shipped engine stays in use until a list changes, so a profile that
//     never touches the settings starts with no extra work.
//
// UI thread. Not a singleton: the browser's main parts own it
// (chrome/browser/aerie_adblock); Get() is for the settings page and the
// toolbar button and is null outside that window.
class FilterListManager {
 public:
  // The text of the built-in list the build shipped, for |id|.
  using BuiltinListProvider = base::RepeatingCallback<std::string(std::string_view id)>;

  class Observer : public base::CheckedObserver {
   public:
    virtual void OnFilterListsChanged() = 0;
  };

  static FilterListManager* Get();

  static constexpr base::TimeDelta kUpdateInterval = base::Days(1);
  static constexpr base::TimeDelta kScheduleCheckInterval = base::Hours(1);
  // The time one list gets to parse (the build step has its own).
  static constexpr base::TimeDelta kCheckBudget = base::Seconds(10);

  // |dir| holds the files. |allow_loopback| is the Bench's switch.
  FilterListManager(base::FilePath dir,
                    base::WeakPtr<FilterEngineService> service,
                    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
                    BuiltinListProvider builtin_lists,
                    bool allow_loopback);
  ~FilterListManager();

  FilterListManager(const FilterListManager&) = delete;
  FilterListManager& operator=(const FilterListManager&) = delete;

  void SetAsProcessInstance();

  // Reads the saved state (on the file sequence), then gives the exceptions
  // and, if the lists differ from the shipped engine, the lists to the
  // service, and starts the schedule timer. Until the state is read the
  // service holds every request (HoldRequestsUntilSettingsLoaded), so the
  // exceptions are in force for the first request of a restored session.
  // busy() is true until then.
  void Init();

  const FilterListState& state() const { return state_; }

  void AddObserver(Observer* observer) { observers_.AddObserver(observer); }
  void RemoveObserver(Observer* observer) { observers_.RemoveObserver(observer); }

  // True while a download, a check or a rebuild is running.
  bool busy() const { return pending_ > 0; }

  // Adds a list and downloads it at once. Returns false, and adds nothing, for
  // an address that is not allowed (see IsAcceptableListUrl), a duplicate, or
  // when the table is full; |*error| says which.
  bool AddList(const GURL& url, std::string* error);
  void RemoveList(std::string_view id);
  void UpdateList(std::string_view id);
  void UpdateAll();

  // |host| as in FilterListState::SetException. Takes effect at once.
  //
  // |session| is null for a regular profile: the exception is saved to disk
  // and holds for every profile. For an off-the-record profile it is the key
  // from SessionKeyFor(): the exception is kept in memory only, holds for that
  // session only, and is gone when ClearSessionExceptions() runs (the profile's
  // end, see WatchSession()). It is never written to the state file, so
  // nothing an incognito window does shows in the regular profile.
  bool SetException(std::string_view host,
                    bool excepted,
                    const void* session = nullptr);
  // True when blocking is off for |host| (saved, or for |session|).
  bool HasException(std::string_view host, const void* session = nullptr) const;
  // True when |host| has a saved exception: one that an off-the-record window
  // cannot turn back on (the regular profile owns it).
  bool HasSavedException(std::string_view host) const;
  // The hosts excepted for |session| only (a copy).
  std::set<std::string> SessionExceptions(const void* session) const;
  void ClearSessionExceptions(const void* session);
  // Calls ClearSessionExceptions() for an off-the-record |context| when it is
  // destroyed. Idempotent. Do it before the first SetException() for the
  // context's session.
  void WatchSession(content::BrowserContext* context);

  void SetAutoUpdate(bool enabled);

  // Runs UpdateAll() if automatic updates are on and kUpdateInterval has
  // passed since the last check. A fresh install starts the clock instead.
  // Called by the timer; public for tests.
  void MaybeRunScheduledUpdate(base::Time now);

 private:
  void OnStateLoaded(FilterListState state);
  void Save();
  void Notify();
  void FetchList(const std::string& id);
  void OnFetched(std::string id, FilterListFetcher::Result result);
  void OnChecked(std::string id, std::string text, ListCheck check);
  void OnWritten(std::string id, ListCheck check, bool written);
  void SetError(const std::string& id, ListFailure failure);
  // Gives the service every list in use. |retry| is true for the one
  // automatic retry after a cached copy failed to build.
  void Rebuild(bool retry = true);
  void OnRebuilt(std::vector<std::string> ids, bool retry, BuildResult result);
  void Begin() { ++pending_; }
  void End();

  const base::FilePath dir_;
  base::WeakPtr<FilterEngineService> service_;
  FilterListFetcher fetcher_;
  BuiltinListProvider builtin_lists_;
  const bool allow_loopback_;
  // Blocking work: the state file and the list files. Runs in order.
  scoped_refptr<base::SequencedTaskRunner> file_runner_;
  FilterListState state_;
  // Off-the-record exceptions, by session. Memory only.
  std::map<const void*, std::set<std::string>> session_exceptions_;
  int pending_ = 0;
  bool loaded_ = false;
  base::RepeatingTimer schedule_timer_;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<FilterListManager> weak_factory_{this};
};

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_FILTER_LIST_MANAGER_H_
