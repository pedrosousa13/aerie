// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_ARC_IMPORT_APPLY_H_
#define COMPONENTS_AERIE_TAB_MODEL_ARC_IMPORT_APPLY_H_

#include <optional>
#include <string>

#include "components/aerie_tab_model/arc_import.h"

namespace aerie_tab_model {

class TabModel;

struct ArcApplyOptions {
  // The name of a folder that Arc left without one (a translated string).
  std::string untitled_folder_name = "Folder";
  // Test seam: the write of record number N (counting from 0) fails, so a test
  // can check that the import is all or nothing. -1: never.
  int fail_at_record_for_testing = -1;
};

// What an import did, and everything it left out (the plan's skips, and the
// Favorites that did not fit because the Workspace has some already).
struct ArcImportCounts {
  ArcImportCounts();
  ArcImportCounts(const ArcImportCounts&);
  ArcImportCounts& operator=(const ArcImportCounts&);
  ~ArcImportCounts();

  int pinned_tabs = 0;
  int folders = 0;
  int favorites = 0;
  int custom_names = 0;
  ArcSkipCounts skipped;
};

// Writes the plan into the Workspace of `model`: the Pinned tree after what is
// in Pinned already (top level, in order), the Favorites after the ones the
// Workspace has, up to kFavoritesCap in all. Nothing that exists is changed or
// moved. A tab is a record with a pinned URL, `sleeping` set and no page: the
// caller makes the unloaded Chromium tabs (PinnedTabsController).
//
// All or nothing: if a write fails, every record this call added is removed
// again, and the result is nullopt.
std::optional<ArcImportCounts> ApplyArcSpacePlan(
    TabModel& model,
    const ArcSpacePlan& plan,
    const ArcApplyOptions& options = ArcApplyOptions());

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_ARC_IMPORT_APPLY_H_
