// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_FILTER_LIST_STATE_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_FILTER_LIST_STATE_H_

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/time/time.h"
#include "base/values.h"
#include "url/gurl.h"

namespace aerie_adblock {

// The user's filter list settings as plain data (issue #10): which lists exist,
// when each was last updated, the per-site exceptions and the update schedule.
// No I/O and no threads here; filter_list_storage.h saves it and
// FilterListManager changes it. Everything read back from disk is untrusted,
// so FromValue() checks every field.

inline constexpr char kEasyListId[] = "easylist";
inline constexpr char kEasyPrivacyId[] = "easyprivacy";
inline constexpr char kEasyListUrl[] = "https://easylist.to/easylist/easylist.txt";
inline constexpr char kEasyPrivacyUrl[] =
    "https://easylist.to/easylist/easyprivacy.txt";

inline constexpr size_t kMaxCustomLists = 20;
inline constexpr size_t kMaxExceptions = 1000;
// Hosts that have cosmetic hiding turned off while blocking stays on (#101).
inline constexpr size_t kMaxCosmeticOffHosts = 1000;
inline constexpr size_t kMaxHostLength = 253;
inline constexpr size_t kMaxErrorLength = 200;

struct FilterListEntry {
  // [a-z0-9]{1,32}. "easylist" and "easyprivacy" are the built-in lists; a
  // custom list's id is derived from its address. Used as a file name, so
  // IsValidListId() is checked before any path is made.
  std::string id;
  std::string url;    // The publisher's address (https).
  std::string title;  // From the list's own "! Title:" line, or built in.
  bool builtin = false;
  int rules = 0;  // Network rules in the copy in use; 0 until a list loads.
  // Exception ("@@") rules in the copy in use that match every address. The
  // engine ignores them; the settings page shows how many.
  int ignored_exceptions = 0;
  // False when the user turned the list off. A list that is off keeps its
  // file on disk and its place in the state, and is left out of the engine.
  // Only RemoveList() deletes a file. EasyList is never off (CanBeDisabled()).
  bool enabled = true;
  // When the copy in use was downloaded. Null for a built-in list that still
  // uses the copy shipped in the build, and for a list not fetched yet.
  base::Time updated;
  std::string error;  // Fixed text from ListFailureMessage(), or "".
};

bool IsValidListId(std::string_view id);

// Whether the user may turn |entry| off. EasyList may not: it carries most of
// the cosmetic hiding. EasyPrivacy and custom lists may.
bool CanBeDisabled(const FilterListEntry& entry);

// Whether the browser may download a list from |url|: https, a host, no user
// info, no fragment, at most 2048 characters. With |allow_loopback| (only the
// Bench's switch sets it) http is also accepted for 127.0.0.1, ::1 and
// localhost, and for nothing else.
bool IsAcceptableListUrl(const GURL& url, bool allow_loopback);

// Lowercase ASCII host with no trailing dot, or "" if |host| is not a plain
// hostname or IP literal (letters, digits, '-', '.', ':', brackets).
std::string NormalizeExceptionHost(std::string_view host);

// True if |host| is in |exceptions| or is a subdomain of one of them.
bool HostHasException(std::string_view host, const std::set<std::string>& exceptions);

class FilterListState {
 public:
  enum class AddResult { kAdded, kInvalidUrl, kDuplicate, kTooMany };

  // The two built-in lists, automatic updates on, nothing excepted.
  FilterListState();
  FilterListState(const FilterListState&);
  FilterListState& operator=(const FilterListState&);
  ~FilterListState();

  const std::vector<FilterListEntry>& lists() const { return lists_; }
  const std::set<std::string>& exceptions() const { return exceptions_; }
  // Hosts (and their subdomains) where cosmetic hiding is off but network
  // blocking is on (issue #101). Keyed like exceptions().
  const std::set<std::string>& cosmetic_off_hosts() const {
    return cosmetic_off_hosts_;
  }
  bool auto_update() const { return auto_update_; }
  base::Time last_update_check() const { return last_update_check_; }

  FilterListEntry* Find(std::string_view id);
  const FilterListEntry* Find(std::string_view id) const;

  // Adds a custom list. On kAdded, |*id| is its id.
  AddResult AddCustomList(const GURL& url, bool allow_loopback, std::string* id);
  // Turns a list on or off. The list and its file stay either way. kRequired
  // (nothing changes) for a list that CanBeDisabled() refuses and |enabled| is
  // false; kUnknownList for an id that is not in the state.
  enum class EnableResult { kOk, kUnknownList, kRequired };
  EnableResult SetEnabled(std::string_view id, bool enabled);
  // Removes a custom list. Built-in lists stay. False if nothing was removed.
  bool RemoveList(std::string_view id);

  // |host| is normalized. False if it is not a valid host or the table is
  // full (only when adding).
  bool SetException(std::string_view host, bool excepted);
  // Turns cosmetic hiding off (|off|) or on for |host|. Same rules as
  // SetException(): normalized, at most kMaxCosmeticOffHosts, and removing
  // always works.
  bool SetCosmeticOff(std::string_view host, bool off);
  void SetAutoUpdate(bool enabled) { auto_update_ = enabled; }
  void set_last_update_check(base::Time time) { last_update_check_ = time; }

  // True when the engine the build shipped is not what this state asks for:
  // a custom list exists, a list has a downloaded copy, or a list is off.
  bool NeedsRebuild() const;

  base::DictValue ToValue() const;
  // Always returns a usable state: bad or missing fields fall back to the
  // defaults, unknown lists and invalid hosts are dropped.
  static FilterListState FromValue(const base::DictValue& dict);

 private:
  std::vector<FilterListEntry> lists_;
  std::set<std::string> exceptions_;
  std::set<std::string> cosmetic_off_hosts_;
  bool auto_update_ = true;
  base::Time last_update_check_;
};

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_FILTER_LIST_STATE_H_
