// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_drag.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_sidebar {
namespace {

// Five rows of 36 px with 4 px between them: a pitch of 40.
std::vector<DragSlot> Slots(int count = 5) {
  std::vector<DragSlot> slots;
  for (int i = 0; i < count; ++i) {
    slots.push_back({4 + i * 40, 36});
  }
  return slots;
}

TEST(AerieSidebarDragTest, APressBecomesADragPastTheThreshold) {
  EXPECT_FALSE(ExceedsDragThreshold(0, 0));
  EXPECT_FALSE(ExceedsDragThreshold(kDragThreshold, -kDragThreshold));
  EXPECT_TRUE(ExceedsDragThreshold(0, kDragThreshold + 1));
  EXPECT_TRUE(ExceedsDragThreshold(-(kDragThreshold + 1), 0));
}

TEST(AerieSidebarDragTest, TheTargetIsTheSlotNearestTheRow) {
  const auto slots = Slots();
  EXPECT_EQ(DragTargetIndex(slots, 1, 0), 1);
  EXPECT_EQ(DragTargetIndex(slots, 1, 19), 1);
  EXPECT_EQ(DragTargetIndex(slots, 1, 21), 2);
  EXPECT_EQ(DragTargetIndex(slots, 1, 85), 3);
  EXPECT_EQ(DragTargetIndex(slots, 1, 1000), 4);
  EXPECT_EQ(DragTargetIndex(slots, 3, -21), 2);
  EXPECT_EQ(DragTargetIndex(slots, 3, -1000), 0);
}

TEST(AerieSidebarDragTest, ASingleRowOrABadIndexHasNowhereToGo) {
  EXPECT_EQ(DragTargetIndex(Slots(1), 0, 300), 0);
  EXPECT_EQ(DragTargetIndex({}, 0, 300), 0);
  EXPECT_EQ(DragTargetIndex(Slots(), 9, 0), 9);
}

TEST(AerieSidebarDragTest, TheOffsetKeepsTheRowBetweenTheFirstAndLastSlot) {
  const auto slots = Slots();
  EXPECT_EQ(ClampDragOffset(slots, 1, 10), 10);
  EXPECT_EQ(ClampDragOffset(slots, 1, -500), -40);
  EXPECT_EQ(ClampDragOffset(slots, 1, 500), 120);
  EXPECT_EQ(ClampDragOffset(slots, 7, 5), 0);
}

TEST(AerieSidebarDragTest, RowsThePassedRowPassesMoveOneSlotTowardIt) {
  const auto slots = Slots();
  // Row 1 dragged down to slot 3: rows 2 and 3 move up one slot.
  EXPECT_EQ(DragShift(slots, 1, 3, 0), 0);
  EXPECT_EQ(DragShift(slots, 1, 3, 1), 0);
  EXPECT_EQ(DragShift(slots, 1, 3, 2), -40);
  EXPECT_EQ(DragShift(slots, 1, 3, 3), -40);
  EXPECT_EQ(DragShift(slots, 1, 3, 4), 0);
  // Row 3 dragged up to slot 1: rows 1 and 2 move down one slot.
  EXPECT_EQ(DragShift(slots, 3, 1, 0), 0);
  EXPECT_EQ(DragShift(slots, 3, 1, 1), 40);
  EXPECT_EQ(DragShift(slots, 3, 1, 2), 40);
  EXPECT_EQ(DragShift(slots, 3, 1, 3), 0);
  EXPECT_EQ(DragShift(slots, 3, 1, 4), 0);
  // Held over its own slot nothing moves.
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(DragShift(slots, 2, 2, i), 0);
  }
}

TEST(AerieSidebarDragTest, ShiftsFollowSlotsOfUnequalHeight) {
  std::vector<DragSlot> slots = {{0, 30}, {34, 50}, {88, 30}};
  EXPECT_EQ(DragShift(slots, 0, 2, 1), -34);
  EXPECT_EQ(DragShift(slots, 0, 2, 2), -54);
  EXPECT_EQ(DragShift(slots, 2, 0, 0), 34);
  EXPECT_EQ(DragShift(slots, 2, 0, 1), 54);
}

TEST(AerieSidebarDragTest, ADropSettlesFromWhereTheRowIsToTheSlotItTakes) {
  const auto slots = Slots();
  // Pulled 85 px down from slot 1 and dropped in slot 3, which is 80 px below
  // it: the row is 5 px below its new slot.
  EXPECT_EQ(SettleOffset(slots, 1, 3, 85), 5);
  EXPECT_EQ(SettleOffset(slots, 3, 1, -85), -5);
  // Dropped where it started: it settles from the pointer offset.
  EXPECT_EQ(SettleOffset(slots, 2, 2, 13), 13);
}

}  // namespace
}  // namespace aerie_sidebar
