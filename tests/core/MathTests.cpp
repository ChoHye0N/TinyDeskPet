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
