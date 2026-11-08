// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/renderer/cosmetic_sheet_state.h"

#include <string>

#include "components/aerie_adblock/common/cosmetic_limits.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_adblock {
namespace {

using mojom::CosmeticSheetKind;

constexpr char kRuleA[] = ".a{display:none!important}\n";
constexpr char kRuleB[] = ".b{display:none!important}\n";

TEST(CosmeticSheetStateTest, ReplaceDropsTheEarlierRules) {
  CosmeticSheetState state;
  EXPECT_EQ(state.Apply(CosmeticSheetKind::kGeneric, kRuleA, false), kRuleA);
  EXPECT_EQ(state.Apply(CosmeticSheetKind::kGeneric, kRuleB, false), kRuleB);
}

TEST(CosmeticSheetStateTest, AppendKeepsTheEarlierRules) {
  CosmeticSheetState state;
  state.Apply(CosmeticSheetKind::kGeneric, kRuleA, false);
  EXPECT_EQ(state.Apply(CosmeticSheetKind::kGeneric, kRuleB, true),
            std::string(kRuleA) + kRuleB);
}

TEST(CosmeticSheetStateTest, AppendToNothingIsTheNewText) {
  CosmeticSheetState state;
  EXPECT_EQ(state.Apply(CosmeticSheetKind::kGeneric, kRuleA, true), kRuleA);
}

// The browser appends at DOMContentLoaded, at load and for late elements; the
// same rules arrive again and must not pile up.
TEST(CosmeticSheetStateTest, AppendedLinesAreNotAddedTwice) {
  CosmeticSheetState state;
  state.Apply(CosmeticSheetKind::kGeneric, kRuleA, true);
  EXPECT_EQ(state.Apply(CosmeticSheetKind::kGeneric,
                        std::string(kRuleA) + kRuleB, true),
            std::string(kRuleA) + kRuleB);
  // Nothing new: nothing to insert.
  EXPECT_FALSE(state.Apply(CosmeticSheetKind::kGeneric, kRuleA, true));
  EXPECT_FALSE(state.Apply(CosmeticSheetKind::kGeneric,
                           std::string(kRuleB) + kRuleA, true));
}

TEST(CosmeticSheetStateTest, ReplaceIsAlwaysInserted) {
  CosmeticSheetState state;
  state.Apply(CosmeticSheetKind::kPage, kRuleA, false);
  EXPECT_EQ(state.Apply(CosmeticSheetKind::kPage, kRuleA, false), kRuleA);
}

TEST(CosmeticSheetStateTest, ALastLineWithoutANewlineIsKept) {
  CosmeticSheetState state;
  EXPECT_EQ(state.Apply(CosmeticSheetKind::kPage, ".a{x:y}\n.b{x:y}", false),
            ".a{x:y}\n.b{x:y}\n");
}

TEST(CosmeticSheetStateTest, TheTwoKindsDoNotMix) {
  CosmeticSheetState state;
  state.Apply(CosmeticSheetKind::kPage, kRuleA, false);
  EXPECT_EQ(state.Apply(CosmeticSheetKind::kGeneric, kRuleB, true), kRuleB);
  EXPECT_EQ(state.Apply(CosmeticSheetKind::kPage, kRuleB, true),
            std::string(kRuleA) + kRuleB);
  // A line that is in the other sheet is still new here.
  EXPECT_EQ(state.Apply(CosmeticSheetKind::kGeneric, kRuleA, true),
            std::string(kRuleB) + kRuleA);
}

TEST(CosmeticSheetStateTest, RefusesTextTheEngineNeverMakes) {
  CosmeticSheetState state;
  EXPECT_FALSE(state.Apply(CosmeticSheetKind::kPage, "", true));
  EXPECT_FALSE(state.Apply(CosmeticSheetKind::kPage, "\xc3\xa9{x:y}", false));
  EXPECT_FALSE(
      state.Apply(CosmeticSheetKind::kPage, std::string_view("a\0b", 3),
                  false));
  EXPECT_FALSE(state.Apply(CosmeticSheetKind::kPage,
                           std::string(kMaxSheetBytes + 1, 'a'), false));
}

// A page with no rules must not keep the sheet of an earlier page.
TEST(CosmeticSheetStateTest, EmptyReplaceClearsTheSheet) {
  CosmeticSheetState state;
  state.Apply(CosmeticSheetKind::kPage, kRuleA, false);
  EXPECT_EQ(state.Apply(CosmeticSheetKind::kPage, "", false), "");
  EXPECT_EQ(state.Apply(CosmeticSheetKind::kPage, kRuleB, true), kRuleB);
}

TEST(CosmeticSheetStateTest, RefusedCallChangesNothing) {
  CosmeticSheetState state;
  state.Apply(CosmeticSheetKind::kGeneric, kRuleA, false);
  EXPECT_FALSE(state.Apply(CosmeticSheetKind::kGeneric, "", true));
  EXPECT_EQ(state.Apply(CosmeticSheetKind::kGeneric, kRuleB, true),
            std::string(kRuleA) + kRuleB);
}

TEST(CosmeticSheetStateTest, SheetIsBoundedAndStopsAtAWholeLine) {
  CosmeticSheetState state;
  // Two lines of kMaxSheetBytes / 2 - 2 bytes plus newline leave room for 2.
  std::string half(kMaxSheetBytes / 2 - 2, 'a');
  std::string other(kMaxSheetBytes / 2 - 2, 'b');
  auto full = state.Apply(CosmeticSheetKind::kGeneric, half + "\n", true);
  ASSERT_TRUE(full);
  full = state.Apply(CosmeticSheetKind::kGeneric, other + "\n", true);
  ASSERT_TRUE(full);
  EXPECT_EQ(full->size(), kMaxSheetBytes - 2);
  // A new line that does not fit is dropped whole; the sheet is not cut.
  EXPECT_FALSE(state.Apply(CosmeticSheetKind::kGeneric,
                           std::string(3, 'c') + "\n", true));
  EXPECT_TRUE(state.Apply(CosmeticSheetKind::kGeneric, "c\n", true));
}

TEST(CosmeticSheetStateTest, TextIsTheWholeSheetNow) {
  CosmeticSheetState state;
  EXPECT_EQ(state.Text(CosmeticSheetKind::kGeneric), "");
  state.Apply(CosmeticSheetKind::kGeneric, kRuleA, true);
  state.Apply(CosmeticSheetKind::kGeneric, kRuleB, true);
  EXPECT_EQ(state.Text(CosmeticSheetKind::kGeneric),
            std::string(kRuleA) + kRuleB);
}

TEST(CosmeticSheetStateTest, ResetForgetsTheOldDocument) {
  CosmeticSheetState state;
  state.Apply(CosmeticSheetKind::kGeneric, kRuleA, false);
  state.Reset();
  EXPECT_EQ(state.Apply(CosmeticSheetKind::kGeneric, kRuleB, true), kRuleB);
}

base::TimeTicks Start() {
  return base::TimeTicks() + base::Seconds(10);
}

base::TimeDelta Ms(int ms) {
  return base::Milliseconds(ms);
}

TEST(GenericInsertPacerTest, TheFirstInsertionIsAtOnce) {
  GenericInsertPacer pacer;
  EXPECT_EQ(pacer.OnChange(Start()).kind, GenericInsertPacer::Decision::kNow);
}

TEST(GenericInsertPacerTest, ALaterChangeWaitsForTheRestOfTheInterval) {
  GenericInsertPacer pacer;
  pacer.OnInserted(Start());
  auto soon = pacer.OnChange(Start() + Ms(100));
  EXPECT_EQ(soon.kind, GenericInsertPacer::Decision::kLater);
  EXPECT_EQ(soon.delay, Ms(kGenericReinsertMinIntervalMs - 100));
  EXPECT_EQ(pacer.OnChange(Start() + Ms(kGenericReinsertMinIntervalMs)).kind,
            GenericInsertPacer::Decision::kNow);
}

TEST(GenericInsertPacerTest, AFloodOfChangesMakesBoundedInsertions) {
  GenericInsertPacer pacer;
  base::TimeTicks t = Start();
  size_t inserted = 0;
  // A change every millisecond for a minute.
  for (int i = 0; i < 60 * 1000; ++i, t += Ms(1)) {
    auto decision = pacer.OnChange(t);
    if (decision.kind == GenericInsertPacer::Decision::kNow) {
      pacer.OnInserted(t);
      ++inserted;
    }
  }
  EXPECT_EQ(inserted, kMaxGenericInsertsPerDocument);
  EXPECT_EQ(pacer.OnChange(t).kind, GenericInsertPacer::Decision::kDrop);
}

TEST(GenericInsertPacerTest, ResetStartsANewDocument) {
  GenericInsertPacer pacer;
  for (size_t i = 0; i < kMaxGenericInsertsPerDocument; ++i) {
    pacer.OnInserted(Start() + Ms(1000 * i));
  }
  pacer.Reset();
  EXPECT_EQ(pacer.OnChange(Start()).kind, GenericInsertPacer::Decision::kNow);
}

}  // namespace
}  // namespace aerie_adblock
