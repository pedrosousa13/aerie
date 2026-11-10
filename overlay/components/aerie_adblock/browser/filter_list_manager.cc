// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/filter_list_manager.h"

#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/metrics/histogram_functions.h"
#include "base/supports_user_data.h"
#include "base/task/thread_pool.h"
#include "components/aerie_adblock/browser/adblock_engine.h"
#include "components/aerie_adblock/browser/filter_list_storage.h"
#include "content/public/browser/browser_context.h"
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
    // The per-site "Hide leftover ad boxes" switch (issue #101). Every late
    // report asks the policy again, so a change needs no renderer work. The
    // policy holds a WeakPtr: once the manager is gone, hiding is on.
    service_->SetCosmeticFilteringPolicy(base::BindRepeating(
        [](base::WeakPtr<FilterListManager> self, const GURL& top_level_url,
           SessionKey session) {
          return !self || self->AllowsCosmeticHiding(top_level_url, session);
        },
        weak_factory_.GetWeakPtr()));
    // The blocking level (issue #109) is asked for every request. Once the
    // manager is gone the level is the default: Aggressive.
    service_->SetBlockingLevelPolicy(base::BindRepeating(
        [](base::WeakPtr<FilterListManager> self, const std::string& host,
           SessionKey session) {
          return self ? self->EffectiveBlockingLevel(host, session)
                      : kDefaultBlockingLevel;
        },
        weak_factory_.GetWeakPtr()));
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
  MeasureShippedLists();
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

FilterListState::EnableResult FilterListManager::SetListEnabled(
    std::string_view id,
    bool enabled) {
  const FilterListEntry* entry = state_.Find(id);
  const bool changed = entry && entry->enabled != enabled;
  FilterListState::EnableResult result = state_.SetEnabled(id, enabled);
  if (result != FilterListState::EnableResult::kOk || !changed) {
    return result;
  }
  Save();
  Notify();
  Rebuild();
  return result;
}

FilterListManager::UpdateStatus FilterListManager::UpdateStatusFor(
    std::string_view id) const {
  const std::string key(id);
  if (updating_.count(key)) {
    return UpdateStatus::kUpdating;
  }
  const FilterListEntry* entry = state_.Find(id);
  if (entry && !entry->error.empty()) {
    return UpdateStatus::kFailed;
  }
  return updated_ok_.count(key) ? UpdateStatus::kUpdated : UpdateStatus::kIdle;
}

void FilterListManager::RemoveList(std::string_view id) {
  if (!state_.RemoveList(id)) {
    return;
  }
  updated_ok_.erase(std::string(id));
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
    if (entry.enabled) {  // A list that is off is not fetched on its own.
      ids.push_back(entry.id);
    }
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

namespace {

constexpr size_t kMaxSessionExceptions = 1000;
const char kSessionWatchKey[] = "aerie_adblock.session_watch";

// Lives in the off-the-record profile's user data, so it is destroyed with it.
class SessionWatch : public base::SupportsUserData::Data {
 public:
  explicit SessionWatch(SessionKey key) : key_(key) {}
  ~SessionWatch() override {
    if (FilterListManager* manager = FilterListManager::Get()) {
      manager->ClearSessionExceptions(key_);
    }
  }

 private:
  const SessionKey key_;
};

}  // namespace

ExceptionResult FilterListManager::SetExceptionForSession(
    std::string_view host,
    bool excepted,
    SessionKey session) {
  if (session.is_off_the_record()) {
    if (!excepted && IsExceptionLocked(host, session)) {
      return ExceptionResult::kSavedInRegularProfile;
    }
    std::string normalized = NormalizeExceptionHost(host);
    if (normalized.empty()) {
      return ExceptionResult::kInvalidHost;
    }
    std::set<std::string>& hosts = session_exceptions_[session];
    if (!excepted) {
      hosts.erase(normalized);
    } else if (hosts.size() < kMaxSessionExceptions || hosts.count(normalized)) {
      hosts.insert(std::move(normalized));
    } else {
      if (hosts.empty()) {
        session_exceptions_.erase(session);
      }
      return ExceptionResult::kTableFull;
    }
    if (service_) {
      service_->SetSessionExceptions(session, hosts);
    }
    if (hosts.empty()) {
      session_exceptions_.erase(session);
    }
    Notify();
    return ExceptionResult::kOk;  // No Save(): this never reaches the disk.
  }
  if (NormalizeExceptionHost(host).empty()) {
    return ExceptionResult::kInvalidHost;
  }
  if (!state_.SetException(host, excepted)) {
    return ExceptionResult::kTableFull;  // The only other way it refuses.
  }
  if (service_) {
    service_->SetExceptions(state_.exceptions());
  }
  Save();
  Notify();
  return ExceptionResult::kOk;
}

ExceptionResult FilterListManager::SetException(
    std::string_view host,
    bool excepted,
    content::BrowserContext* context) {
  WatchSession(context);  // Does nothing for a regular context.
  return SetExceptionForSession(host, excepted, SessionKeyFor(context));
}

bool FilterListManager::HasException(std::string_view host,
                                     SessionKey session) const {
  std::string normalized = NormalizeExceptionHost(host);
  if (HostHasException(normalized, state_.exceptions())) {
    return true;
  }
  auto it = session_exceptions_.find(session);
  return it != session_exceptions_.end() &&
         HostHasException(normalized, it->second);
}

bool FilterListManager::HasSavedException(std::string_view host) const {
  return HostHasException(NormalizeExceptionHost(host), state_.exceptions());
}

bool FilterListManager::IsExceptionLocked(std::string_view host,
                                          SessionKey session) const {
  return session.is_off_the_record() && HasSavedException(host);
}

std::set<std::string> FilterListManager::SessionExceptions(
    SessionKey session) const {
  auto it = session_exceptions_.find(session);
  return it == session_exceptions_.end() ? std::set<std::string>() : it->second;
}

std::vector<std::string> FilterListManager::ExceptionsFor(
    SessionKey session) const {
  std::set<std::string> hosts = state_.exceptions();
  hosts.merge(SessionExceptions(session));  // A host in both stays once.
  return std::vector<std::string>(hosts.begin(), hosts.end());
}

void FilterListManager::ClearSessionExceptions(SessionKey session) {
  const bool had_cosmetic = session_cosmetic_off_.erase(session) > 0;
  const bool had_level = session_levels_.erase(session) > 0;
  if (session_exceptions_.erase(session) == 0) {
    if (had_cosmetic || had_level) {
      Notify();
    }
    return;
  }
  if (service_) {
    service_->SetSessionExceptions(session, {});
  }
  Notify();
}

ExceptionResult FilterListManager::SetCosmeticHidingOffForSession(
    std::string_view host,
    bool off,
    SessionKey session) {
  std::string normalized = NormalizeExceptionHost(host);
  if (normalized.empty()) {
    return ExceptionResult::kInvalidHost;
  }
  if (session.is_off_the_record()) {
    if (!off && IsCosmeticHidingLocked(normalized, session)) {
      return ExceptionResult::kSavedInRegularProfile;
    }
    std::set<std::string>& hosts = session_cosmetic_off_[session];
    if (!off) {
      hosts.erase(normalized);
    } else if (hosts.size() < kMaxCosmeticOffHosts || hosts.count(normalized)) {
      hosts.insert(std::move(normalized));
    } else {
      if (hosts.empty()) {
        session_cosmetic_off_.erase(session);
      }
      return ExceptionResult::kTableFull;
    }
    if (hosts.empty()) {
      session_cosmetic_off_.erase(session);
    }
    Notify();
    return ExceptionResult::kOk;  // No Save(): this never reaches the disk.
  }
  if (!state_.SetCosmeticOff(normalized, off)) {
    return ExceptionResult::kTableFull;
  }
  Save();
  Notify();
  return ExceptionResult::kOk;
}

ExceptionResult FilterListManager::SetCosmeticHidingOff(
    std::string_view host,
    bool off,
    content::BrowserContext* context) {
  WatchSession(context);  // Does nothing for a regular context.
  return SetCosmeticHidingOffForSession(host, off, SessionKeyFor(context));
}

bool FilterListManager::IsCosmeticHidingOff(std::string_view host,
                                            SessionKey session) const {
  std::string normalized = NormalizeExceptionHost(host);
  if (HostHasException(normalized, state_.cosmetic_off_hosts())) {
    return true;
  }
  auto it = session_cosmetic_off_.find(session);
  return it != session_cosmetic_off_.end() &&
         HostHasException(normalized, it->second);
}

bool FilterListManager::IsCosmeticHidingLocked(std::string_view host,
                                               SessionKey session) const {
  return session.is_off_the_record() &&
         HostHasException(NormalizeExceptionHost(host),
                          state_.cosmetic_off_hosts());
}

bool FilterListManager::AllowsCosmeticHiding(const GURL& top_level_url,
                                             SessionKey session) const {
  return !IsCosmeticHidingOff(top_level_url.host(), session);
}

ExceptionResult FilterListManager::SetBlockingLevelForSession(
    std::string_view host,
    BlockingLevel level,
    SessionKey session) {
  std::string normalized = NormalizeExceptionHost(host);
  if (normalized.empty()) {
    return ExceptionResult::kInvalidHost;
  }
  if (session.is_off_the_record()) {
    std::map<std::string, BlockingLevel>& levels = session_levels_[session];
    if (levels.size() >= kMaxLevelOverrides && !levels.count(normalized)) {
      if (levels.empty()) {
        session_levels_.erase(session);
      }
      return ExceptionResult::kTableFull;
    }
    levels[std::move(normalized)] = level;
    Notify();
    return ExceptionResult::kOk;  // No Save(): this never reaches the disk.
  }
  if (!state_.SetLevelOverride(normalized, level)) {
    return ExceptionResult::kTableFull;
  }
  Save();
  Notify();
  return ExceptionResult::kOk;
}

ExceptionResult FilterListManager::SetBlockingLevel(
    std::string_view host,
    BlockingLevel level,
    content::BrowserContext* context) {
  WatchSession(context);  // Does nothing for a regular context.
  return SetBlockingLevelForSession(host, level, SessionKeyFor(context));
}

BlockingLevel FilterListManager::EffectiveBlockingLevel(
    std::string_view host,
    SessionKey session) const {
  const std::string normalized = NormalizeExceptionHost(host);
  auto it = session_levels_.find(session);
  if (it != session_levels_.end()) {
    if (std::optional<BlockingLevel> own =
            FindBlockingLevel(normalized, it->second)) {
      return *own;
    }
  }
  return FindBlockingLevel(normalized, state_.level_overrides())
      .value_or(state_.default_level());
}

void FilterListManager::SetDefaultBlockingLevel(BlockingLevel level) {
  state_.SetDefaultLevel(level);
  Save();
  Notify();
}

void FilterListManager::WatchSession(content::BrowserContext* context) {
  SessionKey key = SessionKeyFor(context);
  if (key.is_regular() || context->GetUserData(kSessionWatchKey)) {
    return;
  }
  context->SetUserData(kSessionWatchKey, std::make_unique<SessionWatch>(key));
}

void FilterListManager::SetAutoUpdate(bool enabled) {
  state_.SetAutoUpdate(enabled);
  Save();
  Notify();
}

void FilterListManager::FetchList(const std::string& id) {
  const FilterListEntry* entry = state_.Find(id);
  if (!entry || !updating_.insert(id).second) {
    return;  // Unknown, or already being updated.
  }
  updated_ok_.erase(id);
  Begin();
  Notify();  // The page shows "Updating" at once.
  fetcher_.Fetch(GURL(entry->url),
                 base::BindOnce(&FilterListManager::OnFetched,
                                weak_factory_.GetWeakPtr(), id));
}

void FilterListManager::SetError(const std::string& id, ListFailure failure) {
  if (FilterListEntry* entry = state_.Find(id)) {
    entry->error = ListFailureMessage(failure);
  }
  updated_ok_.erase(id);
  base::UmaHistogramBoolean("Aerie.Adblock.ListUpdateSuccess", false);
  Save();
}

void FilterListManager::FinishFetch(const std::string& id) {
  updating_.erase(id);
  End();
}

void FilterListManager::OnFetched(std::string id, FilterListFetcher::Result result) {
  if (!state_.Find(id)) {  // Removed while downloading.
    FinishFetch(id);
    return;
  }
  if (result.failure != ListFailure::kNone) {
    SetError(id, result.failure);
    FinishFetch(id);
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
    FinishFetch(id);
    return;
  }
  if (check.failure != ListFailure::kNone) {
    // The copy in use stays. Only the message changes.
    SetError(id, check.failure);
    FinishFetch(id);
    return;
  }
  if (check.ignored_exceptions > 0) {
    LOG(WARNING) << "aerie_adblock: filter list " << id << " has "
                 << check.ignored_exceptions
                 << " exception rule(s) that match every address; they are "
                    "ignored";
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
    FinishFetch(id);
    return;
  }
  if (!written) {
    SetError(id, ListFailure::kUnreadable);
    FinishFetch(id);
    return;
  }
  entry->rules = check.rules;
  entry->ignored_exceptions = check.ignored_exceptions;
  entry->updated = base::Time::Now();
  entry->error.clear();
  if (!entry->builtin && !check.title.empty()) {
    entry->title = check.title;
  }
  updated_ok_.insert(id);
  base::UmaHistogramBoolean("Aerie.Adblock.ListUpdateSuccess", true);
  Save();
  Rebuild();
  FinishFetch(id);
}

void FilterListManager::Rebuild(bool retry) {
  if (!service_) {
    return;
  }
  std::vector<FilterEngineService::ListInput> inputs;
  std::vector<std::string> ids;
  for (const FilterListEntry& entry : state_.lists()) {
    if (!entry.enabled) {
      continue;  // Off: its file stays on disk, the engine does not get it.
    }
    FilterEngineService::ListInput input;
    input.custom = !entry.builtin;
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

void FilterListManager::MeasureShippedLists() {
  for (const FilterListEntry& entry : state_.lists()) {
    if (entry.builtin && entry.updated.is_null()) {
      MeasureShippedList(entry.id);
    }
  }
}

void FilterListManager::MeasureShippedList(const std::string& id) {
  // The count saved by an earlier run is not trusted: the build may have
  // changed. The shipped text is parsed again, off the UI thread.
  Begin();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::BEST_EFFORT},
      base::BindOnce(
          [](std::string text) {
            return CheckList(base::as_byte_span(text), kCheckBudget);
          },
          builtin_lists_.Run(id)),
      base::BindOnce(&FilterListManager::OnShippedMeasured,
                     weak_factory_.GetWeakPtr(), id));
}

void FilterListManager::OnShippedMeasured(std::string id, ListCheck check) {
  FilterListEntry* entry = state_.Find(id);
  // A download that finished meanwhile has its own, newer count.
  if (entry && entry->updated.is_null() && check.failure == ListFailure::kNone) {
    entry->rules = check.rules;
    entry->ignored_exceptions = check.ignored_exceptions;
  }
  End();
}

void FilterListManager::OnRebuilt(std::vector<std::string> ids,
                                  bool retry,
                                  BuildResult result) {
  if (result.failure == ListFailure::kNone) {
    if (result.ignored_exceptions > 0) {
      LOG(WARNING) << "aerie_adblock: " << result.ignored_exceptions
                   << " exception rule(s) that match every address were "
                      "ignored when the lists were built";
    }
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
      entry->ignored_exceptions = 0;
      file_runner_->PostTask(
          FROM_HERE,
          base::BindOnce(&filter_list_storage::DeleteList, dir_, id));
      Save();
      if (entry->builtin) {
        MeasureShippedList(id);  // The shipped copy is in use again.
      }
      if (retry) {
        Rebuild(/*retry=*/false);
      }
    }
  }
  End();
}

}  // namespace aerie_adblock
