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

namespace {

// 왼쪽 절반 섬 + 섬 밖 (12, 5)에 색 칸 하나 (팔레트처럼 점으로 찍어 쓰는 색)
std::vector<std::uint8_t> makePaletteTexture() {
    auto rgba = makeHalfRedTexture();
    const auto i = static_cast<std::size_t>((5 * kSize + 12) * 4);
    rgba[i] = 128;
    rgba[i + 1] = 0;
    rgba[i + 2] = 255;
    return rgba;
}

// 텍셀 (x, y) 중심을 한 점으로 찍는 넓이 0 삼각형
UvTriangle pointAt(int x, int y) {
    const Vec2 p{(static_cast<float>(x) + 0.5f) / kSize, (static_cast<float>(y) + 0.5f) / kSize};
    return {p, p, p};
}

}  // namespace

TEST(TexturePadding, Auto_KeepsTexelsSampledByPointTriangles) {
    // 게임 모델의 얼굴처럼 삼각형이 색 칸 하나를 점으로 찍어 쓰면 그 칸은 "쓰는 텍셀". 덮어쓰면
    // 얼굴 전체가 이웃 섬 색으로 바뀜 (학습 노트 #37)
    auto triangles = kLeftHalf;
    triangles.push_back(pointAt(12, 5));
    auto rgba = makePaletteTexture();
    const int kept = padUvIslands(rgba, kSize, kSize, triangles, 6);
    EXPECT_EQ(pixel(rgba, 12, 5), (std::array<std::uint8_t, 4>{128, 0, 255, 255}));
    EXPECT_GT(kept, 0);
    EXPECT_EQ(pixel(rgba, 9, 5)[0], 255);  // 섬 옆 여백은 그대로 채움

    auto old = makePaletteTexture();  // on: 예전 방식 (모든 여백 채움) → 색 칸이 덮임
    EXPECT_EQ(padUvIslands(old, kSize, kSize, triangles, 6, deskpet::core::TexturePadding::On), 0);
    EXPECT_NE(pixel(old, 12, 5), (std::array<std::uint8_t, 4>{128, 0, 255, 255}));
}

TEST(TexturePadding, Auto_KeepsTexelsAlongLineTriangles) {
    // 가는 선으로 찍는 삼각형(넓이 거의 0): 선이 지나가는 텍셀도 보존
    auto rgba = makeHalfRedTexture();
    for (int x = 10; x <= 14; ++x) {
        rgba[static_cast<std::size_t>((7 * kSize + x) * 4 + 2)] = 200;
    }
    const auto before = rgba;
    auto triangles = kLeftHalf;
    const Vec2 a{10.5f / kSize, 7.5f / kSize};
    const Vec2 b{14.5f / kSize, 7.5f / kSize};
    triangles.push_back({a, b, a});
    (void)padUvIslands(rgba, kSize, kSize, triangles, 6);
    for (int x = 10; x <= 14; ++x) {
        EXPECT_EQ(pixel(rgba, x, 7), pixel(before, x, 7)) << "x=" << x;
    }
}

TEST(TexturePadding, Auto_SameAsOnWithoutThinTriangles) {
    // 보통 삼각형만 있으면 결과가 예전과 똑같음 (일반 모델은 달라지지 않음)
    auto autoResult = makeHalfRedTexture();
    auto onResult = makeHalfRedTexture();
    EXPECT_EQ(padUvIslands(autoResult, kSize, kSize, kLeftHalf, 3), 0);
    (void)padUvIslands(onResult, kSize, kSize, kLeftHalf, 3, deskpet::core::TexturePadding::On);
    EXPECT_EQ(autoResult, onResult);
}

TEST(TexturePadding, Off_DoesNothing) {
    auto rgba = makeHalfRedTexture();
    const auto before = rgba;
    (void)padUvIslands(rgba, kSize, kSize, kLeftHalf, 3, deskpet::core::TexturePadding::Off);
    EXPECT_EQ(rgba, before);
}
