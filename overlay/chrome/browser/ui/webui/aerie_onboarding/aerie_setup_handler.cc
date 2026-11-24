// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/webui/aerie_onboarding/aerie_setup_handler.h"

#include "base/functional/bind.h"
#include "base/logging.h"
#include "components/helium_services/pref_names.h"
#include "components/helium_services/schema.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_ui.h"

namespace aerie_onboarding {

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

}  // namespace aerie_onboarding
