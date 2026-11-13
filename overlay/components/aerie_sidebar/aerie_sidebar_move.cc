// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_move.h"

#include <algorithm>

#include "components/aerie_tab_model/tab_model.h"

namespace aerie_sidebar {

namespace {

using aerie_tab_model::ChildRef;
using aerie_tab_model::ItemKind;
using aerie_tab_model::Section;
using aerie_tab_model::TreeRow;

void SetPosition(aerie_tab_model::TabModel& model,
                 const ChildRef& child,
                 int position) {
  if (child.is_folder) {
    const aerie_tab_model::FolderRecord* f = model.GetFolder(child.id);
    if (f && f->position != position) {
      model.MoveFolder(child.id, f->section, f->parent, position);
    }
  } else {
    const aerie_tab_model::TabRecord* t = model.GetTab(child.id);
    if (t && t->position != position) {
      model.MoveTab(child.id, t->section, t->parent, position);
    }
  }
}

// Pinned and Favorites keep a pinned URL, Today has none.
void FollowSection(aerie_tab_model::TabModel& model,
                   const std::string& tab_id,
                   Section section,
                   base::FunctionRef<std::string(const std::string&)> url_for_tab) {
  const aerie_tab_model::TabRecord* tab = model.GetTab(tab_id);
  if (!tab) {
    return;
  }
  if (section == Section::kToday) {
    if (tab->pinned_url) {
      model.UpdateTab(tab_id, [](aerie_tab_model::TabRecord& r) {
        r.pinned_url.reset();
      });
    }
    return;
  }
  if (!tab->pinned_url) {
    const std::string url = url_for_tab(tab_id);
    if (!url.empty()) {
      model.UpdateTab(tab_id, [&url](aerie_tab_model::TabRecord& r) {
        r.pinned_url = url;
      });
    }
  }
}

// Gives every record below the folder the new section.
void MoveDescendants(aerie_tab_model::TabModel& model,
                     const std::string& folder_id,
                     Section from,
                     Section to,
                     base::FunctionRef<std::string(const std::string&)> url_for_tab,
                     int depth) {
  if (depth > aerie_tab_model::kWalkBound) {
    return;
  }
  for (const ChildRef& child : aerie_tab_model::OrderedChildren(
           model, from, folder_id, /*listed_only=*/false,
           [](const std::string&) { return true; })) {
    if (child.is_folder) {
      const aerie_tab_model::FolderRecord* f = model.GetFolder(child.id);
      if (f) {
        const int position = f->position;
        const std::string parent = f->parent;
        model.MoveFolder(child.id, to, parent, position);
        MoveDescendants(model, child.id, from, to, url_for_tab, depth + 1);
      }
    } else {
      const aerie_tab_model::TabRecord* t = model.GetTab(child.id);
      if (t) {
        const int position = t->position;
        const std::string parent = t->parent;
        model.MoveTab(child.id, to, parent, position);
        FollowSection(model, child.id, to, url_for_tab);
      }
    }
  }
}

size_t CountFavorites(const aerie_tab_model::TabModel& model) {
  size_t count = 0;
  for (const auto& [id, tab] : model.tabs()) {
    if (tab.section == Section::kFavorite &&
        tab.workspace_id == model.workspace().id) {
      ++count;
    }
  }
  return count;
}

}  // namespace

MoveResult ApplyMove(aerie_tab_model::TabModel& model,
                     const MoveRequest& request,
                     base::FunctionRef<bool(const std::string&)> tab_is_listed,
                     base::FunctionRef<std::string(const std::string&)>
                         url_for_tab) {
  const bool is_folder = request.kind == ItemKind::kFolder;
  Section old_section;
  std::string old_parent;
  if (is_folder) {
    const aerie_tab_model::FolderRecord* f = model.GetFolder(request.id);
    if (!f) {
      return MoveResult::kInvalid;
    }
    old_section = f->section;
    old_parent = f->parent;
  } else {
    const aerie_tab_model::TabRecord* t = model.GetTab(request.id);
    if (!t) {
      return MoveResult::kInvalid;
    }
    old_section = t->section;
    old_parent = t->parent;
  }
  if (request.section == Section::kFavorite &&
      (is_folder || !request.container.empty())) {
    return MoveResult::kInvalid;
  }
  if (!request.container.empty()) {
    const aerie_tab_model::FolderRecord* c = model.GetFolder(request.container);
    if (!c || c->section != request.section) {
      return MoveResult::kInvalid;
    }
    if (!aerie_tab_model::CanNest(model, request.kind, request.id,
                                  request.container)) {
      // A loop (into itself or below itself) is invalid; the rest is depth.
      if (is_folder) {
        const aerie_tab_model::FolderRecord* walk =
            model.GetFolder(request.container);
        for (int i = 0; walk && i <= aerie_tab_model::kWalkBound; ++i) {
          if (walk->id == request.id) {
            return MoveResult::kInvalid;
          }
          walk = walk->parent.empty() ? nullptr : model.GetFolder(walk->parent);
        }
      }
      return MoveResult::kRefusedTooDeep;
    }
  } else if (is_folder && request.section != Section::kFavorite &&
             !aerie_tab_model::CanNest(model, request.kind, request.id,
                                       std::string())) {
    return MoveResult::kRefusedTooDeep;
  }
  if (!is_folder && request.section == Section::kFavorite &&
      old_section != Section::kFavorite &&
      CountFavorites(model) >= aerie_tab_model::kFavoritesCap) {
    return MoveResult::kRefusedFavoritesFull;
  }

  // The order after the move: the siblings without the item, the item put at
  // the listed place.
  auto all = [&](bool listed_only) {
    std::vector<ChildRef> list = aerie_tab_model::OrderedChildren(
        model, request.section, request.container, listed_only, tab_is_listed);
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const ChildRef& c) {
                                return c.id == request.id;
                              }),
               list.end());
    return list;
  };
  std::vector<ChildRef> order = all(/*listed_only=*/false);
  const std::vector<ChildRef> visible = all(/*listed_only=*/true);
  size_t insert_at = order.size();
  const size_t wanted = static_cast<size_t>(std::max(request.index, 0));
  if (wanted < visible.size()) {
    for (size_t i = 0; i < order.size(); ++i) {
      if (order[i].id == visible[wanted].id) {
        insert_at = i;
        break;
      }
    }
  } else if (!visible.empty()) {
    for (size_t i = 0; i < order.size(); ++i) {
      if (order[i].id == visible.back().id) {
        insert_at = i + 1;
      }
    }
  }
  order.insert(order.begin() + insert_at, {is_folder, request.id});

  // Nothing to do when the place is the one it has.
  if (old_section == request.section && old_parent == request.container) {
    std::vector<ChildRef> current = aerie_tab_model::OrderedChildren(
        model, request.section, request.container, false,
        [](const std::string&) { return true; });
    bool same = current.size() == order.size();
    for (size_t i = 0; same && i < current.size(); ++i) {
      same = current[i].id == order[i].id;
    }
    if (same) {
      return MoveResult::kUnchanged;
    }
  }

  const int position = static_cast<int>(insert_at);
  bool ok;
  if (is_folder) {
    ok = model.MoveFolder(request.id, request.section, request.container,
                          position);
    if (ok && old_section != request.section) {
      MoveDescendants(model, request.id, old_section, request.section,
                      url_for_tab, 0);
    }
  } else {
    ok = model.MoveTab(request.id, request.section, request.container,
                       position);
    if (!ok) {
      return request.section == Section::kFavorite
                 ? MoveResult::kRefusedFavoritesFull
                 : MoveResult::kInvalid;
    }
    if (old_section != request.section) {
      FollowSection(model, request.id, request.section, url_for_tab);
    }
  }
  if (!ok) {
    return MoveResult::kInvalid;
  }
  for (size_t i = 0; i < order.size(); ++i) {
    SetPosition(model, order[i], static_cast<int>(i));
  }
  return MoveResult::kMoved;
}

namespace {

// Indexes into `rows` of the rows that share `rows[focused]`'s parent, in
// order.
std::vector<int> Siblings(const std::vector<TreeRow>& rows, int focused) {
  std::vector<int> result;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    if (rows[i].parent == rows[focused].parent) {
      result.push_back(i);
    }
  }
  return result;
}

}  // namespace

KeyboardMove ComputeKeyboardMove(const aerie_tab_model::TabModel& model,
                                 Section section,
                                 const std::vector<TreeRow>& rows,
                                 int focused,
                                 MoveKey key) {
  KeyboardMove result;
  if (focused < 0 || focused >= static_cast<int>(rows.size())) {
    return result;
  }
  const TreeRow& row = rows[focused];
  result.request.kind = row.is_folder ? ItemKind::kFolder : ItemKind::kTab;
  result.request.id = row.id;
  result.request.section = section;
  result.request.container = row.parent;

  const std::vector<int> siblings = Siblings(rows, focused);
  const int k = static_cast<int>(
      std::find(siblings.begin(), siblings.end(), focused) - siblings.begin());
  const int n = static_cast<int>(siblings.size());
  switch (key) {
    case MoveKey::kUp:
      if (k == 0) {
        return result;
      }
      result.request.index = k - 1;
      result.announce_container = row.parent;
      result.announce_position = k;
      result.announce_count = n;
      break;
    case MoveKey::kDown:
      if (k == n - 1) {
        return result;
      }
      result.request.index = k + 1;
      result.announce_container = row.parent;
      result.announce_position = k + 2;
      result.announce_count = n;
      break;
    case MoveKey::kIndent: {
      if (k == 0) {
        return result;
      }
      const TreeRow& previous = rows[siblings[k - 1]];
      if (!previous.is_folder) {
        return result;
      }
      result.request.container = previous.id;
      result.request.index = previous.item_count;
      result.announce_container = previous.id;
      result.announce_position = previous.item_count + 1;
      result.announce_count = previous.item_count + 1;
      if (!aerie_tab_model::CanNest(model, result.request.kind, row.id,
                                    previous.id)) {
        result.too_deep = true;
        return result;
      }
      break;
    }
    case MoveKey::kOutdent: {
      if (row.parent.empty()) {
        return result;
      }
      const aerie_tab_model::FolderRecord* parent = model.GetFolder(row.parent);
      if (!parent) {
        return result;
      }
      // The parent's own place among its siblings.
      int parent_index = 0;
      int parent_siblings = 0;
      for (const TreeRow& other : rows) {
        if (other.parent == parent->parent) {
          if (other.id == parent->id) {
            parent_index = parent_siblings;
          }
          ++parent_siblings;
        }
      }
      result.request.container = parent->parent;
      result.request.index = parent_index + 1;
      result.announce_container = parent->parent;
      result.announce_position = parent_index + 2;
      result.announce_count = parent_siblings + 1;
      if (!aerie_tab_model::CanNest(model, result.request.kind, row.id,
                                    parent->parent)) {
        return result;
      }
      break;
    }
  }
  result.applies = true;
  return result;
}

}  // namespace aerie_sidebar
