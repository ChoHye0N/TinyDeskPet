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
// 한쪽으로 치우친 상자 (한 손에 든 소품, 비대칭 꼬리 등)
constexpr Bounds kLopsided{{-0.3f, 0.0f, -0.2f}, {1.2f, 1.6f, 0.2f}};

// 발이 닿는 점 (모델 원점 x = 0, 바닥, z = 0)의 NDC
Vec3 feetNdc(const Bounds& b, const Mat4& viewProjection) {
    const auto clip = deskpet::core::transform({0.0f, b.min.y, 0.0f}, viewProjection);
    return {clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
}

}  // namespace

TEST(CameraFit, WholeModelIsInsideViewAndDepthRange) {
    for (const Bounds& bounds : {kHumanoid, kDeep, kLopsided}) {
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
    // (상자 앞쪽 바닥 모서리는 실제 정점이 아니라 상자의 빈 구석이라 조금 내려가도 됨)
    for (const Bounds& bounds : {kHumanoid, kDeep, kLopsided}) {
        const auto corners = projectCorners(bounds, fitCameraToBounds(bounds, {240, 400}));
        for (std::size_t i = 0; i < 8; ++i) {
            if ((i & 4U) == 0) {  // min.z(발 평면 뒤쪽) 꼭짓점만. 순서는 projectCorners와 같음
                EXPECT_GE(corners[i].y, -1.0f - 1e-4f);
            }
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

TEST(CameraFit, FeetTouchBottomOfView) {
    // 창 바닥 = 작업 표시줄 위. 아래 여백이 있으면 발이 떠 보임.
    // 앞뒤로 깊은 모델이라도 마찬가지 (상자 앞면 기준으로 맞추면 발이 위로 뜸)
    for (const Bounds& bounds : {kHumanoid, kDeep, kLopsided}) {
        EXPECT_NEAR(feetNdc(bounds, fitCameraToBounds(bounds, {240, 400})).y, -1.0f, 1e-4f);
    }
}

TEST(CameraFit, FeetAreHorizontallyCenteredEvenForLopsidedBounds) {
    // 앱은 발이 창 가로 중앙이라고 보고 창 위치·벽을 계산함. 상자 중앙에 맞추면 치우친 모델이
    // 한쪽으로 밀려 그려져 화면 끝과 어긋남
    for (const SizeI viewport : {SizeI{240, 400}, SizeI{400, 200}}) {
        EXPECT_NEAR(feetNdc(kLopsided, fitCameraToBounds(kLopsided, viewport)).x, 0.0f, 1e-4f);
    }
}
