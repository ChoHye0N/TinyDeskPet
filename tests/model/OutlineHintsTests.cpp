#include "model/OutlineHints.h"

#include <gtest/gtest.h>

#include <string>

using deskpet::core::Vec3;
using deskpet::model::HumanBone;
using deskpet::model::Model;
using deskpet::model::OutlineSkip;

namespace {

// 정점 4개짜리 사각형 primitive를 추가 (bone이 0 이상이면 그 본에 가중치 1)
void addQuad(Model& model, const std::string& material, Vec3 origin, Vec3 size, int bone = -1) {
    const auto first = static_cast<std::uint32_t>(model.vertices.size());
    for (int i = 0; i < 8; ++i) {  // 상자 꼭짓점 8개 (size의 한 축이 0이면 판)
        deskpet::model::Vertex v;
        v.position = {origin.x + ((i & 1) != 0 ? size.x : 0.0f),
                      origin.y + ((i & 2) != 0 ? size.y : 0.0f),
                      origin.z + ((i & 4) != 0 ? size.z : 0.0f)};
        if (bone >= 0) {
            v.joints[0] = static_cast<std::uint16_t>(bone);
            v.weights[0] = 1.0f;
        }
        model.vertices.push_back(v);
    }
    const auto index = static_cast<std::uint32_t>(model.indices.size());
    for (std::uint32_t k : {0U, 1U, 2U, 1U, 3U, 2U, 4U, 5U, 6U}) {
        model.indices.push_back(first + k);
    }
    deskpet::model::Material m;
    m.name = material;
    model.materials.push_back(m);
    model.primitives.push_back({index, 9, static_cast<int>(model.materials.size()) - 1});
}

}  // namespace

TEST(OutlineHints, FaceParts_AreSkippedByMaterialName) {
    // "모든 재질" 외곽선에서 얼굴 위에 붙인 판(눈·입·눈썹)과 헤일로는 껍데기가 얼굴을 덮으므로 뺌
    Model model;
    addQuad(model, "CH0295_Body", {0, 0, 0}, {0.4f, 1.0f, 0.3f});
    addQuad(model, "CH0295_EyeMouth", {0, 1, 0}, {0.1f, 0.1f, 0.1f});
    addQuad(model, "Eyebrow", {0, 1, 0}, {0.1f, 0.1f, 0.1f});
    addQuad(model, "目_ハイライト", {0, 1, 0}, {0.1f, 0.1f, 0.1f});
    addQuad(model, "Teeth", {0, 1, 0}, {0.1f, 0.1f, 0.1f});
    addQuad(model, "CH0295_Halo", {0, 1, 0}, {0.3f, 0.3f, 0.3f});
    deskpet::model::markOutlineSkips(model);
    EXPECT_EQ(model.primitives[0].outlineSkip, OutlineSkip::None);
    for (std::size_t i = 1; i < model.primitives.size(); ++i) {
        EXPECT_EQ(model.primitives[i].outlineSkip, OutlineSkip::Name) << i;
    }
}

TEST(OutlineHints, PrimitiveOnEyeBones_IsSkipped) {
    // 이름이 없어도 정점이 주로 눈 본(또는 그 자식)에 붙어 있으면 눈
    Model model;
    model.bones.resize(3);
    model.bones[0].human = HumanBone::Head;
    model.bones[1].human = HumanBone::LeftEye;
    model.bones[1].parent = 0;
    model.bones[2].parent = 1;  // 눈 본의 자식 (하이라이트 등)
    addQuad(model, "mat_a", {0, 1, 0}, {0.1f, 0.1f, 0.1f}, 2);
    addQuad(model, "mat_b", {0, 1, 0}, {0.2f, 0.2f, 0.2f}, 0);
    deskpet::model::markOutlineSkips(model);
    EXPECT_EQ(model.primitives[0].outlineSkip, OutlineSkip::EyeBone);
    EXPECT_EQ(model.primitives[1].outlineSkip, OutlineSkip::None);
}

TEST(OutlineHints, FlatPlate_IsSkipped) {
    // 두께가 다른 축의 5%보다 얇은 판(헤일로, 얼굴에 붙인 장식): 반전 헐이 판 전체를 칠함
    Model model;
    addQuad(model, "plate", {0, 1, 0}, {0.3f, 0.3f, 0.001f});
    addQuad(model, "box", {0, 0, 0}, {0.3f, 0.3f, 0.2f});
    deskpet::model::markOutlineSkips(model);
    EXPECT_EQ(model.primitives[0].outlineSkip, OutlineSkip::Flat);
    EXPECT_EQ(model.primitives[1].outlineSkip, OutlineSkip::None);
}
