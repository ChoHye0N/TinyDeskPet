#include "model/Humanoid.h"

#include <gtest/gtest.h>

using deskpet::model::HumanBone;
using deskpet::model::humanBoneFromFbxName;
using deskpet::model::humanBoneFromMmdName;
using deskpet::model::humanBoneFromVrmName;

TEST(Humanoid, VrmNames) {
    EXPECT_EQ(humanBoneFromVrmName("hips"), HumanBone::Hips);
    EXPECT_EQ(humanBoneFromVrmName("leftUpperArm"), HumanBone::LeftUpperArm);
    EXPECT_EQ(humanBoneFromVrmName("leftThumbMetacarpal"), HumanBone::LeftThumbMetacarpal);
    EXPECT_EQ(humanBoneFromVrmName("rightLittleDistal"), HumanBone::RightLittleDistal);
    EXPECT_EQ(humanBoneFromVrmName("leftThumbIntermediate"), HumanBone::None);  // 0.x 전용 이름
    EXPECT_EQ(deskpet::model::toString(HumanBone::RightToes), "rightToes");
    EXPECT_EQ(deskpet::model::toString(HumanBone::LeftIndexDistal), "leftIndexDistal");
}

TEST(Humanoid, Vrm0Names_ShiftThumbOneJointTowardHand) {
    // 0.x 엄지 proximal·intermediate·distal = 1.0 metacarpal·proximal·distal
    using deskpet::model::humanBoneFromVrm0Name;
    EXPECT_EQ(humanBoneFromVrm0Name("leftThumbProximal"), HumanBone::LeftThumbMetacarpal);
    EXPECT_EQ(humanBoneFromVrm0Name("rightThumbIntermediate"), HumanBone::RightThumbProximal);
    EXPECT_EQ(humanBoneFromVrm0Name("leftThumbDistal"), HumanBone::LeftThumbDistal);
    EXPECT_EQ(humanBoneFromVrm0Name("leftIndexProximal"), HumanBone::LeftIndexProximal);
    EXPECT_EQ(humanBoneFromVrm0Name("hips"), HumanBone::Hips);
}

TEST(Humanoid, FingerNames_InEveryFormat) {
    EXPECT_EQ(humanBoneFromMmdName("左親指０"), HumanBone::LeftThumbMetacarpal);
    EXPECT_EQ(humanBoneFromMmdName("左人指２"), HumanBone::LeftIndexIntermediate);
    EXPECT_EQ(humanBoneFromMmdName("右小指３"), HumanBone::RightLittleDistal);
    EXPECT_EQ(humanBoneFromFbxName("mixamorig:LeftHandThumb1"), HumanBone::LeftThumbMetacarpal);
    EXPECT_EQ(humanBoneFromFbxName("mixamorig:RightHandPinky3"), HumanBone::RightLittleDistal);
    EXPECT_EQ(humanBoneFromFbxName("Bip001 L Finger0"), HumanBone::LeftThumbMetacarpal);
    EXPECT_EQ(humanBoneFromFbxName("Bip001 L Finger21"), HumanBone::LeftMiddleIntermediate);
    EXPECT_EQ(humanBoneFromFbxName("Bip001_R_Finger42"), HumanBone::RightLittleDistal);
}

TEST(Humanoid, MmdStandardNames) {
    EXPECT_EQ(humanBoneFromMmdName("下半身"), HumanBone::Hips);
    EXPECT_EQ(humanBoneFromMmdName("センター"), HumanBone::None);
    EXPECT_EQ(humanBoneFromMmdName("左腕"), HumanBone::LeftUpperArm);
    EXPECT_EQ(humanBoneFromMmdName("右ひざ"), HumanBone::RightLowerLeg);
}

TEST(Humanoid, FbxMixamoNames_IgnorePrefix) {
    EXPECT_EQ(humanBoneFromFbxName("mixamorig:Hips"), HumanBone::Hips);
    EXPECT_EQ(humanBoneFromFbxName("mixamorig1:LeftForeArm"), HumanBone::LeftLowerArm);
    EXPECT_EQ(humanBoneFromFbxName("Spine1"), HumanBone::Chest);
    EXPECT_EQ(humanBoneFromFbxName("Camera"), HumanBone::None);
}

TEST(Humanoid, FbxBipedNames_IgnoreNumberedPrefixAndUnderscores) {
    // 3ds Max Biped. 게임에서 추출한 모델에 흔하고, 내보내기 도구에 따라 공백이 '_'가 됨
    EXPECT_EQ(humanBoneFromFbxName("Bip001 Pelvis"), HumanBone::Hips);
    EXPECT_EQ(humanBoneFromFbxName("Bip01 L UpperArm"), HumanBone::LeftUpperArm);
    EXPECT_EQ(humanBoneFromFbxName("Bip001_R_Forearm"), HumanBone::RightLowerArm);
    EXPECT_EQ(humanBoneFromFbxName("Bip001 Spine1"), HumanBone::Chest);
    EXPECT_EQ(humanBoneFromFbxName("Bip001 L Clavicle"), HumanBone::LeftShoulder);
    EXPECT_EQ(humanBoneFromFbxName("Bip001 R Calf"), HumanBone::RightLowerLeg);
    EXPECT_EQ(humanBoneFromFbxName("Bip001 L Toe0"), HumanBone::LeftToes);
    // 몸 전체 이동용 루트(Bip001)는 hips가 아님 (Pelvis가 hips). 비슷한 이름의 다른 본도 아님
    EXPECT_EQ(humanBoneFromFbxName("Bip001"), HumanBone::None);
    EXPECT_EQ(humanBoneFromFbxName("Bip001_Weapon"), HumanBone::None);
    EXPECT_EQ(humanBoneFromFbxName("Bip001 L Finger5"), HumanBone::None);  // 손가락은 0 ~ 4
    EXPECT_EQ(humanBoneFromFbxName("Biped L Thigh"), HumanBone::None);
}

TEST(Humanoid, LimbChild_IsNextBoneAlongArmsAndLegs) {
    using deskpet::model::limbChild;
    EXPECT_EQ(limbChild(HumanBone::LeftShoulder), HumanBone::LeftUpperArm);
    EXPECT_EQ(limbChild(HumanBone::RightUpperArm), HumanBone::RightLowerArm);
    EXPECT_EQ(limbChild(HumanBone::LeftLowerArm), HumanBone::LeftHand);
    EXPECT_EQ(limbChild(HumanBone::RightUpperLeg), HumanBone::RightLowerLeg);
    EXPECT_EQ(limbChild(HumanBone::LeftLowerLeg), HumanBone::LeftFoot);
    EXPECT_EQ(limbChild(HumanBone::RightFoot), HumanBone::RightToes);
    EXPECT_EQ(limbChild(HumanBone::LeftThumbMetacarpal), HumanBone::LeftThumbProximal);
    EXPECT_EQ(limbChild(HumanBone::RightIndexIntermediate), HumanBone::RightIndexDistal);
    // 몸통·머리·손·손가락 끝은 방향을 재지 않음 (부모 쪽 보정을 따름)
    EXPECT_EQ(limbChild(HumanBone::Spine), HumanBone::None);
    EXPECT_EQ(limbChild(HumanBone::LeftHand), HumanBone::None);
    EXPECT_EQ(limbChild(HumanBone::LeftThumbDistal), HumanBone::None);
    EXPECT_EQ(limbChild(HumanBone::RightLittleDistal), HumanBone::None);
}
