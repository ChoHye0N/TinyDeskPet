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

TEST(SpringBone, ZeroTimeFrame_KeepsCurrentSwingInsteadOfBindPose) {
    // 화면 프레임이 고정 간격 사이에 끼면 dt = 0으로 불림. 이때 흔들림을 건너뛰면 그 프레임만
    // 머리카락이 바인드 포즈로 돌아가 한 프레임씩 번쩍였음 → 직전 끝 위치 그대로 자세를 만듦
    deskpet::model::SpringGroup group;
    group.stiffness = 0.5f;
    group.gravityPower = 2.0f;
    const Model model = makeChainModel({0.1f, 0.0f, 0.0f}, group);
    Rig rig(model);
    const auto bent = rig.run(30, {0.01f, 0.0f, 0.0f});  // 처지고 흔들리는 중
    ASSERT_GT(distance(bent[3], model.bones[3].position), 0.05f);

    rig.rotations.assign(rig.skeleton.size(), Quat{});
    rig.springs.update(rig.skeleton, rig.rotations, {}, 0.0f);
    rig.skeleton.computeSkinMatrices(rig.rotations, rig.skin);
    for (std::size_t j = 1; j < model.bones.size(); ++j) {
        const Vec3 held = transformPoint(model.bones[j].position, rig.skin[j]);
        EXPECT_LT(distance(held, bent[j]), 1e-4f) << "bone " << j;
    }

    // 시간이 0이면 아무것도 진행하지 않음: 다음 프레임은 0 프레임이 없었던 것과 같아야 함
    const auto next = rig.run(1);
    Rig reference(model);
    (void)reference.run(30, {0.01f, 0.0f, 0.0f});
    const auto expected = reference.run(1);
    for (std::size_t j = 1; j < model.bones.size(); ++j) {
        EXPECT_LT(distance(next[j], expected[j]), 1e-5f) << "bone " << j;
    }
}

// ---------------------------------------------------------------------------
// VRM 1.0: 명시한 관절 사슬, 캡슐 충돌체
// ---------------------------------------------------------------------------

namespace {

// 선분 ab에서 p까지 거리
float distanceToSegment(Vec3 p, Vec3 a, Vec3 b) {
    const Vec3 ab = b - a;
    const float t =
        std::clamp(deskpet::core::dot(p - a, ab) / deskpet::core::dot(ab, ab), 0.0f, 1.0f);
    return distance(p, a + ab * t);
}

}  // namespace

TEST(SpringBone, Vrm1Joints_SwingOnlyListedJointsAndLastIsTail) {
    // joints {1, 2}: 본 1만 본 2를 향해 흔들리고, 마지막 관절(본 2)과 그 아래(본 3)는 따라만 감.
    // roots {1}(VRM 0.x)이면 자손 전체가 흔들려 본 2 → 3도 따로 휨
    deskpet::model::SpringGroup group;
    group.stiffness = 0.5f;
    group.gravityPower = 2.0f;
    Model model = makeChainModel({0.1f, 0.0f, 0.0f}, group);
    model.springGroups[0].roots.clear();
    model.springGroups[0].joints = {1, 2};
    Rig rig(model);
    const auto posed = rig.run(120);

    EXPECT_LT(posed[2].y, 1.0f - 0.02f);  // 본 1이 휘어 본 2가 내려감
    const Vec3 first = deskpet::core::normalize(posed[2] - posed[1]);
    const Vec3 second = deskpet::core::normalize(posed[3] - posed[2]);
    EXPECT_GT(deskpet::core::dot(first, second), 0.9999f);  // 본 2는 스스로 돌지 않음
}

TEST(SpringBone, CapsuleCollider_PushesTailOutOfSegment) {
    deskpet::model::SpringGroup group;
    group.stiffness = 0.2f;
    group.gravityPower = 3.0f;
    group.hitRadius = 0.01f;
    Model model = makeChainModel({0.1f, 0.0f, 0.0f}, group);

    // 충돌체가 없을 때 사슬 끝(본 3)이 가는 자리를 앞뒤(z)로 지나는 캡슐. 양 끝은 그 자리에서
    // 10cm씩 떨어져 있어, 캡슐을 끝점 구로만 처리하면 막지 못함
    Rig free(model);
    const Vec3 center = free.run(120)[3];
    const Vec3 offset = center - model.bones[0].position;
    deskpet::model::SpringCollider capsule;
    capsule.bone = 0;
    capsule.offset = offset + Vec3{0.0f, 0.0f, -0.1f};
    capsule.tail = offset + Vec3{0.0f, 0.0f, 0.1f};
    capsule.radius = 0.05f;
    capsule.capsule = true;
    model.springColliders.push_back(capsule);
    model.springGroups[0].colliders = {0};
    Rig blocked(model);

    const Vec3 a = model.bones[0].position + capsule.offset;
    const Vec3 b = model.bones[0].position + capsule.tail;
    EXPECT_GE(distanceToSegment(blocked.run(120)[3], a, b), 0.05f);
}
