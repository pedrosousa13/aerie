// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_TAB_MODEL_H_
#define COMPONENTS_AERIE_TAB_MODEL_TAB_MODEL_H_

#include <deque>
#include <map>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/functional/function_ref.h"
#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/sequence_checker.h"
#include "components/aerie_tab_model/tab_model_observer.h"
#include "components/aerie_tab_model/tab_model_types.h"

namespace base {
class Clock;
}

namespace aerie_tab_model {

// The tabs, folders and the one Workspace of a profile (issue #176). Headless:
// no Views, no Chromium tab. It lives on one sequence. Time comes from the
// injected clock, so tests control `modified`, the change log and, later, the
// auto-archive and sleep timers.
//
// Every write stamps the record (`modified`, `change`), appends a ChangeRecord
// to a bounded log and tells the observers. The model does not read or write
// files; see TabModelPersistence.
class TabModel {
 public:
  using IdGenerator = base::RepeatingCallback<std::string()>;

  // `clock` must outlive the model. An empty `id_generator` makes random UUIDs.
  explicit TabModel(const base::Clock* clock,
                    IdGenerator id_generator = IdGenerator(),
                    size_t max_change_log_entries = kMaxChangeLogEntries);
  TabModel(const TabModel&) = delete;
  TabModel& operator=(const TabModel&) = delete;
  ~TabModel();

  void AddObserver(TabModelObserver* observer);
  void RemoveObserver(TabModelObserver* observer);

  // The one Workspace. A new model has "Personal", colour `dusk`, theme
  // `default`, mode `default`.
  const Workspace& workspace() const { return workspace_; }
  void UpdateWorkspace(base::FunctionRef<void(Workspace&)> mutate);

  const TabRecord* GetTab(const std::string& id) const;
  const FolderRecord* GetFolder(const std::string& id) const;
  const std::map<std::string, TabRecord>& tabs() const { return tabs_; }
  const std::map<std::string, FolderRecord>& folders() const { return folders_; }

  // Adds `tab` and returns its new id. The model sets id, created, modified and
  // change. An empty `workspace_id` becomes the Workspace's. Returns an empty
  // string, and adds nothing, when the parent folder does not exist or the
  // Favorites cap (kFavoritesCap) is reached.
  std::string AddTab(TabRecord tab);
  std::string AddFolder(FolderRecord folder);

  // Calls `mutate` on the record and logs a change. The placement fields (workspace,
  // section, parent, position) and the id and created time are put back after
  // `mutate`: use MoveTab to change them. Returns false for an unknown id.
  bool UpdateTab(const std::string& id,
                 base::FunctionRef<void(TabRecord&)> mutate);
  bool UpdateFolder(const std::string& id,
                    base::FunctionRef<void(FolderRecord&)> mutate);

  // Returns false, and changes nothing, for an unknown id or parent, a parent
  // in another Workspace, a folder moved into itself or below itself, or a
  // move into Favorites that would pass the cap.
  bool MoveTab(const std::string& id,
               Section section,
               const std::string& parent,
               int position);
  bool MoveFolder(const std::string& id,
                  Section section,
                  const std::string& parent,
                  int position);

  bool RemoveTab(const std::string& id);
  // What was inside the folder moves up to the folder's own parent.
  bool RemoveFolder(const std::string& id);

  const std::deque<ChangeRecord>& change_log() const { return change_log_; }
  // The counter the next write gets.
  int64_t next_change() const { return next_change_; }

  // The JSON of state.json.
  base::DictValue ToValue() const;

  // Joins what was read from disk with what this run wrote while the read was
  // going on. The stored records and log come first; this run's records keep
  // their ids, and their counters move up behind the stored ones. A stored
  // Today tab that was neither sleeping nor archived was open in the run that
  // saved it; nothing can match it to a tab now, so it is removed (and
  // logged). Pass nullopt when there is nothing stored. Then tells the
  // observers with OnModelLoaded.
  void MergeLoaded(std::optional<ModelState> stored);

 private:
  // Sets modified and change on `record`, appends a ChangeRecord.
  void Stamp(Record& record, ChangeType type, RecordKind kind);
  void AppendChange(ChangeType type,
                    RecordKind kind,
                    const std::string& record_id,
                    base::Time now);
  std::string NewId() const;
  size_t CountFavorites(const std::string& workspace_id,
                        const std::string& except_id) const;
  bool PlacementIsValid(const std::string& workspace_id,
                        const std::string& self_folder_id,
                        const std::string& parent) const;
  void NotifyChanged();

  const raw_ptr<const base::Clock> clock_;
  IdGenerator id_generator_;
  const size_t max_change_log_entries_;

  Workspace workspace_;
  std::map<std::string, TabRecord> tabs_;
  std::map<std::string, FolderRecord> folders_;
  std::deque<ChangeRecord> change_log_;
  int64_t next_change_ = 1;

  base::ObserverList<TabModelObserver> observers_;
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_TAB_MODEL_H_
