#pragma once

// 발 아래 바닥 높이 계산 (다른 창 위를 걷기, docs/03-detailed-design/app.md §4.9). 플랫폼 독립

#include "core/Math.h"

#include <span>

namespace deskpet::app {

// 발 아래의 가장 높은 바닥 (화면 y, 아래가 +).
//   floor: 작업 영역 바닥 (작업 표시줄 위). 이보다 낮은 바닥은 없음
//   windows: 다른 창들의 화면 사각형, 앞(z 순서 위) → 뒤. 발 x를 지나는 윗변이 바닥 후보
//   stepUp: 발보다 이만큼 위에 있는 윗변까지 후보 (서 있는 창이 조금 올라가면 따라감).
//           0이면 발 아래만 → 창 앞에 있다가 떨어져도 그 창 위로 순간 이동하지 않음
// 더 앞 창에 가려진 윗변(그 지점 바로 위가 앞 창 안)은 설 수 없음
[[nodiscard]] float groundBelow(core::Vec2 feet, float stepUp, float floor,
                                std::span<const core::RectI> windows);

}  // namespace deskpet::app
