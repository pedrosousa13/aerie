// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_TITLE_RESOLVER_H_
#define COMPONENTS_AERIE_TAB_MODEL_TITLE_RESOLVER_H_

#include <stddef.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "components/aerie_tab_model/tab_model_types.h"

// Which title the sidebar shows for a tab (issue #145, docs/sidebar-design.md,
// "Tab rename and title precedence"). Only the sidebar uses it: the page's
// `document.title`, history and the window title are never changed.
namespace aerie_tab_model {

class TabModel;

// How many UTF-16 characters a manual name holds. The sidebar's field stops
// there, and NormalizeManualName cuts at it.
inline constexpr size_t kMaxManualNameLength = 200;

// The sources of a title, in the order of the design's precedence table (first
// match wins). M2 has `manual` and `page`; #146 adds `rule` and `tidy`, #147
// adds `ai`. The names are listed now so that a provider for them only has to
// be added to the list.
enum class TitleSource { kManual, kRule, kTidy, kAi, kPage };

struct ResolvedTitle {
  std::string text;
  TitleSource source = TitleSource::kPage;
};

// One source of titles. Returns nothing (or an empty string) when it has no
// title for this tab; the resolver then asks the next one.
class TitleProvider {
 public:
  virtual ~TitleProvider() = default;
  virtual TitleSource source() const = 0;
  virtual std::optional<std::string> Provide(
      const TabRecord& record,
      std::string_view page_title) const = 0;
};

// An ordered list of providers. #146 only adds entries to the default list;
// nothing here changes for it.
class TitleResolver {
 public:
  explicit TitleResolver(std::vector<std::unique_ptr<TitleProvider>> providers);
  TitleResolver(TitleResolver&&);
  TitleResolver& operator=(TitleResolver&&);
  ~TitleResolver();

  // The M2 list: the manual name, then the page title.
  static TitleResolver CreateDefault();

  // The first provider with a title wins. When none has one, the page title.
  ResolvedTitle ResolveTitle(const TabRecord& record,
                             std::string_view page_title) const;

  // The sources of the providers, in order, for tests.
  std::vector<TitleSource> provider_sources() const;

 private:
  std::vector<std::unique_ptr<TitleProvider>> providers_;
};

// Cuts `text` to kMaxManualNameLength UTF-16 units, dropping a high surrogate
// that the cut would leave without its pair. It does not trim. The name fields
// in the sidebar and NormalizeManualName use this one cut.
std::u16string CutToManualNameLength(std::u16string_view text);

// What to store for the text the user typed: trimmed, cut at
// kMaxManualNameLength UTF-16 characters (never in the middle of a surrogate
// pair). Returns nothing, which means "reset the name", for an empty text and
// for a text equal to the page title (case-sensitive; also when the field
// held a long page title and the text is that title cut at the limit).
std::optional<std::string> NormalizeManualName(std::string_view input,
                                               std::string_view page_title);

enum class RenameResult { kUnknownTab, kUnchanged, kSet, kReset };

// Normalizes `input` and writes it as the tab's `manual_name` (or clears it).
// A write that would change nothing writes nothing. Works on a tab in any
// section, in a folder or archived; nothing else of the record changes.
RenameResult ApplyManualName(TabModel& model,
                             const std::string& tab_id,
                             std::string_view input,
                             std::string_view page_title);

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_TITLE_RESOLVER_H_
