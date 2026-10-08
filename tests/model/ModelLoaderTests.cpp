#include "model/GltfBuilder.h"
#include "model/ModelLoader.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

using deskpet::model::AlphaMode;
using deskpet::model::loadModelFromMemory;
using deskpet::model::LoadResult;
using deskpet::model::VrmVersion;
using deskpet::test::GltfBuilder;

namespace {

LoadResult load(const std::string& json) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(json.data());
    return loadModelFromMemory(std::span(bytes, json.size()));
}

// 삼각형 하나: (0,0,0) (1,0,0) (0,2,0), 법선 +Z
struct Triangle {
    int position;
    int normal;
    int indices;
};

Triangle addTriangle(GltfBuilder& b) {
    return {
        b.addFloats({0, 0, 0, 1, 0, 0, 0, 2, 0}, "VEC3"),
        b.addFloats({0, 0, 1, 0, 0, 1, 0, 0, 1}, "VEC3"),
        b.addUint16({0, 1, 2}, "SCALAR"),
    };
}

std::string meshJson(const Triangle& t, const std::string& primitiveExtra = "",
                     const std::string& extraAttributes = "") {
    return "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":" +
           std::to_string(t.position) + ",\"NORMAL\":" + std::to_string(t.normal) +
           extraAttributes + "},\"indices\":" + std::to_string(t.indices) + primitiveExtra + "}]}]";
}

const std::string kSingleNodeScene = R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0)";

}  // namespace

TEST(ModelLoader, InvalidBytes_ReturnsError) {
    const LoadResult result = load("not a gltf");
    EXPECT_FALSE(result.model.has_value());
    EXPECT_FALSE(result.error.empty());
}

TEST(ModelLoader, PlainGltfTriangle_LoadsGeometryAndBounds) {
    GltfBuilder b;
    const Triangle t = addTriangle(b);
    const LoadResult result = load(b.build(meshJson(t) + "," + kSingleNodeScene));

    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& model = *result.model;
    EXPECT_EQ(model.version, VrmVersion::None);
    ASSERT_EQ(model.vertices.size(), 3U);
    EXPECT_EQ(model.indices, (std::vector<std::uint32_t>{0, 1, 2}));
    ASSERT_EQ(model.primitives.size(), 1U);
    EXPECT_EQ(model.primitives[0].indexCount, 3U);
    EXPECT_EQ(model.primitives[0].material, -1);
    EXPECT_EQ(model.vertices[1].position, (deskpet::core::Vec3{1, 0, 0}));
    EXPECT_EQ(model.vertices[0].normal, (deskpet::core::Vec3{0, 0, 1}));
    EXPECT_EQ(model.bounds.min, (deskpet::core::Vec3{0, 0, 0}));
    EXPECT_EQ(model.bounds.max, (deskpet::core::Vec3{1, 2, 0}));
}

TEST(ModelLoader, NodeTransform_IsAppliedToUnskinnedMesh) {
    GltfBuilder b;
    const Triangle t = addTriangle(b);
    const LoadResult result = load(b.build(
        meshJson(t) +
        R"(,"nodes":[{"children":[1],"translation":[0,1,0]},{"mesh":0,"translation":[2,0,0]}],)"
        R"("scenes":[{"nodes":[0]}],"scene":0)"));

    ASSERT_TRUE(result.model.has_value()) << result.error;
    EXPECT_EQ(result.model->vertices[0].position, (deskpet::core::Vec3{2, 1, 0}));
}

TEST(ModelLoader, IndicesOfSecondPrimitive_AreOffsetByVertexBase) {
    GltfBuilder b;
    const Triangle t = addTriangle(b);
    const std::string p = R"({"attributes":{"POSITION":)" + std::to_string(t.position) +
                          R"(},"indices":)" + std::to_string(t.indices) + "}";
    const LoadResult result =
        load(b.build("\"meshes\":[{\"primitives\":[" + p + "," + p + "]}]," + kSingleNodeScene));

    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& model = *result.model;
    EXPECT_EQ(model.vertices.size(), 6U);
    EXPECT_EQ(model.indices, (std::vector<std::uint32_t>{0, 1, 2, 3, 4, 5}));
    ASSERT_EQ(model.primitives.size(), 2U);
    EXPECT_EQ(model.primitives[1].firstIndex, 3U);
}

TEST(ModelLoader, Vrm1_KeepsFacingPositiveZ) {
    GltfBuilder b;
    const Triangle t = addTriangle(b);
    const LoadResult result =
        load(b.build(meshJson(t) + "," + kSingleNodeScene + R"(,"extensionsUsed":["VRMC_vrm"])"));

    ASSERT_TRUE(result.model.has_value()) << result.error;
    EXPECT_EQ(result.model->version, VrmVersion::V1);
    EXPECT_EQ(result.model->vertices[1].position, (deskpet::core::Vec3{1, 0, 0}));
}

TEST(ModelLoader, Vrm0_IsRotatedToFacePositiveZ) {
    // VRM 0.x는 정면이 -Z → Y축 180° 회전으로 1.0 규약(+Z)에 맞춤
    GltfBuilder b;
    const Triangle t = addTriangle(b);
    const LoadResult result =
        load(b.build(meshJson(t) + "," + kSingleNodeScene + R"(,"extensionsUsed":["VRM"])"));

    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& model = *result.model;
    EXPECT_EQ(model.version, VrmVersion::V0);
    EXPECT_NEAR(model.vertices[1].position.x, -1.0f, 1e-6f);
    EXPECT_NEAR(model.vertices[0].normal.z, -1.0f, 1e-6f);
    EXPECT_NEAR(model.bounds.min.x, -1.0f, 1e-6f);
}

TEST(ModelLoader, Material_ReadsFactorsAlphaAndTexture) {
    GltfBuilder b;
    const Triangle t = addTriangle(b);
    const int uv = b.addFloats({0, 0, 1, 0, 0, 1}, "VEC2");
    const int image = b.addBufferView({0x89, 'P', 'N', 'G'});
    const std::string materials =
        R"("materials":[{"name":"Skin","pbrMetallicRoughness":{"baseColorFactor":[1,0.5,0.25,1],)"
        R"("baseColorTexture":{"index":0}},"alphaMode":"MASK","alphaCutoff":0.3,"doubleSided":true}],)"
        R"("textures":[{"source":0}],"images":[{"bufferView":)" +
        std::to_string(image) + R"(,"mimeType":"image/png"}])";
    const LoadResult result =
        load(b.build(meshJson(t, ",\"material\":0", ",\"TEXCOORD_0\":" + std::to_string(uv)) + "," +
                     kSingleNodeScene + "," + materials));

    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& model = *result.model;
    ASSERT_EQ(model.materials.size(), 1U);
    const auto& material = model.materials[0];
    EXPECT_EQ(material.name, "Skin");
    EXPECT_FLOAT_EQ(material.baseColor.y, 0.5f);
    EXPECT_EQ(material.alphaMode, AlphaMode::Mask);
    EXPECT_FLOAT_EQ(material.alphaCutoff, 0.3f);
    EXPECT_TRUE(material.doubleSided);
    EXPECT_EQ(material.baseColorTexture, 0);
    EXPECT_EQ(model.primitives[0].material, 0);
    EXPECT_FLOAT_EQ(model.vertices[1].u, 1.0f);

    ASSERT_EQ(model.textures.size(), 1U);
    EXPECT_EQ(model.textures[0].mimeType, "image/png");
    EXPECT_EQ(model.textures[0].encoded, (std::vector<std::uint8_t>{0x89, 'P', 'N', 'G'}));
}

namespace {

// 관절 하나짜리 스킨. jointY: 관절 노드의 현재 위치, 역바인드 행렬은 (0,-1,0) 이동
LoadResult loadSkinned(float jointY) {
    GltfBuilder b;
    const Triangle t = addTriangle(b);
    const int joints = b.addUint8({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, "VEC4");
    const int weights = b.addFloats({1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}, "VEC4");
    const int inverseBind = b.addFloats({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1}, "MAT4");

    // 스킨이 있는 메시 노드의 자체 변환(5,0,0)은 glTF 규칙상 무시되어야 함
    const std::string rest =
        meshJson(t, "",
                 ",\"JOINTS_0\":" + std::to_string(joints) +
                     ",\"WEIGHTS_0\":" + std::to_string(weights)) +
        R"(,"nodes":[{"mesh":0,"skin":0,"translation":[5,0,0]},{"translation":[0,)" +
        std::to_string(jointY) + R"(,0]}],"skins":[{"joints":[1],"inverseBindMatrices":)" +
        std::to_string(inverseBind) + R"(}],"scenes":[{"nodes":[0,1]}],"scene":0)";
    return load(b.build(rest));
}

}  // namespace

TEST(ModelLoader, SkinnedMesh_AtBindPose_KeepsVertexPositions) {
    const LoadResult result = loadSkinned(1.0f);
    ASSERT_TRUE(result.model.has_value()) << result.error;
    EXPECT_EQ(result.model->vertices[1].position, (deskpet::core::Vec3{1, 0, 0}));
}

TEST(ModelLoader, SkinnedMesh_FollowsJointPose) {
    const LoadResult result = loadSkinned(2.0f);  // 관절이 바인드 포즈보다 1 위
    ASSERT_TRUE(result.model.has_value()) << result.error;
    EXPECT_EQ(result.model->vertices[1].position, (deskpet::core::Vec3{1, 1, 0}));
    EXPECT_EQ(result.model->bounds.max.y, 3.0f);
}

// ---------------------------------------------------------------------------
// 본·휴머노이드 매핑 (ADR-0009)
// ---------------------------------------------------------------------------

TEST(ModelLoader, Vrm1Humanoid_MapsNodesToHumanBones) {
    GltfBuilder b;
    const Triangle t = addTriangle(b);
    const std::string rest =
        meshJson(t) +
        R"(,"nodes":[{"mesh":0},{"name":"J_Hips","translation":[0,1,0],"children":[2]},)"
        R"({"name":"J_Arm_L","translation":[0.2,0.4,0]}],"scenes":[{"nodes":[0,1]}],"scene":0,)"
        R"("extensionsUsed":["VRMC_vrm"],"extensions":{"VRMC_vrm":{"specVersion":"1.0",)"
        R"("humanoid":{"humanBones":{"hips":{"node":1},"leftUpperArm":{"node":2}}}}})";
    const LoadResult result = load(b.build(rest));

    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& model = *result.model;
    EXPECT_EQ(model.format, deskpet::model::ModelFormat::Gltf);
    ASSERT_EQ(model.bones.size(), 3U);  // 모든 노드가 본
    EXPECT_EQ(model.bones[1].human, deskpet::model::HumanBone::Hips);
    EXPECT_EQ(model.bones[2].human, deskpet::model::HumanBone::LeftUpperArm);
    EXPECT_EQ(model.bones[2].parent, 1);
    EXPECT_NEAR(model.bones[2].position.y, 1.4f, 1e-5f);
}

TEST(ModelLoader, Vrm0Humanoid_ArrayFormIsMapped) {
    GltfBuilder b;
    const Triangle t = addTriangle(b);
    // VRM 0.x는 배열 형식이고 항목 안에 중첩 객체(min/max 등)가 있음
    const std::string rest =
        meshJson(t) +
        R"(,"nodes":[{"mesh":0},{"name":"Hips","translation":[0.5,1,0]}],)"
        R"("scenes":[{"nodes":[0,1]}],"scene":0,"extensionsUsed":["VRM"],)"
        R"("extensions":{"VRM":{"humanoid":{"humanBones":[)"
        R"({"bone":"hips","node":1,"useDefaultValues":true,"min":{"x":0,"y":0,"z":0}}]}}})";
    const LoadResult result = load(b.build(rest));

    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& bones = result.model->bones;
    ASSERT_EQ(bones.size(), 2U);
    EXPECT_EQ(bones[1].human, deskpet::model::HumanBone::Hips);
    EXPECT_NEAR(bones[1].position.x, -0.5f, 1e-5f);  // 0.x 회전(180°) 적용
}

// ---------------------------------------------------------------------------
// 스킨 가중치·표정 (애니메이션, ADR-0010)
// ---------------------------------------------------------------------------

TEST(ModelLoader, SkinnedMesh_KeepsWeightsMappedToBoneIndices) {
    const LoadResult result = loadSkinned(1.0f);
    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& v = result.model->vertices[1];
    EXPECT_EQ(v.joints[0], 1);  // skin.joints[0] = 노드 1 = 본 1
    EXPECT_FLOAT_EQ(v.weights[0], 1.0f);
}

TEST(ModelLoader, UnskinnedMesh_IsRigidlyBoundToItsNode) {
    GltfBuilder b;
    const Triangle t = addTriangle(b);
    const LoadResult result = load(
        b.build(meshJson(t) +
                R"(,"nodes":[{"children":[1]},{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0)"));
    ASSERT_TRUE(result.model.has_value()) << result.error;
    EXPECT_EQ(result.model->vertices[0].joints[0], 1);  // 메시가 달린 노드를 따라 움직임
    EXPECT_FLOAT_EQ(result.model->vertices[0].weights[0], 1.0f);
}

namespace {

// 정점 0, 2를 움직이는 모프 타깃 하나가 있는 삼각형
std::string morphMeshJson(GltfBuilder& b, const Triangle& t) {
    const int target = b.addFloats({0, -1, 0, 0, 0, 0, 0, -2, 0}, "VEC3");
    return meshJson(t, R"(,"targets":[{"POSITION":)" + std::to_string(target) + "}]");
}

}  // namespace

TEST(ModelLoader, Vrm1Expression_ReadsMorphTargetBinds) {
    GltfBuilder b;
    const Triangle t = addTriangle(b);
    const std::string rest =
        morphMeshJson(b, t) + "," + kSingleNodeScene +
        R"(,"extensionsUsed":["VRMC_vrm"],"extensions":{"VRMC_vrm":{"expressions":{"preset":{)"
        R"("blink":{"morphTargetBinds":[{"node":0,"index":0,"weight":0.5}]}}}}})";
    const LoadResult result = load(b.build(rest));

    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& blink = result.model->expression(deskpet::model::Expression::Blink);
    ASSERT_EQ(blink.vertices, (std::vector<std::uint32_t>{0, 2}));  // 0인 델타는 저장하지 않음
    EXPECT_FLOAT_EQ(blink.deltas[1].y, -1.0f);                      // -2 × 가중치 0.5
}

TEST(ModelLoader, Vrm0BlendShape_ReadsMeshBindsWithPercentWeight) {
    GltfBuilder b;
    const Triangle t = addTriangle(b);
    const std::string rest =
        morphMeshJson(b, t) + "," + kSingleNodeScene +
        R"(,"extensionsUsed":["VRM"],"extensions":{"VRM":{"blendShapeMaster":{"blendShapeGroups":[)"
        R"({"name":"Joy","presetName":"joy","binds":[{"mesh":0,"index":0,"weight":100}]}]}}})";
    const LoadResult result = load(b.build(rest));

    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& happy = result.model->expression(deskpet::model::Expression::Happy);
    ASSERT_EQ(happy.vertices.size(), 2U);
    EXPECT_FLOAT_EQ(happy.deltas[0].y, -1.0f);  // 가중치 100% = 1.0
}

// ---------------------------------------------------------------------------
// 외곽선 (MToon): 재질별 굵기(m)와 색
// ---------------------------------------------------------------------------

TEST(ModelLoader, Vrm0MToonOutline_IsReadPerMaterialByName) {
    // VRM 0.x: _OutlineWidth는 cm 단위, _OutlineWidthMode 0이면 없음
    GltfBuilder b;
    const Triangle t = addTriangle(b);
    const std::string json =
        meshJson(t, ",\"material\":0") + "," + kSingleNodeScene +
        R"(,"materials":[{"name":"Body"},{"name":"Eye"}],"extensionsUsed":["VRM"],)"
        R"("extensions":{"VRM":{"materialProperties":[)"
        R"({"name":"Eye","shader":"VRM/MToon","floatProperties":{"_OutlineWidthMode":0,"_OutlineWidth":0.5}},)"
        R"({"name":"Body","shader":"VRM/MToon","floatProperties":{"_OutlineWidthMode":1,"_OutlineWidth":0.6},)"
        R"("vectorProperties":{"_OutlineColor":[0.3,0.2,0.1,1]}}]}})";
    const LoadResult result = load(b.build(json));

    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& materials = result.model->materials;
    ASSERT_EQ(materials.size(), 2U);
    EXPECT_NEAR(materials[0].outlineWidth, 0.006f, 1e-6f);
    EXPECT_NEAR(materials[0].outlineColor.x, 0.07323f, 1e-4f);  // sRGB 0.3 → 선형
    EXPECT_NEAR(materials[0].outlineColor.z, 0.01002f, 1e-4f);
    EXPECT_FLOAT_EQ(materials[1].outlineWidth, 0.0f);  // 모드 0 (외곽선 없음)
}

TEST(ModelLoader, Vrm1MToonOutline_ReadsWorldAndScreenModes) {
    // VRM 1.0: worldCoordinates는 m, screenCoordinates는 화면 높이 비율 → 모델 높이(2)로 근사
    GltfBuilder b;
    const Triangle t = addTriangle(b);
    const std::string json =
        meshJson(t, ",\"material\":0") + "," + kSingleNodeScene +
        R"(,"extensionsUsed":["VRMC_vrm","VRMC_materials_mtoon"],"materials":[)"
        R"({"name":"World","extensions":{"VRMC_materials_mtoon":{"outlineWidthMode":"worldCoordinates",)"
        R"("outlineWidthFactor":0.004,"outlineColorFactor":[0.5,0.25,0]}}},)"
        R"({"name":"Screen","extensions":{"VRMC_materials_mtoon":{"outlineWidthMode":"screenCoordinates",)"
        R"("outlineWidthFactor":0.01}}},)"
        R"({"name":"None","extensions":{"VRMC_materials_mtoon":{"outlineWidthMode":"none",)"
        R"("outlineWidthFactor":0.01}}}])";
    const LoadResult result = load(b.build(json));

    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& materials = result.model->materials;
    ASSERT_EQ(materials.size(), 3U);
    EXPECT_NEAR(materials[0].outlineWidth, 0.004f, 1e-6f);
    EXPECT_FLOAT_EQ(materials[0].outlineColor.x, 0.5f);
    EXPECT_FLOAT_EQ(materials[0].outlineColor.w, 1.0f);
    EXPECT_NEAR(materials[1].outlineWidth, 0.02f, 1e-6f);
    EXPECT_FLOAT_EQ(materials[1].outlineColor.x, 0.0f);  // 색 지정 없으면 검정
    EXPECT_FLOAT_EQ(materials[2].outlineWidth, 0.0f);
}

TEST(ModelLoader, PlainGltfMaterial_HasNoOutline) {
    GltfBuilder b;
    const Triangle t = addTriangle(b);
    const LoadResult result = load(b.build(meshJson(t, ",\"material\":0") + "," + kSingleNodeScene +
                                           R"(,"materials":[{"name":"A"}])"));
    ASSERT_TRUE(result.model.has_value()) << result.error;
    EXPECT_FLOAT_EQ(result.model->materials[0].outlineWidth, 0.0f);
}

// ---------------------------------------------------------------------------
// SpringBone (VRM 0.x secondaryAnimation)
// ---------------------------------------------------------------------------

TEST(ModelLoader, Vrm0SpringBone_ReadsGroupsAndCollidersInModelSpace) {
    // VRM 0.x 좌표는 정면 -Z라 오프셋·중력 방향도 Y축 180° 회전 (x, z 부호 반전)
    GltfBuilder b;
    const Triangle t = addTriangle(b);
    const std::string json =
        meshJson(t) +
        R"(,"nodes":[{"mesh":0,"children":[1]},{"name":"hair","translation":[0,1,0],"children":[2]},)"
        R"({"name":"hair_end","translation":[0,-0.1,0]}],"scenes":[{"nodes":[0]}],"scene":0,)"
        R"("extensionsUsed":["VRM"],"extensions":{"VRM":{"secondaryAnimation":{)"
        R"("boneGroups":[{"stiffiness":2,"gravityPower":0.5,"gravityDir":{"x":1,"y":-1,"z":0},)"
        R"("dragForce":0.3,"hitRadius":0.02,"bones":[1],"colliderGroups":[0]}],)"
        R"("colliderGroups":[{"node":1,"colliders":[{"offset":{"x":0.1,"y":0,"z":0.2},"radius":0.05},)"
        R"({"offset":{"x":0,"y":0.1,"z":0},"radius":0.03}]}]}}})";
    const LoadResult result = load(b.build(json));

    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& model = *result.model;
    ASSERT_EQ(model.springGroups.size(), 1U);
    const auto& group = model.springGroups[0];
    EXPECT_FLOAT_EQ(group.stiffness, 2.0f);
    EXPECT_FLOAT_EQ(group.gravityPower, 0.5f);
    EXPECT_FLOAT_EQ(group.gravityDir.x, -1.0f);
    EXPECT_FLOAT_EQ(group.gravityDir.y, -1.0f);
    EXPECT_FLOAT_EQ(group.dragForce, 0.3f);
    EXPECT_FLOAT_EQ(group.hitRadius, 0.02f);
    EXPECT_EQ(group.roots, (std::vector<int>{1}));
    EXPECT_EQ(group.colliders, (std::vector<int>{0, 1}));  // 충돌체 그룹 0의 구 2개

    ASSERT_EQ(model.springColliders.size(), 2U);
    EXPECT_EQ(model.springColliders[0].bone, 1);
    EXPECT_FLOAT_EQ(model.springColliders[0].offset.x, -0.1f);
    EXPECT_FLOAT_EQ(model.springColliders[0].offset.z, -0.2f);
    EXPECT_FLOAT_EQ(model.springColliders[0].radius, 0.05f);
}

TEST(ModelLoader, PlainGltf_HasNoSpringBones) {
    GltfBuilder b;
    const Triangle t = addTriangle(b);
    const LoadResult result = load(b.build(meshJson(t) + "," + kSingleNodeScene));
    ASSERT_TRUE(result.model.has_value()) << result.error;
    EXPECT_TRUE(result.model->springGroups.empty());
    EXPECT_TRUE(result.model->springColliders.empty());
}

// ---------------------------------------------------------------------------
// MToon 셰이딩: 그림자 색·단계, 림, MatCap, 발광 (색은 선형 공간)
// ---------------------------------------------------------------------------

namespace {

// 이미지 3개 + 텍스처 3개. 텍스처 i → 이미지 (2 - i) (텍스처 번호와 이미지 번호가 달라야 검증됨)
std::string threeTextures(GltfBuilder& b) {
    std::string images;
    for (int i = 0; i < 3; ++i) {
        const int view = b.addBufferView({0x89, 'P', 'N', 'G', static_cast<std::uint8_t>(i)});
        images += (i > 0 ? "," : "") + std::string(R"({"bufferView":)") + std::to_string(view) +
                  R"(,"mimeType":"image/png"})";
    }
    return R"("textures":[{"source":2},{"source":1},{"source":0}],"images":[)" + images + "]";
}

}  // namespace

TEST(ModelLoader, Vrm0MToon_ReadsShadingRimMatcapAndConvertsSrgbColors) {
    // VRM 0.x 색(_ShadeColor, _RimColor, _OutlineColor)은 sRGB → 선형으로 변환.
    // _ShadeShift 0, _ShadeToony 0.5 → 경계 [0, 0.5] → VRM 1.0 정의로 shift −0.25, toony 0.75
    GltfBuilder b;
    const Triangle t = addTriangle(b);
    const std::string json =
        meshJson(t, ",\"material\":0") + "," + kSingleNodeScene + "," + threeTextures(b) +
        R"(,"materials":[{"name":"Body","emissiveFactor":[1,1,1]}],"extensionsUsed":["VRM"],)"
        R"("extensions":{"VRM":{"materialProperties":[{"name":"Body","shader":"VRM/MToon",)"
        R"("floatProperties":{"_ShadeShift":0,"_ShadeToony":0.5,"_RimFresnelPower":2,)"
        R"("_RimLift":0.1,"_RimLightingMix":0.5,"_OutlineWidthMode":1,"_OutlineWidth":0.5},)"
        R"("vectorProperties":{"_ShadeColor":[0.5,1,0,1],"_RimColor":[1,0.5,0,1],)"
        R"("_EmissionColor":[0.25,0,0,1],"_OutlineColor":[0.5,0,0,1]},)"
        R"("textureProperties":{"_MainTex":0,"_ShadeTexture":1,"_SphereAdd":2,"_EmissionMap":0}}]}})";
    const LoadResult result = load(b.build(json));

    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& m = result.model->materials[0];
    EXPECT_TRUE(m.mtoon);
    EXPECT_NEAR(m.shadeColor.x, 0.214041f, 1e-5f);
    EXPECT_FLOAT_EQ(m.shadeColor.y, 1.0f);
    EXPECT_EQ(m.shadeTexture, 1);  // 텍스처 1 → 이미지 1
    EXPECT_NEAR(m.shadingShift, -0.25f, 1e-6f);
    EXPECT_NEAR(m.shadingToony, 0.75f, 1e-6f);
    EXPECT_FLOAT_EQ(m.rimColor.x, 1.0f);
    EXPECT_NEAR(m.rimColor.y, 0.214041f, 1e-5f);
    EXPECT_FLOAT_EQ(m.rimFresnelPower, 2.0f);
    EXPECT_FLOAT_EQ(m.rimLift, 0.1f);
    EXPECT_FLOAT_EQ(m.rimLightingMix, 0.5f);
    EXPECT_EQ(m.matcapTexture, 0);  // 텍스처 2 → 이미지 0
    EXPECT_FLOAT_EQ(m.matcapColor.x, 1.0f);
    EXPECT_EQ(m.emissiveTexture, 2);  // 텍스처 0 → 이미지 2
    // 발광은 HDR이라 선형 그대로. glTF emissiveFactor(대체용 unlit 재질의 값)는 무시
    EXPECT_FLOAT_EQ(m.emissiveColor.x, 0.25f);
    EXPECT_FLOAT_EQ(m.emissiveColor.y, 0.0f);
    EXPECT_NEAR(m.outlineColor.x, 0.214041f, 1e-5f);
}

TEST(ModelLoader, Vrm0NonMToonShader_KeepsDefaultToon) {
    GltfBuilder b;
    const Triangle t = addTriangle(b);
    const std::string json =
        meshJson(t, ",\"material\":0") + "," + kSingleNodeScene +
        R"(,"materials":[{"name":"Metal"}],"extensionsUsed":["VRM"],)"
        R"("extensions":{"VRM":{"materialProperties":[{"name":"Metal","shader":"VRM_USE_GLTFSHADER",)"
        R"("floatProperties":{"_ShadeToony":0.5}}]}})";
    const LoadResult result = load(b.build(json));
    ASSERT_TRUE(result.model.has_value()) << result.error;
    EXPECT_FALSE(result.model->materials[0].mtoon);
}

TEST(ModelLoader, Vrm1MToon_ReadsFactorsTexturesAndGltfEmissive) {
    GltfBuilder b;
    const Triangle t = addTriangle(b);
    const std::string json =
        meshJson(t, ",\"material\":0") + "," + kSingleNodeScene + "," + threeTextures(b) +
        R"(,"extensionsUsed":["VRMC_vrm","VRMC_materials_mtoon"],"materials":[{"name":"Body",)"
        R"("emissiveFactor":[0.5,0.25,0],"emissiveTexture":{"index":2},)"
        R"("extensions":{"VRMC_materials_mtoon":{"shadeColorFactor":[0.5,0.25,0],)"
        R"("shadeMultiplyTexture":{"index":0},"shadingShiftFactor":-0.1,"shadingToonyFactor":0.8,)"
        R"("matcapFactor":[0.5,0.5,0.5],"matcapTexture":{"index":1},)"
        R"("parametricRimColorFactor":[0,1,0],"parametricRimFresnelPowerFactor":3,)"
        R"("parametricRimLiftFactor":0.2,"rimLightingMixFactor":0.4}}}])";
    const LoadResult result = load(b.build(json));

    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& m = result.model->materials[0];
    EXPECT_TRUE(m.mtoon);
    EXPECT_FLOAT_EQ(m.shadeColor.x, 0.5f);  // VRM 1.0 색은 이미 선형
    EXPECT_FLOAT_EQ(m.shadeColor.y, 0.25f);
    EXPECT_EQ(m.shadeTexture, 2);
    EXPECT_FLOAT_EQ(m.shadingShift, -0.1f);
    EXPECT_FLOAT_EQ(m.shadingToony, 0.8f);
    EXPECT_FLOAT_EQ(m.matcapColor.x, 0.5f);
    EXPECT_EQ(m.matcapTexture, 1);
    EXPECT_FLOAT_EQ(m.rimColor.y, 1.0f);
    EXPECT_FLOAT_EQ(m.rimFresnelPower, 3.0f);
    EXPECT_FLOAT_EQ(m.rimLift, 0.2f);
    EXPECT_FLOAT_EQ(m.rimLightingMix, 0.4f);
    EXPECT_FLOAT_EQ(m.emissiveColor.x, 0.5f);
    EXPECT_EQ(m.emissiveTexture, 0);
}

TEST(ModelLoader, PlainGltfMaterial_IsNotMToonButReadsEmissiveAndUnlit) {
    GltfBuilder b;
    const Triangle t = addTriangle(b);
    const LoadResult result =
        load(b.build(meshJson(t, ",\"material\":0") + "," + kSingleNodeScene +
                     R"(,"extensionsUsed":["KHR_materials_unlit"],"materials":[{"name":"A",)"
                     R"("emissiveFactor":[0,0,1],"extensions":{"KHR_materials_unlit":{}}}])"));
    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& m = result.model->materials[0];
    EXPECT_FALSE(m.mtoon);
    EXPECT_TRUE(m.unlit);
    EXPECT_FLOAT_EQ(m.emissiveColor.z, 1.0f);
    EXPECT_EQ(m.emissiveTexture, -1);
    EXPECT_EQ(m.matcapTexture, -1);
}
