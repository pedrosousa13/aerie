// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/tab_model_persistence.h"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/values_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_traits.h"
#include "base/time/time.h"
#include "base/values.h"

namespace aerie_tab_model {

LoadOutcome::LoadOutcome() = default;
LoadOutcome::LoadOutcome(LoadOutcome&&) = default;
LoadOutcome& LoadOutcome::operator=(LoadOutcome&&) = default;
LoadOutcome::~LoadOutcome() = default;

namespace {

void KeepAsBad(const base::FilePath& path) {
  base::FilePath bad(path.value() + kBadSuffix);
  // Replaces an older .bad: the newest bad file is the one worth keeping. If
  // the move fails the file stays, and the next save replaces it.
  base::ReplaceFile(path, bad, nullptr);
}

// Removes archived tabs from the "tabs" list of `state`, oldest `archived_at`
// first, until the estimated size (`size`, then less what each removed record
// took) is at most kMaxStateBytes, or no archived tab is left. Returns how many
// were removed.
size_t DropOldestArchivedTabs(base::DictValue& state, size_t size) {
  base::ListValue* tabs = state.FindList("tabs");
  if (!tabs) {
    return 0;
  }
  struct Candidate {
    base::Time archived_at;
    size_t index;
    size_t bytes;
  };
  std::vector<Candidate> archived;
  for (size_t i = 0; i < tabs->size(); ++i) {
    const base::DictValue* tab = (*tabs)[i].GetIfDict();
    const base::Value* when = tab ? tab->Find("archived_at") : nullptr;
    if (!when) {
      continue;
    }
    std::optional<base::Time> time = base::ValueToTime(when);
    std::optional<std::string> json = base::WriteJson(*tab);
    if (time && json) {
      archived.push_back({*time, i, json->size() + 1});
    }
  }
  std::stable_sort(archived.begin(), archived.end(),
                   [](const Candidate& a, const Candidate& b) {
                     return a.archived_at < b.archived_at;
                   });
  std::vector<size_t> drop;
  for (const Candidate& candidate : archived) {
    if (size <= kMaxStateBytes) {
      break;
    }
    size -= std::min(size, candidate.bytes);
    drop.push_back(candidate.index);
  }
  std::sort(drop.begin(), drop.end(), std::greater<size_t>());
  for (size_t index : drop) {
    tabs->erase(tabs->begin() + index);
  }
  return drop.size();
}

}  // namespace

LoadOutcome ReadStateFile(const base::FilePath& path) {
  LoadOutcome outcome;
  // The writer needs the folder. It is made here, off the UI sequence.
  base::CreateDirectory(path.DirName());
  if (!base::PathExists(path)) {
    outcome.status = LoadStatus::kMissing;
    return outcome;
  }
  std::string text;
  if (!base::ReadFileToStringWithMaxSize(path, &text, kMaxStateBytes)) {
    // Too large, or not readable. Keep it and start empty.
    outcome.status = LoadStatus::kCorrupt;
    KeepAsBad(path);
    return outcome;
  }
  std::optional<base::Value> json =
      base::JSONReader::Read(text, base::JSON_PARSE_RFC);
  if (!json || !json->is_dict()) {
    outcome.status = LoadStatus::kCorrupt;
    KeepAsBad(path);
    return outcome;
  }
  ParseResult parsed = ParseState(json->GetDict());
  outcome.status = parsed.status;
  if (parsed.status == LoadStatus::kOk) {
    outcome.state = std::move(parsed.state);
  } else {
    KeepAsBad(path);
  }
  return outcome;
}

TabModelPersistence::TabModelPersistence(
    TabModel* model,
    base::FilePath path,
    scoped_refptr<base::SequencedTaskRunner> io_runner)
    : model_(model), path_(std::move(path)), io_runner_(std::move(io_runner)) {
  CHECK(model_);
  if (!path_.empty()) {
    CHECK(io_runner_);
    writer_.emplace(path_, io_runner_, "AerieTabModel");
  }
  observation_.Observe(model_);
}

TabModelPersistence::~TabModelPersistence() {
  FlushPendingWrite();
}

void TabModelPersistence::Load(base::OnceCallback<void(LoadStatus)> done) {
  CHECK(!load_started_);
  load_started_ = true;
  if (path_.empty()) {
    // In memory only: there is nothing to read.
    OnRead(std::move(done), LoadOutcome());
    return;
  }
  io_runner_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&ReadStateFile, path_),
      base::BindOnce(&TabModelPersistence::OnRead, weak_factory_.GetWeakPtr(),
                     std::move(done)));
}

void TabModelPersistence::OnRead(base::OnceCallback<void(LoadStatus)> done,
                                 LoadOutcome outcome) {
  const LoadStatus status = outcome.status;
  // Mark as loaded first, so that the changes the merge makes are saved.
  loaded_ = true;
  model_->MergeLoaded(std::move(outcome.state));
  if (writer_) {
    // A fresh profile writes its Workspace; a bad file is replaced by an
    // empty one (the bad one was kept).
    writer_->ScheduleWrite(this);
  }
  if (done) {
    std::move(done).Run(status);
  }
}

void TabModelPersistence::FlushPendingWrite() {
  if (writer_ && writer_->HasPendingWrite()) {
    writer_->DoScheduledWrite();
  }
}

void TabModelPersistence::OnModelChanged() {
  if (loaded_ && writer_) {
    writer_->ScheduleWrite(this);
  }
}

std::optional<std::string> TabModelPersistence::SerializeData() {
  base::DictValue state = model_->ToValue();
  std::optional<std::string> text = base::WriteJson(state);
  if (!text || text->size() <= kMaxStateBytes) {
    return text;
  }
  // ReadStateFile would refuse this file and start the next session empty.
  // Degrade without losing what matters: leave the oldest archived tabs out of
  // the file (the model in memory is not changed) until it fits.
  const size_t dropped = DropOldestArchivedTabs(state, text->size());
  if (dropped > 0) {
    text = base::WriteJson(state);
  }
  if (!text || text->size() > kMaxStateBytes) {
    // Still too large with no archive left. Returning nothing makes the writer
    // skip the write, so the last good file stays on disk.
    LOG(ERROR) << "Tab Model state is over the read limit of " << kMaxStateBytes
               << " bytes even without the archive: not written";
    return std::nullopt;
  }
  LOG(WARNING) << "Tab Model state was over the read limit of "
               << kMaxStateBytes << " bytes: " << dropped
               << " oldest archived tabs left out of the file";
  return text;
}

}  // namespace aerie_tab_model
