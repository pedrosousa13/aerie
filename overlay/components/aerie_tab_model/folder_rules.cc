// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/folder_rules.h"

#include <algorithm>
#include <utility>

#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "base/values.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/tab_model.h"
#include "url/gurl.h"

namespace aerie_tab_model {

namespace {

std::vector<std::string> ReadStringList(const base::DictValue& dict,
                                        const char* key) {
  std::vector<std::string> out;
  if (const base::ListValue* list = dict.FindList(key)) {
    for (const base::Value& value : *list) {
      if (value.is_string() && !value.GetString().empty() &&
          out.size() < kMaxExceptionsPerFolder) {
        out.push_back(value.GetString());
      }
    }
  }
  return out;
}

base::ListValue StringListToValue(const std::vector<std::string>& items) {
  base::ListValue list;
  for (const std::string& item : items) {
    list.Append(item);
  }
  return list;
}

// What is stored for the text typed for a rule of `type`, or empty when
// nothing usable was typed.
std::string StoredValue(RuleType type, std::string_view value) {
  std::string trimmed(base::TrimWhitespaceASCII(value, base::TRIM_ALL));
  if (trimmed.size() > kMaxRuleValueLength) {
    return std::string();
  }
  if (type == RuleType::kDomain) {
    return NormalizeDomain(trimmed);
  }
  return trimmed;
}

// Runs `edit` on the rules of the folder and writes them back. `edit` returns
// false to leave the record alone.
template <typename Edit>
bool EditRules(TabModel& model, const std::string& folder_id, Edit edit) {
  const FolderRecord* folder = model.GetFolder(folder_id);
  if (!folder) {
    return false;
  }
  FolderRules rules = ReadFolderRules(*folder);
  if (!edit(rules)) {
    return false;
  }
  return model.UpdateFolder(folder_id, [&rules](FolderRecord& record) {
    record.rules = FolderRulesToValue(rules);
  });
}

}  // namespace

FolderRules ReadFolderRules(const FolderRecord& folder) {
  FolderRules rules;
  if (!folder.rules) {
    return rules;
  }
  const base::DictValue& dict = *folder.rules;
  rules.enabled = dict.FindBool("enabled").value_or(true);
  if (const base::ListValue* list = dict.FindList("rules")) {
    for (const base::Value& item : *list) {
      const base::DictValue* rule_dict = item.GetIfDict();
      if (!rule_dict || rules.rules.size() >= kMaxRulesPerFolder) {
        continue;
      }
      const std::string* id = rule_dict->FindString("id");
      const std::string* type_text = rule_dict->FindString("type");
      const std::string* value = rule_dict->FindString("value");
      Rule rule;
      if (!id || id->empty() || !type_text || !value ||
          !RuleTypeFromString(*type_text, &rule.type)) {
        continue;
      }
      rule.id = *id;
      rule.value = *value;
      rule.enabled = rule_dict->FindBool("enabled").value_or(true);
      rules.rules.push_back(std::move(rule));
    }
  }
  rules.excluded_hosts = ReadStringList(dict, "excluded_hosts");
  rules.excluded_tabs = ReadStringList(dict, "excluded_tabs");
  return rules;
}

base::DictValue FolderRulesToValue(const FolderRules& rules) {
  base::DictValue dict;
  dict.Set("enabled", rules.enabled);
  base::ListValue list;
  for (const Rule& rule : rules.rules) {
    base::DictValue item;
    item.Set("id", rule.id);
    item.Set("type", RuleTypeToString(rule.type));
    item.Set("value", rule.value);
    item.Set("enabled", rule.enabled);
    list.Append(std::move(item));
  }
  dict.Set("rules", std::move(list));
  dict.Set("excluded_hosts", StringListToValue(rules.excluded_hosts));
  dict.Set("excluded_tabs", StringListToValue(rules.excluded_tabs));
  return dict;
}

bool IsAutoFiling(const FolderRecord& folder) {
  return folder.kind == FolderKind::kAuto;
}

bool SetFolderRuleKind(TabModel& model,
                       const std::string& folder_id,
                       FolderKind kind) {
  const FolderRecord* folder = model.GetFolder(folder_id);
  if (!folder || kind == FolderKind::kSmart) {
    return false;
  }
  if (folder->kind == kind) {
    return true;
  }
  return model.UpdateFolder(
      folder_id, [kind](FolderRecord& record) { record.kind = kind; });
}

std::string AddRule(TabModel& model,
                    const std::string& folder_id,
                    RuleType type,
                    std::string_view value) {
  const std::string stored = StoredValue(type, value);
  if (stored.empty()) {
    return std::string();
  }
  std::string id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  const bool ok = EditRules(model, folder_id, [&](FolderRules& rules) {
    if (rules.rules.size() >= kMaxRulesPerFolder) {
      return false;
    }
    Rule rule;
    rule.id = id;
    rule.type = type;
    rule.value = stored;
    rules.rules.push_back(std::move(rule));
    return true;
  });
  return ok ? id : std::string();
}

bool UpdateRule(TabModel& model,
                const std::string& folder_id,
                const std::string& rule_id,
                RuleType type,
                std::string_view value) {
  const std::string stored = StoredValue(type, value);
  if (stored.empty()) {
    return false;
  }
  return EditRules(model, folder_id, [&](FolderRules& rules) {
    for (Rule& rule : rules.rules) {
      if (rule.id == rule_id) {
        if (rule.type == type && rule.value == stored) {
          return false;
        }
        rule.type = type;
        rule.value = stored;
        return true;
      }
    }
    return false;
  });
}

bool RemoveRule(TabModel& model,
                const std::string& folder_id,
                const std::string& rule_id) {
  return EditRules(model, folder_id, [&](FolderRules& rules) {
    return std::erase_if(rules.rules, [&](const Rule& rule) {
             return rule.id == rule_id;
           }) > 0;
  });
}

bool SetRuleEnabled(TabModel& model,
                    const std::string& folder_id,
                    const std::string& rule_id,
                    bool enabled) {
  return EditRules(model, folder_id, [&](FolderRules& rules) {
    for (Rule& rule : rules.rules) {
      if (rule.id == rule_id && rule.enabled != enabled) {
        rule.enabled = enabled;
        return true;
      }
    }
    return false;
  });
}

bool SetFolderRulesEnabled(TabModel& model,
                           const std::string& folder_id,
                           bool enabled) {
  return EditRules(model, folder_id, [&](FolderRules& rules) {
    if (rules.enabled == enabled) {
      return false;
    }
    rules.enabled = enabled;
    return true;
  });
}

bool AddExcludedHost(TabModel& model,
                     const std::string& folder_id,
                     std::string_view host) {
  const std::string stored = NormalizeDomain(host);
  if (stored.empty() || stored.size() > kMaxRuleValueLength) {
    return false;
  }
  return EditRules(model, folder_id, [&](FolderRules& rules) {
    if (rules.excluded_hosts.size() >= kMaxExceptionsPerFolder ||
        std::ranges::contains(rules.excluded_hosts, stored)) {
      return false;
    }
    rules.excluded_hosts.push_back(stored);
    return true;
  });
}

bool RemoveExcludedHost(TabModel& model,
                        const std::string& folder_id,
                        std::string_view host) {
  const std::string stored = NormalizeDomain(host);
  return EditRules(model, folder_id, [&](FolderRules& rules) {
    return std::erase(rules.excluded_hosts, stored) > 0;
  });
}

bool AddExcludedTab(TabModel& model,
                    const std::string& folder_id,
                    const std::string& tab_id) {
  if (tab_id.empty() || !model.GetTab(tab_id)) {
    return false;
  }
  return EditRules(model, folder_id, [&](FolderRules& rules) {
    if (rules.excluded_tabs.size() >= kMaxExceptionsPerFolder ||
        std::ranges::contains(rules.excluded_tabs, tab_id)) {
      return false;
    }
    rules.excluded_tabs.push_back(tab_id);
    return true;
  });
}

std::optional<AutoFileTarget> FindAutoFileTarget(const TabModel& model,
                                                 const GURL& url,
                                                 std::string_view title,
                                                 const std::string& tab_id,
                                                 bool rules_enabled) {
  if (!rules_enabled) {
    return std::nullopt;
  }
  // Pinned before Today, each depth first from the top.
  for (Section section : {Section::kPinned, Section::kToday}) {
    for (const FolderChoice& choice : FolderChoices(model, section)) {
      const FolderRecord* folder = model.GetFolder(choice.id);
      if (!folder || !IsAutoFiling(*folder) || !folder->rules) {
        continue;
      }
      const FolderRules rules = ReadFolderRules(*folder);
      const Rule* rule = FindMatchingRule(rules, url, title, tab_id);
      if (!rule) {
        continue;
      }
      if (!CanNest(model, ItemKind::kTab, tab_id, folder->id)) {
        continue;
      }
      return AutoFileTarget{folder->id, rule->id};
    }
  }
  return std::nullopt;
}

bool FileTabIntoFolder(TabModel& model,
                       const std::string& tab_id,
                       const AutoFileTarget& target,
                       const std::string& pinned_url) {
  const TabRecord* tab = model.GetTab(tab_id);
  const FolderRecord* folder = model.GetFolder(target.folder_id);
  if (!tab || !folder || tab->section == Section::kFavorite ||
      folder->section == Section::kFavorite ||
      !CanNest(model, ItemKind::kTab, tab_id, folder->id)) {
    return false;
  }
  const Section section = folder->section;
  if (!model.MoveTab(tab_id, section, folder->id,
                     FirstPosition(model, section, folder->id))) {
    return false;
  }
  model.UpdateTab(tab_id, [&](TabRecord& record) {
    record.filed_by = FiledBy{target.folder_id, target.rule_id};
    if (section == Section::kPinned) {
      record.pinned_url = pinned_url;
    }
  });
  return true;
}

bool UndoFiling(TabModel& model, const std::string& tab_id) {
  const TabRecord* tab = model.GetTab(tab_id);
  if (!tab || !tab->filed_by) {
    return false;
  }
  if (!model.MoveTab(tab_id, Section::kToday, std::string(),
                     FirstPosition(model, Section::kToday, std::string()))) {
    return false;
  }
  model.UpdateTab(tab_id, [](TabRecord& record) {
    record.pinned_url.reset();
    record.filed_by.reset();
  });
  return true;
}

bool StopRuleMatchingTab(TabModel& model, const std::string& tab_id) {
  const TabRecord* tab = model.GetTab(tab_id);
  if (!tab || !tab->filed_by) {
    return false;
  }
  return AddExcludedTab(model, tab->filed_by->folder_id, tab_id);
}

}  // namespace aerie_tab_model
