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

// What one import may add (issue #275). Only the importer enforces these:
// the Tab Model itself has no cap on Pinned tabs. The maintainer fixed the first two on
// 2026-11-20. Over a cap, the import brings in a whole prefix of the plan, in
// plan order (Pinned tree first, then Favorites), and the rest is counted as
// ArcSkipReason::kOverCap.
inline constexpr int kArcMaxTabsPerImport = 250;
inline constexpr int kArcMaxPinnedTabsPerWorkspace = 1000;
// URL, last title and custom name bytes of all the tabs of one import. Even if
// every character grew six-fold when the state is written as JSON, this stays
// far under kMaxStateBytes (tab_model_persistence.h).
inline constexpr size_t kArcMaxImportTextBytes = 1024 * 1024;

struct ArcApplyOptions {
  // The name of a folder that Arc left without one (a translated string).
  std::string untitled_folder_name = "Folder";
  // Test seam: the write of record number N (counting from 0) fails, so a test
  // can check that a failed import is rolled back. -1: never.
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
// If a write fails, every record this call added is removed again (rolled back
// in memory), and the result is nullopt. The Tab Model has no transaction API
// and this adds none. What reaches the disk is one atomic write after the call
// returns: TabModelPersistence::OnModelChanged only schedules a write, and the
// writer batches a burst of changes into one (tab_model_persistence.h:60-61,
// tab_model_persistence.cc:120-124). A crash during the apply therefore leaves
// the file as it was before; it does not leave half of the import.
std::optional<ArcImportCounts> ApplyArcSpacePlan(
    TabModel& model,
    const ArcSpacePlan& plan,
    const ArcApplyOptions& options = ArcApplyOptions());

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_ARC_IMPORT_APPLY_H_
