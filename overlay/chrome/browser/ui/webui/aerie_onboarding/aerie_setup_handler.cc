// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/webui/aerie_onboarding/aerie_setup_handler.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/thread_pool.h"
#include "components/aerie_import_sources/browser_icon.h"
#include "components/helium_services/pref_names.h"
#include "components/helium_services/schema.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_ui.h"

namespace aerie_onboarding {

namespace {

// The page lists a handful of browsers; this is far more than it can have.
constexpr size_t kMaxIconNames = 64;
constexpr size_t kMaxIconNameLength = 100;

// Runs on a blocking-capable thread: reads the .desktop entries and icon files.
base::DictValue ResolveIcons(std::vector<base::FilePath> data_dirs,
                             std::vector<std::string> names) {
  base::DictValue icons;
  const aerie_import_sources::IconResolver resolver(std::move(data_dirs));
  for (const std::string& name : names) {
    std::string url = resolver.DataUrlFor(
        aerie_import_sources::DesktopIdsForBrowser(name));
    if (!url.empty()) {
      icons.Set(name, std::move(url));
    }
  }
  return icons;
}

}  // namespace

AerieSetupHandler::AerieSetupHandler(PrefService* prefs) : prefs_(prefs) {}

AerieSetupHandler::~AerieSetupHandler() = default;

void AerieSetupHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "setPref", base::BindRepeating(&AerieSetupHandler::HandleSetPref,
                                     base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "acceptLatestSchema",
      base::BindRepeating(&AerieSetupHandler::HandleAcceptLatestSchema,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "aerieSetup.getBrowserIcons",
      base::BindRepeating(&AerieSetupHandler::HandleGetBrowserIcons,
                          base::Unretained(this)));
}

void AerieSetupHandler::HandleSetPref(const base::ListValue& args) {
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  AllowJavascript();
  const std::string& callback_id = args[0].GetString();
  if (args.size() != 3 || !args[1].is_string() || !args[2].is_bool() ||
      args[1].GetString() != "completed_onboarding") {
    DVLOG(1) << "aerie setup: setPref refused";
    RejectJavascriptCallback(base::Value(callback_id),
                             base::Value("pref not allowed"));
    return;
  }
  prefs_->SetBoolean(::prefs::kHeliumDidOnboarding, args[2].GetBool());
  ResolveJavascriptCallback(base::Value(callback_id), base::Value());
}

void AerieSetupHandler::HandleAcceptLatestSchema(const base::ListValue&) {
  helium::AcceptCurrentSchema(*prefs_);
}

void AerieSetupHandler::HandleGetBrowserIcons(const base::ListValue& args) {
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  AllowJavascript();
  const std::string& callback_id = args[0].GetString();
  if (args.size() != 2 || !args[1].is_list() ||
      args[1].GetList().size() > kMaxIconNames) {
    DVLOG(1) << "aerie setup: getBrowserIcons refused";
    RejectJavascriptCallback(base::Value(callback_id),
                             base::Value("bad request"));
    return;
  }
  std::vector<std::string> names;
  std::set<std::string> seen;
  for (const base::Value& value : args[1].GetList()) {
    if (value.is_string() && value.GetString().size() <= kMaxIconNameLength &&
        seen.insert(value.GetString()).second) {
      names.push_back(value.GetString());
    }
  }
  std::vector<base::FilePath> data_dirs =
      icon_data_dirs_ ? *icon_data_dirs_
                      : aerie_import_sources::SystemDataDirs();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&ResolveIcons, std::move(data_dirs), std::move(names)),
      base::BindOnce(&AerieSetupHandler::OnBrowserIcons,
                     weak_factory_.GetWeakPtr(), callback_id));
}

void AerieSetupHandler::OnBrowserIcons(const std::string& callback_id,
                                       base::DictValue icons) {
  if (!IsJavascriptAllowed()) {
    return;
  }
  ResolveJavascriptCallback(base::Value(callback_id),
                            base::Value(std::move(icons)));
}

}  // namespace aerie_onboarding
