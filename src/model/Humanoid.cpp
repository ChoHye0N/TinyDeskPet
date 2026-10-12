#include "model/Humanoid.h"

#include <algorithm>
#include <array>
#include <string>
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
    Entry{"leftThumbMetacarpal", HumanBone::LeftThumbMetacarpal},
    Entry{"leftThumbProximal", HumanBone::LeftThumbProximal},
    Entry{"leftThumbDistal", HumanBone::LeftThumbDistal},
    Entry{"leftIndexProximal", HumanBone::LeftIndexProximal},
    Entry{"leftIndexIntermediate", HumanBone::LeftIndexIntermediate},
    Entry{"leftIndexDistal", HumanBone::LeftIndexDistal},
    Entry{"leftMiddleProximal", HumanBone::LeftMiddleProximal},
    Entry{"leftMiddleIntermediate", HumanBone::LeftMiddleIntermediate},
    Entry{"leftMiddleDistal", HumanBone::LeftMiddleDistal},
    Entry{"leftRingProximal", HumanBone::LeftRingProximal},
    Entry{"leftRingIntermediate", HumanBone::LeftRingIntermediate},
    Entry{"leftRingDistal", HumanBone::LeftRingDistal},
    Entry{"leftLittleProximal", HumanBone::LeftLittleProximal},
    Entry{"leftLittleIntermediate", HumanBone::LeftLittleIntermediate},
    Entry{"leftLittleDistal", HumanBone::LeftLittleDistal},
    Entry{"rightThumbMetacarpal", HumanBone::RightThumbMetacarpal},
    Entry{"rightThumbProximal", HumanBone::RightThumbProximal},
    Entry{"rightThumbDistal", HumanBone::RightThumbDistal},
    Entry{"rightIndexProximal", HumanBone::RightIndexProximal},
    Entry{"rightIndexIntermediate", HumanBone::RightIndexIntermediate},
    Entry{"rightIndexDistal", HumanBone::RightIndexDistal},
    Entry{"rightMiddleProximal", HumanBone::RightMiddleProximal},
    Entry{"rightMiddleIntermediate", HumanBone::RightMiddleIntermediate},
    Entry{"rightMiddleDistal", HumanBone::RightMiddleDistal},
    Entry{"rightRingProximal", HumanBone::RightRingProximal},
    Entry{"rightRingIntermediate", HumanBone::RightRingIntermediate},
    Entry{"rightRingDistal", HumanBone::RightRingDistal},
    Entry{"rightLittleProximal", HumanBone::RightLittleProximal},
    Entry{"rightLittleIntermediate", HumanBone::RightLittleIntermediate},
    Entry{"rightLittleDistal", HumanBone::RightLittleDistal},
};

// MMD 표준 본. "センター"는 몸 전체 이동용이라 휴머노이드의 hips가 아니라 "下半身"이 hips에 해당.
// 손가락 숫자는 전각. 엄지 "親指０"(손바닥 쪽 첫 마디)는 준표준이라 없는 모델도 많음
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
    Entry{"左親指０", HumanBone::LeftThumbMetacarpal},
    Entry{"左親指１", HumanBone::LeftThumbProximal},
    Entry{"左親指２", HumanBone::LeftThumbDistal},
    Entry{"左人指１", HumanBone::LeftIndexProximal},
    Entry{"左人指２", HumanBone::LeftIndexIntermediate},
    Entry{"左人指３", HumanBone::LeftIndexDistal},
    Entry{"左中指１", HumanBone::LeftMiddleProximal},
    Entry{"左中指２", HumanBone::LeftMiddleIntermediate},
    Entry{"左中指３", HumanBone::LeftMiddleDistal},
    Entry{"左薬指１", HumanBone::LeftRingProximal},
    Entry{"左薬指２", HumanBone::LeftRingIntermediate},
    Entry{"左薬指３", HumanBone::LeftRingDistal},
    Entry{"左小指１", HumanBone::LeftLittleProximal},
    Entry{"左小指２", HumanBone::LeftLittleIntermediate},
    Entry{"左小指３", HumanBone::LeftLittleDistal},
    Entry{"右親指０", HumanBone::RightThumbMetacarpal},
    Entry{"右親指１", HumanBone::RightThumbProximal},
    Entry{"右親指２", HumanBone::RightThumbDistal},
    Entry{"右人指１", HumanBone::RightIndexProximal},
    Entry{"右人指２", HumanBone::RightIndexIntermediate},
    Entry{"右人指３", HumanBone::RightIndexDistal},
    Entry{"右中指１", HumanBone::RightMiddleProximal},
    Entry{"右中指２", HumanBone::RightMiddleIntermediate},
    Entry{"右中指３", HumanBone::RightMiddleDistal},
    Entry{"右薬指１", HumanBone::RightRingProximal},
    Entry{"右薬指２", HumanBone::RightRingIntermediate},
    Entry{"右薬指３", HumanBone::RightRingDistal},
    Entry{"右小指１", HumanBone::RightLittleProximal},
    Entry{"右小指２", HumanBone::RightLittleIntermediate},
    Entry{"右小指３", HumanBone::RightLittleDistal},
};

// Mixamo 이름. Spine1/Spine2는 Mixamo 관례상 chest/upperChest. 손가락은 손 쪽부터 1, 2, 3
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
    Entry{"LeftHandThumb1", HumanBone::LeftThumbMetacarpal},
    Entry{"LeftHandThumb2", HumanBone::LeftThumbProximal},
    Entry{"LeftHandThumb3", HumanBone::LeftThumbDistal},
    Entry{"LeftHandIndex1", HumanBone::LeftIndexProximal},
    Entry{"LeftHandIndex2", HumanBone::LeftIndexIntermediate},
    Entry{"LeftHandIndex3", HumanBone::LeftIndexDistal},
    Entry{"LeftHandMiddle1", HumanBone::LeftMiddleProximal},
    Entry{"LeftHandMiddle2", HumanBone::LeftMiddleIntermediate},
    Entry{"LeftHandMiddle3", HumanBone::LeftMiddleDistal},
    Entry{"LeftHandRing1", HumanBone::LeftRingProximal},
    Entry{"LeftHandRing2", HumanBone::LeftRingIntermediate},
    Entry{"LeftHandRing3", HumanBone::LeftRingDistal},
    Entry{"LeftHandPinky1", HumanBone::LeftLittleProximal},
    Entry{"LeftHandPinky2", HumanBone::LeftLittleIntermediate},
    Entry{"LeftHandPinky3", HumanBone::LeftLittleDistal},
    Entry{"RightHandThumb1", HumanBone::RightThumbMetacarpal},
    Entry{"RightHandThumb2", HumanBone::RightThumbProximal},
    Entry{"RightHandThumb3", HumanBone::RightThumbDistal},
    Entry{"RightHandIndex1", HumanBone::RightIndexProximal},
    Entry{"RightHandIndex2", HumanBone::RightIndexIntermediate},
    Entry{"RightHandIndex3", HumanBone::RightIndexDistal},
    Entry{"RightHandMiddle1", HumanBone::RightMiddleProximal},
    Entry{"RightHandMiddle2", HumanBone::RightMiddleIntermediate},
    Entry{"RightHandMiddle3", HumanBone::RightMiddleDistal},
    Entry{"RightHandRing1", HumanBone::RightRingProximal},
    Entry{"RightHandRing2", HumanBone::RightRingIntermediate},
    Entry{"RightHandRing3", HumanBone::RightRingDistal},
    Entry{"RightHandPinky1", HumanBone::RightLittleProximal},
    Entry{"RightHandPinky2", HumanBone::RightLittleIntermediate},
    Entry{"RightHandPinky3", HumanBone::RightLittleDistal},
};

// 3ds Max Biped ("Bip001 L Thigh"의 "Bip001 " 뒤). 게임에서 추출한 모델에 흔함.
// 루트 "Bip001"은 몸 전체 이동용이라 hips가 아니라 "Pelvis"가 hips. Spine1/Spine2는 Mixamo와 같게.
// 손가락은 "Finger" + 손가락 번호(0 = 엄지 … 4 = 새끼) + 마디 번호(첫 마디는 없음)
constexpr std::array kBipedNames = {
    Entry{"Pelvis", HumanBone::Hips},
    Entry{"Spine", HumanBone::Spine},
    Entry{"Spine1", HumanBone::Chest},
    Entry{"Spine2", HumanBone::UpperChest},
    Entry{"Neck", HumanBone::Neck},
    Entry{"Head", HumanBone::Head},
    Entry{"L Clavicle", HumanBone::LeftShoulder},
    Entry{"L UpperArm", HumanBone::LeftUpperArm},
    Entry{"L Forearm", HumanBone::LeftLowerArm},
    Entry{"L Hand", HumanBone::LeftHand},
    Entry{"R Clavicle", HumanBone::RightShoulder},
    Entry{"R UpperArm", HumanBone::RightUpperArm},
    Entry{"R Forearm", HumanBone::RightLowerArm},
    Entry{"R Hand", HumanBone::RightHand},
    Entry{"L Thigh", HumanBone::LeftUpperLeg},
    Entry{"L Calf", HumanBone::LeftLowerLeg},
    Entry{"L Foot", HumanBone::LeftFoot},
    Entry{"L Toe0", HumanBone::LeftToes},
    Entry{"R Thigh", HumanBone::RightUpperLeg},
    Entry{"R Calf", HumanBone::RightLowerLeg},
    Entry{"R Foot", HumanBone::RightFoot},
    Entry{"R Toe0", HumanBone::RightToes},
    Entry{"L Finger0", HumanBone::LeftThumbMetacarpal},
    Entry{"L Finger01", HumanBone::LeftThumbProximal},
    Entry{"L Finger02", HumanBone::LeftThumbDistal},
    Entry{"L Finger1", HumanBone::LeftIndexProximal},
    Entry{"L Finger11", HumanBone::LeftIndexIntermediate},
    Entry{"L Finger12", HumanBone::LeftIndexDistal},
    Entry{"L Finger2", HumanBone::LeftMiddleProximal},
    Entry{"L Finger21", HumanBone::LeftMiddleIntermediate},
    Entry{"L Finger22", HumanBone::LeftMiddleDistal},
    Entry{"L Finger3", HumanBone::LeftRingProximal},
    Entry{"L Finger31", HumanBone::LeftRingIntermediate},
    Entry{"L Finger32", HumanBone::LeftRingDistal},
    Entry{"L Finger4", HumanBone::LeftLittleProximal},
    Entry{"L Finger41", HumanBone::LeftLittleIntermediate},
    Entry{"L Finger42", HumanBone::LeftLittleDistal},
    Entry{"R Finger0", HumanBone::RightThumbMetacarpal},
    Entry{"R Finger01", HumanBone::RightThumbProximal},
    Entry{"R Finger02", HumanBone::RightThumbDistal},
    Entry{"R Finger1", HumanBone::RightIndexProximal},
    Entry{"R Finger11", HumanBone::RightIndexIntermediate},
    Entry{"R Finger12", HumanBone::RightIndexDistal},
    Entry{"R Finger2", HumanBone::RightMiddleProximal},
    Entry{"R Finger21", HumanBone::RightMiddleIntermediate},
    Entry{"R Finger22", HumanBone::RightMiddleDistal},
    Entry{"R Finger3", HumanBone::RightRingProximal},
    Entry{"R Finger31", HumanBone::RightRingIntermediate},
    Entry{"R Finger32", HumanBone::RightRingDistal},
    Entry{"R Finger4", HumanBone::RightLittleProximal},
    Entry{"R Finger41", HumanBone::RightLittleIntermediate},
    Entry{"R Finger42", HumanBone::RightLittleDistal},
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

// "Bip" + 숫자 + 공백(또는 '_') 접두사를 떼고 Biped 표에서 찾음. 내보내기 도구에 따라 이름의
// 공백이 '_'로 바뀌므로 ("Bip001_L_Thigh") 공백으로 되돌려 비교
HumanBone lookupBiped(std::string_view name) {
    constexpr std::string_view kPrefix = "Bip";
    if (!name.starts_with(kPrefix)) {
        return HumanBone::None;
    }
    std::size_t pos = kPrefix.size();
    while (pos < name.size() && name[pos] >= '0' && name[pos] <= '9') {
        ++pos;
    }
    if (pos == kPrefix.size() || pos >= name.size() || (name[pos] != ' ' && name[pos] != '_')) {
        return HumanBone::None;
    }
    std::string rest(name.substr(pos + 1));
    std::ranges::replace(rest, '_', ' ');
    return lookup(kBipedNames, rest);
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

HumanBone humanBoneFromVrm0Name(std::string_view name) {
    constexpr std::array kThumbs = {
        Entry{"leftThumbProximal", HumanBone::LeftThumbMetacarpal},
        Entry{"leftThumbIntermediate", HumanBone::LeftThumbProximal},
        Entry{"rightThumbProximal", HumanBone::RightThumbMetacarpal},
        Entry{"rightThumbIntermediate", HumanBone::RightThumbProximal},
    };
    const HumanBone thumb = lookup(kThumbs, name);
    return thumb != HumanBone::None ? thumb : lookup(kVrmNames, name);
}

HumanBone humanBoneFromMmdName(std::string_view name) {
    return lookup(kMmdNames, name);
}

HumanBone humanBoneFromFbxName(std::string_view name) {
    if (const auto colon = name.rfind(':'); colon != std::string_view::npos) {
        name.remove_prefix(colon + 1);
    }
    const HumanBone mixamo = lookup(kFbxNames, name);
    return mixamo != HumanBone::None ? mixamo : lookupBiped(name);
}

HumanBone limbChild(HumanBone bone) {
    // 손가락: 열거형에 손가락마다 세 마디가 손 쪽부터 이어져 있음 (끝 마디는 None)
    constexpr auto kFirstFinger = static_cast<int>(HumanBone::LeftThumbMetacarpal);
    if (const int finger = static_cast<int>(bone) - kFirstFinger; finger >= 0) {
        return finger % 3 != 2 ? static_cast<HumanBone>(static_cast<int>(bone) + 1)
                               : HumanBone::None;
    }
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
