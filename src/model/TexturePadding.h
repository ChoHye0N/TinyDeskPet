#pragma once

// 텍스처 패딩: UV 섬 바깥 여백을 섬 가장자리 색으로 채웁니다.
// 명세: docs/03-detailed-design/model.md §4.8
//
// 왜 필요한가: 텍스처는 그림 조각(UV 섬)과 그 사이 여백(보통 검정)으로 되어 있습니다. 샘플링할 때
// 주변 텍셀을 섞는 선형 필터·밉맵·축소 때문에 섬 가장자리에서 여백 색이 섞여 들어오고, 조각끼리
// 맞닿는 이음매(몸통 중앙, 입 테두리 등)에 얇은 선이 생깁니다. 여백을 섬 색으로 미리 채워 두면
// 섞여도 같은 색이라 선이 생기지 않습니다 (게임 엔진이 텍스처를 구울 때 하는 표준 처리).

#include "core/Math.h"
#include "model/Model.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace deskpet::model {

using UvTriangle = std::array<core::Vec2, 3>;

// rgba: width × height × 4 (RGBA8, 왼쪽 위 원점). triangles: 이 텍스처를 쓰는 삼각형의 UV.
// 섬에 덮이지 않은 텍셀을 maxDistance 픽셀까지 이웃 섬 색의 평균으로 채웁니다.
// UV는 반복(REPEAT)으로 해석합니다. 삼각형이 없으면 아무것도 하지 않습니다.
void padUvIslands(std::span<std::uint8_t> rgba, int width, int height,
                  std::span<const UvTriangle> triangles, int maxDistance);

// baseColorTexture가 textureIndex인 머티리얼로 그리는 삼각형들의 UV
[[nodiscard]] std::vector<UvTriangle> collectUvTriangles(const Model& model, int textureIndex);

}  // namespace deskpet::model
