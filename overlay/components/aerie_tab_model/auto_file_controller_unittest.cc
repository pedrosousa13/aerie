// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/auto_file_controller.h"

#include <memory>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/simple_test_clock.h"
#include "base/test/task_environment.h"
#include "components/aerie_tab_model/folder_rules.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/tab_model.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace aerie_tab_model {

namespace {

constexpr base::TimeDelta kDelay = base::Milliseconds(280);
constexpr base::TimeDelta kReducedDelay = base::Milliseconds(120);

class RecordingObserver : public AutoFileController::Observer {
 public:
  void OnTabFiled(const AutoFileController::Filed& filed) override {
    events.push_back(filed);
  }
  std::vector<AutoFileController::Filed> events;
};

class AutoFileControllerTest : public testing::Test {
 protected:
  AutoFileControllerTest()
      : model_(&clock_,
               base::BindRepeating(
                   [](int* next) { return "id-" + base::NumberToString((*next)++); },
                   &next_id_)) {
    clock_.SetNow(base::Time::FromMillisecondsSinceUnixEpoch(1'700'000'000'000));
  }

  void MakeController(base::TimeDelta delay = kDelay) {
    controller_ = std::make_unique<AutoFileController>(
        &model_, task_environment_.GetMockTickClock(),
        base::BindRepeating([](bool* on) { return *on; }, &rules_enabled_),
        base::BindRepeating([](base::TimeDelta d) { return d; }, delay));
    controller_->AddObserver(&observer_);
  }

  ~AutoFileControllerTest() override {
    if (controller_) {
      controller_->RemoveObserver(&observer_);
    }
  }

  std::string AutoFolder(Section section, const std::string& domain) {
    std::string id = CreateFolder(model_, section, "", "F " + domain);
    SetFolderRuleKind(model_, id, FolderKind::kAuto);
    AddRule(model_, id, RuleType::kDomain, domain);
    return id;
  }

  std::string NewTab() {
    TabRecord tab;
    tab.last_active = clock_.Now();
    return model_.AddTab(std::move(tab));
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::SimpleTestClock clock_;
  int next_id_ = 1;
  TabModel model_;
  bool rules_enabled_ = true;
  RecordingObserver observer_;
  std::unique_ptr<AutoFileController> controller_;
};

TEST(AutoFileDelayTest, IsTheTokenAndShorterWhenMotionIsReduced) {
  EXPECT_EQ(base::Milliseconds(280), AutoFileDelay(false));
  EXPECT_EQ(base::Milliseconds(120), AutoFileDelay(true));
}

TEST_F(AutoFileControllerTest, ANewMatchingTabStaysInTodayThenFliesIn) {
  std::string folder = AutoFolder(Section::kToday, "example.com");
  MakeController();
  std::string tab = NewTab();
  ASSERT_TRUE(controller_->IsWatching(tab));
  controller_->OnTabPage(tab, GURL("https://example.com/a"), "A");
  // It lands in Today as usual and waits.
  EXPECT_TRUE(model_.GetTab(tab)->parent.empty());
  task_environment_.FastForwardBy(kDelay - base::Milliseconds(1));
  EXPECT_TRUE(model_.GetTab(tab)->parent.empty());
  EXPECT_TRUE(observer_.events.empty());
  task_environment_.FastForwardBy(base::Milliseconds(1));
  EXPECT_EQ(folder, model_.GetTab(tab)->parent);
  ASSERT_EQ(1u, observer_.events.size());
  EXPECT_EQ(tab, observer_.events[0].tab_id);
  EXPECT_EQ(folder, observer_.events[0].folder_id);
  EXPECT_EQ("example.com", observer_.events[0].host);
  ASSERT_TRUE(model_.GetTab(tab)->filed_by);
  EXPECT_EQ(observer_.events[0].rule_id, model_.GetTab(tab)->filed_by->rule_id);
}

TEST_F(AutoFileControllerTest, TheReducedMotionPauseIsShorter) {
  std::string folder = AutoFolder(Section::kToday, "example.com");
  MakeController(kReducedDelay);
  std::string tab = NewTab();
  controller_->OnTabPage(tab, GURL("https://example.com/"), "");
  task_environment_.FastForwardBy(kReducedDelay - base::Milliseconds(1));
  EXPECT_TRUE(model_.GetTab(tab)->parent.empty());
  task_environment_.FastForwardBy(base::Milliseconds(1));
  EXPECT_EQ(folder, model_.GetTab(tab)->parent);
}

TEST_F(AutoFileControllerTest, UndoReturnsTheTabAndTheExceptionStopsTheNextOne) {
  std::string folder = AutoFolder(Section::kToday, "example.com");
  MakeController();
  std::string tab = NewTab();
  controller_->OnTabPage(tab, GURL("https://news.example.com/a"), "A");
  task_environment_.FastForwardBy(kDelay);
  ASSERT_EQ(folder, model_.GetTab(tab)->parent);

  // Undo.
  ASSERT_TRUE(UndoFiling(model_, tab));
  EXPECT_TRUE(model_.GetTab(tab)->parent.empty());
  // Undo does not file the tab again: it is not new any more.
  controller_->OnTabPage(tab, GURL("https://news.example.com/a"), "A");
  task_environment_.FastForwardBy(kDelay);
  EXPECT_TRUE(model_.GetTab(tab)->parent.empty());

  // "Don't file this site", then another tab of the site is left alone.
  ASSERT_TRUE(AddExcludedHost(model_, folder, observer_.events[0].host));
  std::string second = NewTab();
  controller_->OnTabPage(second, GURL("https://news.example.com/b"), "B");
  task_environment_.FastForwardBy(kDelay);
  EXPECT_TRUE(model_.GetTab(second)->parent.empty());
  // The rest of the site's parent still files.
  std::string third = NewTab();
  controller_->OnTabPage(third, GURL("https://example.com/c"), "C");
  task_environment_.FastForwardBy(kDelay);
  EXPECT_EQ(folder, model_.GetTab(third)->parent);
}

TEST_F(AutoFileControllerTest, AnEmptyOrBlankAddressWaitsForTheRealOne) {
  std::string folder = AutoFolder(Section::kToday, "example.com");
  MakeController();
  std::string tab = NewTab();
  controller_->OnTabPage(tab, GURL(), "");
  controller_->OnTabPage(tab, GURL("about:blank"), "");
  task_environment_.FastForwardBy(kDelay);
  EXPECT_TRUE(model_.GetTab(tab)->parent.empty());
  controller_->OnTabPage(tab, GURL("https://example.com/"), "");
  task_environment_.FastForwardBy(kDelay);
  EXPECT_EQ(folder, model_.GetTab(tab)->parent);
}

TEST_F(AutoFileControllerTest, ATitleThatArrivesLaterFilesTheTab) {
  std::string folder = CreateFolder(model_, Section::kToday, "", "Trips");
  SetFolderRuleKind(model_, folder, FolderKind::kAuto);
  AddRule(model_, folder, RuleType::kTitleContains, "trip");
  MakeController();
  std::string tab = NewTab();
  controller_->OnTabPage(tab, GURL("https://example.com/"), "");
  task_environment_.FastForwardBy(kDelay);
  EXPECT_TRUE(model_.GetTab(tab)->parent.empty());
  task_environment_.FastForwardBy(base::Seconds(1));
  controller_->OnTabPage(tab, GURL("https://example.com/"), "My TRIP plan");
  task_environment_.FastForwardBy(kDelay);
  EXPECT_EQ(folder, model_.GetTab(tab)->parent);
}

TEST_F(AutoFileControllerTest, NothingFilesAfterTheWindow) {
  AutoFolder(Section::kToday, "example.com");
  MakeController();
  std::string tab = NewTab();
  task_environment_.FastForwardBy(kAutoFileWindow);
  EXPECT_FALSE(controller_->IsWatching(tab));
  controller_->OnTabPage(tab, GURL("https://example.com/"), "A");
  task_environment_.FastForwardBy(kDelay);
  EXPECT_TRUE(model_.GetTab(tab)->parent.empty());
  EXPECT_TRUE(observer_.events.empty());
  // Just inside the window it still files.
  std::string inside = NewTab();
  task_environment_.FastForwardBy(kAutoFileWindow - base::Milliseconds(1));
  controller_->OnTabPage(inside, GURL("https://example.com/"), "A");
  task_environment_.FastForwardBy(kDelay);
  EXPECT_FALSE(model_.GetTab(inside)->parent.empty());
}

TEST_F(AutoFileControllerTest, ALaterNavigationIntoAMatchingUrlDoesNotMoveTheTab) {
  AutoFolder(Section::kToday, "example.com");
  MakeController();
  std::string tab = NewTab();
  // The first page does not match; the user moves on within the window.
  controller_->OnTabPage(tab, GURL("https://elsewhere.test/"), "Home");
  task_environment_.FastForwardBy(base::Seconds(1));
  controller_->OnTabPage(tab, GURL("https://example.com/"), "Example");
  task_environment_.FastForwardBy(kDelay);
  EXPECT_TRUE(model_.GetTab(tab)->parent.empty());
  // And well after the window.
  task_environment_.FastForwardBy(base::Seconds(30));
  controller_->OnTabPage(tab, GURL("https://example.com/x"), "Example");
  task_environment_.FastForwardBy(kDelay);
  EXPECT_TRUE(model_.GetTab(tab)->parent.empty());
}

TEST_F(AutoFileControllerTest, ATabMovedByHandIsNotFiled) {
  AutoFolder(Section::kToday, "example.com");
  std::string manual = CreateFolder(model_, Section::kToday, "", "Mine");
  MakeController();
  // Moved before the page is known.
  std::string early = NewTab();
  ASSERT_TRUE(MoveTabToFolder(model_, early, manual));
  EXPECT_FALSE(controller_->IsWatching(early));
  controller_->OnTabPage(early, GURL("https://example.com/"), "");
  task_environment_.FastForwardBy(kDelay);
  EXPECT_EQ(manual, model_.GetTab(early)->parent);

  // Moved while it waits for the pause.
  std::string waiting = NewTab();
  controller_->OnTabPage(waiting, GURL("https://example.com/"), "");
  task_environment_.FastForwardBy(kDelay / 2);
  ASSERT_TRUE(MoveTabToFolder(model_, waiting, manual));
  task_environment_.FastForwardBy(kDelay);
  EXPECT_EQ(manual, model_.GetTab(waiting)->parent);
  EXPECT_TRUE(observer_.events.empty());
}

TEST_F(AutoFileControllerTest, ATabThatLeftItsFolderIsNotFiledAgain) {
  std::string folder = AutoFolder(Section::kToday, "example.com");
  MakeController();
  std::string tab = NewTab();
  controller_->OnTabPage(tab, GURL("https://example.com/"), "");
  task_environment_.FastForwardBy(kDelay);
  ASSERT_EQ(folder, model_.GetTab(tab)->parent);
  ASSERT_TRUE(MoveTabToFolder(model_, tab, ""));
  controller_->OnTabPage(tab, GURL("https://example.com/"), "");
  task_environment_.FastForwardBy(kDelay);
  EXPECT_TRUE(model_.GetTab(tab)->parent.empty());
  EXPECT_EQ(1u, observer_.events.size());
}

TEST_F(AutoFileControllerTest, AChangeOfPositionInTodayIsNotAMoveByHand) {
  std::string folder = AutoFolder(Section::kToday, "example.com");
  MakeController();
  std::string tab = NewTab();
  // The tab strip follows the window and reorders Today.
  model_.MoveTab(tab, Section::kToday, "", 7);
  EXPECT_TRUE(controller_->IsWatching(tab));
  controller_->OnTabPage(tab, GURL("https://example.com/"), "");
  task_environment_.FastForwardBy(kDelay);
  EXPECT_EQ(folder, model_.GetTab(tab)->parent);
}

TEST_F(AutoFileControllerTest, TheFirstFolderWinsAndPinnedBeatsToday) {
  std::string today = AutoFolder(Section::kToday, "example.com");
  std::string pinned = AutoFolder(Section::kPinned, "example.com");
  MakeController();
  std::string tab = NewTab();
  controller_->OnTabPage(tab, GURL("https://example.com/"), "");
  task_environment_.FastForwardBy(kDelay);
  EXPECT_EQ(pinned, model_.GetTab(tab)->parent);
  EXPECT_EQ(Section::kPinned, model_.GetTab(tab)->section);
  EXPECT_NE(today, model_.GetTab(tab)->parent);
  EXPECT_EQ("https://example.com/", model_.GetTab(tab)->pinned_url);
}

TEST_F(AutoFileControllerTest, EachSwitchStopsFiling) {
  std::string folder = AutoFolder(Section::kToday, "example.com");
  const std::string rule = ReadFolderRules(*model_.GetFolder(folder)).rules[0].id;
  MakeController();
  auto try_one = [&]() {
    std::string tab = NewTab();
    controller_->OnTabPage(tab, GURL("https://example.com/"), "");
    task_environment_.FastForwardBy(kDelay);
    return !model_.GetTab(tab)->parent.empty();
  };
  EXPECT_TRUE(try_one());
  SetRuleEnabled(model_, folder, rule, false);
  EXPECT_FALSE(try_one());
  SetRuleEnabled(model_, folder, rule, true);
  SetFolderRulesEnabled(model_, folder, false);
  EXPECT_FALSE(try_one());
  SetFolderRulesEnabled(model_, folder, true);
  rules_enabled_ = false;
  EXPECT_FALSE(try_one());
  rules_enabled_ = true;
  EXPECT_TRUE(try_one());
}

TEST_F(AutoFileControllerTest, TheGlobalSwitchIsReadAgainAfterThePause) {
  AutoFolder(Section::kToday, "example.com");
  MakeController();
  std::string tab = NewTab();
  controller_->OnTabPage(tab, GURL("https://example.com/"), "");
  task_environment_.FastForwardBy(kDelay / 2);
  rules_enabled_ = false;
  task_environment_.FastForwardBy(kDelay);
  EXPECT_TRUE(model_.GetTab(tab)->parent.empty());
  EXPECT_TRUE(observer_.events.empty());
}

TEST_F(AutoFileControllerTest, ARuleRemovedDuringThePauseFilesNothing) {
  std::string folder = AutoFolder(Section::kToday, "example.com");
  const std::string rule = ReadFolderRules(*model_.GetFolder(folder)).rules[0].id;
  MakeController();
  std::string tab = NewTab();
  controller_->OnTabPage(tab, GURL("https://example.com/"), "");
  task_environment_.FastForwardBy(kDelay / 2);
  RemoveRule(model_, folder, rule);
  task_environment_.FastForwardBy(kDelay);
  EXPECT_TRUE(model_.GetTab(tab)->parent.empty());
}

TEST_F(AutoFileControllerTest, ATabExceptionKeepsThatTabOut) {
  std::string folder = AutoFolder(Section::kToday, "example.com");
  MakeController();
  std::string tab = NewTab();
  AddExcludedTab(model_, folder, tab);
  controller_->OnTabPage(tab, GURL("https://example.com/"), "");
  task_environment_.FastForwardBy(kDelay);
  EXPECT_TRUE(model_.GetTab(tab)->parent.empty());
}

TEST_F(AutoFileControllerTest, ATabClosedDuringThePauseIsSkipped) {
  AutoFolder(Section::kToday, "example.com");
  MakeController();
  std::string tab = NewTab();
  controller_->OnTabPage(tab, GURL("https://example.com/"), "");
  task_environment_.FastForwardBy(kDelay / 2);
  model_.RemoveTab(tab);
  task_environment_.FastForwardBy(kDelay);
  EXPECT_TRUE(observer_.events.empty());
  EXPECT_EQ(0u, controller_->watched_count());
}

TEST_F(AutoFileControllerTest, NoAutoFilingFolderMeansNoTabIsWatched) {
  CreateFolder(model_, Section::kToday, "", "Manual");
  MakeController();
  std::string tab = NewTab();
  EXPECT_FALSE(controller_->IsWatching(tab));
  EXPECT_EQ(0u, controller_->watched_count());
}

TEST_F(AutoFileControllerTest, AForgottenTabIsNotFiled) {
  AutoFolder(Section::kToday, "example.com");
  MakeController();
  std::string tab = NewTab();
  // The owner saw that session restore brought this tab back.
  controller_->Forget(tab);
  EXPECT_FALSE(controller_->IsWatching(tab));
  controller_->OnTabPage(tab, GURL("https://example.com/"), "");
  task_environment_.FastForwardBy(kDelay);
  EXPECT_TRUE(model_.GetTab(tab)->parent.empty());
}

TEST_F(AutoFileControllerTest, AMatchOnlyInAnotherSectionOrFolderKindDoesNothing) {
  std::string manual = CreateFolder(model_, Section::kToday, "", "Manual");
  AddRule(model_, manual, RuleType::kDomain, "example.com");
  AutoFolder(Section::kToday, "other.test");
  MakeController();
  std::string tab = NewTab();
  controller_->OnTabPage(tab, GURL("https://example.com/"), "");
  task_environment_.FastForwardBy(kDelay);
  EXPECT_TRUE(model_.GetTab(tab)->parent.empty());
}

TEST_F(AutoFileControllerTest, WatchedTabsAreForgottenWhenTheWindowPasses) {
  AutoFolder(Section::kToday, "example.com");
  MakeController();
  for (int i = 0; i < 10; ++i) {
    NewTab();
  }
  EXPECT_EQ(10u, controller_->watched_count());
  task_environment_.FastForwardBy(kAutoFileWindow);
  // The next new tab sweeps the old ones.
  NewTab();
  EXPECT_EQ(1u, controller_->watched_count());
}

TEST_F(AutoFileControllerTest, TheControllerDoesNothingForTabsThatExistedBeforeIt) {
  AutoFolder(Section::kToday, "example.com");
  std::string tab = NewTab();
  MakeController();
  EXPECT_EQ(0u, controller_->watched_count());
  controller_->OnTabPage(tab, GURL("https://example.com/"), "");
  task_environment_.FastForwardBy(kDelay);
  EXPECT_TRUE(model_.GetTab(tab)->parent.empty());
}

}  // namespace

}  // namespace aerie_tab_model
