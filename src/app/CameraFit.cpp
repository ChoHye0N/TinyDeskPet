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
    //
    // 세로: 발(min.y)은 화면 맨 아래(NDC −1, 여백 없음 — 창 바닥이 곧 작업 표시줄 위),
    //       위쪽만 여백. max.y − (min.y + H) ≤ usable·(H − front·tan)
    // 가로: 발(x = 0)이 화면 가운데. 앱이 발 = 창 가로 중앙으로 보고 창 위치·벽을 계산하므로
    //       상자가 한쪽으로 치우쳐도 넓은 쪽 반폭으로 좌우 대칭으로 맞춤
    const float halfWidth = std::max(-bounds.min.x, bounds.max.x);
    const float forHeight = (size.y + usable * front * tanHalf) / (1.0f + usable);
    const float forWidth = halfWidth / (aspect * usable) + front * tanHalf;
    const float halfHeight = std::max(forHeight, forWidth);
    const float distance = halfHeight / tanHalf;

    const float centerY = bounds.min.y + halfHeight;  // 발(min.y, z = 0)이 화면 맨 아래
    const core::Vec3 target{0.0f, centerY, 0.0f};
    const core::Vec3 eye{0.0f, centerY, distance};  // 모델 정면(+Z)에서 바라봄

    const float nearZ = (distance - front) * 0.5f;
    const float farZ = (distance - std::min(bounds.min.z, 0.0f)) * 2.0f;

    const core::Mat4 view = core::lookAtRH(eye, target, {0.0f, 1.0f, 0.0f});
    return view * core::perspectiveFovRH(kFovY, aspect, nearZ, farZ);
}

}  // namespace deskpet::app
