#include "model/ModelLoader.h"
#include "model/PmxBuilder.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

using deskpet::core::Vec3;
using deskpet::model::AlphaMode;
using deskpet::model::HumanBone;
using deskpet::model::ImportContext;
using deskpet::model::loadModelFromMemory;
using deskpet::model::LoadResult;
using deskpet::model::ModelFormat;
using deskpet::test::buildTestPmx;
namespace fs = std::filesystem;

namespace {

LoadResult load(const std::vector<std::uint8_t>& bytes, const ImportContext& context = {}) {
    return loadModelFromMemory(std::span(bytes), ".pmx", context);
}

void expectNear(Vec3 actual, Vec3 expected) {
    EXPECT_NEAR(actual.x, expected.x, 1e-5f);
    EXPECT_NEAR(actual.y, expected.y, 1e-5f);
    EXPECT_NEAR(actual.z, expected.z, 1e-5f);
}

}  // namespace

TEST(PmxImporter, Geometry_IsConvertedToMetersRightHandedFacingPlusZ) {
    const LoadResult result = load(buildTestPmx(0, 2));
    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& model = *result.model;

    EXPECT_EQ(model.format, ModelFormat::Pmx);
    ASSERT_EQ(model.vertices.size(), 3U);
    // 1 MMD 단위 = 0.08m, 왼손 → 오른손: z 부호 반전
    expectNear(model.vertices[1].position, {0.8f, 0.0f, 0.0f});
    expectNear(model.vertices[2].position, {0.0f, 1.6f, -0.4f});
    expectNear(model.vertices[0].normal, {0.0f, 0.0f, 1.0f});  // 정면 -Z → +Z
    EXPECT_FLOAT_EQ(model.vertices[1].u, 1.0f);

    // 거울 변환으로 감기 순서가 뒤집히므로 인덱스 순서를 바꿈 (CW → CCW)
    EXPECT_EQ(model.indices, (std::vector<std::uint32_t>{0, 2, 1}));
    ASSERT_EQ(model.primitives.size(), 1U);
    EXPECT_EQ(model.primitives[0].indexCount, 3U);
    EXPECT_EQ(model.primitives[0].material, 0);
    EXPECT_NEAR(model.bounds.max.y, 1.6f, 1e-5f);
}

TEST(PmxImporter, Material_ReadsColorFlagsAndTexture) {
    const LoadResult result = load(buildTestPmx(0, 2));
    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& model = *result.model;

    ASSERT_EQ(model.materials.size(), 1U);
    const auto& material = model.materials[0];
    EXPECT_EQ(material.name, "体");
    EXPECT_FLOAT_EQ(material.baseColor.y, 0.5f);
    EXPECT_TRUE(material.doubleSided);
    EXPECT_EQ(material.baseColorTexture, 0);
    EXPECT_EQ(material.alphaMode, AlphaMode::Mask);  // 텍스처 알파로 머리카락 끝 등을 잘라냄
    // 에지 플래그(0x10) + 에지 크기 1 → MToon과 같은 단위(m)의 외곽선, 에지 색 (0, 0, 0, 1)
    EXPECT_GT(material.outlineWidth, 0.0f);
    EXPECT_FLOAT_EQ(material.outlineColor.x, 0.0f);
    EXPECT_FLOAT_EQ(material.outlineColor.w, 1.0f);

    ASSERT_EQ(model.textures.size(), 1U);
    EXPECT_TRUE(model.textures[0].empty());  // 기준 폴더가 없어 외부 파일을 읽지 않음
}

TEST(PmxImporter, Bones_AreConvertedAndMappedToHumanoid) {
    const LoadResult result = load(buildTestPmx(0, 2));
    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& bones = result.model->bones;

    ASSERT_EQ(bones.size(), 4U);  // 가변 길이 필드를 모두 건너뛰고 마지막 본까지 읽음
    EXPECT_EQ(bones[0].name, "センター");
    EXPECT_EQ(bones[0].human, HumanBone::None);
    EXPECT_EQ(bones[1].human, HumanBone::Hips);
    EXPECT_EQ(bones[1].parent, 0);
    expectNear(bones[1].position, {0.0f, 0.96f, -0.08f});
    EXPECT_EQ(bones[2].human, HumanBone::LeftUpperArm);
    EXPECT_EQ(bones[3].human, HumanBone::RightEye);
    EXPECT_EQ(bones[3].parent, 2);
    EXPECT_EQ(result.model->findBone(HumanBone::LeftUpperArm), 2);
}

TEST(PmxImporter, Utf8AndIndexSizes_GiveSameResult) {
    for (const std::uint8_t indexSize : {std::uint8_t{1}, std::uint8_t{4}}) {
        const LoadResult result = load(buildTestPmx(1, indexSize, 2));
        ASSERT_TRUE(result.model.has_value()) << result.error;
        EXPECT_EQ(result.model->bones.size(), 4U);
        EXPECT_EQ(result.model->bones[2].name, "左腕");
        EXPECT_EQ(result.model->indices, (std::vector<std::uint32_t>{0, 2, 1}));
    }
}

TEST(PmxImporter, ExternalTexture_IsLoadedRelativeToModelFolder) {
    const fs::path dir = fs::temp_directory_path() / "deskpet_pmx_texture";
    fs::remove_all(dir);
    fs::create_directories(dir / "tex");
    {
        // 1×1 TGA
        const std::vector<std::uint8_t> tga = {0, 0, 2, 0, 0, 0,  0,    0, 0, 0, 0,
                                               0, 1, 0, 1, 0, 32, 0x28, 1, 2, 3, 255};
        std::ofstream out(dir / "tex" / "body.tga", std::ios::binary);
        out.write(reinterpret_cast<const char*>(tga.data()),
                  static_cast<std::streamsize>(tga.size()));
    }

    const LoadResult result = load(buildTestPmx(0, 2), {dir});
    ASSERT_TRUE(result.model.has_value()) << result.error;
    EXPECT_EQ(result.model->textures[0].width, 1);
    fs::remove_all(dir);
}

TEST(PmxImporter, TruncatedFile_ReturnsErrorWithoutCrash) {
    const std::vector<std::uint8_t> full = buildTestPmx(0, 2);
    for (std::size_t size : {std::size_t{4}, std::size_t{20}, full.size() / 2, full.size() - 30}) {
        const std::vector<std::uint8_t> cut(full.begin(),
                                            full.begin() + static_cast<std::ptrdiff_t>(size));
        const LoadResult result = load(cut);
        EXPECT_FALSE(result.model.has_value()) << "size " << size;
        EXPECT_FALSE(result.error.empty());
    }
}

TEST(PmxImporter, OutOfRangeFaceIndex_IsRejected) {
    std::vector<std::uint8_t> bytes = buildTestPmx(1, 4);
    // 면 데이터의 첫 인덱스(값 0)를 찾기 어려우므로, 정점 수를 1로 줄여 인덱스 1·2를 범위 밖으로
    // 만듦 헤더(4+4+1+8) + 이름 4개(UTF-8: 4+9, 4+4, 4, 4) 다음이 정점 수
    const std::size_t vertexCountOffset = 17 + 13 + 8 + 4 + 4;
    ASSERT_EQ(bytes[vertexCountOffset], 3);
    bytes[vertexCountOffset] = 1;
    const LoadResult result = load(bytes);
    EXPECT_FALSE(result.model.has_value());
}

// ---------------------------------------------------------------------------
// 스킨 가중치·표정 (애니메이션, ADR-0010)
// ---------------------------------------------------------------------------

TEST(PmxImporter, SkinWeights_BdefAndSdefAreKept) {
    const LoadResult result = load(buildTestPmx(0, 2));
    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& v = result.model->vertices;

    EXPECT_EQ(v[0].joints[0], 0);  // BDEF1
    EXPECT_FLOAT_EQ(v[0].weights[0], 1.0f);
    EXPECT_FLOAT_EQ(v[0].weights[1], 0.0f);

    EXPECT_EQ(v[1].joints[0], 0);  // BDEF2 (0.5 / 0.5)
    EXPECT_EQ(v[1].joints[1], 1);
    EXPECT_FLOAT_EQ(v[1].weights[0], 0.5f);
    EXPECT_FLOAT_EQ(v[1].weights[1], 0.5f);

    EXPECT_EQ(v[2].joints[1], 1);  // SDEF는 BDEF2로 근사
    EXPECT_FLOAT_EQ(v[2].weights[1], 0.5f);
}

TEST(PmxImporter, VertexMorphs_BecomeExpressions) {
    const LoadResult result = load(buildTestPmx(1, 4));
    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& model = *result.model;

    const auto& blink = model.expression(deskpet::model::Expression::Blink);
    ASSERT_EQ(blink.vertices, (std::vector<std::uint32_t>{0, 2}));
    // 오프셋도 위치와 같은 변환 (× 0.08, z 반전)
    expectNear(blink.deltas[0], {0.0f, -0.08f, -0.16f});

    EXPECT_EQ(model.expression(deskpet::model::Expression::Surprised).vertices.size(), 1U);
    EXPECT_TRUE(model.expression(deskpet::model::Expression::Happy).vertices.empty());
}
