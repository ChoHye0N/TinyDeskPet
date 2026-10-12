#include "app/Ground.h"

#include <gtest/gtest.h>

#include <vector>

using deskpet::app::groundBelow;
using deskpet::core::RectI;
using deskpet::core::Vec2;

namespace {

constexpr float kFloor = 1040.0f;  // 작업 영역 바닥 (작업 표시줄 위)

}  // namespace

TEST(Ground, NoWindows_IsFloor) {
    EXPECT_FLOAT_EQ(groundBelow({500.0f, 300.0f}, 0.0f, kFloor, {}), kFloor);
}

TEST(Ground, WindowTopBelowFeet_IsGround) {
    const std::vector<RectI> windows = {{400, 600, 900, 900}};
    EXPECT_FLOAT_EQ(groundBelow({500.0f, 300.0f}, 0.0f, kFloor, windows), 600.0f);
    // 발이 정확히 윗변에 있어도 (서 있는 중)
    EXPECT_FLOAT_EQ(groundBelow({500.0f, 600.0f}, 0.0f, kFloor, windows), 600.0f);
}

TEST(Ground, WindowNotUnderFeet_IsIgnored) {
    const std::vector<RectI> windows = {{400, 600, 900, 900}};
    EXPECT_FLOAT_EQ(groundBelow({300.0f, 300.0f}, 0.0f, kFloor, windows), kFloor);
    EXPECT_FLOAT_EQ(groundBelow({900.0f, 300.0f}, 0.0f, kFloor, windows), kFloor);  // 오른쪽 끝 밖
}

TEST(Ground, WindowTopAboveFeet_IsIgnoredUnlessWithinStepUp) {
    // 창 앞(아래쪽)에 있는 캐릭터는 그 창 위로 순간 이동하지 않음
    const std::vector<RectI> windows = {{400, 600, 900, 900}};
    EXPECT_FLOAT_EQ(groundBelow({500.0f, 700.0f}, 0.0f, kFloor, windows), kFloor);
    // 서 있던 창이 조금 올라간 경우: 허용치 안이면 따라 올라감
    EXPECT_FLOAT_EQ(groundBelow({500.0f, 610.0f}, 24.0f, kFloor, windows), 600.0f);
    EXPECT_FLOAT_EQ(groundBelow({500.0f, 630.0f}, 24.0f, kFloor, windows), kFloor);
}

TEST(Ground, HighestSurfaceBelowFeet_Wins) {
    const std::vector<RectI> windows = {{400, 700, 900, 900}, {450, 500, 800, 650}};
    EXPECT_FLOAT_EQ(groundBelow({500.0f, 300.0f}, 0.0f, kFloor, windows), 500.0f);
    EXPECT_FLOAT_EQ(groundBelow({500.0f, 550.0f}, 0.0f, kFloor, windows), 700.0f);
}

TEST(Ground, TopEdgeCoveredByWindowInFront_IsIgnored) {
    // 목록은 앞 → 뒤. 앞 창이 뒤 창의 윗변을 덮으면 그 지점에는 설 수 없음
    const std::vector<RectI> windows = {{300, 550, 700, 800}, {400, 600, 900, 900}};
    EXPECT_FLOAT_EQ(groundBelow({500.0f, 300.0f}, 0.0f, kFloor, windows), 550.0f);  // 앞 창 위
    EXPECT_FLOAT_EQ(groundBelow({800.0f, 300.0f}, 0.0f, kFloor, windows),
                    600.0f);  // 덮이지 않은 곳
    // 앞 창 위가 아닌 아래쪽에서 떨어지면: 뒤 창 윗변(600)은 앞 창에 가려 있으므로 바닥까지
    EXPECT_FLOAT_EQ(groundBelow({500.0f, 560.0f}, 0.0f, kFloor, windows), kFloor);
}

TEST(Ground, WindowBelowFloor_DoesNotLowerGround) {
    const std::vector<RectI> windows = {{400, 1050, 900, 1200}};  // 작업 표시줄 뒤로 내려간 창
    EXPECT_FLOAT_EQ(groundBelow({500.0f, 300.0f}, 0.0f, kFloor, windows), kFloor);
}
