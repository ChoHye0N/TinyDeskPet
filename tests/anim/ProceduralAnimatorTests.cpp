#include "anim/AnimTestModels.h"
#include "anim/ProceduralAnimator.h"

#include <gtest/gtest.h>

#include <algorithm>
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

// ---------------------------------------------------------------------------
// 상태 전환 보간 (animate)
// ---------------------------------------------------------------------------

namespace {

constexpr float kDt = 1.0f / 60.0f;

AnimationInput staticInput(Motion motion) {
    AnimationInput input;
    input.motion = motion;
    input.idleMotion = false;
    return input;
}

float distance(Vec3 a, Vec3 b) {
    const Vec3 d = a - b;
    return std::sqrt(deskpet::core::dot(d, d));
}

}  // namespace

TEST(ProceduralAnimatorTransition, FirstFrame_MatchesTargetPose) {
    const auto model = deskpet::test::makeSkeletonModel();
    ProceduralAnimator animator(model);
    AnimationOutput out;
    animator.animate(staticInput(Motion::Dragged), kDt, out);
    EXPECT_EQ(out.skin, evaluate(model, staticInput(Motion::Dragged)).skin);
}

TEST(ProceduralAnimatorTransition, MotionChange_BlendsFromPreviousPose) {
    const auto model = deskpet::test::makeSkeletonModel();
    ProceduralAnimator animator(model);
    AnimationOutput out;
    animator.animate(staticInput(Motion::Idle), kDt, out);
    const Vec3 idleHand = posed(model, out, 3);
    const Vec3 airHand = posed(model, evaluate(model, staticInput(Motion::Airborne)), 3);

    animator.animate(staticInput(Motion::Airborne), kDt, out);
    const Vec3 hand = posed(model, out, 3);
    EXPECT_LT(distance(hand, idleHand), distance(hand, airHand));  // 바로 바뀌지 않음
    EXPECT_GT(distance(hand, idleHand), 0.0f);                     // 그래도 움직이기 시작

    // 전환 시간이 지나면 목표 자세와 같아짐
    for (float t = kDt; t < ProceduralAnimator::kTransitionSeconds; t += kDt) {
        animator.animate(staticInput(Motion::Airborne), kDt, out);
    }
    EXPECT_LT(distance(posed(model, out, 3), airHand), 1e-4f);
}

TEST(ProceduralAnimatorTransition, InterruptedTransition_DoesNotJump) {
    const auto model = deskpet::test::makeSkeletonModel();
    ProceduralAnimator animator(model);
    AnimationOutput out;
    animator.animate(staticInput(Motion::Idle), kDt, out);
    for (int i = 0; i < 5; ++i) {
        animator.animate(staticInput(Motion::Airborne), kDt, out);
    }
    const Vec3 before = posed(model, out, 3);
    animator.animate(staticInput(Motion::Idle), kDt, out);  // 전환 도중 다시 바뀜
    EXPECT_LT(distance(posed(model, out, 3), before), 0.05f);
}

// ---------------------------------------------------------------------------
// 착지: 무릎을 굽혀 몸을 낮추되 발은 바닥에 붙어 있음
// ---------------------------------------------------------------------------

namespace {

// 공중 → 착지(squash 0.18, 이후 캐릭터처럼 0.18초 동안 줄어듦)를 seconds 동안 진행
AnimationOutput landAndRun(ProceduralAnimator& animator, float seconds) {
    AnimationOutput out;
    animator.animate(staticInput(Motion::Airborne), kDt, out);
    for (float t = 0.0f; t < seconds; t += kDt) {
        AnimationInput input = staticInput(Motion::Idle);
        input.squash = std::max(0.0f, 0.18f * (1.0f - t / 0.18f));
        animator.animate(input, kDt, out);
    }
    return out;
}

}  // namespace

TEST(ProceduralAnimatorLanding, Crouch_LowersHipsKeepingFeetOnGround) {
    const auto model = deskpet::test::makeSkeletonModel();
    ProceduralAnimator animator(model);
    const AnimationOutput out = landAndRun(animator, 0.1f);

    EXPECT_LT(posed(model, out, 1).y, 1.0f - 0.03f);  // 엉덩이(바인드 1.0)가 내려감
    const float lowestFoot = std::min(posed(model, out, 8).y, posed(model, out, 11).y);
    EXPECT_NEAR(lowestFoot, 0.08f, 2e-3f);  // 발은 바닥 높이 그대로 (뜨지도 박히지도 않음)
}

TEST(ProceduralAnimatorLanding, NoWholeBodyStretch_FeetStayUnderHips) {
    // 발이 엉덩이 아래에 있어야 웅크린 모양 (무릎만 굽히면 발이 뒤로 들림)
    const auto model = deskpet::test::makeSkeletonModel();
    ProceduralAnimator animator(model);
    const AnimationOutput out = landAndRun(animator, 0.1f);
    EXPECT_NEAR(posed(model, out, 8).z, posed(model, out, 6).z, 0.03f);
}

TEST(ProceduralAnimatorLanding, RecoversToStandingPose) {
    const auto model = deskpet::test::makeSkeletonModel();
    ProceduralAnimator animator(model);
    const AnimationOutput out = landAndRun(animator, 0.8f);
    EXPECT_EQ(out.skin, evaluate(model, staticInput(Motion::Idle)).skin);
}

TEST(ProceduralAnimatorLanding, WalkKeepsLowerFootOnGround) {
    // 다리를 앞뒤로 흔들어도 낮은 쪽 발이 바닥에 닿도록 몸이 오르내림
    const auto model = deskpet::test::makeSkeletonModel();
    for (const float time : {0.0f, 0.15f, 0.3f, 0.45f}) {
        AnimationInput input;
        input.motion = Motion::Walk;
        input.time = time;
        const AnimationOutput out = evaluate(model, input);
        const float lowestFoot = std::min(posed(model, out, 8).y, posed(model, out, 11).y);
        EXPECT_NEAR(lowestFoot, 0.08f, 2e-3f) << time;
    }
}

TEST(ProceduralAnimatorTransition, Expressions_FadeWithPose) {
    const auto model = deskpet::test::makeSkeletonModel();
    ProceduralAnimator animator(model);
    AnimationOutput out;
    animator.animate(staticInput(Motion::Dragged), kDt, out);
    animator.animate(staticInput(Motion::Idle), kDt, out);
    const float surprised = expression(out, Expression::Surprised);
    EXPECT_GT(surprised, 0.0f);
    EXPECT_LT(surprised, 1.0f);
    for (float t = 0.0f; t < ProceduralAnimator::kTransitionSeconds; t += kDt) {
        animator.animate(staticInput(Motion::Idle), kDt, out);
    }
    EXPECT_FLOAT_EQ(expression(out, Expression::Surprised), 0.0f);
}

TEST(ProceduralAnimatorTransition, AfterTransition_IdleIsStatic) {
    // 전환이 끝나면 다시 같은 skin → Present 생략 유지 (DEBT-02)
    const auto model = deskpet::test::makeSkeletonModel();
    ProceduralAnimator animator(model);
    AnimationOutput a;
    AnimationOutput b;
    animator.animate(staticInput(Motion::Walk), kDt, a);
    for (float t = 0.0f; t <= ProceduralAnimator::kTransitionSeconds; t += kDt) {
        animator.animate(staticInput(Motion::Idle), kDt, a);
    }
    animator.animate(staticInput(Motion::Idle), kDt, b);
    EXPECT_EQ(a.skin, b.skin);
}

// ---------------------------------------------------------------------------
// 표시용 경계 상자: 걷기 자세, 걷는 방향으로 돌린 몸
// ---------------------------------------------------------------------------

TEST(ProceduralAnimator, DisplayBounds_IncludeWalkingStride) {
    // 걷기에서 발이 앞뒤로 약 0.35 나감 (다리 0.82 × sin 25°)
    const auto model = deskpet::test::makeSkeletonModel();
    const auto bounds = ProceduralAnimator(model).displayBounds();
    EXPECT_GT(bounds.max.z, 0.3f);
    EXPECT_LT(bounds.min.z, -0.3f);
}

TEST(ProceduralAnimator, DisplayBounds_CoverBodyTurnedWhileWalking) {
    // 뒤로 1.0 뻗은 꼬리: 정면에서는 폭에 영향이 없지만, 50° 돌리면 옆으로 sin 50° ≈ 0.77
    auto model = deskpet::test::makeSkeletonModel();
    deskpet::model::Vertex tail;
    tail.position = {0.0f, 1.0f, -1.0f};
    tail.joints[0] = 1;  // hips
    tail.weights[0] = 1.0f;
    model.vertices.push_back(tail);
    model.bounds = deskpet::model::computeBounds(model.vertices);
    const ProceduralAnimator animator(model);

    EXPECT_LT(animator.displayBounds().max.x, 0.75f);

    constexpr float kTurn = 50.0f * 3.14159265f / 180.0f;
    const auto turned = animator.displayBounds(kTurn);
    EXPECT_GT(turned.max.x, 0.76f);  // 왼쪽·오른쪽으로 돌 때 모두 포함
    EXPECT_LT(turned.min.x, -0.76f);
}

TEST(ProceduralAnimator, DisplayBounds_CoverIntermediateTurnAngles) {
    // (0.6, -0.6) 점은 x' = 0.6·cos θ − 0.6·sin θ가 θ = −45°에서 최대 (0.849).
    // 양 끝(±50°)만 보면 0.845로 조금 모자람 → 중간 각도도 샘플링해야 함
    auto model = deskpet::test::makeSkeletonModel();
    deskpet::model::Vertex point;
    point.position = {0.6f, 1.0f, -0.6f};
    point.joints[0] = 1;
    point.weights[0] = 1.0f;
    model.vertices.push_back(point);
    model.bounds = deskpet::model::computeBounds(model.vertices);

    constexpr float kTurn = 50.0f * 3.14159265f / 180.0f;
    const auto bounds = ProceduralAnimator(model).displayBounds(kTurn);
    EXPECT_GT(bounds.max.x, 0.6f * std::sqrt(2.0f) - 0.002f);
}
