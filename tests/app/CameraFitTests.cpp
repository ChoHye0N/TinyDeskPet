#include "app/CameraFit.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

using deskpet::app::fitCameraToBounds;
using deskpet::core::Mat4;
using deskpet::core::SizeI;
using deskpet::core::Vec3;
using deskpet::model::Bounds;

namespace {

// 경계 상자 8개 꼭짓점의 NDC 좌표
std::array<Vec3, 8> projectCorners(const Bounds& b, const Mat4& viewProjection) {
    std::array<Vec3, 8> result;
    for (std::size_t i = 0; i < 8; ++i) {
        const Vec3 corner{(i & 1U) != 0 ? b.max.x : b.min.x, (i & 2U) != 0 ? b.max.y : b.min.y,
                          (i & 4U) != 0 ? b.max.z : b.min.z};
        const auto clip = deskpet::core::transform(corner, viewProjection);
        result[i] = {clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
    }
    return result;
}

// 사람 크기 모델 (T포즈: 폭 1.5m, 키 1.6m)
constexpr Bounds kHumanoid{{-0.75f, 0.0f, -0.15f}, {0.75f, 1.6f, 0.15f}};
// 걸을 때 돌린 몸까지 포함한 깊은 상자 (꼬리·치마가 앞뒤로 0.8m)
constexpr Bounds kDeep{{-0.8f, 0.0f, -0.8f}, {0.8f, 1.6f, 0.8f}};

// 발이 닿는 점 (상자 가로 중앙, 바닥, z = 0)의 NDC y
float feetNdcY(const Bounds& b, const Mat4& viewProjection) {
    const auto clip =
        deskpet::core::transform({(b.min.x + b.max.x) * 0.5f, b.min.y, 0.0f}, viewProjection);
    return clip.y / clip.w;
}

}  // namespace

TEST(CameraFit, WholeModelIsInsideViewAndDepthRange) {
    for (const Bounds& bounds : {kHumanoid, kDeep}) {
        for (const SizeI viewport : {SizeI{240, 400}, SizeI{400, 200}}) {
            for (const Vec3& p : projectCorners(bounds, fitCameraToBounds(bounds, viewport))) {
                EXPECT_GE(p.x, -1.0f);
                EXPECT_LE(p.x, 1.0f);
                EXPECT_LE(p.y, 1.0f);
                EXPECT_GT(p.z, 0.0f);
                EXPECT_LT(p.z, 1.0f);
            }
        }
    }
}

TEST(CameraFit, PointsOnFeetPlaneAndBehindAreAboveBottom) {
    // 아래 기준은 발이 닿는 평면(z = 0). 그보다 뒤의 바닥 점은 원근 때문에 더 위로 보임.
    // (상자 앞쪽 바닥 모서리는 실제 정점이 아니라 상자의 빈 구석이므로 검사하지 않음)
    for (const Bounds& bounds : {kHumanoid, kDeep}) {
        const Mat4 camera = fitCameraToBounds(bounds, {240, 400});
        for (const Vec3& p : projectCorners(bounds, camera)) {
            EXPECT_GE(p.y, p.z > 0.0f ? -1.1f : -1.0f);
        }
        EXPECT_GE(feetNdcY(bounds, camera), -1.0f);
    }
}

TEST(CameraFit, ModelFillsLimitingAxisAndIsCentered) {
    // 세로로 긴 창: 가로(T포즈 팔)가 먼저 꽉 참
    const auto corners = projectCorners(kHumanoid, fitCameraToBounds(kHumanoid, {240, 400}));
    float minX = 1.0f;
    float maxX = -1.0f;
    for (const Vec3& p : corners) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
    }
    EXPECT_GT(maxX - minX, 1.8f);
    EXPECT_NEAR(minX + maxX, 0.0f, 1e-4f);
}

TEST(CameraFit, FeetAreNearBottomOfView) {
    // 발이 창 아래쪽에 있어야 창을 바닥에 맞춰 놓았을 때 땅에 서 있음.
    // 앞뒤로 깊은 모델이라도 떠 보이면 안 됨 (상자 앞면 기준으로 맞추면 발이 위로 뜸)
    for (const Bounds& bounds : {kHumanoid, kDeep}) {
        EXPECT_LT(feetNdcY(bounds, fitCameraToBounds(bounds, {240, 400})), -0.95f);
    }
}
