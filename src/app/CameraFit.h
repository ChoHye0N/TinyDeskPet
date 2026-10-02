#pragma once

// 모델 경계 상자를 창에 맞추는 카메라 (docs/03-detailed-design/app.md §4.7)

#include "core/Math.h"
#include "core/Math3D.h"
#include "model/Model.h"

namespace deskpet::app {

// 정면(+Z)에서 바라보는 원근 카메라의 뷰×투영 행렬. 발(min.y, z = 0)이 화면 아래쪽에
// 오도록 세로 방향은 아래 정렬, 가로는 가운데 정렬합니다. 모델 원점이 발 중앙이라고 가정합니다.
[[nodiscard]] core::Mat4 fitCameraToBounds(const model::Bounds& bounds, core::SizeI viewport);

}  // namespace deskpet::app
