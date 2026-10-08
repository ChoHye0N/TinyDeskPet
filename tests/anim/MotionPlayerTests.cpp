#include "anim/AnimTestModels.h"
#include "anim/MotionPlayer.h"
#include "anim/Skeleton.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

using deskpet::anim::MotionPlayer;
using deskpet::anim::Skeleton;
using deskpet::core::Quat;
using deskpet::core::Vec3;
using deskpet::model::HumanBone;
using deskpet::model::MotionClip;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// 테스트 모델의 본 번호 (AnimTestModels.h)
constexpr std::size_t kLowerArmL = 0;
constexpr std::size_t kHips = 1;
constexpr std::size_t kUpperArmL = 2;
constexpr std::size_t kHandL = 3;
constexpr std::size_t kHead = 12;

float radians(float degrees) {
    return degrees * kPi / 180.0f;
}

void expectNear(Vec3 actual, Vec3 expected, float tolerance = 1e-3f) {
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
}

// 프레임 하나짜리 클립. 지정한 본에 같은 월드 델타
std::shared_ptr<MotionClip> poseClip(std::initializer_list<std::pair<HumanBone, Quat>> deltas) {
    auto clip = std::make_shared<MotionClip>();
    clip->frameCount = 1;
    for (const auto& [bone, q] : deltas) {
        clip->track(bone) = {q};
    }
    return clip;
}

struct Posed {
    std::vector<Quat> rotations;
    std::vector<Quat> accumulated;
    std::vector<Vec3> positions;
};

Posed pose(const Skeleton& skeleton, const MotionPlayer& player, float time) {
    Posed p;
    p.rotations.assign(skeleton.size(), Quat{});
    player.sample(time, p.rotations);
    skeleton.computePose(p.rotations, p.accumulated, p.positions);
    return p;
}

Vec3 direction(const Posed& p, std::size_t from, std::size_t to) {
    return deskpet::core::normalize(p.positions[to] - p.positions[from]);
}

}  // namespace

TEST(MotionPlayer, EmptyClip_KeepsRestPose) {
    const auto model = deskpet::test::makeSkeletonModel();
    const Skeleton skeleton(model);
    const MotionPlayer player(skeleton, poseClip({}));
    const Posed p = pose(skeleton, player, 0.0f);
    for (const Quat& q : p.rotations) {
        EXPECT_FLOAT_EQ(q.w, 1.0f);
    }
}

TEST(MotionPlayer, WorldDeltas_BecomeLocalRotationsInModelAxes) {
    // 엉덩이 Y 90°, 위팔은 월드 기준 Z −45° (엉덩이 회전 위에 덧붙인 결과가 아님)
    const auto model = deskpet::test::makeSkeletonModel();
    const Skeleton skeleton(model);
    const Quat hips = Quat::axisAngle({0.0f, 1.0f, 0.0f}, radians(90.0f));
    const Quat arm = Quat::axisAngle({0.0f, 0.0f, 1.0f}, radians(-45.0f));
    const MotionPlayer player(skeleton,
                              poseClip({{HumanBone::Hips, hips}, {HumanBone::LeftUpperArm, arm}}));
    const Posed p = pose(skeleton, player, 0.0f);

    // 누적 회전 = 클립의 월드 델타
    const Vec3 x{1.0f, 0.0f, 0.0f};
    expectNear(p.accumulated[kHips].rotate(x), hips.rotate(x));
    expectNear(p.accumulated[kUpperArmL].rotate(x), arm.rotate(x));
    // 트랙이 없는 아래팔·손·머리는 부모를 그대로 따라감 (로컬 회전 없음)
    EXPECT_NEAR(p.rotations[kLowerArmL].w, 1.0f, 1e-5f);
    EXPECT_NEAR(p.rotations[kHead].w, 1.0f, 1e-5f);
    expectNear(direction(p, kUpperArmL, kLowerArmL), arm.rotate(x));
}

TEST(MotionPlayer, SourceRestPose_IsMatchedToTargetRestPose) {
    // 원본은 A포즈(팔 37° 아래)가 기본 자세. 원본이 기본 자세 그대로면 T포즈 대상도 팔을 37° 내림
    const auto model = deskpet::test::makeSkeletonModel(0.0f);
    const Skeleton skeleton(model);
    auto clip = poseClip({{HumanBone::LeftUpperArm, Quat{}}, {HumanBone::LeftHand, Quat{}}});
    const Vec3 aPose{std::cos(radians(37.0f)), -std::sin(radians(37.0f)), 0.0f};
    clip->restDirections[static_cast<std::size_t>(HumanBone::LeftUpperArm)] = aPose;
    const MotionPlayer player(skeleton, clip);
    const Posed p = pose(skeleton, player, 0.0f);

    expectNear(direction(p, kUpperArmL, kLowerArmL), aPose);
    // 아래팔(트랙 없음)과 손(방향 정보 없음)은 위팔의 보정을 따라 일직선 유지
    expectNear(direction(p, kLowerArmL, kHandL), aPose);
}

TEST(MotionPlayer, SameRestPose_NeedsNoCorrection) {
    const auto model = deskpet::test::makeSkeletonModel(0.0f);
    const Skeleton skeleton(model);
    auto clip = poseClip({{HumanBone::LeftUpperArm, Quat{}}});
    clip->restDirections[static_cast<std::size_t>(HumanBone::LeftUpperArm)] = {1.0f, 0.0f, 0.0f};
    const MotionPlayer player(skeleton, clip);
    expectNear(direction(pose(skeleton, player, 0.0f), kUpperArmL, kLowerArmL), {1.0f, 0.0f, 0.0f});
}

TEST(MotionPlayer, Frames_AreInterpolatedAndLooped) {
    const auto model = deskpet::test::makeSkeletonModel();
    const Skeleton skeleton(model);
    auto clip = std::make_shared<MotionClip>();
    clip->frameCount = 3;
    const Vec3 y{0.0f, 1.0f, 0.0f};
    clip->track(HumanBone::Hips) = {Quat{}, Quat::axisAngle(y, radians(10.0f)),
                                    Quat::axisAngle(y, radians(20.0f))};
    const MotionPlayer player(skeleton, clip);
    EXPECT_NEAR(player.duration(), 2.0f / 30.0f, 1e-6f);

    const auto yaw = [&](float time) {
        const Vec3 v = pose(skeleton, player, time).accumulated[kHips].rotate({0.0f, 0.0f, 1.0f});
        return std::atan2(v.x, v.z) * 180.0f / kPi;
    };
    EXPECT_NEAR(yaw(0.5f / 30.0f), 5.0f, 1e-2f);   // 프레임 0과 1 사이
    EXPECT_NEAR(yaw(1.5f / 30.0f), 15.0f, 1e-2f);  // 프레임 1과 2 사이
    EXPECT_NEAR(yaw(3.0f / 30.0f), 10.0f, 1e-2f);  // 길이(2프레임)를 넘으면 처음부터 반복
}
