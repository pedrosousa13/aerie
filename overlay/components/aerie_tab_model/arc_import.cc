// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/arc_import.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversion_utils.h"
#include "base/strings/utf_string_conversions.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/tab_model_types.h"
#include "url/gurl.h"
#include "url/url_util.h"

namespace aerie_tab_model {

ArcPlanNode::ArcPlanNode() = default;
ArcPlanNode::ArcPlanNode(const ArcPlanNode&) = default;
ArcPlanNode::ArcPlanNode(ArcPlanNode&&) = default;
ArcPlanNode& ArcPlanNode::operator=(const ArcPlanNode&) = default;
ArcPlanNode& ArcPlanNode::operator=(ArcPlanNode&&) = default;
ArcPlanNode::~ArcPlanNode() = default;

ArcSpacePlan::ArcSpacePlan() = default;
ArcSpacePlan::ArcSpacePlan(const ArcSpacePlan&) = default;
ArcSpacePlan::ArcSpacePlan(ArcSpacePlan&&) = default;
ArcSpacePlan& ArcSpacePlan::operator=(const ArcSpacePlan&) = default;
ArcSpacePlan& ArcSpacePlan::operator=(ArcSpacePlan&&) = default;
ArcSpacePlan::~ArcSpacePlan() = default;

ArcParseResult::ArcParseResult() = default;
ArcParseResult::ArcParseResult(ArcParseResult&&) = default;
ArcParseResult& ArcParseResult::operator=(ArcParseResult&&) = default;
ArcParseResult::~ArcParseResult() = default;

const char* ArcSkipReasonToString(ArcSkipReason reason) {
  switch (reason) {
    case ArcSkipReason::kUnsupportedScheme:
      return "unsupported_scheme";
    case ArcSkipReason::kNoUsableAddress:
      return "no_usable_address";
    case ArcSkipReason::kArcOnlyItem:
      return "arc_only_item";
    case ArcSkipReason::kTooMany:
      return "too_many";
    case ArcSkipReason::kTooDeep:
      return "too_deep";
    case ArcSkipReason::kFolderMerged:
      return "folder_merged";
    case ArcSkipReason::kFavoritesFull:
      return "favorites_full";
    case ArcSkipReason::kLittleArc:
      return "little_arc";
    case ArcSkipReason::kOverCap:
      return "over_cap";
    case ArcSkipReason::kAlreadyThere:
      return "already_there";
  }
  return "unknown";
}

namespace {

int CountTabs(const std::vector<ArcPlanNode>& nodes) {
  int count = 0;
  for (const ArcPlanNode& node : nodes) {
    count += node.is_folder ? CountTabs(node.children) : 1;
  }
  return count;
}

int CountFolders(const std::vector<ArcPlanNode>& nodes) {
  int count = 0;
  for (const ArcPlanNode& node : nodes) {
    if (node.is_folder) {
      count += 1 + CountFolders(node.children);
    }
  }
  return count;
}

int CountNames(const std::vector<ArcPlanNode>& nodes) {
  int count = 0;
  for (const ArcPlanNode& node : nodes) {
    count += node.is_folder ? CountNames(node.children)
                            : (node.tab.custom_name ? 1 : 0);
  }
  return count;
}

bool IsBidiControl(char16_t c) {
  return (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069);
}

enum class ItemKind { kTab, kFolder, kSplit, kContainer, kUnknown };

struct Item {
  std::string id;
  std::string parent_id;
  std::optional<std::string> title;
  std::vector<std::string> children;
  ItemKind kind = ItemKind::kUnknown;
  // Tabs.
  std::optional<std::string> url;
  std::optional<std::string> saved_title;
};

struct Container {
  std::map<std::string, Item> items;
  // The children of an id by `parentID`, in file order. For items with no
  // `childrenIds`.
  std::map<std::string, std::vector<std::string>> by_parent;
  // Profile key -> id of the Favorites container.
  std::map<std::string, std::string> favorite_roots;
};

// Calls fn(id, dict) for the dictionaries of `value`. A Swift dictionary is a
// list that alternates an id and a record; a record can also carry its own `id`,
// and a plain object keyed by id is read too. At most `max` records are passed
// on; returns how many were left unread.
template <typename Fn>
size_t ForEachRecord(const base::Value* value, size_t max, Fn&& fn) {
  size_t seen = 0;
  size_t unread = 0;
  auto visit = [&](std::string_view fallback_id, const base::DictValue& dict) {
    if (seen >= max) {
      ++unread;
      return;
    }
    ++seen;
    const std::string* own = dict.FindString("id");
    fn(own && !own->empty() ? *own : std::string(fallback_id), dict);
  };
  if (!value) {
    return 0;
  }
  if (const base::ListValue* list = value->GetIfList()) {
    std::string pending;
    for (const base::Value& entry : *list) {
      if (entry.is_string()) {
        pending = entry.GetString();
      } else if (const base::DictValue* dict = entry.GetIfDict()) {
        visit(pending, *dict);
        pending.clear();
      } else {
        pending.clear();
      }
    }
  } else if (const base::DictValue* dict = value->GetIfDict()) {
    for (auto [key, entry] : *dict) {
      if (const base::DictValue* record = entry.GetIfDict()) {
        visit(key, *record);
      }
    }
  }
  return unread;
}

std::optional<std::string> OptionalString(const base::DictValue& dict,
                                          std::string_view key) {
  const std::string* text = dict.FindString(key);
  if (!text) {
    return std::nullopt;
  }
  return *text;
}

Item ReadItem(const std::string& id, const base::DictValue& dict) {
  Item item;
  item.id = id;
  if (const std::string* parent = dict.FindString("parentID")) {
    item.parent_id = *parent;
  }
  item.title = OptionalString(dict, "title");
  if (const base::ListValue* children = dict.FindList("childrenIds")) {
    for (const base::Value& child : *children) {
      if (child.is_string()) {
        item.children.push_back(child.GetString());
      }
    }
  }
  if (const base::DictValue* data = dict.FindDict("data")) {
    if (const base::DictValue* tab = data->FindDict("tab")) {
      item.kind = ItemKind::kTab;
      item.url = OptionalString(*tab, "savedURL");
      item.saved_title = OptionalString(*tab, "savedTitle");
    } else if (data->FindDict("list")) {
      item.kind = ItemKind::kFolder;
    } else if (data->FindDict("splitView")) {
      item.kind = ItemKind::kSplit;
    } else if (data->FindDict("itemContainer")) {
      item.kind = ItemKind::kContainer;
    }
  }
  return item;
}

// Which Arc profile a marker names: `{"default": ...}`, or `{"custom": {"_0":
// {"machineID", "directoryBasename"}}}`. A missing or odd marker is the main
// profile, as in Zen.
std::string ProfileKey(const base::Value* marker) {
  const base::DictValue* dict = marker ? marker->GetIfDict() : nullptr;
  if (!dict || dict->contains("default")) {
    return "default";
  }
  const base::DictValue* custom = dict->FindDict("custom");
  const base::DictValue* details = custom ? custom->FindDict("_0") : nullptr;
  if (!details) {
    return "default";
  }
  const std::string* machine = details->FindString("machineID");
  const std::string* directory = details->FindString("directoryBasename");
  return "custom:" + (machine ? *machine : std::string()) + ":" +
         (directory ? *directory : std::string());
}

std::string SpaceIcon(const base::DictValue& space) {
  const base::DictValue* info = space.FindDict("customInfo");
  const base::DictValue* icon = info ? info->FindDict("iconType") : nullptr;
  if (!icon) {
    return std::string();
  }
  if (const std::string* text = icon->FindString("emoji_v2")) {
    std::string clean = SanitizeArcTitle(*text);
    // An emoji sequence is a few units; anything longer is not an icon.
    if (!clean.empty() && clean.size() <= 64) {
      return clean;
    }
  }
  std::optional<int> code = icon->FindInt("emoji");
  if (code && *code > 0 && *code <= 0x10FFFF &&
      !(*code >= 0xD800 && *code <= 0xDFFF)) {
    std::string out;
    base::WriteUnicodeCharacter(*code, &out);
    return SanitizeArcTitle(out);
  }
  return std::string();
}

// Walks the item tree under one root and builds the plan nodes.
class Walker {
 public:
  Walker(const Container& container, ArcSkipCounts* skipped)
      : container_(container), skipped_(skipped) {}

  // `flat`: every folder is merged into the list (Favorites), and it is not
  // counted as a merge.
  void Walk(const std::string& root, bool flat, std::vector<ArcPlanNode>* out) {
    flat_ = flat;
    VisitChildren(root, /*depth=*/0, /*level=*/0, out);
  }

 private:
  std::vector<std::string> ChildrenOf(const std::string& id) const {
    std::vector<std::string> result;
    auto it = container_.items.find(id);
    if (it == container_.items.end()) {
      return result;
    }
    for (const std::string& child : it->second.children) {
      if (container_.items.contains(child)) {
        result.push_back(child);
      }
    }
    if (result.empty()) {
      auto by_parent = container_.by_parent.find(id);
      if (by_parent != container_.by_parent.end()) {
        result = by_parent->second;
      }
    }
    return result;
  }

  void VisitChildren(const std::string& id,
                     int depth,
                     int level,
                     std::vector<ArcPlanNode>* out) {
    for (const std::string& child : ChildrenOf(id)) {
      VisitItem(child, depth + 1, level, out);
    }
  }

  void VisitItem(const std::string& id,
                 int depth,
                 int level,
                 std::vector<ArcPlanNode>* out) {
    if (!visited_.insert(id).second) {
      return;
    }
    auto it = container_.items.find(id);
    if (it == container_.items.end()) {
      return;
    }
    const Item& item = it->second;
    if (depth > kArcMaxDepth) {
      ++(*skipped_)[ArcSkipReason::kTooDeep];
      return;
    }
    switch (item.kind) {
      case ItemKind::kTab:
        AddTab(item, out);
        return;
      case ItemKind::kFolder:
        if (!flat_ && level < kMaxFolderDepth) {
          ArcPlanNode folder;
          folder.is_folder = true;
          folder.folder_name = SanitizeArcTitle(item.title.value_or(""));
          VisitChildren(id, depth, level + 1, &folder.children);
          out->push_back(std::move(folder));
        } else {
          if (!flat_) {
            ++(*skipped_)[ArcSkipReason::kFolderMerged];
          }
          VisitChildren(id, depth, level, out);
        }
        return;
      case ItemKind::kSplit:
      case ItemKind::kContainer:
        VisitChildren(id, depth, level, out);
        return;
      case ItemKind::kUnknown:
        ++(*skipped_)[ArcSkipReason::kArcOnlyItem];
        return;
    }
  }

  void AddTab(const Item& item, std::vector<ArcPlanNode>* out) {
    if (!item.url) {
      ++(*skipped_)[ArcSkipReason::kNoUsableAddress];
      return;
    }
    ArcUrlResult url = ClassifyArcUrl(*item.url);
    if (!url.url) {
      ++(*skipped_)[url.reason];
      return;
    }
    ArcPlanNode node;
    node.tab.url = *url.url;
    node.tab.last_title = SanitizeArcTitle(item.saved_title.value_or(""));
    if (item.title) {
      std::string name = SanitizeArcTitle(*item.title);
      if (!name.empty() && name != node.tab.last_title) {
        node.tab.custom_name = std::move(name);
      }
    }
    out->push_back(std::move(node));
  }

  const Container& container_;
  const raw_ptr<ArcSkipCounts> skipped_;
  std::set<std::string> visited_;
  bool flat_ = false;
};

void FlattenTabs(const std::vector<ArcPlanNode>& nodes,
                 std::vector<ArcPlanTab>* out) {
  for (const ArcPlanNode& node : nodes) {
    if (node.is_folder) {
      FlattenTabs(node.children, out);
    } else {
      out->push_back(node.tab);
    }
  }
}

size_t TabBytes(const ArcPlanTab& tab) {
  return sizeof(ArcPlanTab) + tab.url.capacity() + tab.last_title.capacity() +
         (tab.custom_name ? tab.custom_name->capacity() : 0);
}

size_t NodeBytes(const std::vector<ArcPlanNode>& nodes) {
  size_t total = nodes.capacity() * sizeof(ArcPlanNode);
  for (const ArcPlanNode& node : nodes) {
    // The node's own struct (in the vector above) already holds its tab.
    total += node.folder_name.capacity() + TabBytes(node.tab) -
             sizeof(ArcPlanTab) + NodeBytes(node.children);
  }
  return total;
}

}  // namespace

size_t ArcSpacePlan::ApproxBytes() const {
  size_t total = sizeof(ArcSpacePlan) + id.capacity() + name.capacity() +
                 icon.capacity() + NodeBytes(pinned);
  for (const ArcPlanTab& tab : favorites) {
    total += TabBytes(tab);
  }
  return total;
}

size_t ArcParseResult::PlanBytes() const {
  size_t total = 0;
  for (const ArcSpacePlan& space : spaces) {
    total += space.ApproxBytes();
  }
  return total;
}

int ArcSpacePlan::PinnedTabCount() const {
  return CountTabs(pinned);
}
int ArcSpacePlan::FolderCount() const {
  return CountFolders(pinned);
}
int ArcSpacePlan::CustomNameCount() const {
  int count = CountNames(pinned);
  for (const ArcPlanTab& tab : favorites) {
    count += tab.custom_name ? 1 : 0;
  }
  return count;
}

std::string SanitizeArcTitle(std::string_view text) {
  const std::u16string wide = base::UTF8ToUTF16(text);
  std::u16string clean;
  clean.reserve(std::min(wide.size(), kArcMaxTitleLength * 2));
  for (char16_t c : wide) {
    if (c == u'\t' || c == u'\n' || c == u'\r' || c == 0x2028 || c == 0x2029) {
      clean.push_back(u' ');
    } else if (c < 0x20 || (c >= 0x7F && c <= 0x9F) || IsBidiControl(c)) {
      continue;
    } else {
      clean.push_back(c);
    }
  }
  std::u16string trimmed;
  base::TrimWhitespace(clean, base::TRIM_ALL, &trimmed);
  if (trimmed.size() > kArcMaxTitleLength) {
    trimmed.resize(kArcMaxTitleLength);
    if ((trimmed.back() & 0xFC00) == 0xD800) {
      trimmed.pop_back();
    }
    base::TrimWhitespace(trimmed, base::TRIM_TRAILING, &trimmed);
  }
  return base::UTF16ToUTF8(trimmed);
}

ArcUrlResult ClassifyArcUrl(std::string_view raw) {
  ArcUrlResult result;
  const std::string trimmed(base::TrimWhitespaceASCII(raw, base::TRIM_ALL));
  if (trimmed.empty() || trimmed.size() > kArcMaxUrlLength) {
    return result;
  }
  url::Component scheme;
  if (url::ExtractScheme(trimmed, &scheme) && scheme.is_valid()) {
    const std::string name =
        base::ToLowerASCII(trimmed.substr(scheme.begin, scheme.len));
    if (name != "http" && name != "https") {
      result.reason = ArcSkipReason::kUnsupportedScheme;
      return result;
    }
  }
  GURL gurl(trimmed);
  if (!gurl.is_valid() || !gurl.SchemeIsHTTPOrHTTPS() || !gurl.has_host()) {
    return result;
  }
  if (gurl.has_username() || gurl.has_password()) {
    GURL::Replacements replacements;
    replacements.ClearUsername();
    replacements.ClearPassword();
    gurl = gurl.ReplaceComponents(replacements);
  }
  if (!gurl.is_valid() || gurl.spec().size() > kArcMaxUrlLength) {
    return result;
  }
  result.url = gurl.spec();
  return result;
}

ArcParseResult ParseArcSidebar(const base::Value& root) {
  ArcParseResult result;
  const base::DictValue* top = root.GetIfDict();
  const base::DictValue* sidebar = top ? top->FindDict("sidebar") : nullptr;
  const base::ListValue* containers =
      sidebar ? sidebar->FindList("containers") : nullptr;
  if (!containers) {
    result.error = ArcParseError::kNotAnArcSidebar;
    return result;
  }

  size_t item_budget = kArcMaxItems;
  size_t plan_bytes = 0;
  bool plan_full = false;
  bool next_is_little_arc = false;
  for (const base::Value& entry : *containers) {
    const base::DictValue* dict = entry.GetIfDict();
    if (!dict) {
      continue;
    }
    if (dict->contains("littleBrowser")) {
      next_is_little_arc = true;
      continue;
    }
    if (!dict->FindList("items") && !dict->FindDict("items") &&
        !dict->FindList("spaces") && !dict->FindDict("spaces")) {
      continue;  // A marker such as {"global": {}}.
    }
    if (next_is_little_arc) {
      next_is_little_arc = false;
      ++result.file_skipped[ArcSkipReason::kLittleArc];
      continue;
    }

    Container container;
    size_t read = 0;
    const size_t unread_items = ForEachRecord(
        dict->Find("items"), item_budget,
        [&](const std::string& id, const base::DictValue& record) {
          ++read;
          if (id.empty()) {
            return;
          }
          Item item = ReadItem(id, record);
          if (!item.parent_id.empty()) {
            // File order, for items with no `childrenIds`.
            container.by_parent[item.parent_id].push_back(id);
          }
          // A later record with the same id replaces an earlier one (Crest).
          container.items[id] = std::move(item);
        });
    item_budget -= std::min(item_budget, read);
    if (unread_items) {
      result.file_skipped[ArcSkipReason::kTooMany] +=
          static_cast<int>(unread_items);
    }

    if (const base::ListValue* tops = dict->FindList("topAppsContainerIDs")) {
      for (size_t i = 0; i + 1 < tops->size(); i += 2) {
        if ((*tops)[i + 1].is_string()) {
          container.favorite_roots[ProfileKey(&(*tops)[i])] =
              (*tops)[i + 1].GetString();
        }
      }
    }

    const size_t unread_spaces = ForEachRecord(
        dict->Find("spaces"), kArcMaxSpaces - result.spaces.size(),
        [&](const std::string& id, const base::DictValue& record) {
          if (plan_full) {
            // Over the byte budget (issue #277): the rest of the Spaces are not
            // read, as with the cap of Spaces.
            ++result.file_skipped[ArcSkipReason::kTooMany];
            return;
          }
          ArcSpacePlan space;
          space.id = id;
          if (const std::string* title = record.FindString("title")) {
            space.name = SanitizeArcTitle(*title);
          }
          space.icon = SpaceIcon(record);

          const base::ListValue* ids = record.FindList("containerIDs");
          if (!ids || ids->empty()) {
            ids = record.FindList("newContainerIDs");
          }
          bool in_pinned = false;
          if (ids) {
            Walker walker(container, &space.skipped);
            for (const base::Value& value : *ids) {
              if (!value.is_string()) {
                continue;
              }
              const std::string& text = value.GetString();
              if (text == "pinned") {
                in_pinned = true;
              } else if (text == "unpinned") {
                in_pinned = false;
              } else if (in_pinned) {
                walker.Walk(text, /*flat=*/false, &space.pinned);
              }
            }
          }

          auto favorites =
              container.favorite_roots.find(ProfileKey(record.Find("profile")));
          if (favorites != container.favorite_roots.end()) {
            std::vector<ArcPlanNode> nodes;
            Walker walker(container, &space.skipped);
            walker.Walk(favorites->second, /*flat=*/true, &nodes);
            std::vector<ArcPlanTab> tabs;
            FlattenTabs(nodes, &tabs);
            for (ArcPlanTab& tab : tabs) {
              if (space.favorites.size() < kFavoritesCap) {
                space.favorites.push_back(std::move(tab));
              } else {
                ++space.skipped[ArcSkipReason::kFavoritesFull];
              }
            }
          }
          const size_t space_bytes = space.ApproxBytes();
          if (space_bytes > kArcMaxPlanBytes - plan_bytes) {
            plan_full = true;
            ++result.file_skipped[ArcSkipReason::kTooMany];
            return;
          }
          plan_bytes += space_bytes;
          result.spaces.push_back(std::move(space));
        });
    if (unread_spaces) {
      result.file_skipped[ArcSkipReason::kTooMany] +=
          static_cast<int>(unread_spaces);
    }
  }

  if (result.spaces.empty()) {
    result.error = ArcParseError::kNoSpaces;
  }
  return result;
}

}  // namespace aerie_tab_model
