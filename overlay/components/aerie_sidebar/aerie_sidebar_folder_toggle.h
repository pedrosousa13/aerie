// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_FOLDER_TOGGLE_H_
#define COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_FOLDER_TOGGLE_H_

#include <string>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"

namespace aerie_sidebar {

// The hand-off between a folder toggle and the sync that follows it (issue
// #228; docs/sidebar-design.md, "Folders as built (#181)"). Opening or shutting
// a folder writes the Tab Model, and the sidebar then syncs its rows at once,
// in the same call. The sync has to know that a user toggle caused it: the rows
// it adds inside the folder are revealed with the folder motion, and the rows
// it removes from the folder are kept until the collapse has played. This class
// is that knowledge, and nothing else:
//
//   Begin(id, expand);        // before the sync
//   ... the sync calls NoteRevealed / NoteCollapsing for the rows it finds ...
//   Outcome found = Take();   // the sync's last step: what it found
//   End();                    // after the sync
//
// `Row` is the sidebar's row type. The class only holds pointers to it and
// never reads one, so it works with a row that may already be gone.
template <typename Row>
class FolderToggle {
 public:
  // The toggle the user just made, until the sync that follows has run.
  struct Pending {
    bool active = false;
    std::string folder_id;
    bool expand = false;
  };
  // What the sync found: the toggle, the rows it added inside the folder that
  // opened, and the rows it removed from the folder that shut.
  struct Outcome {
    Pending toggle;
    std::vector<raw_ptr<Row>> revealed;
    std::vector<raw_ptr<Row>> collapsing;
  };

  // Marks `folder_id` as just opened (`expand`) or shut. Rows found by an
  // earlier sync that nobody has taken yet stay where they are.
  void Begin(std::string folder_id, bool expand) {
    pending_ = {true, std::move(folder_id), expand};
  }
  // The sync is over: nothing is pending any more.
  void End() { pending_ = Pending(); }

  const Pending& pending() const { return pending_; }
  bool active() const { return pending_.active; }
  // True while a toggle is pending and it opens a folder, or shuts one.
  bool opening() const { return pending_.active && pending_.expand; }
  bool shutting() const { return pending_.active && !pending_.expand; }
  // True when a toggle is pending and it is of `folder_id`.
  bool is_for(const std::string& folder_id) const {
    return pending_.active && pending_.folder_id == folder_id;
  }

  // The sync added `row`, inside the folder that opens.
  void NoteRevealed(Row* row) { revealed_.push_back(row); }
  // The sync removed `row` from the folder that shuts, and keeps it for the
  // collapse.
  void NoteCollapsing(Row* row) { collapsing_.push_back(row); }
  // Forgets the rows kept for a collapse (the collapse is over).
  void ForgetCollapsing() { collapsing_.clear(); }

  // Hands over what the sync found, and clears it. The toggle itself stays
  // pending until End.
  Outcome Take() {
    Outcome outcome;
    outcome.toggle = pending_;
    outcome.revealed = std::move(revealed_);
    outcome.collapsing = std::move(collapsing_);
    revealed_.clear();
    collapsing_.clear();
    return outcome;
  }

 private:
  Pending pending_;
  std::vector<raw_ptr<Row>> revealed_;
  std::vector<raw_ptr<Row>> collapsing_;
};

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_FOLDER_TOGGLE_H_
