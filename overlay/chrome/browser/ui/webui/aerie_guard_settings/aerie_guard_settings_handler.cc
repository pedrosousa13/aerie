// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/webui/aerie_guard_settings/aerie_guard_settings_handler.h"

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "components/aerie_adblock/browser/blocking_level.h"
#include "components/aerie_adblock/browser/filter_list_state.h"
#include "components/aerie_adblock/browser/list_failure.h"
#include "content/public/browser/web_ui.h"
#include "url/gurl.h"

namespace aerie_guard {

namespace {

using aerie_adblock::ExceptionResult;
using aerie_adblock::FilterListEntry;
using aerie_adblock::FilterListManager;
using aerie_adblock::SessionKey;

// The longest string argument the handler accepts. Longer than any acceptable
// list address or host name.
constexpr size_t kMaxArgLength = 4096;

constexpr char kStateChangedEvent[] = "aerie-guard-state-changed";

// The callback id the page's sendWithPromise put first, or null.
const std::string* CallbackId(const base::ListValue& args) {
  if (args.empty() || !args[0].is_string()) {
    return nullptr;
  }
  return &args[0].GetString();
}

// A string argument (index 1 and up, after the callback id) of at most
// kMaxArgLength bytes, or null.
const std::string* StringArg(const base::ListValue& args, size_t index) {
  if (index >= args.size() || !args[index].is_string()) {
    return nullptr;
  }
  const std::string& text = args[index].GetString();
  return text.size() <= kMaxArgLength ? &text : nullptr;
}

const char* UpdateStatusName(FilterListManager::UpdateStatus status) {
  switch (status) {
    case FilterListManager::UpdateStatus::kIdle:
      return "idle";
    case FilterListManager::UpdateStatus::kUpdating:
      return "updating";
    case FilterListManager::UpdateStatus::kUpdated:
      return "updated";
    case FilterListManager::UpdateStatus::kFailed:
      return "failed";
  }
  return "idle";
}

}  // namespace

AerieGuardSettingsHandler::AerieGuardSettingsHandler(Profile* profile)
    : profile_(profile) {}
AerieGuardSettingsHandler::~AerieGuardSettingsHandler() = default;

// static
base::DictValue AerieGuardSettingsHandler::StateToValue(
    const FilterListManager& manager,
    std::string_view add_error,
    std::string_view exception_error,
    std::string_view list_error) {
  const aerie_adblock::FilterListState& state = manager.state();
  base::DictValue out;
  out.Set("ready", true);
  out.Set("busy", manager.busy());
  out.Set("autoUpdate", state.auto_update());
  out.Set("defaultLevel",
          std::string(aerie_adblock::BlockingLevelName(state.default_level())));
  out.Set("addError", std::string(add_error));
  out.Set("exceptionError", std::string(exception_error));
  out.Set("listError", std::string(list_error));
  // Settings runs in the regular profile: the saved exceptions only, never
  // the in-memory ones of an incognito window.
  base::ListValue exceptions;
  for (const std::string& host :
       manager.ExceptionsFor(SessionKey::Regular())) {
    exceptions.Append(host);
  }
  out.Set("exceptions", std::move(exceptions));
  base::ListValue lists;
  for (const FilterListEntry& entry : state.lists()) {
    base::DictValue item;
    item.Set("id", entry.id);
    item.Set("url", entry.url);
    item.Set("title", entry.title);
    item.Set("builtin", entry.builtin);
    item.Set("rules", entry.rules);
    item.Set("ignoredExceptions", entry.ignored_exceptions);
    item.Set("enabled", entry.enabled);
    item.Set("required", !aerie_adblock::CanBeDisabled(entry));
    item.Set("status", UpdateStatusName(manager.UpdateStatusFor(entry.id)));
    // Milliseconds since the epoch, or null when this list still uses the copy
    // that shipped in the build (or has not been downloaded).
    if (entry.updated.is_null()) {
      item.Set("updated", base::Value());
    } else {
      item.Set("updated", entry.updated.InMillisecondsFSinceUnixEpoch());
    }
    item.Set("error", entry.error);
    // The page shows its own text for the code, not the manager's sentence.
    item.Set("errorCode", aerie_adblock::ListFailureCodeForMessage(entry.error));
    lists.Append(std::move(item));
  }
  out.Set("lists", std::move(lists));
  return out;
}

void AerieGuardSettingsHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "aerieGuard.getState",
      base::BindRepeating(&AerieGuardSettingsHandler::HandleGetState,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "aerieGuard.addList",
      base::BindRepeating(&AerieGuardSettingsHandler::HandleAddList,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "aerieGuard.removeList",
      base::BindRepeating(&AerieGuardSettingsHandler::HandleRemoveList,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "aerieGuard.setListEnabled",
      base::BindRepeating(&AerieGuardSettingsHandler::HandleSetListEnabled,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "aerieGuard.updateList",
      base::BindRepeating(&AerieGuardSettingsHandler::HandleUpdateList,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "aerieGuard.updateAll",
      base::BindRepeating(&AerieGuardSettingsHandler::HandleUpdateAll,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "aerieGuard.setException",
      base::BindRepeating(&AerieGuardSettingsHandler::HandleSetException,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "aerieGuard.setDefaultLevel",
      base::BindRepeating(&AerieGuardSettingsHandler::HandleSetDefaultLevel,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "aerieGuard.setAutoUpdate",
      base::BindRepeating(&AerieGuardSettingsHandler::HandleSetAutoUpdate,
                          base::Unretained(this)));
}

void AerieGuardSettingsHandler::OnJavascriptDisallowed() {
  observation_.Reset();
}

void AerieGuardSettingsHandler::OnFilterListsChanged() {
  FilterListManager* manager = FilterListManager::Get();
  if (manager && IsJavascriptAllowed()) {
    FireWebUIListener(kStateChangedEvent,
                      StateToValue(*manager, std::string_view(),
                                   std::string_view()));
  }
}

FilterListManager* AerieGuardSettingsHandler::Begin(
    const base::ListValue& args) {
  AllowJavascript();
  FilterListManager* manager = FilterListManager::Get();
  if (!manager) {
    Reject(args, "unavailable");
    return nullptr;
  }
  if (!observation_.IsObserving()) {
    observation_.Observe(manager);
  }
  return manager;
}

void AerieGuardSettingsHandler::Resolve(const base::ListValue& args,
                                        std::string_view add_error,
                                        std::string_view exception_error,
                                        std::string_view list_error) {
  FilterListManager* manager = FilterListManager::Get();
  const std::string* id = CallbackId(args);
  if (!manager || !id) {
    return;
  }
  ResolveJavascriptCallback(
      base::Value(*id), StateToValue(*manager, add_error, exception_error, list_error));
}

void AerieGuardSettingsHandler::Reject(const base::ListValue& args,
                                       std::string_view code) {
  if (const std::string* id = CallbackId(args)) {
    AllowJavascript();
    RejectJavascriptCallback(base::Value(*id), base::Value(std::string(code)));
  }
}

void AerieGuardSettingsHandler::HandleGetState(const base::ListValue& args) {
  if (!CallbackId(args)) {
    return;
  }
  if (Begin(args)) {
    Resolve(args, std::string_view(), std::string_view());
  }
}

void AerieGuardSettingsHandler::HandleAddList(const base::ListValue& args) {
  const std::string* url = StringArg(args, 1);
  if (!CallbackId(args)) {
    return;
  }
  if (!url) {
    Reject(args, "bad_argument");
    return;
  }
  FilterListManager* manager = Begin(args);
  if (!manager) {
    return;
  }
  std::string error;
  // On success the manager's own change notice updates the page later.
  manager->AddList(GURL(*url), &error);
  Resolve(args, error, std::string_view());
}

void AerieGuardSettingsHandler::HandleRemoveList(const base::ListValue& args) {
  const std::string* id = StringArg(args, 1);
  if (!CallbackId(args)) {
    return;
  }
  if (!id) {
    Reject(args, "bad_argument");
    return;
  }
  if (FilterListManager* manager = Begin(args)) {
    manager->RemoveList(*id);
    Resolve(args, std::string_view(), std::string_view());
  }
}

void AerieGuardSettingsHandler::HandleSetListEnabled(
    const base::ListValue& args) {
  const std::string* id = StringArg(args, 1);
  if (!CallbackId(args)) {
    return;
  }
  if (!id || args.size() != 3 || !args[2].is_bool()) {
    Reject(args, "bad_argument");
    return;
  }
  FilterListManager* manager = Begin(args);
  if (!manager) {
    return;
  }
  // The manager refuses EasyList whatever the page sends.
  std::string_view error;
  switch (manager->SetListEnabled(*id, args[2].GetBool())) {
    case aerie_adblock::FilterListState::EnableResult::kOk:
      break;
    case aerie_adblock::FilterListState::EnableResult::kRequired:
      error = "required";
      break;
    case aerie_adblock::FilterListState::EnableResult::kUnknownList:
      error = "unknown_list";
      break;
  }
  Resolve(args, std::string_view(), std::string_view(), error);
}

void AerieGuardSettingsHandler::HandleUpdateList(const base::ListValue& args) {
  const std::string* id = StringArg(args, 1);
  if (!CallbackId(args)) {
    return;
  }
  if (!id) {
    Reject(args, "bad_argument");
    return;
  }
  if (FilterListManager* manager = Begin(args)) {
    manager->UpdateList(*id);
    Resolve(args, std::string_view(), std::string_view());
  }
}

void AerieGuardSettingsHandler::HandleUpdateAll(const base::ListValue& args) {
  if (!CallbackId(args)) {
    return;
  }
  if (FilterListManager* manager = Begin(args)) {
    manager->UpdateAll();
    Resolve(args, std::string_view(), std::string_view());
  }
}

void AerieGuardSettingsHandler::HandleSetException(
    const base::ListValue& args) {
  const std::string* host = StringArg(args, 1);
  if (!CallbackId(args)) {
    return;
  }
  if (!host || args.size() != 3 || !args[2].is_bool()) {
    Reject(args, "bad_argument");
    return;
  }
  FilterListManager* manager = Begin(args);
  if (!manager) {
    return;
  }
  // The regular profile, whichever profile this page runs in: the manager
  // saves the exception to disk for it, and an incognito window's in-memory
  // exceptions are never touched from here.
  std::string_view error;
  switch (manager->SetException(*host, args[2].GetBool(),
                                profile_->GetOriginalProfile())) {
    case ExceptionResult::kOk:
      break;
    case ExceptionResult::kInvalidHost:
      error = "invalid_host";
      break;
    case ExceptionResult::kTableFull:
      error = "table_full";
      break;
    case ExceptionResult::kSavedInRegularProfile:
      error = "locked";
      break;
  }
  Resolve(args, std::string_view(), error);
}

void AerieGuardSettingsHandler::HandleSetDefaultLevel(
    const base::ListValue& args) {
  const std::string* name = StringArg(args, 1);
  if (!CallbackId(args)) {
    return;
  }
  // Only the two level names are accepted. Anything else changes nothing.
  std::optional<aerie_adblock::BlockingLevel> level =
      name && args.size() == 2 ? aerie_adblock::BlockingLevelFromName(*name)
                               : std::nullopt;
  if (!level) {
    Reject(args, "bad_argument");
    return;
  }
  if (FilterListManager* manager = Begin(args)) {
    manager->SetDefaultBlockingLevel(*level);
    Resolve(args, std::string_view(), std::string_view());
  }
}

void AerieGuardSettingsHandler::HandleSetAutoUpdate(
    const base::ListValue& args) {
  if (!CallbackId(args)) {
    return;
  }
  if (args.size() != 2 || !args[1].is_bool()) {
    Reject(args, "bad_argument");
    return;
  }
  if (FilterListManager* manager = Begin(args)) {
    manager->SetAutoUpdate(args[1].GetBool());
    Resolve(args, std::string_view(), std::string_view());
  }
}

}  // namespace aerie_guard
