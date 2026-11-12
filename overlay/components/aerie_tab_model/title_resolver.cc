// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/title_resolver.h"

#include <utility>

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/aerie_tab_model/tab_model.h"

namespace aerie_tab_model {

namespace {

class ManualNameProvider : public TitleProvider {
 public:
  TitleSource source() const override { return TitleSource::kManual; }
  std::optional<std::string> Provide(const TabRecord& record,
                                     std::string_view) const override {
    return record.manual_name;
  }
};

class PageTitleProvider : public TitleProvider {
 public:
  TitleSource source() const override { return TitleSource::kPage; }
  std::optional<std::string> Provide(
      const TabRecord&,
      std::string_view page_title) const override {
    return std::string(page_title);
  }
};

// Trims and cuts `text` as a manual name is.
std::u16string TrimAndCut(std::string_view text) {
  const std::u16string wide = base::UTF8ToUTF16(text);
  const std::u16string_view view = base::TrimWhitespace(wide, base::TRIM_ALL);
  const std::u16string cut = CutToManualNameLength(view);
  if (cut.size() == view.size()) {
    return cut;
  }
  return std::u16string(base::TrimWhitespace(cut, base::TRIM_TRAILING));
}

}  // namespace

std::u16string CutToManualNameLength(std::u16string_view text) {
  if (text.size() <= kMaxManualNameLength) {
    return std::u16string(text);
  }
  std::u16string result(text.substr(0, kMaxManualNameLength));
  // Never keep half of a surrogate pair.
  if ((result.back() & 0xFC00) == 0xD800) {
    result.pop_back();
  }
  return result;
}

TitleResolver::TitleResolver(
    std::vector<std::unique_ptr<TitleProvider>> providers)
    : providers_(std::move(providers)) {}
TitleResolver::TitleResolver(TitleResolver&&) = default;
TitleResolver& TitleResolver::operator=(TitleResolver&&) = default;
TitleResolver::~TitleResolver() = default;

// static
TitleResolver TitleResolver::CreateDefault() {
  std::vector<std::unique_ptr<TitleProvider>> providers;
  providers.push_back(std::make_unique<ManualNameProvider>());
  providers.push_back(std::make_unique<PageTitleProvider>());
  return TitleResolver(std::move(providers));
}

ResolvedTitle TitleResolver::ResolveTitle(const TabRecord& record,
                                          std::string_view page_title) const {
  for (const auto& provider : providers_) {
    std::optional<std::string> title = provider->Provide(record, page_title);
    if (title && !title->empty()) {
      return {std::move(*title), provider->source()};
    }
  }
  return {std::string(page_title), TitleSource::kPage};
}

std::vector<TitleSource> TitleResolver::provider_sources() const {
  std::vector<TitleSource> sources;
  for (const auto& provider : providers_) {
    sources.push_back(provider->source());
  }
  return sources;
}

std::optional<std::string> NormalizeManualName(std::string_view input,
                                               std::string_view page_title) {
  const std::u16string name = TrimAndCut(input);
  if (name.empty()) {
    return std::nullopt;
  }
  // The page title as the field would hold it (cut), and as it is.
  if (name == TrimAndCut(page_title) ||
      name ==
          base::TrimWhitespace(base::UTF8ToUTF16(page_title), base::TRIM_ALL)) {
    return std::nullopt;
  }
  return base::UTF16ToUTF8(name);
}

RenameResult ApplyManualName(TabModel& model,
                             const std::string& tab_id,
                             std::string_view input,
                             std::string_view page_title) {
  const TabRecord* record = model.GetTab(tab_id);
  if (!record) {
    return RenameResult::kUnknownTab;
  }
  const std::optional<std::string> name =
      NormalizeManualName(input, page_title);
  if (record->manual_name == name) {
    return RenameResult::kUnchanged;
  }
  model.UpdateTab(tab_id, [&name](TabRecord& tab) { tab.manual_name = name; });
  return name ? RenameResult::kSet : RenameResult::kReset;
}

}  // namespace aerie_tab_model
