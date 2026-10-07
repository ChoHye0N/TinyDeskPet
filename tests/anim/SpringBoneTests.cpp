#include "anim/Skeleton.h"
#include "anim/SpringBone.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using deskpet::anim::Skeleton;
using deskpet::anim::SpringBoneSimulator;
using deskpet::core::Quat;
using deskpet::core::transformPoint;
using deskpet::core::Vec3;
using deskpet::model::Model;

namespace {

constexpr float kDt = 1.0f / 60.0f;

// 몸통 본 0 + 흔들리는 사슬 1 → 2 → 3 (본 간격 0.1)
Model makeChainModel(Vec3 step, deskpet::model::SpringGroup group = {}) {
    Model m;
    const auto add = [&](int parent, Vec3 position) {
        deskpet::model::Bone bone;
        bone.parent = parent;
        bone.position = position;
        m.bones.push_back(bone);
    };
    const Vec3 base{0.0f, 1.0f, 0.0f};
    add(-1, base);
    add(0, base + step);
    add(1, base + step * 2.0f);
    add(2, base + step * 3.0f);
    group.roots = {1};
    m.springGroups.push_back(group);
    return m;
}

struct Rig {
    explicit Rig(const Model& source) : model(source), skeleton(source), springs(source) {}

    // n 프레임 진행 후 본별 자세 위치
    std::vector<Vec3> run(int frames, Vec3 movementPerFrame = {}) {
        for (int i = 0; i < frames; ++i) {
            rotations.assign(skeleton.size(), Quat{});
            springs.update(skeleton, rotations, movementPerFrame, kDt);
        }
        skeleton.computeSkinMatrices(rotations, skin);
        std::vector<Vec3> posed;
        for (std::size_t j = 0; j < model.bones.size(); ++j) {
            posed.push_back(transformPoint(model.bones[j].position, skin[j]));
        }
        return posed;
    }

    const Model& model;
    Skeleton skeleton;
    SpringBoneSimulator springs;
    std::vector<Quat> rotations;
    std::vector<deskpet::core::Mat4> skin;
};

float distance(Vec3 a, Vec3 b) {
    const Vec3 d = a - b;
    return std::sqrt(deskpet::core::dot(d, d));
}

}  // namespace

TEST(SpringBone, NoForces_KeepsBindPose) {
    const Model model = makeChainModel({0.1f, 0.0f, 0.0f});
    Rig rig(model);
    const auto posed = rig.run(60);
    EXPECT_LT(distance(posed[3], model.bones[3].position), 1e-3f);
}

TEST(SpringBone, Gravity_BendsHorizontalChainDownKeepingLengths) {
    deskpet::model::SpringGroup group;
    group.stiffness = 0.5f;
    group.gravityPower = 2.0f;
    const Model model = makeChainModel({0.1f, 0.0f, 0.0f}, group);
    Rig rig(model);
    const auto posed = rig.run(120);

    EXPECT_LT(posed[3].y, 1.0f - 0.05f);                     // 끝이 아래로 처짐
    EXPECT_NEAR(distance(posed[1], posed[2]), 0.1f, 1e-3f);  // 본 길이는 그대로
    EXPECT_NEAR(distance(posed[2], posed[3]), 0.1f, 1e-3f);
    EXPECT_LT(distance(posed[1], model.bones[1].position), 1e-4f);  // 뿌리는 제자리
}

TEST(SpringBone, MovingCharacter_TailLagsBehindThenSettles) {
    deskpet::model::SpringGroup group;
    group.stiffness = 1.0f;
    group.dragForce = 0.4f;
    const Model model = makeChainModel({0.0f, -0.1f, 0.0f}, group);  // 아래로 늘어진 머리카락
    Rig rig(model);
    (void)rig.run(10);

    const auto moving = rig.run(5, {0.03f, 0.0f, 0.0f});  // 캐릭터가 오른쪽으로 끌려감
    EXPECT_LT(moving[3].x, -0.01f);                       // 끝은 왼쪽으로 뒤처짐

    const auto settled = rig.run(240);  // 멈추면 원래 자리로
    EXPECT_LT(std::abs(settled[3].x), 0.005f);
}

TEST(SpringBone, Collider_PushesTailOutOfSphere) {
    deskpet::model::SpringGroup group;
    group.stiffness = 0.2f;
    group.gravityPower = 3.0f;
    group.hitRadius = 0.01f;
    Model model = makeChainModel({0.1f, 0.0f, 0.0f}, group);

    // 구가 없을 때 사슬 끝(본 3)이 늘어져 가는 자리에 구 중심을 둠
    Rig free(model);
    const Vec3 center = free.run(120)[3];
    const Vec3 offset = center - model.bones[0].position;

    model.springColliders.push_back({0, offset, 0.05f});
    model.springGroups[0].colliders = {0};
    Rig blocked(model);
    // 밀어낸 뒤 본 길이로 되돌리므로 hitRadius 여유까지는 보장하지 않지만 구 안으로는 안 들어감
    EXPECT_GE(distance(blocked.run(120)[3], center), 0.05f);
}

TEST(SpringBone, Teleport_ResetsInsteadOfExploding) {
    deskpet::model::SpringGroup group;
    group.stiffness = 1.0f;
    const Model model = makeChainModel({0.0f, -0.1f, 0.0f}, group);
    Rig rig(model);
    (void)rig.run(10);
    const auto posed = rig.run(1, {50.0f, 0.0f, 0.0f});  // 위치 초기화·모니터 이동 등
    EXPECT_TRUE(std::isfinite(posed[3].x));
    EXPECT_LT(distance(posed[3], model.bones[3].position), 1e-3f);
}

TEST(SpringBone, ModelWithoutSprings_IsEmptyAndLeavesRotations) {
    Model model;
    model.bones.resize(2);
    model.bones[1].parent = 0;
    const Skeleton skeleton(model);
    SpringBoneSimulator springs(model);
    EXPECT_TRUE(springs.empty());
    std::vector<Quat> rotations(2);
    springs.update(skeleton, rotations, {}, kDt);
    EXPECT_EQ(rotations[1], Quat{});
}

TEST(SpringBone, Settled_GivesExactlySameRotations) {
    // 흔들림이 멈춘 뒤 미세한 오차로 매 프레임 값이 바뀌면 Present 생략(DEBT-02)이 안 됨
    deskpet::model::SpringGroup group;
    group.stiffness = 1.0f;
    group.gravityPower = 0.5f;
    const Model model = makeChainModel({0.1f, 0.0f, 0.0f}, group);
    Rig rig(model);
    (void)rig.run(600);
    const std::vector<Quat> before = rig.rotations;
    (void)rig.run(1);
    EXPECT_EQ(rig.rotations, before);
}

// ---------------------------------------------------------------------------
// 흔들림 범위 제한: 아무리 세게 움직여도 원래 방향에서 kMaxSwingDegrees 안
// ---------------------------------------------------------------------------

namespace {

// 본 j에서 다음 본으로 가는 방향과 원래(바인드) 방향 사이 각도 (도)
float swingDegrees(const Model& model, const std::vector<Vec3>& posed, std::size_t j) {
    const Vec3 rest = model.bones[j + 1].position - model.bones[j].position;
    const Vec3 now = posed[j + 1] - posed[j];
    const float c = deskpet::core::dot(rest, now) /
                    std::sqrt(deskpet::core::dot(rest, rest) * deskpet::core::dot(now, now));
    return std::acos(std::clamp(c, -1.0f, 1.0f)) * 180.0f / 3.14159265f;
}

}  // namespace

TEST(SpringBone, ViolentMovement_SwingStaysWithinLimit) {
    // 빠르게 떨어지다 멈춘 상황: 아래로 늘어진 머리카락이 위로 크게 튀어 오르면 안 됨
    deskpet::model::SpringGroup group;
    group.stiffness = 0.5f;
    group.dragForce = 0.1f;
    const Model model = makeChainModel({0.0f, -0.1f, 0.0f}, group);
    Rig rig(model);
    (void)rig.run(5);
    for (int i = 0; i < 30; ++i) {
        const auto posed =
            rig.run(1, {0.0f, -0.25f, 0.0f});  // 초속 15m로 낙하 (순간 이동 기준 미만)
        for (std::size_t j = 1; j <= 2; ++j) {
            EXPECT_LE(swingDegrees(model, posed, j), SpringBoneSimulator::kMaxSwingDegrees + 0.5f)
                << i << ' ' << j;
        }
        // 마디마다 각도가 누적되지 않음: 뿌리에서 끝까지 전체 방향도 범위 안
        const Vec3 rest = model.bones[3].position - model.bones[1].position;
        const Vec3 now = posed[3] - posed[1];
        const float c = deskpet::core::dot(rest, now) /
                        std::sqrt(deskpet::core::dot(rest, rest) * deskpet::core::dot(now, now));
        EXPECT_LE(std::acos(std::clamp(c, -1.0f, 1.0f)) * 180.0f / 3.14159265f,
                  SpringBoneSimulator::kMaxSwingDegrees + 0.5f)
            << i;
    }
    const auto landed = rig.run(3);  // 멈춘 직후에도
    EXPECT_LE(swingDegrees(model, landed, 2), SpringBoneSimulator::kMaxSwingDegrees + 0.5f);
}

TEST(SpringBone, FastMovement_InertiaIsCapped) {
    // 같은 거리를 빠르게(한 프레임) 움직이든 그 절반 속도로 움직이든 상한 이상은 차이가 없음
    deskpet::model::SpringGroup group;
    group.stiffness = 1.0f;
    const Model model = makeChainModel({0.0f, -0.1f, 0.0f}, group);
    Rig fast(model);
    Rig faster(model);
    (void)fast.run(5);
    (void)faster.run(5);
    const auto a = fast.run(1, {0.2f, 0.0f, 0.0f});
    const auto b = faster.run(1, {0.4f, 0.0f, 0.0f});
    EXPECT_LT(distance(a[3], b[3]), 1e-4f);
}
