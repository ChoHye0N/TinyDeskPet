#include "model/Humanoid.h"

#include <gtest/gtest.h>

using deskpet::model::HumanBone;
using deskpet::model::humanBoneFromFbxName;
using deskpet::model::humanBoneFromMmdName;
using deskpet::model::humanBoneFromVrmName;

TEST(Humanoid, VrmNames) {
    EXPECT_EQ(humanBoneFromVrmName("hips"), HumanBone::Hips);
    EXPECT_EQ(humanBoneFromVrmName("leftUpperArm"), HumanBone::LeftUpperArm);
    EXPECT_EQ(humanBoneFromVrmName("leftThumbProximal"), HumanBone::None);  // 손가락은 미지원
    EXPECT_EQ(deskpet::model::toString(HumanBone::RightToes), "rightToes");
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

TEST(Humanoid, LimbChild_IsNextBoneAlongArmsAndLegs) {
    using deskpet::model::limbChild;
    EXPECT_EQ(limbChild(HumanBone::LeftShoulder), HumanBone::LeftUpperArm);
    EXPECT_EQ(limbChild(HumanBone::RightUpperArm), HumanBone::RightLowerArm);
    EXPECT_EQ(limbChild(HumanBone::LeftLowerArm), HumanBone::LeftHand);
    EXPECT_EQ(limbChild(HumanBone::RightUpperLeg), HumanBone::RightLowerLeg);
    EXPECT_EQ(limbChild(HumanBone::LeftLowerLeg), HumanBone::LeftFoot);
    EXPECT_EQ(limbChild(HumanBone::RightFoot), HumanBone::RightToes);
    // 몸통·머리·손끝은 방향을 재지 않음 (부모 쪽 보정을 따름)
    EXPECT_EQ(limbChild(HumanBone::Spine), HumanBone::None);
    EXPECT_EQ(limbChild(HumanBone::LeftHand), HumanBone::None);
}
