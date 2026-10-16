#include "model/OutlineHints.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <string>
#include <vector>

namespace deskpet::model {
namespace {

// 얼굴 부품·장식 재질 이름에 흔한 단어. 영어는 소문자로 비교, 일본어·한자는 그대로
constexpr std::array<std::string_view, 21> kFacePartWords = {
    "eye",   "iris",  "pupil",  "highlight", "brow",   "lash", "mouth",
    "teeth", "tooth", "tongue", "halo",      "目",     "眉",   "口",
    "歯",    "舌",    "瞳",     "睫",        "まつげ", "白目", "ハイライト"};

constexpr float kFlatRatio = 0.05f;  // 가장 얇은 축 < 가장 긴 축 × 이 값이면 판

std::string lower(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));  // ASCII만 바뀜 (UTF-8 바이트는 그대로)
    });
    return out;
}

bool hasFacePartName(std::string_view materialName) {
    const std::string name = lower(materialName);
    return std::ranges::any_of(kFacePartWords, [&](std::string_view word) {
        return name.find(word) != std::string::npos;
    });
}

// 눈 본이거나 그 자손이면 1 (Model::bones 순서). 본은 부모보다 앞에 올 수 있어 부모를 따라 올라감
std::vector<std::uint8_t> eyeBones(const Model& model) {
    std::vector<std::uint8_t> result(model.bones.size(), 0);
    for (std::size_t j = 0; j < model.bones.size(); ++j) {
        int bone = static_cast<int>(j);
        // 순환 참조가 있는 잘못된 파일에서도 끝나도록 본 수만큼만 올라감
        for (std::size_t depth = 0; bone >= 0 && depth < model.bones.size(); ++depth) {
            const HumanBone human = model.bones[static_cast<std::size_t>(bone)].human;
            if (human == HumanBone::LeftEye || human == HumanBone::RightEye) {
                result[j] = 1;
                break;
            }
            bone = model.bones[static_cast<std::size_t>(bone)].parent;
        }
    }
    return result;
}

OutlineSkip judge(const Model& model, const Primitive& primitive,
                  const std::vector<std::uint8_t>& eyes) {
    const std::size_t end =
        std::min<std::size_t>(primitive.firstIndex + primitive.indexCount, model.indices.size());
    std::size_t onEye = 0;
    std::size_t counted = 0;
    constexpr float kMax = std::numeric_limits<float>::max();
    core::Vec3 lo{kMax, kMax, kMax};
    core::Vec3 hi{-kMax, -kMax, -kMax};
    for (std::size_t i = primitive.firstIndex; i < end; ++i) {
        const std::uint32_t index = model.indices[i];
        if (index >= model.vertices.size()) {
            continue;
        }
        const Vertex& v = model.vertices[index];
        ++counted;
        if (v.weights[0] > 0.0f) {
            const auto dominant =
                static_cast<std::size_t>(std::ranges::max_element(v.weights) - v.weights.begin());
            const std::size_t joint = v.joints[dominant];
            if (joint < eyes.size() && eyes[joint] != 0) {
                ++onEye;
            }
        }
        lo = {std::min(lo.x, v.position.x), std::min(lo.y, v.position.y),
              std::min(lo.z, v.position.z)};
        hi = {std::max(hi.x, v.position.x), std::max(hi.y, v.position.y),
              std::max(hi.z, v.position.z)};
    }
    if (counted == 0) {
        return OutlineSkip::None;
    }
    if (onEye * 2 > counted) {
        return OutlineSkip::EyeBone;
    }
    if (primitive.material >= 0 &&
        static_cast<std::size_t>(primitive.material) < model.materials.size() &&
        hasFacePartName(model.materials[static_cast<std::size_t>(primitive.material)].name)) {
        return OutlineSkip::Name;
    }
    std::array<float, 3> extent = {hi.x - lo.x, hi.y - lo.y, hi.z - lo.z};
    std::ranges::sort(extent);
    if (extent[2] > 0.0f && extent[0] < extent[2] * kFlatRatio) {
        return OutlineSkip::Flat;
    }
    return OutlineSkip::None;
}

}  // namespace

void markOutlineSkips(Model& model) {
    const std::vector<std::uint8_t> eyes = eyeBones(model);
    for (Primitive& primitive : model.primitives) {
        primitive.outlineSkip = judge(model, primitive, eyes);
    }
}

std::string_view toString(OutlineSkip skip) {
    switch (skip) {
        case OutlineSkip::EyeBone: return "눈 본";
        case OutlineSkip::Name: return "이름";
        case OutlineSkip::Flat: return "납작함";
        case OutlineSkip::None: break;
    }
    return "-";
}

}  // namespace deskpet::model
