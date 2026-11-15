// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/title_resolver.h"

#include <utility>

#include "base/memory/raw_ptr.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/aerie_tab_model/rename_rules.h"
#include "components/aerie_tab_model/rule_matcher.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/title_tidier.h"

namespace aerie_tab_model {

namespace {

class ManualNameProvider : public TitleProvider {
 public:
  TitleSource source() const override { return TitleSource::kManual; }
  std::optional<ProvidedTitle> Provide(const TitleInput& input) const override {
    if (!input.record.manual_name) {
      return std::nullopt;
    }
    return ProvidedTitle{*input.record.manual_name, std::string()};
  }
};

// The first enabled rename rule that matches (issue #146).
class RuleTitleProvider : public TitleProvider {
 public:
  explicit RuleTitleProvider(const TitleSettings* settings)
      : settings_(settings) {}
  TitleSource source() const override { return TitleSource::kRule; }
  std::optional<ProvidedTitle> Provide(const TitleInput& input) const override {
    if (!settings_->rename.enabled || settings_->rename.rules.empty()) {
      return std::nullopt;
    }
    const std::string_view title = BoundTitle(input.page_title);
    const MatchInput match(input.url, title);
    std::optional<RenameOutcome> outcome =
        ApplyRenameRules(settings_->rename, match, title);
    if (!outcome) {
      return std::nullopt;
    }
    // A rule with no name is told by what it matches.
    return ProvidedTitle{std::move(outcome->text),
                         outcome->rule->name.empty() ? outcome->rule->match.value
                                                     : outcome->rule->name};
  }

 private:
  const raw_ptr<const TitleSettings> settings_;
};

// The built-in tidy (issue #146). Gives nothing when it would not change the
// title, so the row shows the page title and has no "Tidied" tooltip.
class TidyTitleProvider : public TitleProvider {
 public:
  explicit TidyTitleProvider(const TitleSettings* settings)
      : settings_(settings) {}
  TitleSource source() const override { return TitleSource::kTidy; }
  std::optional<ProvidedTitle> Provide(const TitleInput& input) const override {
    const std::string_view host =
        input.url.is_valid() && input.url.has_host() ? input.url.host_piece()
                                                     : std::string_view();
    if (!settings_->TidyAppliesTo(host)) {
      return std::nullopt;
    }
    std::string tidied = TidyTitle(input.page_title, host);
    if (tidied.empty() || tidied == input.page_title) {
      return std::nullopt;
    }
    return ProvidedTitle{std::move(tidied), std::string()};
  }

 private:
  const raw_ptr<const TitleSettings> settings_;
};

class PageTitleProvider : public TitleProvider {
 public:
  TitleSource source() const override { return TitleSource::kPage; }
  std::optional<ProvidedTitle> Provide(const TitleInput& input) const override {
    return ProvidedTitle{std::string(input.page_title), std::string()};
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
TitleResolver TitleResolver::CreateDefault(const TitleSettings* settings) {
  std::vector<std::unique_ptr<TitleProvider>> providers;
  providers.push_back(std::make_unique<ManualNameProvider>());
  if (settings) {
    providers.push_back(std::make_unique<RuleTitleProvider>(settings));
    providers.push_back(std::make_unique<TidyTitleProvider>(settings));
  }
  providers.push_back(std::make_unique<PageTitleProvider>());
  return TitleResolver(std::move(providers));
}

ResolvedTitle TitleResolver::ResolveTitle(const TabRecord& record,
                                          std::string_view page_title) const {
  return ResolveTitle(record, page_title, GURL());
}

ResolvedTitle TitleResolver::ResolveTitle(const TabRecord& record,
                                          std::string_view page_title,
                                          const GURL& url) const {
  const TitleInput input{record, page_title, url};
  for (const auto& provider : providers_) {
    std::optional<ProvidedTitle> title = provider->Provide(input);
    if (title && !title->text.empty()) {
      return {std::move(title->text), provider->source(),
              std::move(title->detail)};
    }
  }
  return {std::string(page_title), TitleSource::kPage, std::string()};
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
