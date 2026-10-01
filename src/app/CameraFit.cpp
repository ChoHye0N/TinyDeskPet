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

    // 상자 앞면(z = max.z)에서 필요한 화면 반높이. 가로가 더 빡빡하면 가로 기준으로 늘림.
    const float halfHeight = std::max(size.y * 0.5f, size.x * 0.5f / aspect) * (1.0f + kMargin);
    const float distance = halfHeight / std::tan(kFovY * 0.5f);

    // 앞면 기준으로 발(min.y)이 화면 아래 여백 위치에 오도록 시선 높이를 정함.
    // 뒤쪽 점들은 원근 때문에 화면 중앙 쪽으로 모이므로 앞면만 맞추면 전부 들어옵니다.
    const float centerX = (bounds.min.x + bounds.max.x) * 0.5f;
    const float centerY = bounds.min.y + halfHeight * (1.0f - kMargin);
    const core::Vec3 target{centerX, centerY, bounds.max.z};
    const core::Vec3 eye{centerX, centerY, bounds.max.z + distance};  // 모델 정면(+Z)에서 바라봄

    const float nearZ = distance * 0.5f;
    const float farZ = distance * 2.0f + size.z;

    const core::Mat4 view = core::lookAtRH(eye, target, {0.0f, 1.0f, 0.0f});
    return view * core::perspectiveFovRH(kFovY, aspect, nearZ, farZ);
}

}  // namespace deskpet::app
