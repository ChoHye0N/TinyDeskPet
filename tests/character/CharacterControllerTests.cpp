#include "character/CharacterController.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using deskpet::character::CharacterController;
using deskpet::character::Params;
using deskpet::character::State;
using deskpet::core::Vec2;

namespace {

constexpr float kGround = 1000.0f;
constexpr float kDt = 1.0f / 60.0f;

class CharacterControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        character_.setGround(kGround);
        character_.teleport({500.0f, kGround});
    }

    // 고정 시간 간격으로 seconds만큼 진행
    void simulate(float seconds) {
        const int steps = static_cast<int>(seconds / kDt + 0.5f);
        for (int i = 0; i < steps; ++i) {
            character_.update(kDt);
        }
    }

    // 착지할 때까지 진행 (최대 maxSeconds). 공중에 있었던 최소 y(가장 높은 지점)를 반환.
    float simulateUntilLanded(float maxSeconds = 10.0f) {
        float highest = character_.position().y;
        const int maxSteps = static_cast<int>(maxSeconds / kDt);
        for (int i = 0; i < maxSteps && character_.state() == State::Airborne; ++i) {
            character_.update(kDt);
            highest = std::min(highest, character_.position().y);
        }
        return highest;
    }

    CharacterController character_;
};

}  // namespace

TEST_F(CharacterControllerTest, TeleportOnGround_IsIdle) {
    EXPECT_EQ(character_.state(), State::Idle);
    EXPECT_FLOAT_EQ(character_.position().y, kGround);
}

TEST_F(CharacterControllerTest, TeleportAboveGround_IsAirborne) {
    character_.teleport({500.0f, 200.0f});
    EXPECT_EQ(character_.state(), State::Airborne);
}

TEST_F(CharacterControllerTest, MoveBelowThreshold_IsNotDrag) {
    character_.onPointerDown({500.0f, 950.0f});
    character_.onPointerMove({502.0f, 951.0f});  // 약 2.2px < 4px
    EXPECT_EQ(character_.state(), State::Idle);
    EXPECT_FLOAT_EQ(character_.position().x, 500.0f);
}

TEST_F(CharacterControllerTest, MoveBeyondThreshold_StartsDragAndKeepsGrabOffset) {
    character_.onPointerDown({500.0f, 950.0f});  // 발보다 50px 위를 잡음
    character_.onPointerMove({600.0f, 700.0f});

    EXPECT_EQ(character_.state(), State::Dragged);
    EXPECT_EQ(character_.position(), (Vec2{600.0f, 750.0f}));
}

TEST_F(CharacterControllerTest, ReleaseAboveGround_FallsAndLands) {
    character_.onPointerDown({500.0f, 950.0f});
    character_.onPointerMove({500.0f, 400.0f});
    character_.onPointerUp({500.0f, 400.0f});
    EXPECT_EQ(character_.state(), State::Airborne);

    (void)simulateUntilLanded();
    EXPECT_EQ(character_.state(), State::Idle);
    EXPECT_FLOAT_EQ(character_.position().y, kGround);
    EXPECT_EQ(character_.velocity(), Vec2{});
}

TEST_F(CharacterControllerTest, ReleaseBelowGround_SnapsToGround) {
    character_.onPointerDown({500.0f, 950.0f});
    character_.onPointerMove({500.0f, 1100.0f});  // 발 = 1150 (바닥 아래)
    character_.onPointerUp({500.0f, 1100.0f});

    EXPECT_EQ(character_.state(), State::Idle);
    EXPECT_FLOAT_EQ(character_.position().y, kGround);
}

TEST_F(CharacterControllerTest, UpWithoutDown_IsIgnored) {
    character_.onPointerUp({0.0f, 0.0f});
    character_.onPointerMove({0.0f, 0.0f});
    EXPECT_EQ(character_.state(), State::Idle);
    EXPECT_FLOAT_EQ(character_.position().x, 500.0f);
}

TEST_F(CharacterControllerTest, Jump_ReachesTheoreticalHeightAndLands) {
    ASSERT_TRUE(character_.jump());
    EXPECT_EQ(character_.state(), State::Airborne);

    const float highest = simulateUntilLanded();
    const Params& p = character_.params();
    const float expectedHeight = p.jumpSpeed * p.jumpSpeed / (2.0f * p.gravity);  // 168.75px

    // 이산 적분이므로 이론값과 약간 다름 (dt = 1/60에서 오차 수 px)
    EXPECT_NEAR(kGround - highest, expectedHeight, 10.0f);
    EXPECT_EQ(character_.state(), State::Idle);
    EXPECT_FLOAT_EQ(character_.position().y, kGround);
}

TEST_F(CharacterControllerTest, JumpWhileDragged_IsRejected) {
    character_.onPointerDown({500.0f, 950.0f});
    character_.onPointerMove({500.0f, 800.0f});
    ASSERT_EQ(character_.state(), State::Dragged);
    EXPECT_FALSE(character_.jump());
    EXPECT_EQ(character_.state(), State::Dragged);
}

TEST_F(CharacterControllerTest, GrabWhileAirborne_StartsDragImmediately) {
    character_.teleport({500.0f, 300.0f});
    simulate(0.1f);
    ASSERT_EQ(character_.state(), State::Airborne);

    character_.onPointerDown({500.0f, 280.0f});
    EXPECT_EQ(character_.state(), State::Dragged);
    EXPECT_EQ(character_.velocity(), Vec2{});
}

TEST_F(CharacterControllerTest, GroundMovesDown_CharacterFalls) {
    character_.setGround(kGround + 100.0f);
    character_.update(kDt);
    EXPECT_EQ(character_.state(), State::Airborne);

    (void)simulateUntilLanded();
    EXPECT_FLOAT_EQ(character_.position().y, kGround + 100.0f);
}

TEST_F(CharacterControllerTest, GroundMovesUp_CharacterSnaps) {
    character_.setGround(kGround - 50.0f);
    character_.update(kDt);
    EXPECT_EQ(character_.state(), State::Idle);
    EXPECT_FLOAT_EQ(character_.position().y, kGround - 50.0f);
}

TEST_F(CharacterControllerTest, FallSpeed_IsClampedToTerminalVelocity) {
    character_.teleport({500.0f, -100000.0f});
    simulate(3.0f);
    EXPECT_EQ(character_.state(), State::Airborne);
    EXPECT_FLOAT_EQ(character_.velocity().y, character_.params().maxFallSpeed);
}

TEST_F(CharacterControllerTest, Landing_ProducesSquashThatFades) {
    ASSERT_TRUE(character_.jump());
    (void)simulateUntilLanded();
    EXPECT_GT(character_.pose().squash, 0.0f);

    simulate(character_.params().landingSquashDuration + 0.05f);
    EXPECT_FLOAT_EQ(character_.pose().squash, 0.0f);
}

TEST_F(CharacterControllerTest, Idle_BreathesAndBlinksPeriodically) {
    bool sawOpen = false;
    bool sawClosed = false;
    float minBreath = 0.0f;
    for (int i = 0; i < static_cast<int>(4.0f / kDt); ++i) {
        character_.update(kDt);
        const auto pose = character_.pose();
        sawOpen = sawOpen || !pose.eyesClosed;
        sawClosed = sawClosed || pose.eyesClosed;
        minBreath = std::min(minBreath, pose.breathOffset);
    }
    EXPECT_TRUE(sawOpen);
    EXPECT_TRUE(sawClosed);
    EXPECT_NEAR(minBreath, -character_.params().breathAmplitude, 0.05f);
}

TEST_F(CharacterControllerTest, InvalidDt_IsIgnored) {
    character_.teleport({500.0f, 300.0f});
    const Vec2 before = character_.position();
    character_.update(0.0f);
    character_.update(-1.0f);
    EXPECT_EQ(character_.position(), before);
}

// ---------------------------------------------------------------------------
// 던지기 (FR-16)
// ---------------------------------------------------------------------------

namespace {

// 바닥 위 y=500에서 잡아, 매 스텝 (dx, dy)씩 steps번 끌고 놓음
void dragAndThrow(CharacterController& c, Vec2 perStep, int steps, float holdSeconds = 0.0f) {
    Vec2 cursor{500.0f, 500.0f};
    c.teleport(cursor);
    c.onPointerDown(cursor);
    for (int i = 0; i < steps; ++i) {
        cursor += perStep;
        c.onPointerMove(cursor);
        c.update(kDt);
    }
    for (int i = 0; i < static_cast<int>(holdSeconds / kDt); ++i) {
        c.update(kDt);  // 움직이지 않고 붙잡고 있음
    }
    c.onPointerUp(cursor);
}

}  // namespace

TEST_F(CharacterControllerTest, Release_ThrowsWithRecentDragVelocity) {
    dragAndThrow(character_, {10.0f, 0.0f}, 12);  // 10px / (1/60 s) = 600 px/s

    EXPECT_EQ(character_.state(), State::Airborne);
    EXPECT_NEAR(character_.velocity().x, 600.0f, 1.0f);
    EXPECT_NEAR(character_.velocity().y, 0.0f, 1.0f);
}

TEST_F(CharacterControllerTest, ReleaseAfterHoldingStill_DropsWithoutThrow) {
    dragAndThrow(character_, {10.0f, 0.0f}, 12, 0.3f);
    EXPECT_FLOAT_EQ(character_.velocity().x, 0.0f);
}

TEST_F(CharacterControllerTest, ThrowSpeed_IsClamped) {
    dragAndThrow(character_, {500.0f, -500.0f}, 5);
    EXPECT_LE(character_.velocity().length(), character_.params().maxThrowSpeed + 0.5f);
}

TEST_F(CharacterControllerTest, Thrown_MovesHorizontallyWithAirDragAndStopsOnGround) {
    character_.setHorizontalBounds(-100000.0f, 100000.0f);
    dragAndThrow(character_, {10.0f, 0.0f}, 12);
    const float releaseX = character_.position().x;
    const float releaseVx = character_.velocity().x;

    simulate(0.2f);
    EXPECT_GT(character_.position().x, releaseX);
    EXPECT_LT(character_.velocity().x, releaseVx);  // 공기 저항

    simulate(5.0f);  // 착지 후 바닥 마찰로 정지
    EXPECT_EQ(character_.state(), State::Idle);
    EXPECT_FLOAT_EQ(character_.velocity().x, 0.0f);
    EXPECT_FLOAT_EQ(character_.position().y, kGround);
}

// ---------------------------------------------------------------------------
// 화면 경계 (FR-17)
// ---------------------------------------------------------------------------

TEST_F(CharacterControllerTest, ThrownIntoWall_BouncesAndStaysInside) {
    character_.setHorizontalBounds(0.0f, 700.0f);
    dragAndThrow(character_, {40.0f, 0.0f}, 4);  // 2400 px/s 오른쪽으로
    ASSERT_GT(character_.velocity().x, 0.0f);

    bool bounced = false;
    for (int i = 0; i < 120; ++i) {
        character_.update(kDt);
        EXPECT_LE(character_.position().x, 700.0f);
        bounced = bounced || character_.velocity().x < 0.0f;
    }
    EXPECT_TRUE(bounced);
}

TEST_F(CharacterControllerTest, DragOutsideBounds_IsClampedOnRelease) {
    character_.setHorizontalBounds(0.0f, 700.0f);
    character_.onPointerDown({500.0f, kGround});
    character_.onPointerMove({900.0f, kGround});
    character_.update(kDt);
    character_.update(kDt);
    character_.onPointerUp({900.0f, kGround});
    simulate(1.0f);
    EXPECT_LE(character_.position().x, 700.0f);
}

TEST_F(CharacterControllerTest, SetBounds_ClampsCurrentPosition) {
    character_.setHorizontalBounds(0.0f, 300.0f);
    EXPECT_FLOAT_EQ(character_.position().x, 300.0f);
}

// ---------------------------------------------------------------------------
// 걷기
// ---------------------------------------------------------------------------

namespace {

Params walkingParams() {
    Params params;
    params.idleTimeMin = params.idleTimeMax = 1.0f;
    params.walkTimeMin = params.walkTimeMax = 2.0f;
    params.walkSpeed = 60.0f;
    params.randomSeed = 7;
    return params;
}

}  // namespace

TEST(CharacterWalk, AfterIdleTime_WalksThenReturnsToIdle) {
    CharacterController c(walkingParams());
    c.setGround(kGround);
    c.setHorizontalBounds(0.0f, 2000.0f);
    c.teleport({1000.0f, kGround});

    for (int i = 0; i < static_cast<int>(1.1f / kDt); ++i) {
        c.update(kDt);
    }
    ASSERT_EQ(c.state(), State::Walk);
    EXPECT_NE(c.pose().facing, 0);

    const float startX = c.position().x;
    for (int i = 0; i < 60; ++i) {
        c.update(kDt);
    }
    EXPECT_NEAR(std::abs(c.position().x - startX), 60.0f, 2.0f);  // 1초 × 60 px/s
    EXPECT_FLOAT_EQ(c.position().y, kGround);

    for (int i = 0; i < static_cast<int>(1.1f / kDt); ++i) {
        c.update(kDt);
    }
    EXPECT_EQ(c.state(), State::Idle);
    EXPECT_EQ(c.pose().facing, 0);
}

TEST(CharacterWalk, TurnsAroundAtWall) {
    Params params = walkingParams();
    params.walkTimeMin = params.walkTimeMax = 100.0f;
    CharacterController c(params);
    c.setGround(kGround);
    c.setHorizontalBounds(990.0f, 1010.0f);  // 아주 좁은 공간
    c.teleport({1000.0f, kGround});

    bool sawLeft = false;
    bool sawRight = false;
    for (int i = 0; i < static_cast<int>(5.0f / kDt); ++i) {
        c.update(kDt);
        EXPECT_GE(c.position().x, 990.0f);
        EXPECT_LE(c.position().x, 1010.0f);
        sawLeft = sawLeft || c.pose().facing < 0;
        sawRight = sawRight || c.pose().facing > 0;
    }
    EXPECT_TRUE(sawLeft);
    EXPECT_TRUE(sawRight);
}

TEST(CharacterWalk, ZeroWalkSpeed_DisablesWalking) {
    Params params = walkingParams();
    params.walkSpeed = 0.0f;
    CharacterController c(params);
    c.setGround(kGround);
    c.teleport({1000.0f, kGround});
    for (int i = 0; i < static_cast<int>(5.0f / kDt); ++i) {
        c.update(kDt);
        ASSERT_EQ(c.state(), State::Idle);
    }
}

TEST(CharacterWalk, CanJumpAndBeDraggedWhileWalking) {
    CharacterController c(walkingParams());
    c.setGround(kGround);
    c.setHorizontalBounds(0.0f, 2000.0f);
    c.teleport({1000.0f, kGround});
    for (int i = 0; i < static_cast<int>(1.1f / kDt); ++i) {
        c.update(kDt);
    }
    ASSERT_EQ(c.state(), State::Walk);
    EXPECT_TRUE(c.jump());
    EXPECT_EQ(c.state(), State::Airborne);
}

TEST_F(CharacterControllerTest, Pose_ExposesAnimationTime) {
    simulate(0.5f);
    EXPECT_NEAR(character_.pose().time, 0.5f, 1e-4f);
}
