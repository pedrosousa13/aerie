// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_FOLDER_TREE_H_
#define COMPONENTS_AERIE_TAB_MODEL_FOLDER_TREE_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/function_ref.h"
#include "components/aerie_tab_model/tab_model_types.h"

// Folders in Pinned and in Today (issue #181, docs/sidebar-design.md,
// "Folders"). Everything here is plain logic over the Tab Model, so it has unit
// tests: how deep a folder is, whether a move may happen, the rows the sidebar
// shows (a depth-first walk of the model), and the edits the folder menus make.
namespace aerie_tab_model {

class TabModel;

// Folders nest this deep and no deeper. A tab can still go into a folder of
// this level; a folder cannot.
inline constexpr int kMaxFolderDepth = 5;

// How many parents a walk up or down the folders follows before it gives up. A
// loop in damaged data stops there; real trees are 5 levels at most.
inline constexpr int kWalkBound = 64;

// The level of a folder: a folder in the section itself is level 1, a folder
// in that one is level 2. 0 for an empty or unknown id (the section itself).
// A loop in damaged data stops the walk.
int Depth(const TabModel& model, const std::string& folder_id);

// How many levels of folders the subtree of `folder_id` has, the folder
// included: a folder with no folder in it is 1. 0 for an unknown id.
int SubtreeHeight(const TabModel& model, const std::string& folder_id);

enum class ItemKind { kTab, kFolder };

// True when the item may be put into `target_folder_id` (empty: the section
// itself) without a folder passing kMaxFolderDepth. A tab fits in a folder of
// any level up to the limit. A folder fits when its own height added to the
// target's level stays within the limit, so a subtree counts as deep as its
// deepest branch. A folder never goes into itself or into one of its
// descendants. An unknown target or item is refused.
bool CanNest(const TabModel& model,
             ItemKind kind,
             const std::string& item_id,
             const std::string& target_folder_id);

// True when a new, empty folder may be made inside `parent_folder_id` (empty:
// in the section itself).
bool CanCreateFolderIn(const TabModel& model,
                       const std::string& parent_folder_id);

// One row of a tree as it is drawn. `level` is the indent: 0 for a row in the
// section itself, 1 for a row inside one folder, and so on. The accessibility
// level of the row is `level + 1`.
struct TreeRow {
  bool is_folder = false;
  // The record id of the tab or the folder.
  std::string id;
  // The id of the folder that holds the row. Empty: the section itself.
  std::string parent;
  int level = 0;
  // Folders only: the state kept in the record, and how many rows the folder
  // holds directly (tabs and folders, whether or not it is expanded).
  bool expanded = false;
  int item_count = 0;

  bool operator==(const TreeRow&) const = default;
};

// The rows of `section` in the open Workspace, top to bottom: a depth-first
// walk. A folder row comes first and its children follow only while it is
// expanded. The children of a parent are its folders and tabs together in
// order of `position`; at the same position a folder comes before a tab, then
// the ids decide. An archived tab has no row, and `tab_is_listed` can turn
// away others (the sidebar lists only the tabs the window has). A collapsed
// folder with nothing listed in it still has its row.
std::vector<TreeRow> BuildVisibleRows(
    const TabModel& model,
    Section section,
    base::FunctionRef<bool(const std::string&)> tab_is_listed);

// The same walk for every row of the tree, expanded or not. Used where a
// collapsed folder's tabs still matter (restoring Pinned tabs).
std::vector<TreeRow> BuildAllRows(
    const TabModel& model,
    Section section,
    base::FunctionRef<bool(const std::string&)> tab_is_listed);

// True when `tab_id` is inside `folder_id` or inside a folder of it, at any
// depth.
bool FolderContainsTab(const TabModel& model,
                       const std::string& folder_id,
                       const std::string& tab_id);

// The folders of `section` for a "Move to folder" list: every folder, in the
// order of the tree, with its level.
struct FolderChoice {
  std::string id;
  int level = 0;
  std::string name;
};
std::vector<FolderChoice> FolderChoices(const TabModel& model,
                                        Section section);

// --- Edits ---

// Makes an empty manual folder named `name`, expanded, at the top of
// `parent_folder_id` (empty: the top of the section). Returns its id, or an
// empty string when the parent is unknown or the folder would be deeper than
// kMaxFolderDepth.
std::string CreateFolder(TabModel& model,
                         Section section,
                         const std::string& parent_folder_id,
                         const std::string& name);

// Writes `expanded` into the record. False for an unknown folder; nothing is
// written when the value is the same.
bool SetFolderExpanded(TabModel& model, const std::string& id, bool expanded);

// What to store for the text typed into a folder's name field: trimmed and cut
// at kMaxManualNameLength UTF-16 characters. An empty result means "keep the
// old name", so it is returned as nothing, a text that equals `old_name`
// likewise.
std::optional<std::string> NormalizeFolderName(std::string_view input,
                                               std::string_view old_name);

// Renames with NormalizeFolderName. True when the record changed.
bool RenameFolder(TabModel& model,
                  const std::string& id,
                  std::string_view input);

// True when nothing is inside the folder.
bool FolderIsEmpty(const TabModel& model, const std::string& id);

// Deletes the folder. What was inside moves out, in its order, into the place
// the folder had, so the rows next to it make room (the other rows of that
// parent move down). Returns false for an unknown folder.
bool DeleteFolder(TabModel& model, const std::string& id);

// Puts the tab at the end of the folder. `folder_id` empty puts it in the
// section of the tab itself, next to the folder it leaves (one level up). False when
// CanNest refuses, or for an unknown tab or folder.
bool MoveTabToFolder(TabModel& model,
                     const std::string& tab_id,
                     const std::string& folder_id);

// Makes a folder named `name` where the tab is and moves the tab into it.
// Returns the folder id, or an empty string for an unknown tab or a tab that
// is not in Pinned or Today.
std::string MoveTabToNewFolder(TabModel& model,
                               const std::string& tab_id,
                               const std::string& name);

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_FOLDER_TREE_H_
