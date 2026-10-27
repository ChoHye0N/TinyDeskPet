#include "anim/AnimTestModels.h"
#include "anim/ProceduralAnimator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>

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

TEST(ProceduralAnimator, MotionBounds_TurnOnlyWidensWalk) {
    // 앱은 걸을 때만 몸을 돌림 → 뒤로 뻗은 꼬리가 옆으로 나오는 만큼은 걷기 상자에만. 대기 상자까지
    // 넓히면 화면 끝에 서 있을 때 그만큼 떠 보임
    auto model = deskpet::test::makeSkeletonModel();
    deskpet::model::Vertex tail;
    tail.position = {0.0f, 1.0f, -1.0f};
    tail.joints[0] = 1;  // hips
    tail.weights[0] = 1.0f;
    model.vertices.push_back(tail);
    model.bounds = deskpet::model::computeBounds(model.vertices);
    const ProceduralAnimator animator(model);

    constexpr float kTurn = 50.0f * 3.14159265f / 180.0f;
    const auto plain = animator.motionBounds();
    const auto turned = animator.motionBounds(kTurn);
    const auto idle = static_cast<std::size_t>(Motion::Idle);
    const auto walk = static_cast<std::size_t>(Motion::Walk);
    EXPECT_FLOAT_EQ(turned[idle].max.x, plain[idle].max.x);
    EXPECT_FLOAT_EQ(turned[idle].min.x, plain[idle].min.x);
    EXPECT_GT(turned[walk].max.x, 0.76f);
    EXPECT_LT(turned[walk].min.x, -0.76f);
}

TEST(ProceduralAnimator, ScreenXRange_UsesActualVerticesThroughCamera) {
    // 벽은 실제 정점을 카메라로 투영한 화면 폭으로 정함 (상자 꼭짓점을 투영하면 앞쪽 모서리가
    // 원근으로 넓게 잡힘). 단위 행렬이면 x 범위 = 동작별 상자의 x 범위, 걷기는 몸 돌림 포함
    auto model = deskpet::test::makeSkeletonModel();
    deskpet::model::Vertex tail;
    tail.position = {0.0f, 1.0f, -1.0f};
    tail.joints[0] = 1;
    tail.weights[0] = 1.0f;
    model.vertices.push_back(tail);
    model.bounds = deskpet::model::computeBounds(model.vertices);
    const ProceduralAnimator animator(model);
    constexpr float kTurn = 50.0f * 3.14159265f / 180.0f;
    const auto boxes = animator.motionBounds(kTurn);
    const auto ranges = animator.screenXRange(deskpet::core::Mat4::identity(), kTurn);
    const auto idle = static_cast<std::size_t>(Motion::Idle);
    const auto walk = static_cast<std::size_t>(Motion::Walk);
    EXPECT_NEAR(ranges[idle].min, boxes[idle].min.x, 1e-5f);
    EXPECT_NEAR(ranges[idle].max, boxes[idle].max.x, 1e-5f);
    // 걷기는 각도를 나눠 재므로 해석적인 상자보다 아주 조금 작을 수 있음 (2% 안)
    EXPECT_LE(ranges[walk].max, boxes[walk].max.x + 1e-5f);
    EXPECT_GE(ranges[walk].max, boxes[walk].max.x * 0.98f);
    EXPECT_GT(ranges[walk].max, ranges[idle].max);
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

// ---------------------------------------------------------------------------
// 모션 파일 (ADR-0013): 동작별로 클립을 지정하면 코드로 만든 자세 대신 재생
// ---------------------------------------------------------------------------

namespace {

// 왼팔을 수직으로 드는 정지 자세 클립 (월드 델타: +X → +Y)
std::shared_ptr<deskpet::model::MotionClip> raisedArmClip() {
    auto clip = std::make_shared<deskpet::model::MotionClip>();
    clip->frameCount = 1;
    clip->track(deskpet::model::HumanBone::LeftUpperArm) = {
        deskpet::core::Quat::axisAngle({0.0f, 0.0f, 1.0f}, std::numbers::pi_v<float> / 2.0f)};
    return clip;
}

}  // namespace

TEST(ProceduralAnimatorClip, AssignedMotion_PlaysClipInsteadOfProceduralPose) {
    const auto model = deskpet::test::makeSkeletonModel();
    ProceduralAnimator animator(model);
    animator.setClip(Motion::Walk, raisedArmClip());

    AnimationOutput out;
    AnimationInput input;
    input.motion = Motion::Walk;
    animator.evaluate(input, out);
    const Vec3 hand = posed(model, out, 3);
    EXPECT_NEAR(hand.x, 0.2f, 1e-3f);  // 어깨 바로 위
    EXPECT_GT(hand.y, 1.4f + 0.5f);

    input.motion = Motion::Idle;  // 클립이 없는 동작은 그대로 코드로 만든 자세 (팔 내림)
    animator.evaluate(input, out);
    EXPECT_LT(posed(model, out, 3).y, 1.0f);
}

TEST(ProceduralAnimatorClip, HiddenBones_OfThePlayingClip_AreCollapsed) {
    // 클립이 숨기는 본(게임의 소품 숨기기, ADR-0015)은 그 클립을 재생하는 동안만 접힘
    const auto model = deskpet::test::makeSkeletonModel();
    ProceduralAnimator animator(model);
    auto clip = raisedArmClip();
    clip->hiddenBones = {"hand_L"};
    animator.setClip(Motion::Walk, clip);

    AnimationOutput out;
    AnimationInput input;
    input.motion = Motion::Walk;
    animator.evaluate(input, out);
    const Vec3 tip = deskpet::core::transformPoint(model.vertices[0].position, out.skin[3]);
    const Vec3 hand = posed(model, out, 3);
    EXPECT_NEAR(tip.x, hand.x, 1e-4f);  // 손끝 정점이 손 관절로 모임
    EXPECT_NEAR(tip.y, hand.y, 1e-4f);

    // 클립이 없는 동작(코드로 만든 자세)도 숨김: 소품은 이 자세를 모르므로 기본 자세 그대로 떠 있게
    // 됨
    input.motion = Motion::Idle;
    animator.evaluate(input, out);
    const Vec3 hidden = deskpet::core::transformPoint(model.vertices[0].position, out.skin[3]);
    EXPECT_NEAR(hidden.y, posed(model, out, 3).y, 1e-4f);

    // 그 본을 숨기지 않는 클립에서는 보임 (게임도 클립마다 보이고 숨김)
    animator.setClip(Motion::Idle, raisedArmClip());
    animator.evaluate(input, out);
    const Vec3 shown = deskpet::core::transformPoint(model.vertices[0].position, out.skin[3]);
    EXPECT_GT(std::abs(shown.y - posed(model, out, 3).y), 0.1f);
}

TEST(ProceduralAnimatorClip, MotionsWithoutClip_DrapeNamedBonesLikeIdleClip) {
    // 머플러·꼬리 같은 본은 코드로 만든 자세가 움직이지 않음. 기본 자세가 수평으로 뻗은 모델(게임
    // 추출)은 그대로 두면 뻣뻣하게 튀어나오므로, 클립이 없는 동작에서도 대기 클립 첫 프레임 모양
    auto model = deskpet::test::makeSkeletonModel();
    deskpet::model::Bone tail;
    tail.name = "tail";
    tail.parent = 1;  // hips
    tail.position = {0.0f, 1.0f, -0.1f};
    model.bones.push_back(tail);
    deskpet::model::Bone tip;
    tip.name = "tailTip";
    tip.parent = static_cast<int>(model.bones.size()) - 1;
    tip.position = {0.0f, 1.0f, -0.6f};  // 뒤로 수평
    model.bones.push_back(tip);
    const std::size_t kTip = model.bones.size() - 1;

    ProceduralAnimator animator(model);
    auto idle = std::make_shared<deskpet::model::MotionClip>();
    idle->frameCount = 1;
    const deskpet::core::Quat droop =
        deskpet::core::Quat::axisAngle({1.0f, 0.0f, 0.0f}, -std::numbers::pi_v<float> / 2.0f);
    idle->boneTracks = {{"tail", {droop}, {}}};  // 꼬리를 아래로 늘어뜨림
    animator.setClip(Motion::Idle, idle);

    AnimationOutput out;
    AnimationInput input;
    input.motion = Motion::Airborne;  // 클립 없음
    animator.evaluate(input, out);
    const Vec3 end = posed(model, out, kTip);
    EXPECT_LT(end.y, 0.6f);   // 아래로 늘어짐
    EXPECT_GT(end.z, -0.2f);  // 뒤로 뻗지 않음
}

TEST(ProceduralAnimatorClip, MorphWeightsAndOffsets_ComeFromClip_AndIdleForMotionsWithoutClip) {
    // 입 모양(모프 가중치)·눈꺼풀(이동 오프셋)은 재생 중인 클립에서. 클립이 없는 동작은 대기 클립
    // 첫 프레임 (입이 모두 겹쳐 보이거나 눈이 기본 자세로 돌아가지 않게)
    auto model = deskpet::test::makeSkeletonModel();
    deskpet::model::Bone lid;
    lid.name = "eyelid";
    lid.parent = 12;  // head
    lid.position = {0.05f, 1.6f, 0.1f};
    model.bones.push_back(lid);
    const std::size_t kLid = model.bones.size() - 1;
    model.morphs = {{"Mouth_A", 0, {}, 0.0f}, {"Mouth_B", 0, {}, 0.0f}};

    ProceduralAnimator animator(model);
    auto idle = std::make_shared<deskpet::model::MotionClip>();
    idle->frameCount = 1;
    idle->hipsHeight = 1.0f;
    idle->boneTracks = {{"eyelid", {deskpet::core::Quat{}}, {Vec3{0.0f, -0.03f, 0.0f}}}};
    idle->weightTracks = {{"Mouth_A", 0, {1.0f}}, {"Mouth_B", 0, {0.0f}}};
    animator.setClip(Motion::Idle, idle);

    for (const Motion motion : {Motion::Idle, Motion::Airborne}) {
        AnimationOutput out;
        AnimationInput input;
        input.motion = motion;
        animator.evaluate(input, out);
        EXPECT_EQ(out.morphWeights, (std::vector<float>{1.0f, 0.0f}));
        const Vec3 lidNow = posed(model, out, kLid);
        const Vec3 head = posed(model, out, 12);
        // 머리에 대한 눈꺼풀 높이: 기본 0.05에서 0.03 내려감 (자세가 달라도 머리 기준)
        EXPECT_NEAR((lidNow - head).y, 0.02f, 0.01f) << static_cast<int>(motion);
    }
}

TEST(ProceduralAnimatorClip, MorphDefaultWeights_ApplyWhenNoClipGivesThem) {
    // 클립이 없거나 클립에 그 모프의 가중치 트랙이 없으면 모델의 기본 가중치 (입이 겹치지 않게)
    auto model = deskpet::test::makeSkeletonModel();
    model.morphs = {{"Mouth_A", 0, {}, 1.0f}, {"Mouth_B", 0, {}, 0.0f}};
    ProceduralAnimator animator(model);

    AnimationOutput out;
    AnimationInput input;
    animator.evaluate(input, out);  // 클립 없음
    EXPECT_EQ(out.morphWeights, (std::vector<float>{1.0f, 0.0f}));

    auto idle = std::make_shared<deskpet::model::MotionClip>();
    idle->frameCount = 1;
    idle->weightTracks = {{"Mouth_B", 0, {1.0f}}};  // B만 바꿈 → A는 기본값 그대로
    animator.setClip(Motion::Idle, idle);
    animator.evaluate(input, out);
    EXPECT_EQ(out.morphWeights, (std::vector<float>{1.0f, 1.0f}));
}

TEST(ProceduralAnimatorClip, DisplayBounds_IncludeClipPoses) {
    const auto model = deskpet::test::makeSkeletonModel();
    ProceduralAnimator animator(model);
    const float withoutClip = animator.displayBounds().max.y;
    animator.setClip(Motion::Idle, raisedArmClip());
    EXPECT_GT(animator.displayBounds().max.y, withoutClip + 0.4f);  // 든 손끝까지 포함
}

TEST(ProceduralAnimatorClip, MotionBounds_SplitDisplayBoundsByMotion) {
    // 앱은 지금 동작의 상자만 그리기 영역에 씀 → 끌려 다닐 때 클립의 든 팔은 그 동작에만
    const auto model = deskpet::test::makeSkeletonModel();
    ProceduralAnimator animator(model);
    const float idleTop = animator.motionBounds()[0].max.y;
    animator.setClip(Motion::Dragged, raisedArmClip());
    const auto motions = animator.motionBounds();
    EXPECT_FLOAT_EQ(motions[static_cast<std::size_t>(Motion::Idle)].max.y, idleTop);
    EXPECT_GT(motions[static_cast<std::size_t>(Motion::Dragged)].max.y, idleTop + 0.4f);
    float top = 0.0f;
    for (const auto& b : motions) {
        top = std::max(top, b.max.y);
    }
    EXPECT_FLOAT_EQ(animator.displayBounds().max.y, top);
}

TEST(ProceduralAnimator, StandingBounds_BottomIsSoleEvenIfTailHangsLower) {
    // 바닥 기준 = 발바닥 (발·발가락 본에 붙은 정점). 발보다 낮게 늘어진 꼬리 끝을 바닥으로 삼으면
    // 서 있을 때 그만큼 떠 보임. 꼬리는 바닥 아래로 나감
    const auto plain = deskpet::test::makeSkeletonModel();
    const float sole = ProceduralAnimator(plain).standingBounds().min.y;
    auto model = plain;
    deskpet::model::Vertex tail;
    tail.position = {0.0f, -0.2f, -0.3f};
    tail.joints[0] = 1;  // hips
    tail.weights[0] = 1.0f;
    model.vertices.push_back(tail);
    model.bounds = deskpet::model::computeBounds(model.vertices);
    EXPECT_NEAR(ProceduralAnimator(model).standingBounds().min.y, sole, 1e-5f);
    EXPECT_LT(ProceduralAnimator(model).displayBounds().min.y, sole - 0.1f);  // 꼬리는 그대로 포함
}

TEST(ProceduralAnimator, StandingBounds_WithoutFeetUsesLowestVertex) {
    // 발 본이 없는 모델(슬라임형, 휴머노이드 매핑 실패)은 가장 낮은 정점
    auto model = deskpet::test::makeSkeletonModel();
    for (auto& bone : model.bones) {
        if (bone.human == deskpet::model::HumanBone::LeftFoot ||
            bone.human == deskpet::model::HumanBone::RightFoot) {
            bone.human = deskpet::model::HumanBone::None;
        }
    }
    deskpet::model::Vertex tail;
    tail.position = {0.0f, -0.2f, -0.3f};
    tail.joints[0] = 1;
    tail.weights[0] = 1.0f;
    model.vertices.push_back(tail);
    model.bounds = deskpet::model::computeBounds(model.vertices);
    EXPECT_LT(ProceduralAnimator(model).standingBounds().min.y, -0.1f);
}

TEST(ProceduralAnimatorClip, StandingBounds_DoNotDependOnClips) {
    // 화면 크기 기준. 클립을 바꿔도 같아야 캐릭터 크기가 그대로 (모든 동작 상자는 커짐)
    const auto model = deskpet::test::makeSkeletonModel();
    ProceduralAnimator animator(model);
    const auto before = animator.standingBounds();
    animator.setClip(Motion::Idle, raisedArmClip());
    animator.setClip(Motion::Dragged, raisedArmClip());
    const auto after = animator.standingBounds();
    EXPECT_FLOAT_EQ(after.min.x, before.min.x);
    EXPECT_FLOAT_EQ(after.min.y, before.min.y);
    EXPECT_FLOAT_EQ(after.max.x, before.max.x);
    EXPECT_FLOAT_EQ(after.max.y, before.max.y);
    EXPECT_FLOAT_EQ(after.max.z, before.max.z);
    EXPECT_GT(animator.displayBounds().max.y, after.max.y + 0.4f);
    // 기준 자세는 모든 동작 상자 안
    EXPECT_GE(after.min.y, animator.displayBounds().min.y);
}

namespace {

// 엉덩이를 dy만큼 옮기는 정지 자세 클립 (원본 엉덩이 높이 = 테스트 모델과 같은 1 → 그대로)
std::shared_ptr<deskpet::model::MotionClip> hipsMoveClip(float dy) {
    auto clip = std::make_shared<deskpet::model::MotionClip>();
    clip->frameCount = 1;
    clip->hipsHeight = 1.0f;
    clip->hipsTranslation = {{0.0f, dy, 0.0f}};
    return clip;
}

}  // namespace

TEST(ProceduralAnimatorClip, HipsTranslation_LiftsBody) {
    // 집어 올리는 동작처럼 엉덩이가 올라가면 몸 전체가 올라감 (ADR-0014)
    const auto model = deskpet::test::makeSkeletonModel();
    ProceduralAnimator animator(model);
    animator.setClip(Motion::Dragged, hipsMoveClip(0.2f));
    AnimationOutput out;
    AnimationInput input;
    input.motion = Motion::Dragged;
    animator.evaluate(input, out);
    EXPECT_NEAR(posed(model, out, 1).y, 1.2f, 1e-4f);
}

TEST(ProceduralAnimatorClip, HipsLowering_IsNotDoubledByFootPlanting) {
    // 대기 중 클립이 몸을 낮추면 발 붙이기는 더 내리지 않음 (발이 이미 바닥 아래라 내릴 필요 없음)
    const auto model = deskpet::test::makeSkeletonModel();
    ProceduralAnimator animator(model);
    animator.setClip(Motion::Idle, hipsMoveClip(-0.1f));
    AnimationOutput out;
    animator.evaluate({}, out);
    EXPECT_NEAR(posed(model, out, 1).y, 0.9f, 1e-4f);
}

TEST(ProceduralAnimatorClip, ClipRestartsWhenMotionStarts) {
    // 1초 동안 엉덩이를 0 → 30° 돌리는 클립을 매달림에 지정. 누적 시간 5.25초에 매달리기 시작해
    // 0.5초 뒤면 클립의 0.5초(15°). 누적 시간으로 재생하면 5.75 → 0.75초(22.5°)가 됨
    const auto model = deskpet::test::makeSkeletonModel();
    ProceduralAnimator animator(model);
    auto clip = std::make_shared<deskpet::model::MotionClip>();
    clip->frameCount = 31;
    for (int i = 0; i < 31; ++i) {
        clip->track(deskpet::model::HumanBone::Hips)
            .push_back(deskpet::core::Quat::axisAngle(
                {0.0f, 1.0f, 0.0f}, static_cast<float>(i) * std::numbers::pi_v<float> / 180.0f));
    }
    animator.setClip(Motion::Dragged, clip);

    AnimationOutput out;
    AnimationInput input;
    input.time = 5.25f;
    animator.animate(input, 0.0f, out);  // 대기
    input.motion = Motion::Dragged;
    for (int frame = 0; frame <= 10; ++frame) {
        input.time = 5.25f + 0.05f * static_cast<float>(frame);
        animator.animate(input, frame == 0 ? 0.0f : 0.05f, out);
    }
    const Vec3 forward = deskpet::core::transformDirection({0.0f, 0.0f, 1.0f}, out.skin[1]);
    EXPECT_NEAR(std::atan2(forward.x, forward.z) * 180.0f / std::numbers::pi_v<float>, 15.0f, 0.1f);
}

TEST(ProceduralAnimatorClip, InteractClip_RestartsWhenSerialChanges) {
    // 상호작용 중 다시 더블클릭 (동작은 그대로, motionSerial만 바뀜) → 클립을 처음부터
    const auto model = deskpet::test::makeSkeletonModel();
    ProceduralAnimator animator(model);
    auto clip = std::make_shared<deskpet::model::MotionClip>();
    clip->frameCount = 31;
    for (int i = 0; i < 31; ++i) {
        clip->track(deskpet::model::HumanBone::Hips)
            .push_back(deskpet::core::Quat::axisAngle(
                {0.0f, 1.0f, 0.0f}, static_cast<float>(i) * std::numbers::pi_v<float> / 180.0f));
    }
    animator.setClip(Motion::Interact, clip);

    AnimationOutput out;
    AnimationInput input;
    input.motion = Motion::Interact;
    input.motionSerial = 1;
    for (int frame = 0; frame <= 16; ++frame) {  // 0.8초 재생
        input.time = 0.05f * static_cast<float>(frame);
        animator.animate(input, frame == 0 ? 0.0f : 0.05f, out);
    }
    input.motionSerial = 2;                     // 다시 시작
    for (int frame = 0; frame <= 6; ++frame) {  // 0.3초 재생 (전환 0.2초 뒤)
        input.time = 0.85f + 0.05f * static_cast<float>(frame);
        animator.animate(input, 0.05f, out);
    }
    const Vec3 forward = deskpet::core::transformDirection({0.0f, 0.0f, 1.0f}, out.skin[1]);
    EXPECT_NEAR(std::atan2(forward.x, forward.z) * 180.0f / std::numbers::pi_v<float>, 9.0f, 0.1f);
}

TEST(ProceduralAnimatorClip, RemovingClip_RestoresProceduralPose) {
    const auto model = deskpet::test::makeSkeletonModel();
    ProceduralAnimator animator(model);
    animator.setClip(Motion::Idle, raisedArmClip());
    animator.setClip(Motion::Idle, nullptr);
    AnimationOutput out;
    animator.evaluate({}, out);
    EXPECT_LT(posed(model, out, 3).y, 1.0f);
}

// ---------------------------------------------------------------------------
// 커서 바라보기 (LookAt): 머리를 목표 쪽으로 돌림 (각도 제한, animate는 서서히)
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kHeadBone = 12;  // AnimTestModels.h

// 머리 본의 정면(+Z)이 자세에서 가리키는 방향
Vec3 headForward(const AnimationOutput& out) {
    return deskpet::core::normalize(
        deskpet::core::transformDirection({0.0f, 0.0f, 1.0f}, out.skin[kHeadBone]));
}

float degreesBetween(Vec3 a, Vec3 b) {
    return std::acos(std::clamp(deskpet::core::dot(a, b), -1.0f, 1.0f)) * 180.0f /
           std::numbers::pi_v<float>;
}

// 머리(0, 1.55, 0)에서 offset만큼 떨어진 목표
AnimationInput lookingAt(Vec3 offset) {
    AnimationInput input;
    input.idleMotion = false;
    input.lookTarget = Vec3{0.0f, 1.55f, 0.0f} + offset;
    return input;
}

}  // namespace

TEST(ProceduralAnimatorLookAt, NoTargetOrTargetInFront_KeepsHeadForward) {
    const auto model = deskpet::test::makeSkeletonModel();
    AnimationInput input;
    input.idleMotion = false;
    EXPECT_LT(degreesBetween(headForward(evaluate(model, input)), {0.0f, 0.0f, 1.0f}), 0.1f);
    EXPECT_LT(degreesBetween(headForward(evaluate(model, lookingAt({0.0f, 0.0f, 1.0f}))),
                             {0.0f, 0.0f, 1.0f}),
              0.1f);
}

TEST(ProceduralAnimatorLookAt, TargetToTheSideAndAbove_TurnsHeadTowardIt) {
    const auto model = deskpet::test::makeSkeletonModel();
    const AnimationOutput out = evaluate(model, lookingAt({0.5f, 0.2f, 1.0f}));
    const Vec3 forward = headForward(out);
    EXPECT_GT(forward.x, 0.3f);  // 캐릭터 왼쪽(+X)으로
    EXPECT_GT(forward.y, 0.1f);  // 위로
    EXPECT_LT(degreesBetween(forward, deskpet::core::normalize({0.5f, 0.2f, 1.0f})), 1.0f);
}

TEST(ProceduralAnimatorLookAt, TurnIsLimited) {
    // 바로 옆·뒤쪽 목표도 좌우 최대 각도까지만 (목이 꺾이지 않게)
    const auto model = deskpet::test::makeSkeletonModel();
    const Vec3 forward = headForward(evaluate(model, lookingAt({1.0f, 0.0f, -0.2f})));
    const float yaw = std::atan2(forward.x, forward.z) * 180.0f / std::numbers::pi_v<float>;
    EXPECT_NEAR(yaw, ProceduralAnimator::kLookMaxYawDegrees, 0.5f);
    const Vec3 up = headForward(evaluate(model, lookingAt({0.0f, 5.0f, 0.1f})));
    EXPECT_NEAR(std::asin(up.y) * 180.0f / std::numbers::pi_v<float>,
                ProceduralAnimator::kLookMaxUpDegrees, 0.5f);
}

TEST(ProceduralAnimatorLookAt, Animate_TurnsGraduallyThenSettles) {
    const auto model = deskpet::test::makeSkeletonModel();
    ProceduralAnimator animator(model);
    AnimationOutput out;
    animator.animate(lookingAt({0.0f, 0.0f, 1.0f}), 1.0f / 60.0f, out);  // 정면에서 시작

    const AnimationInput input = lookingAt({0.6f, 0.0f, 1.0f});  // 약 31° 왼쪽
    animator.animate(input, 1.0f / 60.0f, out);
    const float first = std::atan2(headForward(out).x, headForward(out).z);
    EXPECT_GT(first, 0.0f);
    EXPECT_LT(first * 180.0f / std::numbers::pi_v<float>, 10.0f);  // 한 프레임에 다 돌지 않음

    for (int i = 0; i < 180; ++i) {
        animator.animate(input, 1.0f / 60.0f, out);
    }
    EXPECT_LT(degreesBetween(headForward(out), deskpet::core::normalize({0.6f, 0.0f, 1.0f})), 0.5f);
    const AnimationOutput settled = out;
    animator.animate(input, 1.0f / 60.0f, out);
    EXPECT_EQ(out.skin, settled.skin);  // 멈추면 정확히 같은 자세 (Present 생략)
}

namespace {

// 머리 아래에 눈 본 두 개를 단 모델
deskpet::model::Model withEyeBones(deskpet::model::Model model) {
    using deskpet::model::HumanBone;
    model.bones.push_back({"eye_L", 12, {0.03f, 1.6f, 0.08f}, HumanBone::LeftEye});
    model.bones.push_back({"eye_R", 12, {-0.03f, 1.6f, 0.08f}, HumanBone::RightEye});
    return model;
}

Vec3 boneForward(const AnimationOutput& out, std::size_t bone) {
    return deskpet::core::normalize(
        deskpet::core::transformDirection({0.0f, 0.0f, 1.0f}, out.skin[bone]));
}

}  // namespace

TEST(ProceduralAnimatorLookAt, WithoutEyeBones_DrivesLookExpressions) {
    const auto model = deskpet::test::makeSkeletonModel();
    const AnimationOutput leftUp = evaluate(model, lookingAt({0.5f, 0.2f, 1.0f}));
    EXPECT_GT(expression(leftUp, Expression::LookLeft), 0.5f);
    EXPECT_GT(expression(leftUp, Expression::LookUp), 0.1f);
    EXPECT_FLOAT_EQ(expression(leftUp, Expression::LookRight), 0.0f);
    EXPECT_FLOAT_EQ(expression(leftUp, Expression::LookDown), 0.0f);

    const AnimationOutput rightDown = evaluate(model, lookingAt({-0.5f, -0.2f, 1.0f}));
    EXPECT_GT(expression(rightDown, Expression::LookRight), 0.5f);
    EXPECT_GT(expression(rightDown, Expression::LookDown), 0.1f);
    EXPECT_FLOAT_EQ(expression(rightDown, Expression::LookLeft), 0.0f);

    // 가장 많이 돌면 가중치 1, 목표가 없으면 0
    const AnimationOutput side = evaluate(model, lookingAt({1.0f, 0.0f, -0.2f}));
    EXPECT_FLOAT_EQ(expression(side, Expression::LookLeft), 1.0f);
    AnimationInput none;
    none.idleMotion = false;
    EXPECT_FLOAT_EQ(expression(evaluate(model, none), Expression::LookLeft), 0.0f);
}

TEST(ProceduralAnimatorLookAt, WithEyeBones_RotatesEyesFurtherThanHead) {
    const auto model = withEyeBones(deskpet::test::makeSkeletonModel());
    const AnimationOutput out = evaluate(model, lookingAt({0.5f, 0.0f, 1.0f}));
    const Vec3 head = headForward(out);
    const Vec3 eye = boneForward(out, 13);
    EXPECT_GT(std::atan2(eye.x, eye.z), std::atan2(head.x, head.z) + 0.1f);  // 눈동자가 더 감
    EXPECT_FLOAT_EQ(expression(out, Expression::LookLeft), 0.0f);  // 본으로 돌면 표정은 쓰지 않음
}

TEST(ProceduralAnimatorLookAt, ExpressionLookAtModel_UsesExpressionsNotEyeBones) {
    // VRM이 시선을 표정으로 지정하면 눈 본이 있어도 돌리지 않음 (두 번 움직이지 않게)
    auto model = withEyeBones(deskpet::test::makeSkeletonModel());
    model.lookAtExpressions = true;
    const AnimationOutput out = evaluate(model, lookingAt({0.5f, 0.0f, 1.0f}));
    EXPECT_LT(degreesBetween(boneForward(out, 13), headForward(out)), 0.1f);
    EXPECT_GT(expression(out, Expression::LookLeft), 0.5f);
}
