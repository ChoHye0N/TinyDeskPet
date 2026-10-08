// 모션 파일 진입점: 형식 판별 → 형식별 로더 (ADR-0013). 명세: docs/03-detailed-design/model.md §4.9

#include "model/Importers.h"
#include "model/Motion.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <string>
#include <vector>

namespace deskpet::model {
namespace {

bool startsWith(std::span<const std::uint8_t> bytes, std::string_view magic) {
    return bytes.size() >= magic.size() &&
           std::equal(magic.begin(), magic.end(), bytes.begin(),
                      [](char m, std::uint8_t b) { return static_cast<std::uint8_t>(m) == b; });
}

std::string toLower(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

}  // namespace

std::size_t MotionClip::trackCount() const noexcept {
    return static_cast<std::size_t>(
        std::ranges::count_if(tracks, [](const auto& track) { return !track.empty(); }));
}

MotionFormat detectMotionFormat(std::span<const std::uint8_t> bytes, std::string_view extension) {
    if (startsWith(bytes, "Vocaloid Motion Data")) {
        return MotionFormat::Vmd;
    }
    // 모델과 같은 판별 (glTF/GLB → VRMA, FBX). PMX 등 모션이 아닌 형식은 Unknown
    switch (detectModelFormat(bytes, extension)) {
        case ModelFormat::Gltf: return MotionFormat::Vrma;
        case ModelFormat::Fbx: return MotionFormat::Fbx;
        default: break;
    }
    return toLower(extension) == ".vmd" ? MotionFormat::Vmd : MotionFormat::Unknown;
}

MotionLoadResult loadMotionFromMemory(std::span<const std::uint8_t> bytes,
                                      std::string_view extension) {
    switch (detectMotionFormat(bytes, extension)) {
        case MotionFormat::Vrma: return detail::importVrma(bytes);
        case MotionFormat::Vmd: return detail::importVmd(bytes);
        case MotionFormat::Fbx: return detail::importFbxMotion(bytes);
        case MotionFormat::Unknown: break;
    }
    return {std::nullopt,
            std::format("지원하지 않는 모션 형식입니다 (확장자 '{}'). VRMA, VMD, FBX만 읽을 수 "
                        "있습니다",
                        extension)};
}

MotionLoadResult loadMotionFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return {std::nullopt, "파일을 열 수 없습니다"};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file.tellg()));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()))) {
        return {std::nullopt, "파일을 읽을 수 없습니다"};
    }
    const std::u8string extension = path.extension().u8string();
    return loadMotionFromMemory(bytes, std::string(extension.begin(), extension.end()));
}

namespace motion {

void setRestDirections(MotionClip& clip,
                       const std::array<std::optional<core::Vec3>, kHumanBoneCount>& positions) {
    for (std::size_t i = 0; i < kHumanBoneCount; ++i) {
        const auto child = static_cast<std::size_t>(limbChild(static_cast<HumanBone>(i)));
        if (child == 0 || !positions[i] || !positions[child]) {
            continue;
        }
        const core::Vec3 d = *positions[child] - *positions[i];
        if (core::dot(d, d) > 1e-12f) {
            clip.restDirections[i] = core::normalize(d);
        }
    }
}

}  // namespace motion

}  // namespace deskpet::model
