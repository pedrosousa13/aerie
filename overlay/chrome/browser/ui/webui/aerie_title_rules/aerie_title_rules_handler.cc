// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/webui/aerie_title_rules/aerie_title_rules_handler.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "components/aerie_tab_model/tab_model_prefs.h"
#include "components/aerie_tab_model/tab_model_types.h"
#include "components/aerie_tab_model/title_resolver.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_ui.h"
#include "url/gurl.h"

namespace aerie_title_rules {

namespace {

// The longest string argument the handler accepts.
constexpr size_t kMaxArgLength = 4096;

const char* SourceName(aerie_tab_model::TitleSource source) {
  switch (source) {
    case aerie_tab_model::TitleSource::kRule:
      return "rule";
    case aerie_tab_model::TitleSource::kTidy:
      return "tidy";
    case aerie_tab_model::TitleSource::kManual:
    case aerie_tab_model::TitleSource::kAi:
    case aerie_tab_model::TitleSource::kPage:
      return "page";
  }
  return "page";
}

}  // namespace

AerieTitleRulesHandler::AerieTitleRulesHandler(Profile* profile)
    : profile_(profile) {}
AerieTitleRulesHandler::~AerieTitleRulesHandler() = default;

void AerieTitleRulesHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "aerieTitleRules.try",
      base::BindRepeating(&AerieTitleRulesHandler::HandleTry,
                          base::Unretained(this)));
}

void AerieTitleRulesHandler::HandleTry(const base::ListValue& args) {
  // args: [callback id, url, title].
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  const base::Value callback_id(args[0].GetString());
  AllowJavascript();
  if (args.size() != 3 || !args[1].is_string() || !args[2].is_string() ||
      args[1].GetString().size() > kMaxArgLength ||
      args[2].GetString().size() > kMaxArgLength) {
    RejectJavascriptCallback(callback_id, base::Value("bad_argument"));
    return;
  }
  // The saved settings, read now, checked as the sidebar checks them.
  const aerie_tab_model::TitleSettings settings =
      aerie_tab_model::GetTitleSettings(*profile_->GetPrefs());
  const aerie_tab_model::TitleResolver resolver =
      aerie_tab_model::TitleResolver::CreateDefault(&settings);
  const GURL url(args[1].GetString());
  const aerie_tab_model::ResolvedTitle resolved = resolver.ResolveTitle(
      aerie_tab_model::TabRecord(), args[2].GetString(), url);
  base::DictValue result;
  result.Set("text", resolved.text);
  result.Set("source", SourceName(resolved.source));
  result.Set("rule", resolved.detail);
  ResolveJavascriptCallback(callback_id, std::move(result));
}

}  // namespace aerie_title_rules
