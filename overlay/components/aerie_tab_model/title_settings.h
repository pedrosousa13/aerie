// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_TITLE_SETTINGS_H_
#define COMPONENTS_AERIE_TAB_MODEL_TITLE_SETTINGS_H_

#include <stddef.h>

#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"
#include "components/aerie_tab_model/rename_rules.h"

// What the user chose for the titles of the sidebar (issue #146): the built-in
// tidy (on by default, off globally or for a host) and the rename rules. Plain
// data, read from the profile prefs (tab_model_prefs.h) and checked on the way
// in; the resolver's `rule` and `tidy` providers read it.
namespace aerie_tab_model {

// The most hosts in the tidy's "off for this site" list.
inline constexpr size_t kMaxTidyOffSites = 200;

struct TitleSettings {
  // The global switch of the built-in tidy.
  bool tidy_enabled = true;
  // Canonical hosts (NormalizeDomain) where the tidy is off, with their
  // subdomains.
  std::vector<std::string> tidy_off_sites;
  RenameRules rename;

  bool operator==(const TitleSettings&) const = default;

  // True when the tidy runs for a page on `host` (a GURL host).
  bool TidyAppliesTo(std::string_view host) const;
};

// The settings for the pref values. Every list entry is checked: a host is made
// canonical and a duplicate or empty one dropped, a bad rule is dropped.
TitleSettings MakeTitleSettings(bool tidy_enabled,
                                const base::ListValue& tidy_off_sites,
                                bool rules_enabled,
                                const base::ListValue& rules);

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_TITLE_SETTINGS_H_
