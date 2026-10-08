#pragma once

// 형식별 로더 (model 모듈 내부 전용). 진입점은 ModelLoader.h (ADR-0009)
// 각 로더는 결과를 Model.h의 공통 규약(미터, 오른손, +Y 위, 정면 +Z, CCW 앞면)으로 맞춰 반환합니다.

#include "model/ModelLoader.h"
#include "model/Motion.h"

#include <cstdint>
#include <span>

namespace deskpet::model::detail {

[[nodiscard]] LoadResult importGltf(std::span<const std::uint8_t> bytes,
                                    const ImportContext& context);
[[nodiscard]] LoadResult importPmx(std::span<const std::uint8_t> bytes,
                                   const ImportContext& context);
[[nodiscard]] LoadResult importFbx(std::span<const std::uint8_t> bytes,
                                   const ImportContext& context);

// 모션 (Motion.h의 MotionClip 규약으로 변환, ADR-0013)
[[nodiscard]] MotionLoadResult importVrma(std::span<const std::uint8_t> bytes);
[[nodiscard]] MotionLoadResult importVmd(std::span<const std::uint8_t> bytes);
[[nodiscard]] MotionLoadResult importFbxMotion(std::span<const std::uint8_t> bytes);

}  // namespace deskpet::model::detail
