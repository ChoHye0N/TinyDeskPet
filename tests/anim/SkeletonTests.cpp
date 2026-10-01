#include "anim/AnimTestModels.h"
#include "anim/Skeleton.h"

#include <gtest/gtest.h>

#include <numbers>
#include <vector>

using deskpet::anim::Skeleton;
using deskpet::core::Mat4;
using deskpet::core::Quat;
using deskpet::core::Vec3;
using deskpet::model::HumanBone;

namespace {

void expectNear(Vec3 actual, Vec3 expected, float tolerance = 1e-4f) {
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
}

}  // namespace

TEST(Skeleton, IdentityRotations_GiveIdentitySkin) {
    const auto model = deskpet::test::makeSkeletonModel();
    const Skeleton skeleton(model);
    std::vector<Quat> rotations(skeleton.size());
    std::vector<Mat4> skin;
    skeleton.computeSkinMatrices(rotations, skin);

    ASSERT_EQ(skin.size(), model.bones.size());
    for (const Mat4& m : skin) {
        expectNear(deskpet::core::transformPoint({1.0f, 2.0f, 3.0f}, m), {1.0f, 2.0f, 3.0f});
    }
}

TEST(Skeleton, ParentRotation_MovesChildrenAroundParentJoint) {
    const auto model = deskpet::test::makeSkeletonModel();
    const Skeleton skeleton(model);
    std::vector<Quat> rotations(skeleton.size());
    // 왼쪽 위팔을 Z축 기준 -90°: 수평(+X)이던 팔이 아래(-Y)를 향함
    rotations[2] = Quat::axisAngle({0.0f, 0.0f, 1.0f}, -std::numbers::pi_v<float> / 2.0f);
    std::vector<Mat4> skin;
    skeleton.computeSkinMatrices(rotations, skin);

    // 아래팔 관절(바인드 (0.5, 1.4, 0))은 어깨 (0.2, 1.4, 0)에서 0.3 아래로
    expectNear(deskpet::core::transformPoint(model.bones[0].position, skin[0]), {0.2f, 1.1f, 0.0f});
    // 손(아래팔의 자식)도 따라감 — 자식이 배열에서 부모보다 앞에 있어도 계층 순서로 계산
    expectNear(deskpet::core::transformPoint(model.bones[3].position, skin[3]),
               {0.2f, 0.85f, 0.0f});
    // 다른 팔은 그대로
    expectNear(deskpet::core::transformPoint(model.bones[5].position, skin[5]),
               model.bones[5].position);
}

TEST(Skeleton, ChildDelta_IsAppliedOnTopOfParentPose) {
    const auto model = deskpet::test::makeSkeletonModel();
    const Skeleton skeleton(model);
    std::vector<Quat> rotations(skeleton.size());
    const float quarter = std::numbers::pi_v<float> / 2.0f;
    rotations[2] = Quat::axisAngle({0.0f, 0.0f, 1.0f}, -quarter);  // 위팔: 아래로
    rotations[0] = Quat::axisAngle({1.0f, 0.0f, 0.0f}, -quarter);  // 아래팔: 모델 X축 기준 앞으로
    std::vector<Mat4> skin;
    skeleton.computeSkinMatrices(rotations, skin);

    // 팔꿈치 (0.2, 1.1, 0)에서 손이 앞(+Z)으로 0.25
    expectNear(deskpet::core::transformPoint(model.bones[3].position, skin[3]),
               {0.2f, 1.1f, 0.25f});
}

TEST(Skeleton, FindsHumanBones) {
    const auto model = deskpet::test::makeSkeletonModel();
    const Skeleton skeleton(model);
    EXPECT_EQ(skeleton.find(HumanBone::LeftUpperArm), 2);
    EXPECT_EQ(skeleton.find(HumanBone::Chest), -1);
}
