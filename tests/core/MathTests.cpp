#include "core/Math.h"

#include <gtest/gtest.h>

using deskpet::core::RectI;
using deskpet::core::Vec2;

TEST(Vec2, Arithmetic) {
    const Vec2 a{1.0f, 2.0f};
    const Vec2 b{3.0f, -1.0f};
    EXPECT_EQ(a + b, (Vec2{4.0f, 1.0f}));
    EXPECT_EQ(a - b, (Vec2{-2.0f, 3.0f}));
    EXPECT_EQ(a * 2.0f, (Vec2{2.0f, 4.0f}));

    Vec2 c = a;
    c += b;
    EXPECT_EQ(c, (Vec2{4.0f, 1.0f}));
    c -= b;
    EXPECT_EQ(c, a);
}

TEST(Vec2, Length) {
    EXPECT_FLOAT_EQ((Vec2{3.0f, 4.0f}).length(), 5.0f);
}

TEST(RectI, WidthAndHeight_AreHalfOpen) {
    const RectI rect{10, 20, 110, 70};
    EXPECT_EQ(rect.width(), 100);
    EXPECT_EQ(rect.height(), 50);
}

TEST(MoveTowards, StepsByMaxDeltaWithoutOvershooting) {
    using deskpet::core::moveTowards;
    EXPECT_FLOAT_EQ(moveTowards(0.0f, 1.0f, 0.25f), 0.25f);
    EXPECT_FLOAT_EQ(moveTowards(0.0f, -1.0f, 0.25f), -0.25f);
    EXPECT_FLOAT_EQ(moveTowards(0.9f, 1.0f, 0.25f), 1.0f);  // 넘어가지 않음
    EXPECT_FLOAT_EQ(moveTowards(1.0f, 1.0f, 0.25f), 1.0f);
}

TEST(SrgbToLinear, MatchesStandardCurve) {
    using deskpet::core::srgbToLinear;
    EXPECT_FLOAT_EQ(srgbToLinear(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(srgbToLinear(1.0f), 1.0f);
    EXPECT_NEAR(srgbToLinear(0.5f), 0.214041f, 1e-5f);
    EXPECT_NEAR(srgbToLinear(0.04f), 0.04f / 12.92f, 1e-7f);  // 어두운 구간은 직선
}
