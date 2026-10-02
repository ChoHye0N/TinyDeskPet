#include "app/CameraFit.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace deskpet::app {
namespace {

// 좁은 화각(망원)일수록 원근 왜곡이 적어 데스크톱 펫처럼 "납작하게" 보입니다.
constexpr float kFovY = 20.0f * std::numbers::pi_v<float> / 180.0f;
constexpr float kMargin = 0.02f;  // 화면 가장자리 여백 (반높이 대비 비율)

}  // namespace

core::Mat4 fitCameraToBounds(const model::Bounds& bounds, core::SizeI viewport) {
    const float aspect = static_cast<float>(std::max(viewport.width, 1)) /
                         static_cast<float>(std::max(viewport.height, 1));
    const core::Vec3 size = bounds.size();
    const float tanHalf = std::tan(kFovY * 0.5f);
    const float usable = 1.0f - kMargin;               // 가장자리 여백을 뺀 NDC 범위
    const float front = std::max(bounds.max.z, 0.0f);  // 발 평면(z = 0)보다 앞으로 나온 깊이

    // 기준 평면은 발이 닿는 z = 0 (모델 원점이 발 중앙). 이 평면에서 화면 반높이를 H, 카메라
    // 거리를 D = H / tan이라 하면, 앞으로 front만큼 나온 점은 D / (D − front)배 커 보임.
    // 축에서 u 떨어진 점이 들어오려면 u·D ≤ H·usable·(D − front) → u ≤ usable·(H − front·tan)
    // 상자 앞면에 맞추면 깊은 모델(돌린 꼬리 등)일수록 z = 0의 발이 원근 때문에 위로 뜸
    const float forHeight = size.y / (2.0f * usable) + front * tanHalf * 0.5f;
    const float forWidth = size.x * 0.5f / (aspect * usable) + front * tanHalf;
    const float halfHeight = std::max(forHeight, forWidth);
    const float distance = halfHeight / tanHalf;

    // 발(min.y, z = 0)이 화면 아래 여백 위치에 오도록 시선 높이를 정함
    const float centerX = (bounds.min.x + bounds.max.x) * 0.5f;
    const float centerY = bounds.min.y + halfHeight * usable;
    const core::Vec3 target{centerX, centerY, 0.0f};
    const core::Vec3 eye{centerX, centerY, distance};  // 모델 정면(+Z)에서 바라봄

    const float nearZ = (distance - front) * 0.5f;
    const float farZ = (distance - std::min(bounds.min.z, 0.0f)) * 2.0f;

    const core::Mat4 view = core::lookAtRH(eye, target, {0.0f, 1.0f, 0.0f});
    return view * core::perspectiveFovRH(kFovY, aspect, nearZ, farZ);
}

}  // namespace deskpet::app
