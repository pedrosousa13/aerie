// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_TAB_MODEL_OBSERVER_H_
#define COMPONENTS_AERIE_TAB_MODEL_TAB_MODEL_OBSERVER_H_

#include <string>

#include "base/observer_list_types.h"
#include "components/aerie_tab_model/tab_model_types.h"

namespace aerie_tab_model {

// Where a record sat before a move.
struct Placement {
  Section section = Section::kToday;
  std::string parent;
  int position = 0;
};

// What the sidebar observes. Every call comes after the model changed, on the
// model's sequence. A record passed to OnTabRemoved or OnFolderRemoved is gone
// from the model by then.
class TabModelObserver : public base::CheckedObserver {
 public:
  virtual void OnTabAdded(const TabRecord& tab) {}
  virtual void OnTabChanged(const TabRecord& tab) {}
  virtual void OnTabMoved(const TabRecord& tab, const Placement& from) {}
  virtual void OnTabRemoved(const TabRecord& tab) {}

  virtual void OnFolderAdded(const FolderRecord& folder) {}
  virtual void OnFolderChanged(const FolderRecord& folder) {}
  virtual void OnFolderMoved(const FolderRecord& folder, const Placement& from) {}
  virtual void OnFolderRemoved(const FolderRecord& folder) {}

  virtual void OnWorkspaceChanged(const Workspace& workspace) {}

  // state.json was read and merged into the model. Records that were in it
  // were not announced one by one: read the model again.
  virtual void OnModelLoaded() {}

  // Called after every one of the calls above that follows a write, once. The
  // persistence layer uses it to schedule a save.
  virtual void OnModelChanged() {}
};

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_TAB_MODEL_OBSERVER_H_
