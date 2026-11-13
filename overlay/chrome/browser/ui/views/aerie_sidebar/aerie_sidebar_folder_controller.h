// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FOLDER_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FOLDER_CONTROLLER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/function_ref.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_folder_group.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_entry.h"
#include "components/aerie_sidebar/aerie_sidebar_folder_toggle.h"

class AerieSidebarRowView;
class PrefService;

namespace aerie_tab_model {
struct TreeRow;
}  // namespace aerie_tab_model

namespace views {
class View;
}  // namespace views

// The state and the motion of a folder opening or shutting (issues #181 and
// #228, docs/sidebar-design.md, "Folders as built (#181)"), taken out of
// AerieSidebarView. It owns:
//   - the toggle hand-off (aerie_sidebar::FolderToggle): what a user toggle
//     tells the sync that follows it;
//   - the rows of a folder that shut and that stay in their list until the
//     collapse has played ("collapsing rows");
//   - the AerieSidebarFolderGroup that plays the motion.
// The view still owns the rows and the Tab Model; the controller asks it
// through Host.
//
// The view calls it in this order, in one call stack:
//   BeginToggle(id, expanded)
//   ... the sync: PrepareForSync, then for each row TakeReopenedRows,
//       NoteRevealedRow, KeepAsCollapsing ...
//   StartGroup()                 // the sync's last step
//   EndToggle()
//
// Destruction: the group's destructor ends the motion, which touches the rows
// and, through Host, the view's lists. The view declares this controller after
// its row lists and runs no code of its own before it is destroyed (see the
// comment at `folders_` in aerie_sidebar_view.h).
class AerieSidebarFolderController {
 public:
  using Row = AerieSidebarRowEntry;

  // What the controller needs from the sidebar view.
  class Host {
   public:
    // True when rows animate (the window is shown, no session is restoring).
    virtual bool CanAnimateRows() const = 0;
    // True when the row `id` is inside the folder `folder_id`, at any depth.
    virtual bool IsInsideFolder(const std::string& folder_id,
                                const std::string& id) const = 0;
    // Pointer comparison only: whether `row` is one of the rows of the Today or
    // the Pinned tree. Never reads `row`, which may be gone.
    virtual bool IsTreeRow(const AerieSidebarRowView* row) const = 0;
    virtual const PrefService* GetPrefs() const = 0;
    // The collapsing rows were removed from their lists.
    virtual void OnCollapsedRowsRemoved() = 0;

   protected:
    ~Host() = default;
  };

  explicit AerieSidebarFolderController(Host* host);
  AerieSidebarFolderController(const AerieSidebarFolderController&) = delete;
  AerieSidebarFolderController& operator=(const AerieSidebarFolderController&) =
      delete;
  ~AerieSidebarFolderController();

  // The toggle hand-off. BeginToggle marks the folder as just opened or shut
  // for the sync that follows; EndToggle clears it.
  void BeginToggle(const std::string& folder_id, bool expanded);
  void EndToggle();
  bool toggle_active() const { return toggle_.active(); }

  // First step of a sync. A motion that runs goes on only when the sync is the
  // turn-around of the same folder or changes no row; anything else ends it at
  // once, so that no row is moved under it. `tree_changed` says whether either
  // tree differs from what the Tab Model wants; it runs only when it matters.
  void PrepareForSync(base::FunctionRef<bool()> tree_changed);

  // A folder that was shutting and is opened again takes its rows back. Removes
  // from the collapsing rows, and returns, those that are in `list` and wanted
  // again in `wanted`; they go on from where they got to.
  std::vector<Row> TakeReopenedRows(
      const views::View* list,
      const std::vector<aerie_tab_model::TreeRow>& wanted);
  // `view`, a new row for `id`, is inside the folder that opens: it is
  // revealed with the motion. False when it is an ordinary new row.
  bool NoteRevealedRow(AerieSidebarRowView* view, const std::string& id);
  // True, and the row stays in its list until the collapse motion ends, when
  // the row goes because the folder the user shut holds it.
  bool KeepAsCollapsing(const Row& row);

  // Last step of a sync: plays the motion for the rows the sync found.
  void StartGroup();
  // Removes the collapsing rows from their lists (the collapse is over).
  void RemoveCollapsedRows();

  // Rows of a collapsed folder that still slide away.
  const std::vector<Row>& collapsing_rows() const { return collapsing_rows_; }
  size_t collapsing_row_count() const { return collapsing_rows_.size(); }
  bool IsCollapsingView(const views::View* view) const;
  bool HasCollapsingRowIn(const views::View* list) const;
  // The motion of the folder that opens or shuts now, or null.
  const AerieSidebarFolderGroup* group() const { return group_.get(); }

 private:
  bool IsLiveRow(const AerieSidebarRowView* row) const;

  const raw_ptr<Host> host_;
  aerie_sidebar::FolderToggle<AerieSidebarRowView> toggle_;
  // Rows of a folder that shut: they are no longer rows but stay in their list
  // until the collapse motion ends.
  std::vector<Row> collapsing_rows_;
  // Must stay declared after collapsing_rows_: its destructor runs End(), which
  // touches them (through RemoveCollapsedRows).
  std::unique_ptr<AerieSidebarFolderGroup> group_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FOLDER_CONTROLLER_H_
