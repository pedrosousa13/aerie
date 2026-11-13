// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_drag.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_sidebar {
namespace {

TEST(AerieSidebarDragTest, APressBecomesADragPastTheThreshold) {
  EXPECT_FALSE(ExceedsDragThreshold(0, 0));
  EXPECT_FALSE(ExceedsDragThreshold(kDragThreshold, -kDragThreshold));
  EXPECT_TRUE(ExceedsDragThreshold(0, kDragThreshold + 1));
  EXPECT_TRUE(ExceedsDragThreshold(-(kDragThreshold + 1), 0));
}

}  // namespace
}  // namespace aerie_sidebar
