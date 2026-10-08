#include "model/Humanoid.h"

#include <array>
#include <utility>

namespace deskpet::model {
namespace {

using Entry = std::pair<std::string_view, HumanBone>;

constexpr std::array kVrmNames = {
    Entry{"hips", HumanBone::Hips},
    Entry{"spine", HumanBone::Spine},
    Entry{"chest", HumanBone::Chest},
    Entry{"upperChest", HumanBone::UpperChest},
    Entry{"neck", HumanBone::Neck},
    Entry{"head", HumanBone::Head},
    Entry{"leftEye", HumanBone::LeftEye},
    Entry{"rightEye", HumanBone::RightEye},
    Entry{"leftShoulder", HumanBone::LeftShoulder},
    Entry{"leftUpperArm", HumanBone::LeftUpperArm},
    Entry{"leftLowerArm", HumanBone::LeftLowerArm},
    Entry{"leftHand", HumanBone::LeftHand},
    Entry{"rightShoulder", HumanBone::RightShoulder},
    Entry{"rightUpperArm", HumanBone::RightUpperArm},
    Entry{"rightLowerArm", HumanBone::RightLowerArm},
    Entry{"rightHand", HumanBone::RightHand},
    Entry{"leftUpperLeg", HumanBone::LeftUpperLeg},
    Entry{"leftLowerLeg", HumanBone::LeftLowerLeg},
    Entry{"leftFoot", HumanBone::LeftFoot},
    Entry{"leftToes", HumanBone::LeftToes},
    Entry{"rightUpperLeg", HumanBone::RightUpperLeg},
    Entry{"rightLowerLeg", HumanBone::RightLowerLeg},
    Entry{"rightFoot", HumanBone::RightFoot},
    Entry{"rightToes", HumanBone::RightToes},
};

// MMD 표준 본. "センター"는 몸 전체 이동용이라 휴머노이드의 hips가 아니라 "下半身"이 hips에 해당
constexpr std::array kMmdNames = {
    Entry{"下半身", HumanBone::Hips},
    Entry{"上半身", HumanBone::Spine},
    Entry{"上半身2", HumanBone::Chest},
    Entry{"首", HumanBone::Neck},
    Entry{"頭", HumanBone::Head},
    Entry{"左目", HumanBone::LeftEye},
    Entry{"右目", HumanBone::RightEye},
    Entry{"左肩", HumanBone::LeftShoulder},
    Entry{"左腕", HumanBone::LeftUpperArm},
    Entry{"左ひじ", HumanBone::LeftLowerArm},
    Entry{"左手首", HumanBone::LeftHand},
    Entry{"右肩", HumanBone::RightShoulder},
    Entry{"右腕", HumanBone::RightUpperArm},
    Entry{"右ひじ", HumanBone::RightLowerArm},
    Entry{"右手首", HumanBone::RightHand},
    Entry{"左足", HumanBone::LeftUpperLeg},
    Entry{"左ひざ", HumanBone::LeftLowerLeg},
    Entry{"左足首", HumanBone::LeftFoot},
    Entry{"左つま先", HumanBone::LeftToes},
    Entry{"右足", HumanBone::RightUpperLeg},
    Entry{"右ひざ", HumanBone::RightLowerLeg},
    Entry{"右足首", HumanBone::RightFoot},
    Entry{"右つま先", HumanBone::RightToes},
};

// Mixamo 이름. Spine1/Spine2는 Mixamo 관례상 chest/upperChest
constexpr std::array kFbxNames = {
    Entry{"Hips", HumanBone::Hips},
    Entry{"Spine", HumanBone::Spine},
    Entry{"Spine1", HumanBone::Chest},
    Entry{"Spine2", HumanBone::UpperChest},
    Entry{"Neck", HumanBone::Neck},
    Entry{"Head", HumanBone::Head},
    Entry{"LeftEye", HumanBone::LeftEye},
    Entry{"RightEye", HumanBone::RightEye},
    Entry{"LeftShoulder", HumanBone::LeftShoulder},
    Entry{"LeftArm", HumanBone::LeftUpperArm},
    Entry{"LeftForeArm", HumanBone::LeftLowerArm},
    Entry{"LeftHand", HumanBone::LeftHand},
    Entry{"RightShoulder", HumanBone::RightShoulder},
    Entry{"RightArm", HumanBone::RightUpperArm},
    Entry{"RightForeArm", HumanBone::RightLowerArm},
    Entry{"RightHand", HumanBone::RightHand},
    Entry{"LeftUpLeg", HumanBone::LeftUpperLeg},
    Entry{"LeftLeg", HumanBone::LeftLowerLeg},
    Entry{"LeftFoot", HumanBone::LeftFoot},
    Entry{"LeftToeBase", HumanBone::LeftToes},
    Entry{"RightUpLeg", HumanBone::RightUpperLeg},
    Entry{"RightLeg", HumanBone::RightLowerLeg},
    Entry{"RightFoot", HumanBone::RightFoot},
    Entry{"RightToeBase", HumanBone::RightToes},
};

template <std::size_t N>
HumanBone lookup(const std::array<Entry, N>& table, std::string_view name) {
    for (const auto& [key, bone] : table) {
        if (key == name) {
            return bone;
        }
    }
    return HumanBone::None;
}

}  // namespace

std::string_view toString(HumanBone bone) {
    for (const auto& [key, value] : kVrmNames) {
        if (value == bone) {
            return key;
        }
    }
    return "none";
}

HumanBone humanBoneFromVrmName(std::string_view name) {
    return lookup(kVrmNames, name);
}

HumanBone humanBoneFromMmdName(std::string_view name) {
    return lookup(kMmdNames, name);
}

HumanBone humanBoneFromFbxName(std::string_view name) {
    if (const auto colon = name.rfind(':'); colon != std::string_view::npos) {
        name.remove_prefix(colon + 1);
    }
    return lookup(kFbxNames, name);
}

HumanBone limbChild(HumanBone bone) {
    switch (bone) {
        case HumanBone::LeftShoulder: return HumanBone::LeftUpperArm;
        case HumanBone::LeftUpperArm: return HumanBone::LeftLowerArm;
        case HumanBone::LeftLowerArm: return HumanBone::LeftHand;
        case HumanBone::RightShoulder: return HumanBone::RightUpperArm;
        case HumanBone::RightUpperArm: return HumanBone::RightLowerArm;
        case HumanBone::RightLowerArm: return HumanBone::RightHand;
        case HumanBone::LeftUpperLeg: return HumanBone::LeftLowerLeg;
        case HumanBone::LeftLowerLeg: return HumanBone::LeftFoot;
        case HumanBone::LeftFoot: return HumanBone::LeftToes;
        case HumanBone::RightUpperLeg: return HumanBone::RightLowerLeg;
        case HumanBone::RightLowerLeg: return HumanBone::RightFoot;
        case HumanBone::RightFoot: return HumanBone::RightToes;
        default: return HumanBone::None;
    }
}

}  // namespace deskpet::model
