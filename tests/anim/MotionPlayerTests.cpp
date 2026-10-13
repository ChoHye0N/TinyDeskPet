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

TEST(MotionPlayer, NamedTracks_DriveBonesWithTheSameName) {
    // 휴머노이드가 아닌 본(꼬리)은 이름이 같은 트랙으로 움직이고, 모델에 없는 이름은 버림.
    // 휴머노이드 본과 이름이 같은 트랙("head")은 휴머노이드 트랙이 우선
    auto model = deskpet::test::makeSkeletonModel();
    deskpet::model::Bone tail;
    tail.name = "tail";
    tail.parent = static_cast<int>(kHips);
    tail.position = {0.0f, 0.9f, -0.1f};
    model.bones.push_back(tail);
    const std::size_t kTail = model.bones.size() - 1;
    const Skeleton skeleton(model);

    const Quat wag = Quat::axisAngle({0.0f, 1.0f, 0.0f}, radians(30.0f));
    const Quat nod = Quat::axisAngle({1.0f, 0.0f, 0.0f}, radians(20.0f));
    auto clip = poseClip({{HumanBone::Head, nod}});
    clip->boneTracks = {{"tail", {wag}},
                        {"ribbon", {wag}},
                        {"head", {Quat::axisAngle({0.0f, 0.0f, 1.0f}, radians(50.0f))}}};
    const MotionPlayer player(skeleton, clip);
    const Posed p = pose(skeleton, player, 0.0f);

    const Vec3 z{0.0f, 0.0f, 1.0f};
    expectNear(p.accumulated[kTail].rotate(z), wag.rotate(z));
    expectNear(p.accumulated[kHead].rotate(z), nod.rotate(z));
}

TEST(MotionPlayer, HiddenBones_AreMarkedByName) {
    // 클립 내내 크기가 0인 본(총알 등)은 이름이 같은 대상 본을 숨김. 모델에 없는 이름은 버림
    auto model = deskpet::test::makeSkeletonModel();
    deskpet::model::Bone bullet;
    bullet.name = "bullet";
    bullet.parent = static_cast<int>(kHips);
    model.bones.push_back(bullet);
    const std::size_t kBullet = model.bones.size() - 1;
    const Skeleton skeleton(model);

    auto clip = poseClip({});
    clip->hiddenBones = {"bullet", "grenade"};
    const MotionPlayer player(skeleton, clip);
    ASSERT_EQ(player.hidden().size(), skeleton.size());
    EXPECT_EQ(player.hidden()[kBullet], 1);
    EXPECT_EQ(player.hidden()[kHips], 0);
}

TEST(MotionPlayer, NamedTrackOffsets_AreScaledByHipsHeight) {
    // 이동 오프셋은 원본 단위라 대상/원본 엉덩이 높이 비율을 곱함 (원본 엉덩이 2 → 대상 1)
    auto model = deskpet::test::makeSkeletonModel();
    deskpet::model::Bone lid;
    lid.name = "eyelid";
    lid.parent = static_cast<int>(kHead);
    lid.position = {0.05f, 1.6f, 0.1f};
    model.bones.push_back(lid);
    const std::size_t kLid = model.bones.size() - 1;
    const Skeleton skeleton(model);

    auto clip = poseClip({});
    clip->hipsHeight = 2.0f;
    clip->boneTracks = {{"eyelid", {Quat{}}, {Vec3{0.0f, -0.04f, 0.0f}}}};
    const MotionPlayer player(skeleton, clip);
    std::vector<Vec3> offsets;
    player.sampleOffsets(0.0f, offsets);
    ASSERT_EQ(offsets.size(), skeleton.size());
    expectNear(offsets[kLid], {0.0f, -0.02f, 0.0f});
    expectNear(offsets[kHead], {});
}

TEST(MotionPlayer, WeightTracks_MapToModelMorphsByNameAndTarget) {
    const auto model = deskpet::test::makeSkeletonModel();
    const Skeleton skeleton(model);
    const std::vector<deskpet::model::NamedMorph> morphs = {
        {"Mouth_A", 0, {}}, {"Mouth_A", 1, {}}, {"Mouth_B", 0, {}}};
    auto clip = poseClip({});
    clip->weightTracks = {{"Mouth_A", 1, {0.5f}}, {"Mouth_C", 0, {1.0f}}};  // C는 모델에 없음
    const MotionPlayer player(skeleton, clip, morphs);
    std::vector<float> weights;
    EXPECT_TRUE(player.sampleWeights(0.0f, weights));
    EXPECT_EQ(weights, (std::vector<float>{0.0f, 0.5f, 0.0f}));
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

namespace {

// 프레임 i에서 엉덩이를 Y축으로 i × 10° 돌리는 클립 (frames 프레임)
std::shared_ptr<MotionClip> turningClip(std::size_t frames) {
    auto clip = std::make_shared<MotionClip>();
    clip->frameCount = frames;
    for (std::size_t i = 0; i < frames; ++i) {
        clip->track(HumanBone::Hips)
            .push_back(Quat::axisAngle({0.0f, 1.0f, 0.0f}, radians(10.0f * static_cast<float>(i))));
    }
    return clip;
}

float hipsYaw(const Skeleton& skeleton, const MotionPlayer& player, float time) {
    const Vec3 v = pose(skeleton, player, time).accumulated[kHips].rotate({0.0f, 0.0f, 1.0f});
    return std::atan2(v.x, v.z) * 180.0f / kPi;
}

}  // namespace

TEST(MotionPlayer, LoopStart_PlaysOnceThenRepeatsFromLoopStart) {
    // 프레임 0 ~ 4 (0° ~ 40°), 반복은 프레임 2부터: 처음엔 끝까지, 그 뒤 2 → 4만 반복
    const auto model = deskpet::test::makeSkeletonModel();
    const Skeleton skeleton(model);
    auto clip = turningClip(5);
    clip->loopStart = 2.0f / 30.0f;
    const MotionPlayer player(skeleton, clip);
    EXPECT_NEAR(hipsYaw(skeleton, player, 1.0f / 30.0f), 10.0f, 1e-2f);  // 첫 재생은 처음부터
    EXPECT_NEAR(hipsYaw(skeleton, player, 4.0f / 30.0f), 40.0f, 1e-2f);  // 끝
    EXPECT_NEAR(hipsYaw(skeleton, player, 5.5f / 30.0f), 35.0f, 1e-2f);  // 2 + 1.5프레임
    EXPECT_NEAR(hipsYaw(skeleton, player, 6.5f / 30.0f), 25.0f, 1e-2f);  // 두 번째 반복
}

TEST(MotionPlayer, LoopStartPastEnd_HoldsLastPose) {
    const auto model = deskpet::test::makeSkeletonModel();
    const Skeleton skeleton(model);
    auto clip = turningClip(5);
    clip->loopStart = 10.0f;
    const MotionPlayer player(skeleton, clip);
    EXPECT_NEAR(hipsYaw(skeleton, player, 2.0f / 30.0f), 20.0f, 1e-2f);
    EXPECT_NEAR(hipsYaw(skeleton, player, 3.0f), 40.0f, 1e-2f);  // 끝난 뒤엔 마지막 자세
}

TEST(MotionPlayer, RootOffset_ScalesByHipsHeightAndKeepsOnlySway) {
    // 원본 엉덩이 높이 2, 대상(테스트 모델) 1 → 이동을 절반으로. 원본은 앞(+X, +Z)으로 걸어가며
    // 가운데 프레임에 X로 0.1 더 흔들리고 0.4 내려감
    const auto model = deskpet::test::makeSkeletonModel();
    const Skeleton skeleton(model);
    auto clip = turningClip(3);
    clip->hipsHeight = 2.0f;
    clip->hipsTranslation = {{0.0f, 0.0f, 0.0f}, {0.3f, -0.4f, 0.1f}, {0.4f, 0.0f, 0.2f}};
    const MotionPlayer player(skeleton, clip);

    // 수평: 걸어간 경로(시작 → 끝을 직선으로)를 빼고 흔들림만. 수직: 그대로 (둘 다 × 0.5)
    expectNear(player.rootOffset(1.0f / 30.0f), {0.05f, -0.2f, 0.0f}, 1e-4f);
    expectNear(player.rootOffset(0.0f), {0.0f, 0.0f, 0.0f}, 1e-4f);
    expectNear(player.rootOffset(2.0f / 30.0f), {0.0f, 0.0f, 0.0f}, 1e-4f);  // 끝도 제자리
}

TEST(MotionPlayer, ClipWithoutHipsTranslation_HasNoRootOffset) {
    const auto model = deskpet::test::makeSkeletonModel();
    const Skeleton skeleton(model);
    const MotionPlayer player(skeleton, turningClip(3));
    expectNear(player.rootOffset(1.0f / 30.0f), {0.0f, 0.0f, 0.0f});
}
