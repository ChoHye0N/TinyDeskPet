#include "core/Math3D.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

using deskpet::core::lookAtRH;
using deskpet::core::Mat4;
using deskpet::core::perspectiveFovRH;
using deskpet::core::transform;
using deskpet::core::transformDirection;
using deskpet::core::transformPoint;
using deskpet::core::Vec3;

namespace {

void expectNear(Vec3 actual, Vec3 expected) {
    EXPECT_NEAR(actual.x, expected.x, 1e-5f);
    EXPECT_NEAR(actual.y, expected.y, 1e-5f);
    EXPECT_NEAR(actual.z, expected.z, 1e-5f);
}

}  // namespace

TEST(Mat4, Translation_MovesPointsButNotDirections) {
    const Mat4 t = Mat4::translation({1.0f, 2.0f, 3.0f});
    expectNear(transformPoint({1.0f, 1.0f, 1.0f}, t), {2.0f, 3.0f, 4.0f});
    expectNear(transformDirection({1.0f, 1.0f, 1.0f}, t), {1.0f, 1.0f, 1.0f});
}

TEST(Mat4, Multiply_AppliesLeftOperandFirst) {
    // 행 벡터 규약: p * (A * B) = (p * A) * B
    const Mat4 rotate = Mat4::rotationY(std::numbers::pi_v<float> / 2.0f);
    const Mat4 move = Mat4::translation({10.0f, 0.0f, 0.0f});
    // +X를 Y축으로 90° 돌리면 -Z, 그다음 X로 10 이동
    expectNear(transformPoint({1.0f, 0.0f, 0.0f}, rotate * move), {10.0f, 0.0f, -1.0f});
}

TEST(Mat4, FromColumnMajor_KeepsGltfTranslation) {
    // glTF 열 우선 배열: 이동은 인덱스 12, 13, 14
    float values[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 5, 6, 7, 1};
    expectNear(transformPoint({}, Mat4::fromColumnMajor(values)), {5.0f, 6.0f, 7.0f});
}

TEST(Mat4, LookAtRH_PutsTargetOnNegativeZ) {
    const Mat4 view = lookAtRH({0.0f, 1.0f, 5.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    expectNear(transformPoint({0.0f, 1.0f, 0.0f}, view), {0.0f, 0.0f, -5.0f});
    expectNear(transformPoint({1.0f, 1.0f, 0.0f}, view), {1.0f, 0.0f, -5.0f});  // 오른쪽 유지
}

TEST(Mat4, PerspectiveFovRH_MapsNearAndFarToZeroAndOne) {
    const Mat4 proj = perspectiveFovRH(1.0f, 1.0f, 0.5f, 10.0f);
    const auto nearPoint = transform({0.0f, 0.0f, -0.5f}, proj);
    const auto farPoint = transform({0.0f, 0.0f, -10.0f}, proj);
    EXPECT_NEAR(nearPoint.z / nearPoint.w, 0.0f, 1e-5f);
    EXPECT_NEAR(farPoint.z / farPoint.w, 1.0f, 1e-5f);
}

// ---------------------------------------------------------------------------
// 쿼터니언 (애니메이션)
// ---------------------------------------------------------------------------

using deskpet::core::Quat;
using deskpet::core::slerp;

TEST(Quat, AxisAngle_RotatesByRightHandRule) {
    // +Z축 기준 90°: +X → +Y
    const Quat q = Quat::axisAngle({0.0f, 0.0f, 1.0f}, std::numbers::pi_v<float> / 2.0f);
    expectNear(q.rotate({1.0f, 0.0f, 0.0f}), {0.0f, 1.0f, 0.0f});
}

TEST(Quat, Multiply_AppliesRightOperandFirst) {
    // a * b = b 먼저, 그다음 a (열 벡터 관례, 해밀턴 곱)
    const float half = std::numbers::pi_v<float> / 2.0f;
    const Quat a = Quat::axisAngle({0.0f, 0.0f, 1.0f}, half);  // Z 90°
    const Quat b = Quat::axisAngle({0.0f, 1.0f, 0.0f}, half);  // Y 90°
    // +X --Y 90°--> -Z --Z 90°--> -Z
    expectNear((a * b).rotate({1.0f, 0.0f, 0.0f}), {0.0f, 0.0f, -1.0f});
}

TEST(Quat, FromTo_RotatesFirstDirectionOntoSecond) {
    const Vec3 from = deskpet::core::normalize(Vec3{1.0f, 0.2f, 0.0f});
    const Vec3 to = deskpet::core::normalize(Vec3{0.2f, -1.0f, 0.1f});
    expectNear(Quat::fromTo(from, to).rotate(from), to);
    // 정반대 방향도 처리 (180°)
    expectNear(Quat::fromTo({1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}).rotate({1.0f, 0.0f, 0.0f}),
               {-1.0f, 0.0f, 0.0f});
}

TEST(Quat, ToMat4_MatchesRotateInRowVectorConvention) {
    const Quat q = Quat::axisAngle(deskpet::core::normalize(Vec3{1.0f, 2.0f, 3.0f}), 0.7f);
    const Vec3 p{0.3f, -1.2f, 2.0f};
    expectNear(transformPoint(p, q.toMat4()), q.rotate(p));
}

TEST(Quat, Slerp_InterpolatesAngleLinearly) {
    constexpr float kPi = std::numbers::pi_v<float>;
    const Vec3 z{0.0f, 0.0f, 1.0f};
    const Quat a{};
    const Quat b = Quat::axisAngle(z, kPi / 2.0f);
    expectNear(slerp(a, b, 0.0f).rotate({1.0f, 0.0f, 0.0f}), {1.0f, 0.0f, 0.0f});
    expectNear(slerp(a, b, 1.0f).rotate({1.0f, 0.0f, 0.0f}), {0.0f, 1.0f, 0.0f});
    const float h = std::sqrt(0.5f);  // 45°
    expectNear(slerp(a, b, 0.5f).rotate({1.0f, 0.0f, 0.0f}), {h, h, 0.0f});
}

TEST(Quat, Slerp_TakesShortestPath) {
    // -q는 q와 같은 회전. 부호를 맞추지 않으면 반대쪽으로 크게 돌아감
    constexpr float kPi = std::numbers::pi_v<float>;
    const Quat b = Quat::axisAngle({0.0f, 0.0f, 1.0f}, kPi / 2.0f);
    const Quat negB{-b.x, -b.y, -b.z, -b.w};
    const float h = std::sqrt(0.5f);
    expectNear(slerp(Quat{}, negB, 0.5f).rotate({1.0f, 0.0f, 0.0f}), {h, h, 0.0f});
}

TEST(Quat, Slerp_NearlyEqualRotationsStayFinite) {
    const Quat a = Quat::axisAngle({0.0f, 1.0f, 0.0f}, 0.3f);
    const Quat b = Quat::axisAngle({0.0f, 1.0f, 0.0f}, 0.3f + 1e-6f);
    const Quat q = slerp(a, b, 0.5f);
    EXPECT_TRUE(std::isfinite(q.w));
    EXPECT_NEAR(q.w, a.w, 1e-5f);
}
