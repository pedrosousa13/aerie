// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_PINNED_TABS_H_
#define COMPONENTS_AERIE_TAB_MODEL_PINNED_TABS_H_

#include <string>
#include <string_view>
#include <vector>

#include "components/aerie_tab_model/tab_model_types.h"

class GURL;

namespace aerie_tab_model {

class TabModel;

// What the Favorites grid and the Pinned section decide without a view or a
// Chromium tab (issue #180, docs/sidebar-design.md, "Favorites, Pinned, folders
// and Today" and "Pinned tab close"). Everything here is plain logic over the
// Tab Model, so it has unit tests.

// The tabs of one section of the Workspace, as the sidebar lists them: not in a
// folder, not archived, ordered by position and then by id.
std::vector<std::string> SectionTabIds(const TabModel& model, Section section);

// The order of the whole sidebar, top to bottom: Favorites, Pinned, Today.
std::vector<std::string> SidebarTabOrder(const TabModel& model);

// The tab to select when `id` goes away or sleeps: the row below it in `order`,
// else the row above. Empty when `id` is not in `order` or is the only row.
std::string PickNextTab(const std::vector<std::string>& order,
                        const std::string& id);

// Pin: the tab moves to the end of Pinned and keeps `url` as its pinned URL.
// False for an unknown tab.
bool PinTab(TabModel& model, const std::string& id, const std::string& url);
// Unpin: the tab moves to the top of Today and loses its pinned URL.
bool UnpinTab(TabModel& model, const std::string& id);

enum class FavoriteResult { kAdded, kFull, kUnknownTab };
// Add to Favorites: the tab moves to the end of Favorites and remembers its
// section in `previous_section`. A tab that has no pinned URL keeps `url` as
// one, so that its tile can open it again after a restart. kFull when the
// Workspace has kFavoritesCap Favorites already; nothing changes then.
FavoriteResult AddToFavorites(TabModel& model,
                              const std::string& id,
                              const std::string& url);
// Remove from Favorites: the tab goes back to its `previous_section` (Pinned
// keeps its pinned URL; Today, and a tab with no remembered section, loses it).
bool RemoveFromFavorites(TabModel& model, const std::string& id);

// True when `current` is the pinned page: the same scheme, host, port and path.
// The query and the fragment are ignored, so a page that adds `#section` or
// `?utm=` to its address is still the pinned page. An empty `current` (a tab
// that has not loaded) counts as being at the pinned page, as does a pinned URL
// that is not valid.
bool IsAtPinnedUrl(const GURL& current, const GURL& pinned);

// The setting `aerie.tabs.pinned_close` (tab_model_prefs.h).
enum class PinnedCloseMode { kResetSleepSwitch, kSleep, kClose };
// An unknown value is the default, kResetSleepSwitch.
PinnedCloseMode ParsePinnedCloseMode(std::string_view value);

// What a press of Ctrl+W or of the close button does on a pinned tab.
enum class PinnedCloseAction {
  // Navigate to the pinned URL (replacing the history entry), put the tab to
  // sleep and select the next tab. The row stays.
  kResetSleepSwitch,
  // Put the tab to sleep and select the next tab. The URL stays. Also the first
  // press in the default mode on a tab that is at its pinned page already.
  kSleepSwitch,
  // Unpin the tab and close it.
  kUnpinClose,
};

// The table of the design. The second press is the one on a tab that is asleep
// already: it closes in the default mode when the tab is also at its pinned URL,
// and in "sleep only" mode whatever the URL is. In the default mode a tab that is
// asleep and away from its page still gets the reset (Chromium cannot change a
// discarded tab's page without loading it; docs/sidebar-design.md says so).
PinnedCloseAction DecidePinnedClose(PinnedCloseMode mode,
                                    bool asleep,
                                    bool at_pinned_url);

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_PINNED_TABS_H_
