// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/title_settings.h"

#include <algorithm>

#include "components/aerie_tab_model/rule_matcher.h"

namespace aerie_tab_model {

bool TitleSettings::TidyAppliesTo(std::string_view host) const {
  if (!tidy_enabled) {
    return false;
  }
  for (const std::string& site : tidy_off_sites) {
    if (HostMatchesDomain(host, site)) {
      return false;
    }
  }
  return true;
}

TitleSettings MakeTitleSettings(bool tidy_enabled,
                                const base::ListValue& tidy_off_sites,
                                bool rules_enabled,
                                const base::ListValue& rules) {
  TitleSettings settings;
  settings.tidy_enabled = tidy_enabled;
  for (const base::Value& entry : tidy_off_sites) {
    if (settings.tidy_off_sites.size() >= kMaxTidyOffSites) {
      break;
    }
    if (!entry.is_string() || entry.GetString().size() > 256) {
      continue;
    }
    const std::string host = NormalizeDomain(entry.GetString());
    if (!host.empty() &&
        std::find(settings.tidy_off_sites.begin(), settings.tidy_off_sites.end(),
                  host) == settings.tidy_off_sites.end()) {
      settings.tidy_off_sites.push_back(host);
    }
  }
  settings.rename.enabled = rules_enabled;
  settings.rename.rules = RenameRulesFromList(rules);
  return settings;
}

}  // namespace aerie_tab_model
