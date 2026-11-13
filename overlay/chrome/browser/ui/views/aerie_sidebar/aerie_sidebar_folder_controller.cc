// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_folder_controller.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view.h"

AerieSidebarFolderController::AerieSidebarFolderController(Host* host)
    : host_(host) {}

AerieSidebarFolderController::~AerieSidebarFolderController() = default;

void AerieSidebarFolderController::BeginToggle(const std::string& folder_id,
                                               bool expanded) {
  toggle_.Begin(folder_id, expanded);
}

void AerieSidebarFolderController::EndToggle() {
  toggle_.End();
}

void AerieSidebarFolderController::PrepareForSync(
    base::FunctionRef<bool()> tree_changed) {
  if (!group_ || !group_->running()) {
    return;
  }
  const bool turn_around = toggle_.is_for(group_->folder_id());
  if (!turn_around && (toggle_.active() || tree_changed())) {
    group_->Finish();
  }
}

std::vector<AerieSidebarFolderController::Row>
AerieSidebarFolderController::TakeReopenedRows(
    const views::View* list,
    const std::vector<aerie_tab_model::TreeRow>& wanted) {
  std::vector<Row> taken;
  if (!toggle_.opening() || !group_ || !group_->running() ||
      group_->folder_id() != toggle_.pending().folder_id) {
    return taken;
  }
  for (auto it = collapsing_rows_.begin(); it != collapsing_rows_.end();) {
    const bool wanted_again =
        it->view->parent() == list &&
        std::any_of(wanted.begin(), wanted.end(),
                    [&](const aerie_tab_model::TreeRow& entry) {
                      return entry.id == it->tab_id;
                    });
    if (wanted_again) {
      it->view->GetViewAccessibility().SetIsIgnored(false);
      taken.push_back(*it);
      it = collapsing_rows_.erase(it);
    } else {
      ++it;
    }
  }
  return taken;
}

bool AerieSidebarFolderController::NoteRevealedRow(AerieSidebarRowView* view,
                                                   const std::string& id) {
  if (toggle_.opening() &&
      host_->IsInsideFolder(toggle_.pending().folder_id, id)) {
    toggle_.NoteRevealed(view);
    return true;
  }
  return false;
}

bool AerieSidebarFolderController::KeepAsCollapsing(const Row& row) {
  // A row that goes because its folder shut keeps its place until the collapse
  // has played, then goes (issue #181). Keyboard focus in it went to the folder
  // row (AerieSidebarView::SetFolderExpanded).
  if (!host_->CanAnimateRows() || !toggle_.shutting() ||
      !host_->IsInsideFolder(toggle_.pending().folder_id, row.tab_id)) {
    return false;
  }
  collapsing_rows_.push_back(row);
  toggle_.NoteCollapsing(row.view);
  row.view->SetFocusBehavior(views::View::FocusBehavior::ACCESSIBLE_ONLY);
  row.view->GetViewAccessibility().SetIsIgnored(true);
  return true;
}

void AerieSidebarFolderController::StartGroup() {
  aerie_sidebar::FolderToggle<AerieSidebarRowView>::Outcome found =
      toggle_.Take();
  const auto& toggle = found.toggle;
  if (!toggle.active) {
    return;
  }
  if (group_ && group_->running()) {
    if (group_->folder_id() == toggle.folder_id) {
      group_->Retarget(toggle.expand);
      return;
    }
    group_->Finish();
  }
  std::vector<raw_ptr<AerieSidebarRowView>> rows =
      toggle.expand ? std::move(found.revealed) : std::move(found.collapsing);
  // A row can go between the sync that found it and here (ending another
  // folder's motion removes the rows that were collapsing). Only rows that are
  // still the sidebar's own are animated; this compares pointers and never
  // reads a row that may be gone.
  std::erase_if(rows, [this](const raw_ptr<AerieSidebarRowView>& row) {
    return !IsLiveRow(row.get());
  });
  if (rows.empty() || !host_->CanAnimateRows()) {
    RemoveCollapsedRows();
    return;
  }
  views::View* list = rows.front()->parent();
  std::sort(rows.begin(), rows.end(),
            [list](const raw_ptr<AerieSidebarRowView>& a,
                   const raw_ptr<AerieSidebarRowView>& b) {
              return list->GetIndexOf(a.get()).value_or(0) <
                     list->GetIndexOf(b.get()).value_or(0);
            });
  group_ = std::make_unique<AerieSidebarFolderGroup>(
      toggle.folder_id, std::move(rows), list, host_->GetPrefs(),
      base::BindOnce(&AerieSidebarFolderController::RemoveCollapsedRows,
                     base::Unretained(this)));
  group_->Start(toggle.expand);
}

void AerieSidebarFolderController::RemoveCollapsedRows() {
  std::vector<Row> rows = std::move(collapsing_rows_);
  collapsing_rows_.clear();
  toggle_.ForgetCollapsing();
  for (const Row& row : rows) {
    if (views::View* parent = row.view->parent()) {
      parent->RemoveChildViewT(row.view.get());
      parent->InvalidateLayout();
    }
  }
  host_->OnCollapsedRowsRemoved();
}

bool AerieSidebarFolderController::IsCollapsingView(
    const views::View* view) const {
  return std::any_of(collapsing_rows_.begin(), collapsing_rows_.end(),
                     [view](const Row& row) { return row.view == view; });
}

bool AerieSidebarFolderController::HasCollapsingRowIn(
    const views::View* list) const {
  return std::any_of(collapsing_rows_.begin(), collapsing_rows_.end(),
                     [list](const Row& row) {
                       return row.view->parent() == list;
                     });
}

bool AerieSidebarFolderController::IsLiveRow(
    const AerieSidebarRowView* row) const {
  if (host_->IsTreeRow(row)) {
    return true;
  }
  return std::any_of(collapsing_rows_.begin(), collapsing_rows_.end(),
                     [row](const Row& entry) { return entry.view == row; });
}
