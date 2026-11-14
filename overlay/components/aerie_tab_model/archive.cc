// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/archive.h"

#include <algorithm>
#include <utility>

#include "base/i18n/case_conversion.h"
#include "base/strings/utf_string_conversions.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/tab_model.h"

namespace aerie_tab_model {

bool ArchiveTab(TabModel& model,
                const std::string& id,
                ArchiveData data,
                base::Time now) {
  const TabRecord* tab = model.GetTab(id);
  if (!tab || tab->section != Section::kToday || tab->archived_at) {
    return false;
  }
  data.folder = tab->parent;
  // Keep the page and up to four entries before it.
  if (data.entries.size() > kMaxArchivedNavigationEntries) {
    data.entries.erase(data.entries.begin(),
                       data.entries.end() - kMaxArchivedNavigationEntries);
  }
  return model.UpdateTab(id, [&](TabRecord& record) {
    record.archived_at = now;
    record.archive = std::move(data);
  });
}

bool RestoreArchivedTab(TabModel& model,
                        const std::string& id,
                        base::Time now) {
  const TabRecord* tab = model.GetTab(id);
  if (!tab || !tab->archived_at) {
    return false;
  }
  std::string folder_id = tab->archive ? tab->archive->folder : std::string();
  const FolderRecord* folder =
      folder_id.empty() ? nullptr : model.GetFolder(folder_id);
  if (!folder || folder->section != Section::kToday ||
      folder->workspace_id != tab->workspace_id) {
    folder_id.clear();
    folder = nullptr;
  }
  // The archived record is no row, so it can be placed before it is restored.
  const int position = FirstPosition(model, Section::kToday, folder_id);
  if (!model.MoveTab(id, Section::kToday, folder_id, position)) {
    return false;
  }
  if (folder) {
    SetFolderExpanded(model, folder_id, true);
  }
  return model.UpdateTab(id, [now](TabRecord& record) {
    record.archived_at.reset();
    record.archive.reset();
    record.last_active = now;
  });
}

size_t PurgeArchive(TabModel& model,
                    base::Time now,
                    std::optional<base::TimeDelta> keep) {
  if (!keep) {
    return 0;
  }
  std::vector<std::string> doomed;
  for (const auto& [id, tab] : model.tabs()) {
    if (tab.archived_at && *tab.archived_at + *keep <= now) {
      doomed.push_back(id);
    }
  }
  for (const std::string& id : doomed) {
    model.RemoveTab(id);
  }
  return doomed.size();
}

std::optional<base::Time> NextPurgeTime(const TabModel& model,
                                        std::optional<base::TimeDelta> keep) {
  if (!keep) {
    return std::nullopt;
  }
  std::optional<base::Time> oldest;
  for (const auto& [id, tab] : model.tabs()) {
    if (tab.archived_at && (!oldest || *tab.archived_at < *oldest)) {
      oldest = tab.archived_at;
    }
  }
  if (!oldest) {
    return std::nullopt;
  }
  return *oldest + *keep;
}

std::vector<ArchivedRow> ArchivedRows(const TabModel& model) {
  std::vector<ArchivedRow> rows;
  for (const auto& [id, tab] : model.tabs()) {
    if (!tab.archived_at) {
      continue;
    }
    // A record with no page to open (every entry was refused on load) has
    // nothing to restore: it waits for retention and shows no row.
    if (tab.archive && tab.archive->entries.empty()) {
      continue;
    }
    ArchivedRow row;
    row.id = id;
    row.workspace_id = tab.workspace_id;
    row.archived_at = *tab.archived_at;
    if (tab.archive) {
      row.title = tab.archive->title;
      row.url = tab.archive->url;
      row.favicon_key = tab.archive->favicon_key;
    }
    rows.push_back(std::move(row));
  }
  std::sort(rows.begin(), rows.end(),
            [](const ArchivedRow& a, const ArchivedRow& b) {
              return a.archived_at != b.archived_at
                         ? a.archived_at > b.archived_at
                         : a.id < b.id;
            });
  return rows;
}

std::vector<ArchivedRow> SearchArchive(const std::vector<ArchivedRow>& rows,
                                       std::string_view query) {
  if (query.empty()) {
    return rows;
  }
  const std::u16string needle =
      base::i18n::ToLower(base::UTF8ToUTF16(query));
  std::vector<ArchivedRow> found;
  for (const ArchivedRow& row : rows) {
    if (base::i18n::ToLower(base::UTF8ToUTF16(row.title)).find(needle) !=
            std::u16string::npos ||
        base::i18n::ToLower(base::UTF8ToUTF16(row.url)).find(needle) !=
            std::u16string::npos) {
      found.push_back(row);
    }
  }
  return found;
}

ArchiveGroup GroupFor(base::Time archived_at, base::Time now) {
  // Whole local days between the two midnights. A day is 23 to 25 hours around
  // a clock change, so the division rounds to the nearest day.
  const base::TimeDelta gap = now.LocalMidnight() - archived_at.LocalMidnight();
  const int days = static_cast<int>((gap + base::Hours(12)) / base::Days(1));
  if (days <= 0) {
    return ArchiveGroup::kToday;
  }
  if (days == 1) {
    return ArchiveGroup::kYesterday;
  }
  return days <= 7 ? ArchiveGroup::kLastWeek : ArchiveGroup::kEarlier;
}

std::vector<ArchiveSection> GroupArchive(const std::vector<ArchivedRow>& rows,
                                         base::Time now) {
  std::vector<ArchiveSection> sections;
  for (ArchiveGroup group :
       {ArchiveGroup::kToday, ArchiveGroup::kYesterday, ArchiveGroup::kLastWeek,
        ArchiveGroup::kEarlier}) {
    ArchiveSection section;
    section.group = group;
    for (const ArchivedRow& row : rows) {
      if (GroupFor(row.archived_at, now) == group) {
        section.rows.push_back(row);
      }
    }
    if (!section.rows.empty()) {
      sections.push_back(std::move(section));
    }
  }
  return sections;
}

}  // namespace aerie_tab_model
