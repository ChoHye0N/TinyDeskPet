// 모션 파일 진입점: 형식 판별 → 형식별 로더 (ADR-0013). 명세: docs/03-detailed-design/model.md §4.9

#include "model/Importers.h"
#include "model/Motion.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <format>
#include <fstream>
#include <string>
#include <unordered_set>
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
    // 모델과 같은 판별 (glTF/GLB → VRMA 계열, 일반 glTF인지는 읽을 때 앎). PMX 등 모션이 아닌
    // 형식은 Unknown
    switch (detectModelFormat(bytes, extension)) {
        case ModelFormat::Gltf: return MotionFormat::Vrma;
        case ModelFormat::Fbx: return MotionFormat::Fbx;
        default: break;
    }
    return toLower(extension) == ".vmd" ? MotionFormat::Vmd : MotionFormat::Unknown;
}

MotionLoadResult loadMotionFromMemory(std::span<const std::uint8_t> bytes,
                                      std::string_view extension, std::string_view clipName) {
    switch (detectMotionFormat(bytes, extension)) {
        case MotionFormat::Vrma:
        case MotionFormat::Gltf: return detail::importGltfMotion(bytes, clipName);
        case MotionFormat::Vmd:
            if (!clipName.empty()) {
                return {std::nullopt,
                        std::format("VMD는 파일 하나에 모션이 하나라 클립 이름('{}')을 "
                                    "지정할 수 없습니다",
                                    clipName)};
            }
            return detail::importVmd(bytes);
        case MotionFormat::Fbx: return detail::importFbxMotion(bytes, clipName);
        case MotionFormat::Unknown: break;
    }
    return {std::nullopt,
            std::format("지원하지 않는 모션 형식입니다 (확장자 '{}'). VRMA·glTF, VMD, FBX만 읽을 "
                        "수 있습니다",
                        extension)};
}

MotionLoadResult loadMotionFile(const std::filesystem::path& reference) {
    // "파일#클립": 경로 전체가 파일이 아닐 때만 마지막 '#' 뒤를 클립 이름으로
    std::filesystem::path path = reference;
    std::string clipName;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(reference, ec)) {
        const std::u8string text = reference.filename().u8string();
        if (const auto hash = text.rfind(u8'#'); hash != std::u8string::npos && hash > 0) {
            path = reference.parent_path() / std::u8string_view(text).substr(0, hash);
            clipName.assign(text.begin() + static_cast<std::ptrdiff_t>(hash) + 1, text.end());
        }
    }
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
    return loadMotionFromMemory(bytes, std::string(extension.begin(), extension.end()), clipName);
}

namespace motion {

void setRestDirections(MotionClip& clip,
                       const std::array<std::optional<core::Vec3>, kHumanBoneCount>& positions) {
    for (std::size_t i = 0; i < kHumanBoneCount; ++i) {
        const auto child = static_cast<std::size_t>(limbChild(static_cast<HumanBone>(i)));
        // 지역 변수로 받아 검사: 배열 원소는 clang-tidy가 검사한 값과 같은 것으로 추적하지 못함
        const std::optional<core::Vec3> from = positions[i];
        const std::optional<core::Vec3> to = child != 0 ? positions[child] : std::nullopt;
        if (!from || !to) {
            continue;
        }
        const core::Vec3 d = *to - *from;
        if (core::dot(d, d) > 1e-12f) {
            clip.restDirections[i] = core::normalize(d);
        }
    }
}

void addBoneTracks(MotionClip& clip, std::span<const SourceNode> nodes,
                   const std::vector<std::vector<core::Quat>>& deltas) {
    // 같은 회전이면 쿼터니언 내적의 절댓값이 1 (q와 −q는 같은 회전). 0.1°쯤까지는 같은 것으로
    constexpr float kSameRotation = 1.0f - 1e-6f;
    std::unordered_set<std::string_view> added;
    for (std::size_t n = 0; n < nodes.size() && n < deltas.size(); ++n) {
        const SourceNode& node = nodes[n];
        if (node.humanoid || node.name.empty() || deltas[n].empty() || added.contains(node.name)) {
            continue;
        }
        const auto parent = static_cast<std::size_t>(node.parent);
        const std::vector<core::Quat>* up =
            node.parent >= 0 && parent < deltas.size() ? &deltas[parent] : nullptr;
        bool moves = false;
        for (std::size_t f = 0; f < deltas[n].size() && !moves; ++f) {
            const core::Quat a = deltas[n][f];
            const core::Quat b = up != nullptr && f < up->size() ? (*up)[f] : core::Quat{};
            moves = std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) < kSameRotation;
        }
        if (moves) {
            added.insert(node.name);
            clip.boneTracks.push_back({std::string(node.name), deltas[n]});
        }
    }
}

void setHipsTranslation(MotionClip& clip, std::span<const core::Vec3> positions, core::Vec3 rest) {
    // 원본 단위의 1e-4배 이하는 계산 오차로 보고 움직이지 않은 것으로 (m면 0.1mm, MMD면 0.01mm)
    const float tolerance = std::max(std::abs(rest.y), 1.0f) * 1e-4f;
    const bool moves = std::ranges::any_of(positions, [&](core::Vec3 p) {
        const core::Vec3 d = p - rest;
        return std::abs(d.x) > tolerance || std::abs(d.y) > tolerance || std::abs(d.z) > tolerance;
    });
    if (!moves || rest.y <= 0.0f) {
        return;  // 이동 없음, 또는 엉덩이가 바닥 아래인 원본 (높이 비율을 잴 수 없음)
    }
    clip.hipsHeight = rest.y;
    clip.hipsTranslation.clear();
    clip.hipsTranslation.reserve(positions.size());
    for (const core::Vec3 p : positions) {
        clip.hipsTranslation.push_back(p - rest);
    }
}

}  // namespace motion

}  // namespace deskpet::model
