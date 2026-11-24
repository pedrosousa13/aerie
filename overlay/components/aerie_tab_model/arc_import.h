// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_ARC_IMPORT_H_
#define COMPONENTS_AERIE_TAB_MODEL_ARC_IMPORT_H_

#include <stddef.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"

// Import of an Arc sidebar into the current Workspace (issue #171,
// docs/arc-import.md). Two pure steps, with no profile, no file and no Chromium
// tab:
//
//  1. ParseArcSidebar turns the parsed JSON of Arc's StorableSidebar.json, which
//     is untrusted, into a plan: one ArcSpacePlan per Space, each with the
//     Pinned tree, the Favorites and the custom names to import, and what was
//     skipped and why.
//  2. ApplyArcSpacePlan (arc_import_apply.h) writes one plan into a TabModel,
//     after what the user already has; rolled back if a write fails.
namespace aerie_tab_model {

// The limits of the file (issue #171). The size is checked by the caller, before
// it reads the file.
inline constexpr size_t kArcMaxFileBytes = 32u * 1024 * 1024;
inline constexpr size_t kArcMaxSpaces = 100;
// The most memory the plans of all Spaces may hold together (ApproxBytes, summed
// by ArcParseResult::PlanBytes). Spaces may name the same container, and each
// plan holds its own copy (issue #277). A Space that would pass it is dropped
// whole, with the rest, and counted as "too many".
inline constexpr size_t kArcMaxPlanBytes = 2 * kArcMaxFileBytes;
inline constexpr size_t kArcMaxItems = 10'000;
inline constexpr int kArcMaxDepth = 32;
inline constexpr size_t kArcMaxTitleLength = 1024;
// The longest address the import accepts.
inline constexpr size_t kArcMaxUrlLength = 8192;

enum class ArcSkipReason {
  // The address is valid but its scheme is not http or https (javascript:,
  // file:, data:, chrome:, aerie:, arc:, ...).
  kUnsupportedScheme,
  // No address, an address that is not a string or does not parse, or one that
  // is too long.
  kNoUsableAddress,
  // An item of a kind Aerie does not know (an Arc-only feature such as an
  // Easel).
  kArcOnlyItem,
  // Entries past the cap of items or Spaces.
  kTooMany,
  // The item tree is deeper than kArcMaxDepth.
  kTooDeep,
  // A folder below the Tab Model's folder limit: the folder is merged into its
  // parent, and its tabs stay. Counts folders, not tabs.
  kFolderMerged,
  // Favorites past the cap.
  kFavoritesFull,
  // Little Arc windows. Counts sidebars.
  kLittleArc,
  // Tabs left out by ApplyArcSpacePlan because the import is over a cap: 250
  // tabs per import, 1000 Pinned tabs per Workspace, or the byte budget of
  // what one import may add (arc_import_apply.h). Counts tabs.
  kOverCap,
  // A tab whose address is already in the same place (the Workspace's Pinned
  // section or Favorites, the same parent folder): importing the same Space
  // twice adds nothing (issue #276). Counts tabs.
  kAlreadyThere,
};

// How many of each reason. Reasons with a count of 0 are absent.
using ArcSkipCounts = std::map<ArcSkipReason, int>;

// The import's own names for the reasons, for tests and for the page
// ("unsupported_scheme", "no_usable_address", ...). The page turns each into a
// translated sentence.
const char* ArcSkipReasonToString(ArcSkipReason reason);

// A tab to import. All text is clean: no control characters, at most
// kArcMaxTitleLength UTF-16 units. `url` is canonical http or https without a
// user name or password.
struct ArcPlanTab {
  std::string url;
  // Arc's `savedTitle`: what the row shows until the page loads.
  std::string last_title;
  // Arc's own name for the tab, set only when it is not blank and differs from
  // `last_title`.
  std::optional<std::string> custom_name;
};

// A tab, or a folder with children, in Pinned.
struct ArcPlanNode {
  ArcPlanNode();
  ArcPlanNode(const ArcPlanNode&);
  ArcPlanNode(ArcPlanNode&&);
  ArcPlanNode& operator=(const ArcPlanNode&);
  ArcPlanNode& operator=(ArcPlanNode&&);
  ~ArcPlanNode();

  bool is_folder = false;
  // Folder only.
  std::string folder_name;
  // Tab only.
  ArcPlanTab tab;
  // Folder only, in order.
  std::vector<ArcPlanNode> children;
};

struct ArcSpacePlan {
  ArcSpacePlan();
  ArcSpacePlan(const ArcSpacePlan&);
  ArcSpacePlan(ArcSpacePlan&&);
  ArcSpacePlan& operator=(const ArcSpacePlan&);
  ArcSpacePlan& operator=(ArcSpacePlan&&);
  ~ArcSpacePlan();

  // Arc's id of the Space.
  std::string id;
  // Arc's title, clean; empty when Arc has none (the page shows "Space 3").
  std::string name;
  // An emoji, or empty.
  std::string icon;
  std::vector<ArcPlanNode> pinned;
  // At most kFavoritesCap, in order.
  std::vector<ArcPlanTab> favorites;
  // What was skipped inside this Space.
  ArcSkipCounts skipped;

  // Counts over `pinned` and `favorites`.
  int PinnedTabCount() const;
  int FolderCount() const;
  int CustomNameCount() const;
  // The memory the plan holds: the text of every address, title, name and the
  // icon, plus the size of every node, over the whole tree and the Favorites.
  size_t ApproxBytes() const;
};

enum class ArcParseError {
  // Not JSON of the shape of an Arc sidebar: no `sidebar.containers` list.
  kNotAnArcSidebar,
  // The file has no Space.
  kNoSpaces,
};

struct ArcParseResult {
  ArcParseResult();
  ArcParseResult(ArcParseResult&&);
  ArcParseResult& operator=(ArcParseResult&&);
  ~ArcParseResult();

  std::optional<ArcParseError> error;
  // At most kArcMaxSpaces, in file order.
  std::vector<ArcSpacePlan> spaces;
  // What was skipped outside every Space: entries past the caps, Little Arc.
  ArcSkipCounts file_skipped;

  // The sum of ApproxBytes over `spaces`.
  size_t PlanBytes() const;
};

// Reads the parsed JSON. Never fails on a single bad item: it is skipped and
// counted. Fails only when the file has no containers (kNotAnArcSidebar) or no
// Space (kNoSpaces); then nothing is returned.
ArcParseResult ParseArcSidebar(const base::Value& root);

// What the page shows for text from the file: control characters removed (a
// tab, a newline and a carriage return become a space), bidirectional override
// characters removed, trimmed, cut at kArcMaxTitleLength UTF-16 units without
// splitting a surrogate pair. Markup is not touched: it is text.
std::string SanitizeArcTitle(std::string_view text);

// The canonical form of `raw` when it is an http or https address that the
// import accepts, with the reason when it is not.
struct ArcUrlResult {
  std::optional<std::string> url;
  ArcSkipReason reason = ArcSkipReason::kNoUsableAddress;
};
ArcUrlResult ClassifyArcUrl(std::string_view raw);

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_ARC_IMPORT_H_
