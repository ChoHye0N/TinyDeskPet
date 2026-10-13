#pragma once

// 휴머노이드 모션 클립 (VRMA·glTF, VMD, FBX에서 읽음, 플랫폼 독립). 명세:
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

// Gltf: VRMC_vrm_animation이 없는 일반 glTF 애니메이션 (본 이름으로 매핑, ADR-0015).
// 판별(detectMotionFormat)은 파싱 전이라 glTF 계열을 모두 Vrma로 돌려줌
enum class MotionFormat : std::uint8_t { Unknown, Vrma, Vmd, Fbx, Gltf };

// 휴머노이드가 아닌 본(머리카락·꼬리·옷 등)의 트랙. 이런 본은 모델마다 달라서 재생할 때 대상
// 모델에서 이름이 같은 본에만 적용하고, 없으면 버림 (ADR-0014). 값은 휴머노이드 트랙과 같은 월드
// 델타
struct NamedTrack {
    std::string name;
    std::vector<core::Quat> rotations;
    // 프레임별 이동 변화 (ADR-0015): 로컬 이동 − 기본 자세 로컬 이동을 부모의 기본 자세 축(모델
    // 축)으로 바꾼 값, 원본 단위. 비어 있으면 이동 없음 (눈꺼풀·눈썹처럼 이동 키로 움직이는 본)
    std::vector<core::Vec3> offsets;
};

// 모프 가중치 트랙 (glTF weights 채널, ADR-0015). 대상 모델에서 노드 이름과 타깃 번호가 같은
// 모프(Model::morphs)에 적용
struct WeightTrack {
    std::string name;
    std::size_t target = 0;
    std::vector<float> weights;  // 프레임별
};

struct MotionClip {
    static constexpr float kFramesPerSecond = 30.0f;

    MotionFormat format = MotionFormat::Unknown;
    std::size_t frameCount = 0;  // 프레임 수 (1이면 정지 자세 하나)
    // tracks[HumanBone]: 프레임별 월드 델타. 비어 있으면 그 본은 부모를 따라감
    std::array<std::vector<core::Quat>, kHumanBoneCount> tracks;
    // 원본 기본 자세에서 팔다리 본 → limbChild 본 방향 (단위 벡터). 0이면 모름 (보정 안 함)
    std::array<core::Vec3, kHumanBoneCount> restDirections{};
    // 부모와 다르게 움직이는 휴머노이드가 아닌 본만 (부모를 그대로 따라가는 본은 없어도 같음)
    std::vector<NamedTrack> boneTracks;
    // 엉덩이(hips) 위치 변화 (ADR-0014): 프레임별 "원본 기본 자세 → 그 시각" (모델 축, 원본 단위).
    // 비어 있으면 위치를 쓰지 않음. 재생할 때 대상 엉덩이 높이 / hipsHeight 배로 맞추므로 단위는
    // 원본 그대로 둠 (VMD는 MMD 단위)
    std::vector<core::Vec3> hipsTranslation;
    // 원본 기본 자세의 엉덩이 높이 (원본 단위, 바닥 = 0). 엉덩이 이동·이름 본 이동을 대상 크기에
    // 맞추는 기준. 0이면 모름
    float hipsHeight = 0.0f;
    std::vector<WeightTrack> weightTracks;
    // 클립 내내 크기가 0.1배 미만인 휴머노이드가 아닌 본 이름 (ADR-0015). 게임은 소품(총알 등)을
    // 크기 0으로 줄여 숨기므로, 이 클립을 재생하는 동안 이름이 같은 본과 그 아래를 접어 숨김.
    // glTF만 읽음
    std::vector<std::string> hiddenBones;
    // 처음 한 번 끝까지 재생한 뒤 반복을 시작할 시각 (s). 0이면 처음부터 반복, 길이 이상이면 마지막
    // 자세 유지. 파일이 아니라 설정([animation] *_loop_start)에서 옴
    float loopStart = 0.0f;

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

// clipName: 파일 안 애니메이션 이름 (glTF animations[].name, FBX 애니메이션 스택 이름).
// 비어 있으면 첫 번째. 없는 이름이면 있는 이름을 나열한 오류. VMD는 이름을 줄 수 없음
[[nodiscard]] MotionLoadResult loadMotionFromMemory(std::span<const std::uint8_t> bytes,
                                                    std::string_view extension = {},
                                                    std::string_view clipName = {});

// "파일#클립 이름" 형식이면 그 파일의 그 클립 (예: models/model.glb#Idle).
// 경로 전체가 있는 파일이면 '#'도 파일 이름으로 봄
[[nodiscard]] MotionLoadResult loadMotionFile(const std::filesystem::path& reference);

// 원본 뼈대 → 클립 변환을 돕는 공통 부분 (형식별 로더가 사용)
namespace motion {

// 기본 자세의 휴머노이드 본 위치 (모델 공간)로 restDirections를 채움
void setRestDirections(MotionClip& clip,
                       const std::array<std::optional<core::Vec3>, kHumanBoneCount>& positions);

// 원본 노드 전체의 프레임별 월드 델타(deltas[노드])에서 휴머노이드가 아니고, 이름이 있고, 부모와
// 다르게 움직이는 노드를 boneTracks에 추가. 같은 이름이 또 나오면 처음 것만
struct SourceNode {
    std::string_view name;
    int parent = -1;  // 노드 번호, 없으면 -1
    bool humanoid = false;
};
// offsets[노드]: 프레임별 이동 변화 (NamedTrack::offsets와 같은 규약, 비어 있으면 없음). 이동만
// 하는 노드도 트랙이 됨
void addBoneTracks(MotionClip& clip, std::span<const SourceNode> nodes,
                   const std::vector<std::vector<core::Quat>>& deltas,
                   const std::vector<std::vector<core::Vec3>>& offsets = {});

// 엉덩이의 프레임별 위치(원본 단위)와 기본 자세 위치로 hipsTranslation·hipsHeight를 채움.
// 한 번도 움직이지 않으면 비워 둠 (재생할 때 위치 계산을 건너뜀)
void setHipsTranslation(MotionClip& clip, std::span<const core::Vec3> positions, core::Vec3 rest);

}  // namespace motion

}  // namespace deskpet::model
