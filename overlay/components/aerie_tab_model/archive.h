// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_ARCHIVE_H_
#define COMPONENTS_AERIE_TAB_MODEL_ARCHIVE_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/time/time.h"
#include "components/aerie_tab_model/tab_model_types.h"

namespace aerie_tab_model {

class TabModel;

// The Archive (issue #183, docs/sidebar-design.md, "Archive, as built"): Today
// tabs that stayed unused are closed and kept as archived records. Everything
// here is plain logic over the Tab Model, so it has unit tests; the closing and
// the reopening of the Chromium tab are in //chrome/browser/aerie_tab_model.

// How long the Archive keeps a record after `archived_at`: nullopt means for
// ever. The names are the values of the pref aerie.tabs.archive_keep.
inline constexpr base::TimeDelta kDefaultArchiveKeep = base::Days(30);

// Turns a Today tab into an archived record: `archived_at` is `now`, `archive`
// is `data` with `folder` set to the folder the tab is in. The record keeps its
// id, so the tab restores into the same record. False, and nothing changes, for
// an unknown tab, a tab that is not in Today, or one that is archived already.
bool ArchiveTab(TabModel& model,
                const std::string& id,
                ArchiveData data,
                base::Time now);

// Restores an archived record: clears `archived_at` and `archive`, sets
// `last_active` to `now`, and puts the record at the top of the folder it was in
// (the folder is expanded so that the tab shows), or, when that folder is gone
// or belongs to another Workspace, at the top of Today. The record keeps its
// Workspace. False for an unknown record or one that is not archived.
bool RestoreArchivedTab(TabModel& model, const std::string& id, base::Time now);

// Removes the archived records that are older than `keep` at `now`. Returns how
// many. Nothing is removed for nullopt (for ever).
size_t PurgeArchive(TabModel& model,
                    base::Time now,
                    std::optional<base::TimeDelta> keep);

// When the oldest archived record is due to be dropped, or nullopt when there
// is none or `keep` is for ever.
std::optional<base::Time> NextPurgeTime(const TabModel& model,
                                        std::optional<base::TimeDelta> keep);

// One row of the archive panel.
struct ArchivedRow {
  std::string id;
  std::string title;
  std::string url;
  std::string favicon_key;
  std::string workspace_id;
  base::Time archived_at;
};

// Every archived record, newest first (ties by id).
std::vector<ArchivedRow> ArchivedRows(const TabModel& model);

// Rows whose title or address contains `query`, ignoring case. An empty query
// keeps every row; the order is kept.
std::vector<ArchivedRow> SearchArchive(const std::vector<ArchivedRow>& rows,
                                       std::string_view query);

enum class ArchiveGroup { kToday, kYesterday, kLastWeek, kEarlier };

struct ArchiveSection {
  ArchiveGroup group = ArchiveGroup::kToday;
  std::vector<ArchivedRow> rows;
};

// The local calendar day of `archived_at` against that of `now`: the same day is
// Today, the day before Yesterday, two to seven days before Last week, older is
// Earlier.
ArchiveGroup GroupFor(base::Time archived_at, base::Time now);

// `rows` (newest first) in sections, in the order Today, Yesterday, Last week,
// Earlier. A group with no row has no section.
std::vector<ArchiveSection> GroupArchive(const std::vector<ArchivedRow>& rows,
                                         base::Time now);

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_ARCHIVE_H_
