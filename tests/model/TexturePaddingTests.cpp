#include "model/TexturePadding.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

using deskpet::core::Vec2;
using deskpet::model::collectUvTriangles;
using deskpet::model::padUvIslands;
using deskpet::model::UvTriangle;

namespace {

constexpr int kSize = 16;

// 16×16 검은 텍스처의 왼쪽 절반(x < 8)을 빨강으로 칠한 것 = "섬" + 검은 여백
std::vector<std::uint8_t> makeHalfRedTexture() {
    std::vector<std::uint8_t> rgba(kSize * kSize * 4, 0);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const auto i = static_cast<std::size_t>((y * kSize + x) * 4);
            rgba[i + 3] = 255;
            if (x < 8) {
                rgba[i] = 255;
            }
        }
    }
    return rgba;
}

std::array<std::uint8_t, 4> pixel(const std::vector<std::uint8_t>& rgba, int x, int y) {
    const auto i = static_cast<std::size_t>((y * kSize + x) * 4);
    return {rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]};
}

// 섬(왼쪽 절반)을 덮는 UV 삼각형 2개
const std::vector<UvTriangle> kLeftHalf = {
    {Vec2{0.0f, 0.0f}, Vec2{0.5f, 0.0f}, Vec2{0.0f, 1.0f}},
    {Vec2{0.5f, 0.0f}, Vec2{0.5f, 1.0f}, Vec2{0.0f, 1.0f}},
};

}  // namespace

TEST(TexturePadding, FillsGutterNextToIslandWithIslandColor) {
    auto rgba = makeHalfRedTexture();
    padUvIslands(rgba, kSize, kSize, kLeftHalf, 3);

    // 섬 바로 바깥 3픽셀(x = 8, 9, 10)은 빨강으로, 그보다 먼 곳은 원래 검정
    for (int x = 8; x <= 10; ++x) {
        EXPECT_EQ(pixel(rgba, x, 5)[0], 255) << "x=" << x;
    }
    EXPECT_EQ(pixel(rgba, 12, 5)[0], 0);
}

TEST(TexturePadding, IslandPixelsAreUnchanged) {
    auto rgba = makeHalfRedTexture();
    const auto before = rgba;
    padUvIslands(rgba, kSize, kSize, kLeftHalf, 3);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < 8; ++x) {
            EXPECT_EQ(pixel(rgba, x, y), pixel(before, x, y));
        }
    }
}

TEST(TexturePadding, WithoutTriangles_DoesNothing) {
    auto rgba = makeHalfRedTexture();
    const auto before = rgba;
    padUvIslands(rgba, kSize, kSize, {}, 3);
    EXPECT_EQ(rgba, before);
}

TEST(TexturePadding, RepeatedUvMapsToSameTexels) {
    // UV가 [1, 2) 범위여도 반복(REPEAT) 샘플링과 같은 텍셀로 봄
    auto rgba = makeHalfRedTexture();
    const std::vector<UvTriangle> shifted = {
        {Vec2{1.0f, 0.0f}, Vec2{1.5f, 0.0f}, Vec2{1.0f, 1.0f}},
        {Vec2{1.5f, 0.0f}, Vec2{1.5f, 1.0f}, Vec2{1.0f, 1.0f}},
    };
    padUvIslands(rgba, kSize, kSize, shifted, 2);
    EXPECT_EQ(pixel(rgba, 9, 5)[0], 255);
    EXPECT_EQ(pixel(rgba, 12, 5)[0], 0);
}

TEST(TexturePadding, CollectsOnlyTrianglesUsingTheTexture) {
    deskpet::model::Model model;
    model.vertices.resize(6);
    model.vertices[1].u = 1.0f;
    model.vertices[2].v = 1.0f;
    model.indices = {0, 1, 2, 3, 4, 5};
    model.materials.resize(2);
    model.materials[0].baseColorTexture = 0;
    model.materials[1].baseColorTexture = 1;
    model.primitives = {{0, 3, 0}, {3, 3, 1}};

    const auto triangles = collectUvTriangles(model, 0);
    ASSERT_EQ(triangles.size(), 1U);
    EXPECT_FLOAT_EQ(triangles[0][1].x, 1.0f);
    EXPECT_FLOAT_EQ(triangles[0][2].y, 1.0f);
    EXPECT_TRUE(collectUvTriangles(model, 5).empty());
}
