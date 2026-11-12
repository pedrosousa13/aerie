// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/title_resolver.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/test/simple_test_clock.h"
#include "base/time/time.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_types.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_tab_model {

namespace {

// A provider that answers a fixed value (or nothing) for one source. #146 and
// #147 add real providers for `rule`, `tidy` and `ai`; the resolver is tested
// here with fakes for every source it knows.
class FakeProvider : public TitleProvider {
 public:
  FakeProvider(TitleSource source, std::optional<std::string> answer)
      : source_(source), answer_(std::move(answer)) {}
  TitleSource source() const override { return source_; }
  std::optional<std::string> Provide(const TabRecord&,
                                     std::string_view) const override {
    ++calls;
    return answer_;
  }
  mutable int calls = 0;

 private:
  TitleSource source_;
  std::optional<std::string> answer_;
};

std::unique_ptr<FakeProvider> Fake(TitleSource source,
                                   std::optional<std::string> answer) {
  return std::make_unique<FakeProvider>(source, std::move(answer));
}

TitleResolver ResolverOf(std::vector<std::unique_ptr<TitleProvider>> providers) {
  return TitleResolver(std::move(providers));
}

}  // namespace

TEST(TitleResolverTest, TheFirstProviderThatAnswersWins) {
  std::vector<std::unique_ptr<TitleProvider>> providers;
  providers.push_back(Fake(TitleSource::kManual, "Mine"));
  providers.push_back(Fake(TitleSource::kPage, "Page"));
  const ResolvedTitle title = ResolverOf(std::move(providers))
                                  .ResolveTitle(TabRecord(), "Page");
  EXPECT_EQ(title.text, "Mine");
  EXPECT_EQ(title.source, TitleSource::kManual);
}

TEST(TitleResolverTest, AProviderThatHasNothingPassesToTheNext) {
  std::vector<std::unique_ptr<TitleProvider>> providers;
  providers.push_back(Fake(TitleSource::kManual, std::nullopt));
  providers.push_back(Fake(TitleSource::kPage, "Page"));
  const ResolvedTitle title = ResolverOf(std::move(providers))
                                  .ResolveTitle(TabRecord(), "Page");
  EXPECT_EQ(title.text, "Page");
  EXPECT_EQ(title.source, TitleSource::kPage);
}

TEST(TitleResolverTest, AnEmptyAnswerCountsAsNothing) {
  std::vector<std::unique_ptr<TitleProvider>> providers;
  providers.push_back(Fake(TitleSource::kManual, ""));
  providers.push_back(Fake(TitleSource::kPage, "Page"));
  EXPECT_EQ(ResolverOf(std::move(providers))
                .ResolveTitle(TabRecord(), "Page")
                .source,
            TitleSource::kPage);
}

TEST(TitleResolverTest, TheOrderOfTheListIsTheOrderOfPrecedence) {
  // Every source the design names, in the design's order. All of them answer;
  // the first one wins, then the first of what is left, and so on.
  const std::vector<TitleSource> order = {TitleSource::kManual,
                                          TitleSource::kRule,
                                          TitleSource::kTidy, TitleSource::kAi,
                                          TitleSource::kPage};
  for (size_t winner = 0; winner < order.size(); ++winner) {
    std::vector<std::unique_ptr<TitleProvider>> providers;
    for (size_t i = 0; i < order.size(); ++i) {
      // The ones before `winner` have nothing; the others answer.
      providers.push_back(Fake(order[i], i < winner
                                             ? std::nullopt
                                             : std::optional<std::string>(
                                                   "title " + std::to_string(i))));
    }
    const ResolvedTitle title =
        ResolverOf(std::move(providers)).ResolveTitle(TabRecord(), "page");
    EXPECT_EQ(title.source, order[winner]) << winner;
    EXPECT_EQ(title.text, "title " + std::to_string(winner)) << winner;
  }
}

TEST(TitleResolverTest, AProviderAfterTheWinnerIsNotAsked) {
  auto first = Fake(TitleSource::kManual, "Mine");
  auto second = Fake(TitleSource::kPage, "Page");
  FakeProvider* second_ptr = second.get();
  std::vector<std::unique_ptr<TitleProvider>> providers;
  providers.push_back(std::move(first));
  providers.push_back(std::move(second));
  const TitleResolver resolver = ResolverOf(std::move(providers));
  resolver.ResolveTitle(TabRecord(), "Page");
  EXPECT_EQ(second_ptr->calls, 0);
}

TEST(TitleResolverTest, NoProviderAnsweringGivesThePageTitleAsPage) {
  const ResolvedTitle title =
      ResolverOf({}).ResolveTitle(TabRecord(), "Fallback");
  EXPECT_EQ(title.text, "Fallback");
  EXPECT_EQ(title.source, TitleSource::kPage);
}

TEST(TitleResolverTest, TheDefaultResolverHasManualThenPage) {
  const TitleResolver resolver = TitleResolver::CreateDefault();
  ASSERT_EQ(resolver.provider_sources().size(), 2u);
  EXPECT_EQ(resolver.provider_sources()[0], TitleSource::kManual);
  EXPECT_EQ(resolver.provider_sources()[1], TitleSource::kPage);
}

TEST(TitleResolverTest, TheDefaultResolverShowsTheManualNameOverThePageTitle) {
  const TitleResolver resolver = TitleResolver::CreateDefault();
  TabRecord tab;
  EXPECT_EQ(resolver.ResolveTitle(tab, "Page").text, "Page");
  EXPECT_EQ(resolver.ResolveTitle(tab, "Page").source, TitleSource::kPage);
  tab.manual_name = "Mine";
  EXPECT_EQ(resolver.ResolveTitle(tab, "Page").text, "Mine");
  EXPECT_EQ(resolver.ResolveTitle(tab, "Page").source, TitleSource::kManual);
}

TEST(TitleResolverTest, TheDefaultResolverIgnoresAnEmptyManualName) {
  const TitleResolver resolver = TitleResolver::CreateDefault();
  TabRecord tab;
  tab.manual_name = "";
  EXPECT_EQ(resolver.ResolveTitle(tab, "Page").source, TitleSource::kPage);
}

TEST(NormalizeManualNameTest, TrimsWhitespace) {
  EXPECT_EQ(NormalizeManualName("  Inbox \t", "Page"), "Inbox");
}

TEST(NormalizeManualNameTest, KeepsInnerWhitespace) {
  EXPECT_EQ(NormalizeManualName("My  inbox", "Page"), "My  inbox");
}

TEST(NormalizeManualNameTest, EmptyAndBlankReset) {
  EXPECT_EQ(NormalizeManualName("", "Page"), std::nullopt);
  EXPECT_EQ(NormalizeManualName("   \t ", "Page"), std::nullopt);
}

TEST(NormalizeManualNameTest, ThePageTitleTypedBackResets) {
  EXPECT_EQ(NormalizeManualName("Page", "Page"), std::nullopt);
  EXPECT_EQ(NormalizeManualName("  Page  ", "Page"), std::nullopt);
}

TEST(NormalizeManualNameTest, ThePageTitleComparisonIsCaseSensitive) {
  EXPECT_EQ(NormalizeManualName("page", "Page"), "page");
  EXPECT_EQ(NormalizeManualName("PAGE", "Page"), "PAGE");
}

TEST(NormalizeManualNameTest, ALongNameIsCutAtTwoHundredCharacters) {
  const std::string long_name(300, 'a');
  const auto name = NormalizeManualName(long_name, "Page");
  ASSERT_TRUE(name);
  EXPECT_EQ(name->size(), kMaxManualNameLength);
  EXPECT_EQ(kMaxManualNameLength, 200u);
}

TEST(NormalizeManualNameTest, ACutNeverSplitsASurrogatePair) {
  // 199 letters, then U+1F600 (two UTF-16 units): the pair would end at 201.
  std::string text(199, 'a');
  text += "\xF0\x9F\x98\x80";
  const auto name = NormalizeManualName(text, "Page");
  ASSERT_TRUE(name);
  EXPECT_EQ(*name, std::string(199, 'a'));
}

TEST(NormalizeManualNameTest, ACutThatLeavesTrailingSpaceTrimsIt) {
  const std::string text = std::string(199, 'a') + " bbb";
  EXPECT_EQ(NormalizeManualName(text, "Page"), std::string(199, 'a'));
}

TEST(NormalizeManualNameTest, ALongPageTitleLeftUntouchedResets) {
  // The editor opens with the page title; the field holds at most 200
  // characters. Pressing Enter on the cut title must not make it a name.
  const std::string long_title(300, 'a');
  EXPECT_EQ(NormalizeManualName(std::string(kMaxManualNameLength, 'a'),
                                long_title),
            std::nullopt);
  EXPECT_EQ(NormalizeManualName(long_title, long_title), std::nullopt);
}

class ApplyManualNameTest : public testing::Test {
 protected:
  ApplyManualNameTest()
      : model_(&clock_) {
    clock_.SetNow(base::Time::FromMillisecondsSinceUnixEpoch(1'700'000'000'000));
  }
  std::string AddTab(Section section = Section::kToday,
                     const std::string& parent = std::string()) {
    TabRecord tab;
    tab.section = section;
    tab.parent = parent;
    return model_.AddTab(std::move(tab));
  }
  base::SimpleTestClock clock_;
  TabModel model_;
};

TEST_F(ApplyManualNameTest, SetsTheName) {
  const std::string id = AddTab();
  EXPECT_EQ(ApplyManualName(model_, id, "Inbox", "Page"), RenameResult::kSet);
  EXPECT_EQ(model_.GetTab(id)->manual_name, "Inbox");
}

TEST_F(ApplyManualNameTest, EmptyResetsAName) {
  const std::string id = AddTab();
  ApplyManualName(model_, id, "Inbox", "Page");
  EXPECT_EQ(ApplyManualName(model_, id, "", "Page"), RenameResult::kReset);
  EXPECT_EQ(model_.GetTab(id)->manual_name, std::nullopt);
}

TEST_F(ApplyManualNameTest, TheRealPageTitleResetsAName) {
  const std::string id = AddTab();
  ApplyManualName(model_, id, "Inbox", "Page");
  EXPECT_EQ(ApplyManualName(model_, id, "Page", "Page"), RenameResult::kReset);
  EXPECT_EQ(model_.GetTab(id)->manual_name, std::nullopt);
}

TEST_F(ApplyManualNameTest, ResettingATabWithoutANameChangesNothing) {
  const std::string id = AddTab();
  const int64_t next = model_.next_change();
  EXPECT_EQ(ApplyManualName(model_, id, "", "Page"), RenameResult::kUnchanged);
  EXPECT_EQ(model_.next_change(), next);
}

TEST_F(ApplyManualNameTest, TheSameNameAgainWritesNothing) {
  const std::string id = AddTab();
  ApplyManualName(model_, id, "Inbox", "Page");
  const int64_t next = model_.next_change();
  EXPECT_EQ(ApplyManualName(model_, id, " Inbox ", "Page"),
            RenameResult::kUnchanged);
  EXPECT_EQ(model_.next_change(), next);
}

TEST_F(ApplyManualNameTest, AnUnknownTabIsReported) {
  EXPECT_EQ(ApplyManualName(model_, "nope", "Inbox", "Page"),
            RenameResult::kUnknownTab);
}

TEST_F(ApplyManualNameTest, AnyTabCanBeRenamedInEverySection) {
  FolderRecord folder;
  folder.name = "Work";
  const std::string folder_id = model_.AddFolder(std::move(folder));
  ASSERT_FALSE(folder_id.empty());
  const std::string today = AddTab(Section::kToday);
  const std::string in_folder = AddTab(Section::kToday, folder_id);
  const std::string pinned = AddTab(Section::kPinned);
  const std::string favorite = AddTab(Section::kFavorite);
  for (const std::string& id : {today, in_folder, pinned, favorite}) {
    ASSERT_FALSE(id.empty());
    EXPECT_EQ(ApplyManualName(model_, id, "Mine", "Page"), RenameResult::kSet)
        << id;
    EXPECT_EQ(model_.GetTab(id)->manual_name, "Mine");
  }
}

TEST_F(ApplyManualNameTest, ARenameKeepsPlacementAndLastTitle) {
  TabRecord tab;
  tab.section = Section::kPinned;
  tab.last_title = "Real title";
  const std::string id = model_.AddTab(std::move(tab));
  ApplyManualName(model_, id, "Mine", "Real title");
  EXPECT_EQ(model_.GetTab(id)->section, Section::kPinned);
  EXPECT_EQ(model_.GetTab(id)->last_title, "Real title");
}

TEST_F(ApplyManualNameTest, TheNameSurvivesAnUpdateThatOnlyTouchesTheTitle) {
  // What a navigation does to the record (TabStripAdapter): it writes
  // `last_title`, and never `manual_name`.
  const std::string id = AddTab(Section::kPinned);
  ApplyManualName(model_, id, "Mine", "Page");
  model_.UpdateTab(id, [](TabRecord& tab) { tab.last_title = "Next page"; });
  EXPECT_EQ(model_.GetTab(id)->manual_name, "Mine");
}

TEST_F(ApplyManualNameTest, TheNameSurvivesArchiveAndRestore) {
  const std::string id = AddTab();
  ApplyManualName(model_, id, "Mine", "Page");
  model_.UpdateTab(id, [this](TabRecord& tab) {
    tab.archived_at = clock_.Now();
  });
  EXPECT_EQ(model_.GetTab(id)->manual_name, "Mine");
  model_.UpdateTab(id, [](TabRecord& tab) { tab.archived_at = std::nullopt; });
  EXPECT_EQ(model_.GetTab(id)->manual_name, "Mine");
}

TEST_F(ApplyManualNameTest, TheNameIsInTheSavedState) {
  const std::string id = AddTab();
  ApplyManualName(model_, id, "Mine", "Page");
  const ParseResult parsed = ParseState(model_.ToValue());
  ASSERT_EQ(parsed.status, LoadStatus::kOk);
  ASSERT_EQ(parsed.state->tabs.size(), 1u);
  EXPECT_EQ(parsed.state->tabs[0].manual_name, "Mine");
}

}  // namespace aerie_tab_model
