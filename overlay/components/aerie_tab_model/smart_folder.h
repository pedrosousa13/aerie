// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_SMART_FOLDER_H_
#define COMPONENTS_AERIE_TAB_MODEL_SMART_FOLDER_H_

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/function_ref.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/rule_matcher.h"
#include "components/aerie_tab_model/tab_model_types.h"
#include "url/gurl.h"

// Smart folders (issue #140, docs/sidebar-design.md, "Folders"). A smart folder
// is a live view: it lists every open Pinned and Today tab of its Workspace
// that matches its rules, and moves nothing. The rules, the exceptions and the
// switches are the ones of auto-filing (folder_rules.h, rule_matcher.h). The
// list is computed at run time and persisted nowhere: a smart folder's record
// holds only its kind and its rules.
//
// The Tab Model does not know the address or the title of a tab; the sidebar
// does. So the index below is fed those facts, one tab at a time, and answers
// which smart folders list the tab. Plain logic with unit tests.
namespace aerie_tab_model {

// The address and title of a tab, as the page shows them (not the manual
// name).
struct TabFacts {
  GURL url;
  std::string title;
};

// Asks for the facts of the tab with this record id. Nothing for a tab the
// window does not have; such a tab is listed nowhere.
using TabFactsFn =
    base::FunctionRef<std::optional<TabFacts>(const std::string& tab_id)>;

// The id of a row that shows a tab inside a smart folder. The same tab has its
// own row too, so the row needs an id of its own: "smart:<folder>:<tab>".
std::string MakeSmartRowId(const std::string& folder_id,
                           const std::string& tab_id);

struct SmartRowRef {
  std::string folder_id;
  std::string tab_id;
};
// The folder and the real tab of a smart row id, or nothing for any other id.
std::optional<SmartRowRef> ParseSmartRowId(std::string_view row_id);

// Which tabs each smart folder lists. It holds the rules of each smart folder
// (read once, when the folder is refreshed) and the ids of its members.
class SmartFolderIndex {
 public:
  SmartFolderIndex();
  SmartFolderIndex(const SmartFolderIndex&) = delete;
  SmartFolderIndex& operator=(const SmartFolderIndex&) = delete;
  ~SmartFolderIndex();

  // True when at least one folder is a smart folder.
  bool HasSmartFolders() const { return !entries_.empty(); }

  // The record ids of the tabs that `folder_id` lists. Empty for a folder that
  // is not smart.
  const std::set<std::string>& Members(const std::string& folder_id) const;

  // Reads the folder again (its kind, its rules, its switches) and matches it
  // against every tab of the model. For a rule change, a new smart folder or
  // the model being loaded: it costs one pass over the tabs. A folder that is
  // not smart (or gone) is forgotten. Returns true when the members changed.
  bool RefreshFolder(const TabModel& model,
                     const std::string& folder_id,
                     TabFactsFn facts);
  void RemoveFolder(const std::string& folder_id);

  // One tab changed (it opened, navigated, changed title or section): matches
  // it against each smart folder and no other tab. `facts` is nothing when the
  // window no longer has the tab. Returns true when any folder's members
  // changed.
  bool UpdateTab(const TabModel& model,
                 const std::string& tab_id,
                 std::optional<TabFacts> facts);
  // The tab closed: it leaves every folder.
  bool RemoveTab(const std::string& tab_id);

  // How many folders the last UpdateTab matched the tab against (tests).
  size_t last_evaluations() const { return last_evaluations_; }

 private:
  struct Entry {
    FolderRules rules;
    std::string workspace_id;
    std::set<std::string> members;
  };

  static bool IsListable(const TabRecord& tab, const Entry& entry);
  static bool Matches(const Entry& entry,
                      const TabRecord& tab,
                      const TabFacts& facts);

  std::map<std::string, Entry> entries_;
  size_t last_evaluations_ = 0;
};

// The rows of `section` as BuildVisibleRows made them, with the rows of the
// smart folders added: after each smart folder row that is expanded, one row
// per member, one level deeper, in this order: the Pinned tabs, then the Today
// tabs, each in the order of the tree. A smart folder row's `item_count` is its
// number of members, expanded or not. `rules_enabled` is the global "Folder
// rules everywhere" switch: when it is off every smart folder is empty.
// `tab_is_listed` is the filter that BuildVisibleRows was given.
std::vector<TreeRow> ExpandSmartFolders(
    const TabModel& model,
    const SmartFolderIndex& index,
    bool rules_enabled,
    Section section,
    std::vector<TreeRow> rows,
    base::FunctionRef<bool(const std::string&)> tab_is_listed);

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_SMART_FOLDER_H_
