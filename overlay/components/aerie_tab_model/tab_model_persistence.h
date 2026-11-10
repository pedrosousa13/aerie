// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_TAB_MODEL_PERSISTENCE_H_
#define COMPONENTS_AERIE_TAB_MODEL_TAB_MODEL_PERSISTENCE_H_

#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/files/important_file_writer.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_observer.h"
#include "components/aerie_tab_model/tab_model_types.h"

namespace base {
class SequencedTaskRunner;
}

namespace aerie_tab_model {

// The folder in the profile, and the file in it.
inline constexpr char kStateDirectory[] = "AerieTabModel";
inline constexpr char kStateFile[] = "state.json";
inline constexpr char kBadSuffix[] = ".bad";

// A state file is some hundred kilobytes at most (the log is bounded). Anything
// this large is not ours.
inline constexpr size_t kMaxStateBytes = 16 * 1024 * 1024;

struct LoadOutcome {
  LoadOutcome();
  LoadOutcome(LoadOutcome&&);
  LoadOutcome& operator=(LoadOutcome&&);
  ~LoadOutcome();

  LoadStatus status = LoadStatus::kMissing;
  // Set when `status` is kOk.
  std::optional<ModelState> state;
};

// Reads and parses `path`. Blocking: call it where blocking is allowed. A file
// that is corrupt or from a newer build is moved to `path` + ".bad" (an older
// ".bad" is replaced) and reported; the caller starts empty.
LoadOutcome ReadStateFile(const base::FilePath& path);

// Reads state.json into a TabModel after startup and saves the model when it
// changes (issue #176).
//
// - Nothing happens in the constructor. Load() posts the read to `io_runner`;
//   the file is read and parsed there, and only the merge runs on the calling
//   sequence.
// - The model works before the read finishes. Nothing is saved until it has,
//   so a write cannot cover the stored state with a partial one.
// - Saves go through base::ImportantFileWriter: atomic, and one write for a
//   burst of changes.
// - An empty `path` keeps the model in memory (off-the-record profiles).
class TabModelPersistence final
    : public TabModelObserver,
      public base::ImportantFileWriter::DataSerializer {
 public:
  TabModelPersistence(TabModel* model,
                      base::FilePath path,
                      scoped_refptr<base::SequencedTaskRunner> io_runner);
  TabModelPersistence(const TabModelPersistence&) = delete;
  TabModelPersistence& operator=(const TabModelPersistence&) = delete;
  ~TabModelPersistence() override;

  // Starts the read. `done` runs, once, after the merge. Call once.
  void Load(base::OnceCallback<void(LoadStatus)> done = {});
  bool loaded() const { return loaded_; }
  // False for an in-memory model (an empty path): nothing is read or written.
  bool persists_to_disk() const { return !path_.empty(); }

  // Writes any pending change now. The write itself runs on `io_runner`.
  void FlushPendingWrite();

 private:
  void OnRead(base::OnceCallback<void(LoadStatus)> done, LoadOutcome outcome);

  // TabModelObserver:
  void OnModelChanged() override;

  // base::ImportantFileWriter::DataSerializer:
  std::optional<std::string> SerializeData() override;

  const raw_ptr<TabModel> model_;
  const base::FilePath path_;
  const scoped_refptr<base::SequencedTaskRunner> io_runner_;
  std::optional<base::ImportantFileWriter> writer_;
  base::ScopedObservation<TabModel, TabModelObserver> observation_{this};
  bool load_started_ = false;
  bool loaded_ = false;
  base::WeakPtrFactory<TabModelPersistence> weak_factory_{this};
};

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_TAB_MODEL_PERSISTENCE_H_
