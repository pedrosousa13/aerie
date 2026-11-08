// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/webui/aerie_filters/aerie_filters_handler.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "components/aerie_adblock/browser/filter_list_state.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "url/gurl.h"

namespace aerie_adblock {

namespace {

// A URL typed by the user. Longer than any acceptable list address.
constexpr size_t kMaxArgLength = 4096;

const std::string* StringArg(const base::ListValue& args, size_t index) {
  if (index >= args.size() || !args[index].is_string()) {
    return nullptr;
  }
  const std::string& text = args[index].GetString();
  return text.size() <= kMaxArgLength ? &text : nullptr;
}

}  // namespace

AerieFiltersHandler::AerieFiltersHandler() = default;
AerieFiltersHandler::~AerieFiltersHandler() = default;

// static
base::DictValue AerieFiltersHandler::StateToValue(
    const FilterListManager& manager,
    const std::string& add_error,
    const void* session) {
  const FilterListState& state = manager.state();
  base::DictValue out;
  out.Set("ready", true);
  out.Set("busy", manager.busy());
  out.Set("autoUpdate", state.auto_update());
  out.Set("addError", add_error);
  base::ListValue exceptions;
  for (const std::string& host : state.exceptions()) {
    exceptions.Append(host);
  }
  for (const std::string& host : manager.SessionExceptions(session)) {
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
    // Milliseconds since the epoch, or null when this list still uses the copy
    // that shipped in the build (or has not been downloaded).
    if (entry.updated.is_null()) {
      item.Set("updated", base::Value());
    } else {
      item.Set("updated", entry.updated.InMillisecondsFSinceUnixEpoch());
    }
    item.Set("error", entry.error);
    lists.Append(std::move(item));
  }
  out.Set("lists", std::move(lists));
  return out;
}

void AerieFiltersHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "aerieFilters.getState",
      base::BindRepeating(&AerieFiltersHandler::HandleGetState,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "aerieFilters.addList",
      base::BindRepeating(&AerieFiltersHandler::HandleAddList,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "aerieFilters.removeList",
      base::BindRepeating(&AerieFiltersHandler::HandleRemoveList,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "aerieFilters.updateList",
      base::BindRepeating(&AerieFiltersHandler::HandleUpdateList,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "aerieFilters.updateAll",
      base::BindRepeating(&AerieFiltersHandler::HandleUpdateAll,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "aerieFilters.setException",
      base::BindRepeating(&AerieFiltersHandler::HandleSetException,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "aerieFilters.setAutoUpdate",
      base::BindRepeating(&AerieFiltersHandler::HandleSetAutoUpdate,
                          base::Unretained(this)));
}

void AerieFiltersHandler::OnJavascriptDisallowed() {
  page_ready_ = false;
  observation_.Reset();
}

void AerieFiltersHandler::OnFilterListsChanged() {
  PushState();
}

const void* AerieFiltersHandler::Session() {
  return SessionKeyFor(web_ui()->GetWebContents()->GetBrowserContext());
}

void AerieFiltersHandler::PushState() {
  FilterListManager* manager = FilterListManager::Get();
  if (!page_ready_ || !manager) {
    return;
  }
  web_ui()->CallJavascriptFunctionUnsafe(
      "aerieFilters.setState",
      base::Value(StateToValue(*manager, add_error_, Session())));
}

void AerieFiltersHandler::HandleGetState(const base::ListValue& args) {
  AllowJavascript();
  if (FilterListManager* manager = FilterListManager::Get()) {
    if (!observation_.IsObserving()) {
      observation_.Observe(manager);
    }
  }
  page_ready_ = true;
  PushState();
}

void AerieFiltersHandler::HandleAddList(const base::ListValue& args) {
  FilterListManager* manager = FilterListManager::Get();
  const std::string* url = StringArg(args, 0);
  if (!manager || !url) {
    return;
  }
  add_error_.clear();
  if (!manager->AddList(GURL(*url), &add_error_)) {
    PushState();  // On success the manager's own change notice does it.
  }
}

void AerieFiltersHandler::HandleRemoveList(const base::ListValue& args) {
  FilterListManager* manager = FilterListManager::Get();
  const std::string* id = StringArg(args, 0);
  if (manager && id) {
    manager->RemoveList(*id);
  }
}

void AerieFiltersHandler::HandleUpdateList(const base::ListValue& args) {
  FilterListManager* manager = FilterListManager::Get();
  const std::string* id = StringArg(args, 0);
  if (manager && id) {
    manager->UpdateList(*id);
  }
}

void AerieFiltersHandler::HandleUpdateAll(const base::ListValue& args) {
  if (FilterListManager* manager = FilterListManager::Get()) {
    manager->UpdateAll();
  }
}

void AerieFiltersHandler::HandleSetException(const base::ListValue& args) {
  FilterListManager* manager = FilterListManager::Get();
  const std::string* host = StringArg(args, 0);
  if (!manager || !host || args.size() < 2 || !args[1].is_bool()) {
    return;
  }
  add_error_.clear();
  const void* session = Session();
  bool excepted = args[1].GetBool();
  if (session && !excepted && manager->HasSavedException(*host)) {
    // The regular profile owns saved exceptions. Do not undo them from here.
    add_error_ =
        "This site was turned off in a regular window. Turn it back on there.";
    PushState();
    return;
  }
  if (session) {
    manager->WatchSession(web_ui()->GetWebContents()->GetBrowserContext());
  }
  if (!manager->SetException(*host, excepted, session)) {
    add_error_ = "That is not a valid site name.";
    PushState();
  }
}

void AerieFiltersHandler::HandleSetAutoUpdate(const base::ListValue& args) {
  FilterListManager* manager = FilterListManager::Get();
  if (manager && !args.empty() && args[0].is_bool()) {
    manager->SetAutoUpdate(args[0].GetBool());
  }
}

}  // namespace aerie_adblock
