#include "model/TextureSource.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using deskpet::model::loadTextureFile;
using deskpet::model::makeTexture;
namespace fs = std::filesystem;

namespace {

// 2×1 무압축 32비트 TGA. 픽셀은 BGRA 순서, 디스크립터 0x28 = 위에서 아래 + 알파 8비트
std::vector<std::uint8_t> makeTga() {
    std::vector<std::uint8_t> tga = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 0, 32, 0x28};
    const std::vector<std::uint8_t> pixels = {0, 0, 255, 255, 255, 0, 0, 128};
    tga.insert(tga.end(), pixels.begin(), pixels.end());
    return tga;
}

}  // namespace

TEST(TextureSource, Png_IsKeptEncodedForRenderer) {
    const std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    const auto texture = makeTexture(png, "a.png");
    EXPECT_EQ(texture.encoded, png);
    EXPECT_EQ(texture.mimeType, "image/png");
    EXPECT_TRUE(texture.rgba.empty());
    EXPECT_EQ(texture.name, "a.png");
}

TEST(TextureSource, Tga_IsDecodedToRgba) {
    const auto texture = makeTexture(makeTga(), "skin.tga");
    EXPECT_TRUE(texture.encoded.empty());
    ASSERT_EQ(texture.width, 2);
    ASSERT_EQ(texture.height, 1);
    EXPECT_EQ(texture.rgba, (std::vector<std::uint8_t>{255, 0, 0, 255, 0, 0, 255, 128}));
}

TEST(TextureSource, UnknownBytes_AreKeptEncoded) {
    const std::vector<std::uint8_t> bytes = {1, 2, 3};
    const auto texture = makeTexture(bytes, "x.bin");
    EXPECT_EQ(texture.encoded, bytes);
    EXPECT_FALSE(texture.empty());
}

TEST(TextureSource, LoadFile_ResolvesBackslashRelativePath) {
    const fs::path dir = fs::temp_directory_path() / "deskpet_texture_source";
    fs::remove_all(dir);
    fs::create_directories(dir / "tex");
    {
        std::ofstream out(dir / "tex" / "face.tga", std::ios::binary);
        const auto tga = makeTga();
        out.write(reinterpret_cast<const char*>(tga.data()),
                  static_cast<std::streamsize>(tga.size()));
    }

    const auto texture = loadTextureFile(dir, "tex\\face.tga");  // MMD는 Windows 경로 구분자 사용
    EXPECT_EQ(texture.width, 2);

    EXPECT_TRUE(loadTextureFile(dir, "tex\\missing.png").empty());
    EXPECT_TRUE(loadTextureFile({}, "tex\\face.tga").empty());  // 기준 폴더 없음 → 읽지 않음
    fs::remove_all(dir);
}
