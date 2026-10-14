#pragma once

// 모델 경계 상자를 창에 맞추는 카메라 (docs/03-detailed-design/app.md §4.7)

#include "core/Math.h"
#include "core/Math3D.h"
#include "model/Model.h"

namespace deskpet::app {

// 정면(+Z)에서 바라보는 원근 카메라의 뷰×투영 행렬. 크기는 fit(서 있는 자세)으로 정하고, 발
// (x = 0, fit.min.y, z = 0)이 화면 아래 가운데에 오도록 맞춥니다 (아래 여백 없음). 모델 원점이
// 발 중앙이라고 가정합니다. extent(모든 동작의 자세)는 깊이 범위에만 씀: 크기를 extent로 정하면
// 클립을 바꿀 때마다 캐릭터 크기가 변함. extent에서 fit 밖으로 나가는 부분은 화면 상자 밖에 그려짐
[[nodiscard]] core::Mat4 fitCameraToBounds(const model::Bounds& fit, core::SizeI viewport,
                                           const model::Bounds& extent);

[[nodiscard]] inline core::Mat4 fitCameraToBounds(const model::Bounds& bounds,
                                                  core::SizeI viewport) {
    return fitCameraToBounds(bounds, viewport, bounds);
}

}  // namespace deskpet::app
