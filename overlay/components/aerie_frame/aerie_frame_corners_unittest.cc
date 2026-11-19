// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_frame/aerie_frame_corners.h"

#include "components/aerie_design_tokens/aerie_tokens.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_frame {
namespace {

TEST(AerieFrameCornersTest, ANonZeroProviderRadiusIsUsedAsItIs) {
  EXPECT_EQ(WindowCornerRadius(9.f, /*restored=*/true, /*can_draw=*/true), 9.f);
}

TEST(AerieFrameCornersTest, AZeroProviderRadiusWithCompositingIsTheToken) {
  EXPECT_EQ(WindowCornerRadius(0.f, true, true),
            static_cast<float>(aerie::tokens::kRadiusXl));
  EXPECT_GT(WindowCornerRadius(0.f, true, true), 0.f);
}

TEST(AerieFrameCornersTest, WithoutCompositingTheCornersAreSquare) {
  EXPECT_EQ(WindowCornerRadius(0.f, true, false), 0.f);
  EXPECT_EQ(WindowCornerRadius(9.f, true, false), 0.f);
}

TEST(AerieFrameCornersTest, AMaximizedFullscreenOrTiledWindowIsSquare) {
  EXPECT_EQ(WindowCornerRadius(0.f, /*restored=*/false, true), 0.f);
  EXPECT_EQ(WindowCornerRadius(9.f, false, true), 0.f);
}

}  // namespace
}  // namespace aerie_frame
