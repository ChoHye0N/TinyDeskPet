#pragma once

// 형식마다 다른 본 이름을 하나의 휴머노이드 본으로 통일합니다 (ADR-0009).
// 애니메이션(M4)은 이 이름으로 본을 찾으므로, 모델 형식과 무관하게 같은 코드로 움직입니다.

#include <cstdint>
#include <string_view>

namespace deskpet::model {

// VRM 1.0 humanBones의 주요 본 (손가락 제외)
enum class HumanBone : std::uint8_t {
    None,
    Hips,
    Spine,
    Chest,
    UpperChest,
    Neck,
    Head,
    LeftEye,
    RightEye,
    LeftShoulder,
    LeftUpperArm,
    LeftLowerArm,
    LeftHand,
    RightShoulder,
    RightUpperArm,
    RightLowerArm,
    RightHand,
    LeftUpperLeg,
    LeftLowerLeg,
    LeftFoot,
    LeftToes,
    RightUpperLeg,
    RightLowerLeg,
    RightFoot,
    RightToes,
};

// VRM 이름 ("leftUpperArm" 등). None이면 "none"
[[nodiscard]] std::string_view toString(HumanBone bone);

// VRM 0.x / 1.0 humanBones 키 ("hips", "leftUpperArm" …). 대소문자 구분
[[nodiscard]] HumanBone humanBoneFromVrmName(std::string_view name);

// MMD 표준 본 이름 (UTF-8, "下半身", "左腕" …)
[[nodiscard]] HumanBone humanBoneFromMmdName(std::string_view name);

// Mixamo 등 FBX 관례 ("mixamorig:LeftArm", "LeftArm" …). ':' 앞 접두사는 무시
[[nodiscard]] HumanBone humanBoneFromFbxName(std::string_view name);

// 팔다리 본의 다음 본 (어깨 → 위팔 → 아래팔 → 손, 허벅지 → 정강이 → 발 → 발가락).
// 기본 자세의 본 방향을 잴 때 씀 (모션 리타기팅). 몸통·머리·손끝은 None
[[nodiscard]] HumanBone limbChild(HumanBone bone);

}  // namespace deskpet::model
