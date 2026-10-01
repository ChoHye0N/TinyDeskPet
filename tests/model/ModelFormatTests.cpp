#include "model/ModelLoader.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

using deskpet::model::detectModelFormat;
using deskpet::model::loadModelFromMemory;
using deskpet::model::ModelFormat;

namespace {

ModelFormat detect(std::string_view head, std::string_view extension = {}) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(head.data());
    return detectModelFormat(std::span(bytes, head.size()), extension);
}

}  // namespace

TEST(ModelFormat, DetectsByMagicBytes) {
    EXPECT_EQ(detect("glTF\x02\0\0\0"), ModelFormat::Gltf);    // GLB / VRM
    EXPECT_EQ(detect("  {\"asset\":{}}"), ModelFormat::Gltf);  // glTF JSON
    EXPECT_EQ(detect("PMX \0\0\0\x40"), ModelFormat::Pmx);
    EXPECT_EQ(detect(std::string_view("Kaydara FBX Binary  \0\x1a\0", 23)), ModelFormat::Fbx);
}

TEST(ModelFormat, MagicWinsOverExtension) {
    EXPECT_EQ(detect("PMX \0\0\0\x40", ".vrm"), ModelFormat::Pmx);  // 확장자가 잘못된 파일
}

TEST(ModelFormat, AsciiFbx_IsDetectedByExtensionOrHeaderComment) {
    EXPECT_EQ(detect("FBXHeaderExtension: {", ".FBX"), ModelFormat::Fbx);
    EXPECT_EQ(detect("; FBX 7.4.0 project file\n"), ModelFormat::Fbx);
}

TEST(ModelFormat, UnknownFormat_IsRejectedWithMessage) {
    EXPECT_EQ(detect("PK\x03\x04", ".zip"), ModelFormat::Unknown);
    EXPECT_EQ(detect("Pmd"), ModelFormat::Unknown);  // 구 PMD 형식은 미지원

    const std::string_view zip = "PK\x03\x04";
    const auto result = loadModelFromMemory(
        std::span(reinterpret_cast<const std::uint8_t*>(zip.data()), zip.size()), ".zip");
    EXPECT_FALSE(result.model.has_value());
    EXPECT_NE(result.error.find("지원하지 않는"), std::string::npos);
}

// ---------------------------------------------------------------------------
// 스킨 가중치 정리 (모든 로더 공통)
// ---------------------------------------------------------------------------

TEST(SkinWeights, KeepsLargestFourMergesDuplicatesAndNormalizes) {
    deskpet::model::Vertex v;
    const std::vector<std::pair<int, float>> influences = {
        {3, 0.1f}, {1, 0.4f}, {3, 0.1f}, {7, 0.05f}, {2, 0.3f}, {9, 0.02f}, {-1, 0.5f}};
    deskpet::model::setSkinWeights(v, influences);

    // 3은 합쳐져 0.2, 음수 본(-1)과 가장 작은 9는 버려짐 → 0.4, 0.3, 0.2, 0.05 정규화
    EXPECT_EQ(v.joints[0], 1);
    EXPECT_EQ(v.joints[1], 2);
    EXPECT_EQ(v.joints[2], 3);
    EXPECT_EQ(v.joints[3], 7);
    EXPECT_NEAR(v.weights[0] + v.weights[1] + v.weights[2] + v.weights[3], 1.0f, 1e-6f);
    EXPECT_NEAR(v.weights[0], 0.4f / 0.95f, 1e-6f);
}

TEST(SkinWeights, AllZero_LeavesVertexUnskinned) {
    deskpet::model::Vertex v;
    const std::vector<std::pair<int, float>> influences = {{0, 0.0f}};
    deskpet::model::setSkinWeights(v, influences);
    EXPECT_FLOAT_EQ(v.weights[0], 0.0f);
}
