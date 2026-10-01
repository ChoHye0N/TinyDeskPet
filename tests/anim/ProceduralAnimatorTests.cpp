#include "anim/AnimTestModels.h"
#include "anim/ProceduralAnimator.h"

#include <gtest/gtest.h>

#include <cmath>

using deskpet::anim::AnimationInput;
using deskpet::anim::AnimationOutput;
using deskpet::anim::Motion;
using deskpet::anim::ProceduralAnimator;
using deskpet::core::transformPoint;
using deskpet::core::Vec3;
using deskpet::model::Expression;

namespace {

Vec3 posed(const deskpet::model::Model& model, const AnimationOutput& out, std::size_t bone) {
    return transformPoint(model.bones[bone].position, out.skin[bone]);
}

float expression(const AnimationOutput& out, Expression e) {
    return out.expressions[static_cast<std::size_t>(e)];
}

AnimationOutput evaluate(const deskpet::model::Model& model, AnimationInput input) {
    const ProceduralAnimator animator(model);
    AnimationOutput out;
    animator.evaluate(input, out);
    return out;
}

}  // namespace

TEST(ProceduralAnimator, Idle_LowersTPoseArms) {
    const auto model = deskpet::test::makeSkeletonModel(0.0f);
    const AnimationOutput out = evaluate(model, {});

    // 손(바인드: 어깨에서 수평으로 0.55)이 어깨보다 충분히 아래, 몸 가까이
    const Vec3 hand = posed(model, out, 3);
    EXPECT_LT(hand.y, 1.4f - 0.45f);
    EXPECT_LT(std::abs(hand.x), 0.4f);
    EXPECT_GT(hand.x, 0.0f);  // 왼팔은 계속 +X 쪽
    const Vec3 rightElbow = posed(model, out, 5);
    EXPECT_LT(rightElbow.x, 0.0f);
}

TEST(ProceduralAnimator, Idle_LowersAPoseArmsToSameDirection) {
    // MMD처럼 이미 팔이 35° 내려간 모델도 같은 목표 방향으로 맞춤
    const auto tPose = deskpet::test::makeSkeletonModel(0.0f);
    const auto aPose = deskpet::test::makeSkeletonModel(35.0f);
    const Vec3 handT = posed(tPose, evaluate(tPose, {}), 3);
    const Vec3 handA = posed(aPose, evaluate(aPose, {}), 3);
    EXPECT_NEAR(handT.x, handA.x, 0.02f);
    EXPECT_NEAR(handT.y, handA.y, 0.02f);
}

TEST(ProceduralAnimator, Walk_SwingsLegsInOppositeDirections) {
    const auto model = deskpet::test::makeSkeletonModel();
    AnimationInput input;
    input.motion = Motion::Walk;
    input.time = 0.15f;  // 한 걸음 주기의 1/4 근처
    const AnimationOutput out = evaluate(model, input);

    const float leftFootZ = posed(model, out, 8).z;
    const float rightFootZ = posed(model, out, 11).z;
    EXPECT_GT(std::abs(leftFootZ - rightFootZ), 0.1f);
    EXPECT_LT(leftFootZ * rightFootZ, 0.0f);  // 한쪽은 앞, 한쪽은 뒤
}

TEST(ProceduralAnimator, Expressions_FollowMotion) {
    const auto model = deskpet::test::makeSkeletonModel();
    AnimationInput input;
    input.blink = true;
    AnimationOutput out = evaluate(model, input);
    EXPECT_FLOAT_EQ(expression(out, Expression::Blink), 1.0f);
    EXPECT_FLOAT_EQ(expression(out, Expression::Surprised), 0.0f);

    input.motion = Motion::Dragged;  // 들려 있으면 놀람, 깜빡임은 끔
    out = evaluate(model, input);
    EXPECT_FLOAT_EQ(expression(out, Expression::Surprised), 1.0f);
    EXPECT_FLOAT_EQ(expression(out, Expression::Blink), 0.0f);

    input.motion = Motion::Walk;
    input.blink = false;
    out = evaluate(model, input);
    EXPECT_GT(expression(out, Expression::Happy), 0.0f);
}

TEST(ProceduralAnimator, IdleMotionDisabled_IsStaticOverTime) {
    const auto model = deskpet::test::makeSkeletonModel();
    AnimationInput input;
    input.idleMotion = false;
    input.time = 0.3f;
    const AnimationOutput a = evaluate(model, input);
    input.time = 1.7f;
    const AnimationOutput b = evaluate(model, input);
    EXPECT_EQ(a.skin, b.skin);  // 화면이 안 바뀌어 Present 생략이 동작 (DEBT-02)
}

TEST(ProceduralAnimator, DisplayBounds_AreNarrowerThanTPoseButCoverRaisedArms) {
    const auto model = deskpet::test::makeSkeletonModel();
    const ProceduralAnimator animator(model);
    const auto bounds = animator.displayBounds();
    // 손끝: T포즈 0.9 → 가장 많이 벌리는 공중 자세(45°)에서 약 0.69
    EXPECT_LT(bounds.max.x, model.bounds.max.x - 0.15f);
    EXPECT_GT(bounds.max.x, 0.6f);
    EXPECT_NEAR(bounds.min.y, 0.0f, 1e-4f);  // 발은 그대로
}

TEST(ProceduralAnimator, ModelWithoutHumanoid_StaysInBindPose) {
    deskpet::model::Model model;
    model.bones.resize(3);
    const AnimationOutput out = evaluate(model, {});
    ASSERT_EQ(out.skin.size(), 3U);
    EXPECT_EQ(out.skin[1], deskpet::core::Mat4::identity());
}
