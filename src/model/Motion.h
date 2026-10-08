#pragma once

// 휴머노이드 모션 클립 (VRMA, VMD, FBX에서 읽음, 플랫폼 독립). 명세:
// docs/03-detailed-design/model.md §4.9, ADR-0013
//
// 형식마다 다른 뼈대·좌표계·보간을 읽을 때 한 가지로 통일합니다:
//   - 30fps로 다시 샘플링 (VMD 베지어, glTF 선형·계단 보간은 읽을 때 계산)
//   - 본마다 "원본 기본 자세 → 그 시각"의 모델 공간 회전 (월드 델타). 공통 규약(오른손, +Y 위,
//     정면 +Z) 기준. 로컬 회전이 아니라서 원본 뼈대 구조(중간 본, 본 축 방향)를 몰라도 됨
//   - 원본 기본 자세의 팔다리 방향 → 재생할 때 대상 모델의 기본 자세(T포즈·A포즈) 차이를 보정

#include "core/Math3D.h"
#include "model/Humanoid.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace deskpet::model {

inline constexpr std::size_t kHumanBoneCount = static_cast<std::size_t>(HumanBone::RightToes) + 1;

enum class MotionFormat : std::uint8_t { Unknown, Vrma, Vmd, Fbx };

struct MotionClip {
    static constexpr float kFramesPerSecond = 30.0f;

    MotionFormat format = MotionFormat::Unknown;
    std::size_t frameCount = 0;  // 프레임 수 (1이면 정지 자세 하나)
    // tracks[HumanBone]: 프레임별 월드 델타. 비어 있으면 그 본은 부모를 따라감
    std::array<std::vector<core::Quat>, kHumanBoneCount> tracks;
    // 원본 기본 자세에서 팔다리 본 → limbChild 본 방향 (단위 벡터). 0이면 모름 (보정 안 함)
    std::array<core::Vec3, kHumanBoneCount> restDirections{};

    [[nodiscard]] float duration() const noexcept {
        return frameCount > 1 ? static_cast<float>(frameCount - 1) / kFramesPerSecond : 0.0f;
    }
    [[nodiscard]] const std::vector<core::Quat>& track(HumanBone bone) const noexcept {
        return tracks[static_cast<std::size_t>(bone)];
    }
    [[nodiscard]] std::vector<core::Quat>& track(HumanBone bone) noexcept {
        return tracks[static_cast<std::size_t>(bone)];
    }
    [[nodiscard]] std::size_t trackCount() const noexcept;
};

struct MotionLoadResult {
    std::optional<MotionClip> clip;  // 실패하면 비어 있음
    std::string error;
};

// 매직 바이트로 판별: "Vocaloid Motion Data" = VMD, glTF/GLB = VRMA, FBX
[[nodiscard]] MotionFormat detectMotionFormat(std::span<const std::uint8_t> bytes,
                                              std::string_view extension = {});

[[nodiscard]] MotionLoadResult loadMotionFromMemory(std::span<const std::uint8_t> bytes,
                                                    std::string_view extension = {});

[[nodiscard]] MotionLoadResult loadMotionFile(const std::filesystem::path& path);

// 원본 뼈대 → 클립 변환을 돕는 공통 부분 (형식별 로더가 사용)
namespace motion {

// 기본 자세의 휴머노이드 본 위치 (모델 공간)로 restDirections를 채움
void setRestDirections(MotionClip& clip,
                       const std::array<std::optional<core::Vec3>, kHumanBoneCount>& positions);

}  // namespace motion

}  // namespace deskpet::model
