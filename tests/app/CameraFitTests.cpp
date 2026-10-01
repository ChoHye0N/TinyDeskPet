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

}  // namespace

TEST(CameraFit, WholeModelIsInsideViewAndDepthRange) {
    for (const SizeI viewport : {SizeI{240, 400}, SizeI{400, 200}}) {
        for (const Vec3& p : projectCorners(kHumanoid, fitCameraToBounds(kHumanoid, viewport))) {
            EXPECT_GE(p.x, -1.0f);
            EXPECT_LE(p.x, 1.0f);
            EXPECT_GE(p.y, -1.0f);
            EXPECT_LE(p.y, 1.0f);
            EXPECT_GT(p.z, 0.0f);
            EXPECT_LT(p.z, 1.0f);
        }
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
    // 발이 창 아래쪽에 있어야 창을 바닥에 맞춰 놓았을 때 땅에 서 있음
    const auto corners = projectCorners(kHumanoid, fitCameraToBounds(kHumanoid, {240, 400}));
    float minY = 1.0f;
    for (const Vec3& p : corners) {
        minY = std::min(minY, p.y);
    }
    EXPECT_LT(minY, -0.9f);
}
