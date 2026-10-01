#include "model/ModelLoader.h"

#include "model/Importers.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <limits>
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

Bounds computeBounds(const std::vector<Vertex>& vertices) {
    constexpr float kMax = std::numeric_limits<float>::max();
    core::Vec3 lo{kMax, kMax, kMax};
    core::Vec3 hi{-kMax, -kMax, -kMax};
    for (const Vertex& v : vertices) {
        lo = {std::min(lo.x, v.position.x), std::min(lo.y, v.position.y),
              std::min(lo.z, v.position.z)};
        hi = {std::max(hi.x, v.position.x), std::max(hi.y, v.position.y),
              std::max(hi.z, v.position.z)};
    }
    return {lo, hi};
}

void setSkinWeights(Vertex& vertex, std::span<const std::pair<int, float>> influences) {
    // 같은 본은 합치고(BDEF4에 같은 본이 두 번 나오는 파일 등), 음수 본·0 이하 가중치는 버림
    std::vector<std::pair<int, float>> merged;
    for (const auto& [bone, weight] : influences) {
        if (bone < 0 || bone > std::numeric_limits<std::uint16_t>::max() || !(weight > 0.0f)) {
            continue;
        }
        const auto it = std::ranges::find(merged, bone, &std::pair<int, float>::first);
        if (it != merged.end()) {
            it->second += weight;
        } else {
            merged.emplace_back(bone, weight);
        }
    }
    std::ranges::sort(merged, [](const auto& a, const auto& b) { return a.second > b.second; });
    if (merged.size() > kMaxInfluences) {
        merged.resize(kMaxInfluences);  // GPU 스키닝은 정점당 4개까지
    }

    float total = 0.0f;
    for (const auto& entry : merged) {
        total += entry.second;
    }
    vertex.joints = {};
    vertex.weights = {};
    if (total <= 0.0f) {
        return;
    }
    for (std::size_t i = 0; i < merged.size(); ++i) {
        vertex.joints[i] = static_cast<std::uint16_t>(merged[i].first);
        vertex.weights[i] = merged[i].second / total;
    }
}

ModelFormat detectModelFormat(std::span<const std::uint8_t> bytes, std::string_view extension) {
    // 매직 바이트가 확장자보다 우선 (확장자가 잘못 붙은 파일 대비)
    if (startsWith(bytes, "glTF")) {
        return ModelFormat::Gltf;  // GLB 컨테이너 (.glb, .vrm)
    }
    if (startsWith(bytes, "PMX ")) {
        return ModelFormat::Pmx;
    }
    if (startsWith(bytes, "Kaydara FBX Binary")) {
        return ModelFormat::Fbx;
    }
    if (startsWith(bytes, "; FBX")) {
        return ModelFormat::Fbx;  // ASCII FBX의 첫 줄 주석
    }

    if (toLower(extension) == ".fbx") {
        return ModelFormat::Fbx;  // 주석 없는 ASCII FBX
    }
    // glTF JSON: 첫 비공백 문자가 '{'
    const auto first = std::ranges::find_if(
        bytes, [](std::uint8_t c) { return c != ' ' && c != '\t' && c != '\r' && c != '\n'; });
    if (first != bytes.end() && *first == '{') {
        return ModelFormat::Gltf;
    }
    return ModelFormat::Unknown;
}

LoadResult loadModelFromMemory(std::span<const std::uint8_t> bytes, std::string_view extension,
                               const ImportContext& context) {
    switch (detectModelFormat(bytes, extension)) {
        case ModelFormat::Gltf: return detail::importGltf(bytes, context);
        case ModelFormat::Pmx: return detail::importPmx(bytes, context);
        case ModelFormat::Fbx: return detail::importFbx(bytes, context);
        case ModelFormat::Unknown: break;
    }
    return {std::nullopt, std::format("지원하지 않는 모델 형식입니다 (확장자 '{}'). "
                                      "VRM/glTF/GLB, PMX, FBX만 읽을 수 있습니다",
                                      extension)};
}

LoadResult loadModelFile(const std::filesystem::path& path) {
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
    // 라이브러리에 경로(char*)를 넘기지 않고 직접 읽어 메모리로 넘깁니다 (한글·일본어 경로 문제
    // 회피).
    const std::u8string extension = path.extension().u8string();
    return loadModelFromMemory(bytes, std::string(extension.begin(), extension.end()),
                               {path.parent_path()});
}

}  // namespace deskpet::model
