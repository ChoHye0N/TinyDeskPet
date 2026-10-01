#include "model/TextureSource.h"

#include "core/Log.h"

#include <algorithm>
#include <fstream>
#include <memory>
#include <span>
#include <stb_image.h>
#include <system_error>

namespace deskpet::model {
namespace {

bool startsWith(std::span<const std::uint8_t> bytes, std::string_view magic) {
    return bytes.size() >= magic.size() &&
           std::equal(magic.begin(), magic.end(), bytes.begin(),
                      [](char m, std::uint8_t b) { return static_cast<std::uint8_t>(m) == b; });
}

// WIC가 읽을 수 있는 형식의 MIME. 모르면 빈 문자열
std::string wicMimeType(std::span<const std::uint8_t> bytes) {
    if (startsWith(bytes, "\x89PNG")) {
        return "image/png";
    }
    if (startsWith(bytes, "\xFF\xD8\xFF")) {
        return "image/jpeg";
    }
    if (startsWith(bytes, "BM")) {
        return "image/bmp";
    }
    if (startsWith(bytes, "GIF8")) {
        return "image/gif";
    }
    if (startsWith(bytes, "DDS ")) {
        return "image/vnd-ms.dds";
    }
    if (startsWith(bytes, "II*") || startsWith(bytes, std::string_view("MM\0*", 4))) {
        return "image/tiff";
    }
    return {};
}

struct StbFree {
    void operator()(stbi_uc* pixels) const noexcept { stbi_image_free(pixels); }
};

}  // namespace

Texture makeTexture(std::vector<std::uint8_t> bytes, std::string name) {
    Texture texture;
    texture.name = std::move(name);
    texture.mimeType = wicMimeType(bytes);
    if (!texture.mimeType.empty() || bytes.empty()) {
        texture.encoded = std::move(bytes);
        return texture;
    }

    // TGA는 매직 바이트가 없어 stb_image(TGA 전용 빌드)로 직접 시도합니다.
    int width = 0;
    int height = 0;
    int channels = 0;
    const std::unique_ptr<stbi_uc, StbFree> pixels(
        stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height,
                              &channels, STBI_rgb_alpha));  // 원본 채널 수와 무관하게 RGBA로
    if (!pixels || width <= 0 || height <= 0) {
        texture.encoded = std::move(bytes);  // 렌더러가 마지막으로 시도 (실패하면 흰색)
        return texture;
    }
    texture.width = width;
    texture.height = height;
    const auto size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    texture.rgba.assign(pixels.get(), pixels.get() + size);
    return texture;
}

Texture loadTextureFile(const std::filesystem::path& baseDirectory,
                        std::string_view relativePathUtf8) {
    std::string normalized(relativePathUtf8);
    std::ranges::replace(normalized, '\\', '/');  // MMD·FBX는 Windows 구분자를 쓰는 경우가 많음

    Texture empty;
    empty.name = normalized;
    if (baseDirectory.empty() || normalized.empty()) {
        return empty;
    }

    // UTF-8 → path: u8string으로 만들어야 Windows에서 일본어·한글 파일 이름이 깨지지 않음
    const std::filesystem::path path =
        baseDirectory / std::filesystem::path(std::u8string(normalized.begin(), normalized.end()));
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        core::logging::warn("텍스처 파일을 찾을 수 없습니다: {}", normalized);
        return empty;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file.tellg()));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()))) {
        core::logging::warn("텍스처 파일을 읽을 수 없습니다: {}", normalized);
        return empty;
    }
    return makeTexture(std::move(bytes), normalized);
}

}  // namespace deskpet::model
