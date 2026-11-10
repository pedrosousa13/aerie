// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/filter_list_state.h"

#include <algorithm>
#include <utility>

#include "base/strings/string_util.h"
#include "base/strings/string_number_conversions.h"
#include "components/aerie_adblock/browser/list_failure.h"
#include "crypto/hash.h"

namespace aerie_adblock {

namespace {

constexpr size_t kMaxUrlLength = 2048;
constexpr size_t kMaxTitleLength = 100;
constexpr int kMaxRules = 50'000'000;

bool IsLoopbackHost(const GURL& url) {
  std::string host(url.host());
  return host == "127.0.0.1" || host == "localhost" || host == "[::1]";
}

bool IsKnownErrorMessage(const std::string& text) {
  if (text.empty()) {
    return true;
  }
  for (int i = static_cast<int>(ListFailure::kTooLarge);
       i <= static_cast<int>(ListFailure::kInsecureUrl); ++i) {
    if (text == ListFailureMessage(static_cast<ListFailure>(i))) {
      return true;
    }
  }
  return false;
}

std::string CustomListId(const GURL& url) {
  // 16 hex digits of the address's hash: stable, short, safe in a file name.
  auto digest = crypto::hash::Sha256(url.spec());
  return "c" + base::ToLowerASCII(base::HexEncode(
                   base::span(digest).first<8u>()));
}

// The lists that have an address and are not built in. The user's own rules
// are a list too, but have no slot in the table.
size_t CountCustomLists(const std::vector<FilterListEntry>& lists) {
  return std::count_if(lists.begin(), lists.end(), [](const auto& e) {
    return !e.builtin && e.id != kCustomRulesId;
  });
}

FilterListEntry BuiltIn(const char* id, const char* url, const char* title) {
  FilterListEntry entry;
  entry.id = id;
  entry.url = url;
  entry.title = title;
  entry.builtin = true;
  return entry;
}

}  // namespace

bool IsValidListId(std::string_view id) {
  return !id.empty() && id.size() <= 32 &&
         std::all_of(id.begin(), id.end(), [](char c) {
           return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
         });
}

bool CanBeDisabled(const FilterListEntry& entry) {
  return entry.id != kEasyListId;
}

bool IsAcceptableListUrl(const GURL& url, bool allow_loopback) {
  if (!url.is_valid() || url.spec().size() > kMaxUrlLength || url.has_ref() ||
      url.has_username() || url.has_password() || !url.has_host()) {
    return false;
  }
  if (url.SchemeIs("https")) {
    return true;
  }
  return allow_loopback && url.SchemeIs("http") && IsLoopbackHost(url);
}

std::string NormalizeExceptionHost(std::string_view host) {
  std::string out = base::ToLowerASCII(host);
  if (!out.empty() && out.back() == '.') {
    out.pop_back();
  }
  if (out.empty() || out.size() > kMaxHostLength || out.front() == '.' ||
      out.find("..") != std::string::npos) {
    return std::string();
  }
  for (char c : out) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
              c == '.' || c == ':' || c == '[' || c == ']';
    if (!ok) {
      return std::string();
    }
  }
  return out;
}

bool HostHasException(std::string_view host,
                      const std::set<std::string>& exceptions) {
  if (host.empty()) {
    return false;
  }
  std::string current(host);
  while (true) {
    if (exceptions.count(current)) {
      return true;
    }
    size_t dot = current.find('.');
    if (dot == std::string::npos) {
      return false;
    }
    current.erase(0, dot + 1);
  }
}

std::optional<BlockingLevel> FindBlockingLevel(
    std::string_view host,
    const std::map<std::string, BlockingLevel>& levels) {
  if (host.empty()) {
    return std::nullopt;
  }
  std::string current(host);
  while (true) {
    auto it = levels.find(current);
    if (it != levels.end()) {
      return it->second;
    }
    size_t dot = current.find('.');
    if (dot == std::string::npos) {
      return std::nullopt;
    }
    current.erase(0, dot + 1);
  }
}

FilterListState::FilterListState() {
  lists_.push_back(BuiltIn(kEasyListId, kEasyListUrl, "EasyList"));
  lists_.push_back(BuiltIn(kEasyPrivacyId, kEasyPrivacyUrl, "EasyPrivacy"));
}

FilterListState::FilterListState(const FilterListState&) = default;
FilterListState& FilterListState::operator=(const FilterListState&) = default;
FilterListState::~FilterListState() = default;

FilterListEntry* FilterListState::Find(std::string_view id) {
  for (FilterListEntry& entry : lists_) {
    if (entry.id == id) {
      return &entry;
    }
  }
  return nullptr;
}

const FilterListEntry* FilterListState::Find(std::string_view id) const {
  return const_cast<FilterListState*>(this)->Find(id);
}

FilterListState::AddResult FilterListState::AddCustomList(const GURL& url,
                                                          bool allow_loopback,
                                                          std::string* id) {
  if (!IsAcceptableListUrl(url, allow_loopback)) {
    return AddResult::kInvalidUrl;
  }
  for (const FilterListEntry& entry : lists_) {
    if (entry.url == url.spec()) {
      return AddResult::kDuplicate;
    }
  }
  if (CountCustomLists(lists_) >= kMaxCustomLists) {
    return AddResult::kTooMany;
  }
  FilterListEntry entry;
  entry.id = CustomListId(url);
  entry.url = url.spec();
  entry.title = std::string(url.host());
  if (Find(entry.id)) {  // A hash clash: cannot happen in practice.
    return AddResult::kDuplicate;
  }
  *id = entry.id;
  lists_.push_back(std::move(entry));
  return AddResult::kAdded;
}

FilterListEntry& FilterListState::EnsureCustomRulesEntry() {
  if (FilterListEntry* entry = Find(kCustomRulesId)) {
    return *entry;
  }
  FilterListEntry entry;
  entry.id = kCustomRulesId;
  lists_.push_back(std::move(entry));
  return lists_.back();
}

FilterListState::EnableResult FilterListState::SetEnabled(std::string_view id,
                                                          bool enabled) {
  FilterListEntry* entry = Find(id);
  if (!entry) {
    return EnableResult::kUnknownList;
  }
  if (!enabled && !CanBeDisabled(*entry)) {
    return EnableResult::kRequired;
  }
  entry->enabled = enabled;
  return EnableResult::kOk;
}

bool FilterListState::RemoveList(std::string_view id) {
  auto it = std::find_if(lists_.begin(), lists_.end(), [&](const auto& e) {
    return e.id == id && !e.builtin;
  });
  if (it == lists_.end()) {
    return false;
  }
  lists_.erase(it);
  return true;
}

bool FilterListState::SetException(std::string_view host, bool excepted) {
  std::string normalized = NormalizeExceptionHost(host);
  if (normalized.empty()) {
    return false;
  }
  if (!excepted) {
    exceptions_.erase(normalized);
    return true;
  }
  if (exceptions_.size() >= kMaxExceptions && !exceptions_.count(normalized)) {
    return false;
  }
  exceptions_.insert(std::move(normalized));
  return true;
}

bool FilterListState::SetCosmeticOff(std::string_view host, bool off) {
  std::string normalized = NormalizeExceptionHost(host);
  if (normalized.empty()) {
    return false;
  }
  if (!off) {
    cosmetic_off_hosts_.erase(normalized);
    return true;
  }
  if (cosmetic_off_hosts_.size() >= kMaxCosmeticOffHosts &&
      !cosmetic_off_hosts_.count(normalized)) {
    return false;
  }
  cosmetic_off_hosts_.insert(std::move(normalized));
  return true;
}

bool FilterListState::SetLevelOverride(std::string_view host,
                                       BlockingLevel level) {
  std::string normalized = NormalizeExceptionHost(host);
  if (normalized.empty()) {
    return false;
  }
  if (level_overrides_.size() >= kMaxLevelOverrides &&
      !level_overrides_.count(normalized)) {
    return false;
  }
  level_overrides_[std::move(normalized)] = level;
  return true;
}

bool FilterListState::ClearLevelOverride(std::string_view host) {
  std::string normalized = NormalizeExceptionHost(host);
  if (normalized.empty()) {
    return false;
  }
  level_overrides_.erase(normalized);
  return true;
}

bool FilterListState::NeedsRebuild() const {
  return std::any_of(lists_.begin(), lists_.end(), [](const auto& e) {
    return !e.builtin || !e.updated.is_null() || !e.enabled;
  });
}

base::DictValue FilterListState::ToValue() const {
  base::DictValue out;
  out.Set("auto_update", auto_update_);
  out.Set("last_update_check", base::NumberToString(
                                   last_update_check_.InMillisecondsSinceUnixEpoch()));
  base::ListValue exceptions;
  for (const std::string& host : exceptions_) {
    exceptions.Append(host);
  }
  out.Set("exceptions", std::move(exceptions));
  base::ListValue cosmetic_off;
  for (const std::string& host : cosmetic_off_hosts_) {
    cosmetic_off.Append(host);
  }
  out.Set("cosmetic_off", std::move(cosmetic_off));
  out.Set("default_level", std::string(BlockingLevelName(default_level_)));
  base::DictValue levels;
  for (const auto& [host, level] : level_overrides_) {
    levels.Set(host, std::string(BlockingLevelName(level)));
  }
  out.Set("levels", std::move(levels));
  base::ListValue lists;
  for (const FilterListEntry& entry : lists_) {
    base::DictValue item;
    item.Set("id", entry.id);
    item.Set("url", entry.url);
    item.Set("title", entry.title);
    item.Set("builtin", entry.builtin);
    item.Set("rules", entry.rules);
    item.Set("ignored_exceptions", entry.ignored_exceptions);
    item.Set("enabled", entry.enabled);
    item.Set("updated",
             base::NumberToString(entry.updated.InMillisecondsSinceUnixEpoch()));
    item.Set("error", entry.error);
    lists.Append(std::move(item));
  }
  out.Set("lists", std::move(lists));
  return out;
}

namespace {

base::Time TimeFromValue(const base::DictValue& dict, const char* key) {
  const std::string* text = dict.FindString(key);
  int64_t ms = 0;
  if (!text || !base::StringToInt64(*text, &ms) || ms <= 0) {
    return base::Time();
  }
  return base::Time::FromMillisecondsSinceUnixEpoch(ms);
}

}  // namespace

// static
FilterListState FilterListState::FromValue(const base::DictValue& dict) {
  FilterListState state;
  if (std::optional<bool> auto_update = dict.FindBool("auto_update")) {
    state.auto_update_ = *auto_update;
  }
  state.last_update_check_ = TimeFromValue(dict, "last_update_check");

  if (const base::ListValue* hosts = dict.FindList("exceptions")) {
    for (const base::Value& host : *hosts) {
      if (host.is_string()) {
        state.SetException(host.GetString(), true);
      }
    }
  }

  if (const base::ListValue* hosts = dict.FindList("cosmetic_off")) {
    for (const base::Value& host : *hosts) {
      if (host.is_string()) {
        state.SetCosmeticOff(host.GetString(), true);
      }
    }
  }

  // A missing or unknown level is Aggressive, today's behaviour.
  if (const std::string* name = dict.FindString("default_level")) {
    state.default_level_ =
        BlockingLevelFromName(*name).value_or(kDefaultBlockingLevel);
  }
  if (const base::DictValue* levels = dict.FindDict("levels")) {
    for (const auto [host, value] : *levels) {
      const std::string* name = value.GetIfString();
      std::optional<BlockingLevel> level =
          name ? BlockingLevelFromName(*name) : std::nullopt;
      if (level) {
        state.SetLevelOverride(host, *level);
      }
    }
  }

  const base::ListValue* lists = dict.FindList("lists");
  if (!lists) {
    return state;
  }
  for (const base::Value& value : *lists) {
    const base::DictValue* item = value.GetIfDict();
    const std::string* id = item ? item->FindString("id") : nullptr;
    const std::string* url = item ? item->FindString("url") : nullptr;
    if (!id || !url || !IsValidListId(*id)) {
      continue;
    }
    FilterListEntry* entry = state.Find(*id);
    if (!entry && *id == kCustomRulesId) {
      // The user's own rules have no address. A file that gives them one is
      // not ours.
      if (!url->empty()) {
        continue;
      }
      state.EnsureCustomRulesEntry();
      entry = state.Find(kCustomRulesId);
    }
    if (!entry) {
      // A custom list: its address must be acceptable, and its id the one
      // this build would give that address (so a file name cannot be chosen).
      // Loopback http is not re-checked here: the manager refuses to fetch it
      // without the switch.
      GURL gurl(*url);
      if (!IsAcceptableListUrl(gurl, /*allow_loopback=*/true) ||
          CustomListId(gurl) != *id ||
          CountCustomLists(state.lists_) >= kMaxCustomLists) {
        continue;
      }
      FilterListEntry fresh;
      fresh.id = *id;
      fresh.url = gurl.spec();
      fresh.title = std::string(gurl.host());
      state.lists_.push_back(std::move(fresh));
      entry = &state.lists_.back();
    }
    // Built-in lists keep their own address and title, and the user's own rules
    // have no title: the page names them.
    if (!entry->builtin && entry->id != kCustomRulesId) {
      if (const std::string* title = item->FindString("title")) {
        entry->title = title->substr(0, kMaxTitleLength);
      }
    }
    int rules = item->FindInt("rules").value_or(0);
    entry->rules = (rules > 0 && rules <= kMaxRules) ? rules : 0;
    int ignored = item->FindInt("ignored_exceptions").value_or(0);
    entry->ignored_exceptions = (ignored > 0 && ignored <= kMaxRules) ? ignored : 0;
    // Anything but a stored false is on, and so is a list that cannot be off:
    // a damaged file never leaves blocking weaker than the default.
    entry->enabled =
        item->FindBool("enabled").value_or(true) || !CanBeDisabled(*entry);
    entry->updated = TimeFromValue(*item, "updated");
    const std::string* error = item->FindString("error");
    if (error && error->size() <= kMaxErrorLength && IsKnownErrorMessage(*error)) {
      entry->error = *error;
    }
  }
  return state;
}

}  // namespace aerie_adblock
