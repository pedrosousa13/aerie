// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_regions.h"

#include <utility>

#include "chrome/browser/ui/view_ids.h"
#include "ui/base/metadata/metadata_impl_macros.h"

AerieSidebarPill::AerieSidebarPill() = default;
AerieSidebarPill::~AerieSidebarPill() = default;

void AerieSidebarPill::RequestFocus() {
  if (views::View* omnibox = GetViewByID(VIEW_ID_OMNIBOX)) {
    omnibox->RequestFocus();
  }
}

AerieSidebarTodayTree::AerieSidebarTodayTree() = default;
AerieSidebarTodayTree::~AerieSidebarTodayTree() = default;

void AerieSidebarTodayTree::SetFocusHandler(
    base::RepeatingClosure focus_tab_stop) {
  focus_tab_stop_ = std::move(focus_tab_stop);
}

void AerieSidebarTodayTree::RequestFocus() {
  if (focus_tab_stop_) {
    focus_tab_stop_.Run();
  }
}

BEGIN_METADATA(AerieSidebarPill)
END_METADATA

BEGIN_METADATA(AerieSidebarTodayTree)
END_METADATA
