// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/filter_list_manager.h"

#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/metrics/histogram_functions.h"
#include "base/task/thread_pool.h"
#include "components/aerie_adblock/browser/adblock_engine.h"
#include "components/aerie_adblock/browser/filter_list_storage.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace aerie_adblock {

namespace {
FilterListManager* g_process_instance = nullptr;
}  // namespace

// static
FilterListManager* FilterListManager::Get() {
  return g_process_instance;
}

FilterListManager::FilterListManager(
    base::FilePath dir,
    base::WeakPtr<FilterEngineService> service,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    BuiltinListProvider builtin_lists,
    bool allow_loopback)
    : dir_(std::move(dir)),
      service_(std::move(service)),
      fetcher_(std::move(url_loader_factory), allow_loopback),
      builtin_lists_(std::move(builtin_lists)),
      allow_loopback_(allow_loopback),
      file_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {}

FilterListManager::~FilterListManager() {
  if (!loaded_ && service_) {
    service_->ReleaseHeldRequests();  // Never leave requests waiting.
  }
  if (g_process_instance == this) {
    g_process_instance = nullptr;
  }
}

void FilterListManager::SetAsProcessInstance() {
  CHECK(!g_process_instance || g_process_instance == this);
  g_process_instance = this;
}

void FilterListManager::Init() {
  if (service_) {
    service_->HoldRequestsUntilSettingsLoaded();
  }
  Begin();
  file_runner_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&filter_list_storage::LoadState, dir_),
      base::BindOnce(&FilterListManager::OnStateLoaded,
                     weak_factory_.GetWeakPtr()));
}

void FilterListManager::OnStateLoaded(FilterListState state) {
  state_ = std::move(state);
  loaded_ = true;
  if (service_) {
    service_->SetExceptions(state_.exceptions());  // Also ends the hold.
  }
  if (state_.last_update_check().is_null()) {
    state_.set_last_update_check(base::Time::Now());  // The clock starts now.
    Save();
  }
  if (state_.NeedsRebuild()) {
    Rebuild();
  }
  schedule_timer_.Start(
      FROM_HERE, kScheduleCheckInterval,
      base::BindRepeating(
          [](base::WeakPtr<FilterListManager> self) {
            if (self) {
              self->MaybeRunScheduledUpdate(base::Time::Now());
            }
          },
          weak_factory_.GetWeakPtr()));
  MaybeRunScheduledUpdate(base::Time::Now());
  End();
}

void FilterListManager::Save() {
  file_runner_->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::FilePath dir, FilterListState state) {
                       if (!filter_list_storage::SaveState(dir, state)) {
                         LOG(ERROR) << "aerie_adblock: cannot save the filter "
                                       "list settings";
                       }
                     },
                     dir_, state_));
}

void FilterListManager::Notify() {
  for (Observer& observer : observers_) {
    observer.OnFilterListsChanged();
  }
}

void FilterListManager::End() {
  CHECK_GT(pending_, 0);
  --pending_;
  Notify();
}

bool FilterListManager::AddList(const GURL& url, std::string* error) {
  std::string id;
  switch (state_.AddCustomList(url, allow_loopback_, &id)) {
    case FilterListState::AddResult::kAdded:
      break;
    case FilterListState::AddResult::kInvalidUrl:
      *error = ListFailureMessage(ListFailure::kInsecureUrl);
      return false;
    case FilterListState::AddResult::kDuplicate:
      *error = "This list is already added.";
      return false;
    case FilterListState::AddResult::kTooMany:
      *error = "Too many lists. Remove one first.";
      return false;
  }
  Save();
  Notify();
  FetchList(id);
  return true;
}

void FilterListManager::RemoveList(std::string_view id) {
  if (!state_.RemoveList(id)) {
    return;
  }
  file_runner_->PostTask(
      FROM_HERE, base::BindOnce(&filter_list_storage::DeleteList, dir_,
                                std::string(id)));
  Save();
  Notify();
  Rebuild();
}

void FilterListManager::UpdateList(std::string_view id) {
  if (state_.Find(id)) {
    FetchList(std::string(id));
  }
}

void FilterListManager::UpdateAll() {
  state_.set_last_update_check(base::Time::Now());
  Save();
  std::vector<std::string> ids;
  for (const FilterListEntry& entry : state_.lists()) {
    ids.push_back(entry.id);
  }
  for (const std::string& id : ids) {
    FetchList(id);
  }
}

void FilterListManager::MaybeRunScheduledUpdate(base::Time now) {
  if (!state_.auto_update()) {
    return;
  }
  base::Time last = state_.last_update_check();
  if (last.is_null() || last > now) {  // A clock that went back: start over.
    state_.set_last_update_check(now);
    Save();
    return;
  }
  if (now - last >= kUpdateInterval) {
    UpdateAll();
  }
}

bool FilterListManager::SetException(std::string_view host, bool excepted) {
  if (!state_.SetException(host, excepted)) {
    return false;
  }
  if (service_) {
    service_->SetExceptions(state_.exceptions());
  }
  Save();
  Notify();
  return true;
}

bool FilterListManager::HasException(std::string_view host) const {
  return HostHasException(NormalizeExceptionHost(host), state_.exceptions());
}

void FilterListManager::SetAutoUpdate(bool enabled) {
  state_.SetAutoUpdate(enabled);
  Save();
  Notify();
}

void FilterListManager::FetchList(const std::string& id) {
  const FilterListEntry* entry = state_.Find(id);
  if (!entry) {
    return;
  }
  Begin();
  fetcher_.Fetch(GURL(entry->url),
                 base::BindOnce(&FilterListManager::OnFetched,
                                weak_factory_.GetWeakPtr(), id));
}

void FilterListManager::SetError(const std::string& id, ListFailure failure) {
  if (FilterListEntry* entry = state_.Find(id)) {
    entry->error = ListFailureMessage(failure);
  }
  base::UmaHistogramBoolean("Aerie.Adblock.ListUpdateSuccess", false);
  Save();
}

void FilterListManager::OnFetched(std::string id, FilterListFetcher::Result result) {
  if (!state_.Find(id)) {  // Removed while downloading.
    End();
    return;
  }
  if (result.failure != ListFailure::kNone) {
    SetError(id, result.failure);
    End();
    return;
  }
  // The parse runs off the UI thread, with a time budget.
  auto text = std::make_unique<std::string>(std::move(result.body));
  std::string* text_ptr = text.get();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE},
      base::BindOnce(
          [](std::string* text) {
            return CheckList(base::as_byte_span(*text), kCheckBudget);
          },
          text_ptr),
      base::BindOnce(
          [](base::WeakPtr<FilterListManager> self, std::string id,
             std::unique_ptr<std::string> text, ListCheck check) {
            if (self) {
              self->OnChecked(std::move(id), std::move(*text), std::move(check));
            }
          },
          weak_factory_.GetWeakPtr(), id, std::move(text)));
}

void FilterListManager::OnChecked(std::string id, std::string text, ListCheck check) {
  if (!state_.Find(id)) {
    End();
    return;
  }
  if (check.failure != ListFailure::kNone) {
    // The copy in use stays. Only the message changes.
    SetError(id, check.failure);
    End();
    return;
  }
  file_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](base::FilePath dir, std::string id, std::string text) {
            return filter_list_storage::WriteList(dir, id, text);
          },
          dir_, id, std::move(text)),
      base::BindOnce(&FilterListManager::OnWritten, weak_factory_.GetWeakPtr(),
                     id, std::move(check)));
}

void FilterListManager::OnWritten(std::string id, ListCheck check, bool written) {
  FilterListEntry* entry = state_.Find(id);
  if (!entry) {
    End();
    return;
  }
  if (!written) {
    SetError(id, ListFailure::kUnreadable);
    End();
    return;
  }
  entry->rules = check.rules;
  entry->updated = base::Time::Now();
  entry->error.clear();
  if (!entry->builtin && !check.title.empty()) {
    entry->title = check.title;
  }
  base::UmaHistogramBoolean("Aerie.Adblock.ListUpdateSuccess", true);
  Save();
  Rebuild();
  End();
}

void FilterListManager::Rebuild(bool retry) {
  if (!service_) {
    return;
  }
  std::vector<FilterEngineService::ListInput> inputs;
  std::vector<std::string> ids;
  for (const FilterListEntry& entry : state_.lists()) {
    FilterEngineService::ListInput input;
    if (!entry.updated.is_null()) {
      input.path = filter_list_storage::ListPath(dir_, entry.id);
    } else if (entry.builtin) {
      input.text = builtin_lists_.Run(entry.id);
    } else {
      continue;  // A custom list that was never downloaded: nothing to use.
    }
    inputs.push_back(std::move(input));
    ids.push_back(entry.id);
  }
  Begin();
  service_->ReplaceLists(
      std::move(inputs),
      base::BindOnce(&FilterListManager::OnRebuilt, weak_factory_.GetWeakPtr(),
                     std::move(ids), retry));
}

void FilterListManager::OnRebuilt(std::vector<std::string> ids,
                                  bool retry,
                                  BuildResult result) {
  if (result.failure == ListFailure::kNone) {
    End();
    return;
  }
  LOG(ERROR) << "aerie_adblock: a filter list could not be built";
  // The list that failed is a saved copy gone bad (the downloads were
  // checked). Drop that copy: a built-in list goes back to the shipped text,
  // a custom list is skipped until it is updated again. The old rules keep
  // blocking meanwhile, and one retry puts the rest in force.
  if (result.failed_index < ids.size()) {
    const std::string& id = ids[result.failed_index];
    if (FilterListEntry* entry = state_.Find(id)) {
      entry->error = ListFailureMessage(result.failure);
      entry->updated = base::Time();
      entry->rules = 0;
      file_runner_->PostTask(
          FROM_HERE,
          base::BindOnce(&filter_list_storage::DeleteList, dir_, id));
      Save();
      if (retry) {
        Rebuild(/*retry=*/false);
      }
    }
  }
  End();
}

}  // namespace aerie_adblock
