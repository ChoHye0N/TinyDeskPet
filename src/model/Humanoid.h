#pragma once

// 형식마다 다른 본 이름을 하나의 휴머노이드 본으로 통일합니다 (ADR-0009).
// 애니메이션(M4)은 이 이름으로 본을 찾으므로, 모델 형식과 무관하게 같은 코드로 움직입니다.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace deskpet::model {

// VRM 1.0 humanBones (턱 제외). 손가락은 엄지가 metacarpal·proximal·distal, 나머지가
// proximal·intermediate·distal (VRM 0.x 엄지 proximal·intermediate는 읽을 때 한 칸씩 옮김)
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
    LeftThumbMetacarpal,
    LeftThumbProximal,
    LeftThumbDistal,
    LeftIndexProximal,
    LeftIndexIntermediate,
    LeftIndexDistal,
    LeftMiddleProximal,
    LeftMiddleIntermediate,
    LeftMiddleDistal,
    LeftRingProximal,
    LeftRingIntermediate,
    LeftRingDistal,
    LeftLittleProximal,
    LeftLittleIntermediate,
    LeftLittleDistal,
    RightThumbMetacarpal,
    RightThumbProximal,
    RightThumbDistal,
    RightIndexProximal,
    RightIndexIntermediate,
    RightIndexDistal,
    RightMiddleProximal,
    RightMiddleIntermediate,
    RightMiddleDistal,
    RightRingProximal,
    RightRingIntermediate,
    RightRingDistal,
    RightLittleProximal,
    RightLittleIntermediate,
    RightLittleDistal,
};

inline constexpr std::size_t kHumanBoneCount =
    static_cast<std::size_t>(HumanBone::RightLittleDistal) + 1;

// VRM 이름 ("leftUpperArm" 등). None이면 "none"
[[nodiscard]] std::string_view toString(HumanBone bone);

// VRM 1.0 humanBones 키 ("hips", "leftUpperArm", "leftThumbMetacarpal" …). 대소문자 구분
[[nodiscard]] HumanBone humanBoneFromVrmName(std::string_view name);

// VRM 0.x humanBones 키. 엄지만 1.0과 이름이 한 칸씩 어긋남:
// 0.x thumbProximal → 1.0 thumbMetacarpal, 0.x thumbIntermediate → 1.0 thumbProximal
[[nodiscard]] HumanBone humanBoneFromVrm0Name(std::string_view name);

// MMD 표준 본 이름 (UTF-8, "下半身", "左腕", "左人指１" …)
[[nodiscard]] HumanBone humanBoneFromMmdName(std::string_view name);

// FBX 관례: Mixamo ("mixamorig:LeftArm", "LeftArm" …, ':' 앞 접두사는 무시),
// 3ds Max Biped ("Bip001 L UpperArm", "Bip01_L_UpperArm" …)
[[nodiscard]] HumanBone humanBoneFromFbxName(std::string_view name);

// 팔다리 본의 다음 본 (어깨 → 위팔 → 아래팔 → 손, 허벅지 → 정강이 → 발 → 발가락, 손가락 마디).
// 기본 자세의 본 방향을 잴 때 씀 (모션 리타기팅). 몸통·머리·손·손가락 끝은 None
[[nodiscard]] HumanBone limbChild(HumanBone bone);

}  // namespace deskpet::model
