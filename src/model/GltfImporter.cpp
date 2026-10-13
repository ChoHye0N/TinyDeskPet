// glTF 2.0 / GLB / VRM 0.x·1.0 로더 (cgltf, ADR-0005). 명세: docs/03-detailed-design/model.md §4.1

#include "core/Math.h"
#include "model/Importers.h"
#include "model/TextureSource.h"

#include <algorithm>
#include <array>
#include <cgltf.h>
#include <charconv>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace deskpet::model::detail {
namespace {

using core::Mat4;
using core::Vec3;

struct DataDeleter {
    void operator()(cgltf_data* data) const noexcept { cgltf_free(data); }
};
using DataPtr = std::unique_ptr<cgltf_data, DataDeleter>;

std::string resultToString(cgltf_result result) {
    switch (result) {
        case cgltf_result_data_too_short: return "데이터가 너무 짧습니다";
        case cgltf_result_unknown_format: return "glTF/GLB 형식이 아닙니다";
        case cgltf_result_invalid_json: return "JSON이 잘못되었습니다";
        case cgltf_result_invalid_gltf: return "glTF 구조가 잘못되었습니다";
        case cgltf_result_file_not_found: return "외부 버퍼 파일을 찾을 수 없습니다";
        case cgltf_result_io_error: return "입출력 오류";
        case cgltf_result_out_of_memory: return "메모리 부족";
        case cgltf_result_legacy_gltf: return "glTF 1.0은 지원하지 않습니다";
        default: return std::format("cgltf 오류 {}", static_cast<int>(result));
    }
}

VrmVersion detectVersion(const cgltf_data& data) {
    for (cgltf_size i = 0; i < data.extensions_used_count; ++i) {
        const std::string_view name = data.extensions_used[i];
        if (name == "VRMC_vrm") {
            return VrmVersion::V1;  // 1.0이 우선 (둘 다 있으면 1.0으로 해석)
        }
    }
    for (cgltf_size i = 0; i < data.extensions_used_count; ++i) {
        if (std::string_view(data.extensions_used[i]) == "VRM") {
            return VrmVersion::V0;
        }
    }
    return VrmVersion::None;
}

Mat4 worldMatrix(const cgltf_node& node) {
    std::array<float, 16> values{};
    cgltf_node_transform_world(&node, values.data());
    return Mat4::fromColumnMajor(values.data());
}

// 노드 메시의 모프 타깃 수 = weights 채널이 키마다 주는 값 수. 메시 weights 수, 없으면 첫
// 프리미티브의 타깃 수
cgltf_size morphTargetCount(const cgltf_node& node) {
    const cgltf_mesh* mesh = node.mesh;
    if (mesh == nullptr) {
        return 0;
    }
    if (mesh->weights_count > 0) {
        return mesh->weights_count;
    }
    return mesh->primitives_count > 0 ? mesh->primitives[0].targets_count : 0;
}

// 관절 행렬 = 역바인드 × 관절의 현재 월드 (행 벡터 규약). 바인드 포즈에서는 단위 행렬에 가깝습니다.
std::vector<Mat4> jointMatrices(const cgltf_skin& skin) {
    std::vector<Mat4> result(skin.joints_count, Mat4::identity());
    for (cgltf_size j = 0; j < skin.joints_count; ++j) {
        Mat4 inverseBind = Mat4::identity();
        if (skin.inverse_bind_matrices != nullptr) {
            std::array<float, 16> values{};
            cgltf_accessor_read_float(skin.inverse_bind_matrices, j, values.data(), 16);
            inverseBind = Mat4::fromColumnMajor(values.data());
        }
        result[j] = inverseBind * worldMatrix(*skin.joints[j]);
    }
    return result;
}

AlphaMode toAlphaMode(cgltf_alpha_mode mode) {
    switch (mode) {
        case cgltf_alpha_mode_mask: return AlphaMode::Mask;
        case cgltf_alpha_mode_blend: return AlphaMode::Blend;
        default: return AlphaMode::Opaque;
    }
}

// ---------------------------------------------------------------------------
// VRM 휴머노이드 (확장 JSON에서 humanBones만 읽는 최소 스캐너)
// cgltf는 모르는 확장을 JSON 문자열 그대로 넘겨주므로, 필요한 부분만 직접 찾습니다.
// ---------------------------------------------------------------------------

std::size_t skipSpaces(std::string_view json, std::size_t pos) {
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' ||
                                 json[pos] == '\t' || json[pos] == ',')) {
        ++pos;
    }
    return pos;
}

// json[pos]가 '{' 또는 '['일 때 짝이 맞는 닫는 괄호의 다음 위치. 문자열 안 괄호는 무시
std::size_t skipBalanced(std::string_view json, std::size_t pos) {
    int depth = 0;
    bool inString = false;
    for (; pos < json.size(); ++pos) {
        const char c = json[pos];
        if (inString) {
            if (c == '\\') {
                ++pos;  // 이스케이프 다음 문자 건너뜀
            } else if (c == '"') {
                inString = false;
            }
        } else if (c == '"') {
            inString = true;
        } else if (c == '{' || c == '[') {
            ++depth;
        } else if ((c == '}' || c == ']') && --depth == 0) {
            return pos + 1;
        }
    }
    return std::string_view::npos;
}

// "key": 다음의 값 시작 위치 (첫 번째로 나오는 키)
std::optional<std::size_t> valueOf(std::string_view json, std::string_view key) {
    const std::string quoted = std::format("\"{}\"", key);
    std::size_t pos = json.find(quoted);
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    pos = json.find(':', pos + quoted.size());
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    return skipSpaces(json, pos + 1);
}

std::optional<int> intMember(std::string_view json, std::string_view key) {
    const auto pos = valueOf(json, key);
    int value = 0;
    if (!pos ||
        std::from_chars(json.data() + *pos, json.data() + json.size(), value).ec != std::errc{}) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::string_view> stringMember(std::string_view json, std::string_view key) {
    const auto pos = valueOf(json, key);
    if (!pos || *pos >= json.size() || json[*pos] != '"') {
        return std::nullopt;
    }
    const std::size_t end = json.find('"', *pos + 1);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    return json.substr(*pos + 1, end - *pos - 1);
}

void assignHumanBone(std::vector<Bone>& bones, HumanBone human, std::optional<int> node) {
    if (human != HumanBone::None && node && *node >= 0 &&
        static_cast<std::size_t>(*node) < bones.size()) {
        bones[static_cast<std::size_t>(*node)].human = human;
    }
}

// key의 값이 객체·배열이면 괄호를 포함한 범위
std::optional<std::string_view> valueSpan(std::string_view json, std::string_view key) {
    const auto start = valueOf(json, key);
    if (!start || *start >= json.size() || (json[*start] != '{' && json[*start] != '[')) {
        return std::nullopt;
    }
    const std::size_t end = skipBalanced(json, *start);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    return json.substr(*start, end - *start);
}

// 배열 [ {...}, {...} ]의 각 객체에 대해 f(객체 문자열)
template <class F>
void forEachObject(std::string_view array, F&& f) {
    for (std::size_t pos = skipSpaces(array, 1); pos < array.size(); pos = skipSpaces(array, pos)) {
        if (array[pos] != '{') {
            return;
        }
        const std::size_t end = skipBalanced(array, pos);
        if (end == std::string_view::npos) {
            return;
        }
        f(array.substr(pos, end - pos));
        pos = end;
    }
}

std::optional<float> floatMember(std::string_view json, std::string_view key) {
    const auto pos = valueOf(json, key);
    float value = 0.0f;
    if (!pos ||
        std::from_chars(json.data() + *pos, json.data() + json.size(), value).ec != std::errc{}) {
        return std::nullopt;
    }
    return value;
}

// 객체 바로 아래 단계의 key 값(객체·배열)만 찾음. valueSpan은 처음 나온 같은 이름을 찾으므로
// 안쪽 객체에 같은 키가 있으면 엉뚱한 값을 읽음 (예:
// secondaryAnimation.boneGroups[].colliderGroups)
std::optional<std::string_view> childSpan(std::string_view object, std::string_view key) {
    for (std::size_t pos = skipSpaces(object, 1); pos < object.size() && object[pos] == '"';
         pos = skipSpaces(object, pos)) {
        const std::size_t nameEnd = object.find('"', pos + 1);
        const std::size_t colon = object.find(':', nameEnd);
        if (nameEnd == std::string_view::npos || colon == std::string_view::npos) {
            return std::nullopt;
        }
        const std::string_view name = object.substr(pos + 1, nameEnd - pos - 1);
        const std::size_t start = skipSpaces(object, colon + 1);
        if (start >= object.size()) {
            return std::nullopt;
        }
        std::size_t end = start;
        if (object[start] == '{' || object[start] == '[') {
            end = skipBalanced(object, start);
            if (end == std::string_view::npos) {
                return std::nullopt;
            }
            if (name == key) {
                return object.substr(start, end - start);
            }
        } else if (object[start] == '"') {
            end = object.find('"', start + 1);
            end = end == std::string_view::npos ? object.size() : end + 1;
        } else {
            end = object.find_first_of(",}", start);
            end = end == std::string_view::npos ? object.size() : end;
        }
        pos = end;
    }
    return std::nullopt;
}

// {"x":…, "y":…, "z":…} 객체 멤버. 없으면 fallback
core::Vec3 vec3Member(std::string_view object, std::string_view key, core::Vec3 fallback) {
    const auto span = valueSpan(object, key);
    if (!span) {
        return fallback;
    }
    return {floatMember(*span, "x").value_or(0.0f), floatMember(*span, "y").value_or(0.0f),
            floatMember(*span, "z").value_or(0.0f)};
}

// "[0.1, 0.2, ...]" 배열의 숫자들. 숫자가 아닌 값을 만나면 거기서 멈춤
std::vector<float> floatArray(std::string_view array) {
    std::vector<float> values;
    for (std::size_t pos = skipSpaces(array, 1); pos < array.size() && array[pos] != ']';
         pos = skipSpaces(array, pos)) {
        float value = 0.0f;
        const auto [end, ec] =
            std::from_chars(array.data() + pos, array.data() + array.size(), value);
        if (ec != std::errc{}) {
            break;
        }
        values.push_back(value);
        pos = static_cast<std::size_t>(end - array.data());
    }
    return values;
}

core::Vec4 colorOr(std::optional<std::string_view> array, core::Vec4 fallback) {
    if (!array) {
        return fallback;
    }
    const std::vector<float> v = floatArray(*array);
    if (v.size() < 3) {
        return fallback;
    }
    return {v[0], v[1], v[2], v.size() >= 4 ? v[3] : 1.0f};
}

core::Vec3 rgbOr(std::optional<std::string_view> array, core::Vec3 fallback) {
    const core::Vec4 c = colorOr(array, {fallback.x, fallback.y, fallback.z, 1.0f});
    return {c.x, c.y, c.z};
}

// VRM 0.x MToon 색은 Unity 재질 색 그대로(sRGB)라 선형으로 변환 (VRM 1.0·glTF는 이미 선형)
core::Vec3 linearRgb(core::Vec3 srgb) {
    return {core::srgbToLinear(srgb.x), core::srgbToLinear(srgb.y), core::srgbToLinear(srgb.z)};
}

std::string_view extensionJson(const cgltf_data& data, std::string_view wanted) {
    for (cgltf_size i = 0; i < data.data_extensions_count; ++i) {
        const cgltf_extension& extension = data.data_extensions[i];
        if (extension.name != nullptr && extension.data != nullptr && wanted == extension.name) {
            return extension.data;
        }
    }
    return {};
}

// humanBones 괄호 안쪽(list)의 항목을 차례로 읽어 본에 휴머노이드 이름을 붙임
void mapHumanBoneEntries(std::string_view list, std::vector<Bone>& bones) {
    for (std::size_t pos = skipSpaces(list, 0); pos < list.size(); pos = skipSpaces(list, pos)) {
        if (list[pos] == '"') {  // 1.0: "이름": { ... }
            const std::size_t nameEnd = list.find('"', pos + 1);
            const std::size_t objectStart = list.find('{', nameEnd);
            const std::size_t objectEnd = skipBalanced(list, objectStart);
            if (nameEnd == std::string_view::npos || objectEnd == std::string_view::npos) {
                return;
            }
            assignHumanBone(bones, humanBoneFromVrmName(list.substr(pos + 1, nameEnd - pos - 1)),
                            intMember(list.substr(objectStart, objectEnd - objectStart), "node"));
            pos = objectEnd;
        } else if (list[pos] == '{') {  // 0.x: { "bone": "이름", "node": n }
            const std::size_t objectEnd = skipBalanced(list, pos);
            if (objectEnd == std::string_view::npos) {
                return;
            }
            const std::string_view object = list.substr(pos, objectEnd - pos);
            if (const auto bone = stringMember(object, "bone")) {
                // 0.x는 엄지 이름이 1.0과 한 칸 어긋남 (thumbProximal = 1.0 thumbMetacarpal)
                assignHumanBone(bones, humanBoneFromVrm0Name(*bone), intMember(object, "node"));
            }
            pos = objectEnd;
        } else {
            return;
        }
    }
}

// VRM 1.0: "humanBones": { "hips": { "node": 3 }, ... }
// VRM 0.x: "humanBones": [ { "bone": "hips", "node": 3, ... }, ... ]
void mapVrmHumanoid(const cgltf_data& data, std::vector<Bone>& bones) {
    for (cgltf_size i = 0; i < data.data_extensions_count; ++i) {
        const cgltf_extension& extension = data.data_extensions[i];
        const std::string_view name = extension.name != nullptr ? extension.name : "";
        if ((name != "VRMC_vrm" && name != "VRM") || extension.data == nullptr) {
            continue;
        }
        const std::string_view json = extension.data;
        const auto start = valueOf(json, "humanBones");
        if (!start || *start >= json.size()) {
            continue;
        }
        const std::size_t end = skipBalanced(json, *start);
        if (end != std::string_view::npos) {
            mapHumanBoneEntries(json.substr(*start + 1, end - *start - 2), bones);  // 괄호 안쪽
        }
    }
}

// VRM이 아닌 glTF(게임·DCC에서 내보낸 모델): FBX·PMX처럼 본 이름으로 (Mixamo·3ds Max Biped, 아니면
// MMD). 한 휴머노이드 본에 이름이 여럿 맞으면 처음 것만 (ADR-0015)
void mapHumanoidByName(std::vector<Bone>& bones) {
    std::array<bool, kHumanBoneCount> taken{};
    for (Bone& bone : bones) {
        HumanBone human = humanBoneFromFbxName(bone.name);
        if (human == HumanBone::None) {
            human = humanBoneFromMmdName(bone.name);
        }
        auto& used = taken[static_cast<std::size_t>(human)];
        if (human != HumanBone::None && !used) {
            bone.human = human;
            used = true;
        }
    }
}

class Converter {
public:
    Converter(const cgltf_data& data, const ImportContext& context)
        : data_(data), context_(context) {}

    std::optional<Model> run(std::string& error) {
        model_.format = ModelFormat::Gltf;
        model_.version = detectVersion(data_);
        readTextures();
        readMaterials();
        readBones();
        if (!readMeshes(error)) {
            return std::nullopt;
        }
        if (model_.version == VrmVersion::V0) {
            faceVrm0Forward();
        }
        readExpressions();
        if (model_.version == VrmVersion::None) {
            keepNamedMorphs();
        }
        if (model_.version == VrmVersion::V1) {
            readVrm1SpringBones();
        } else {
            readVrm0SpringBones();
        }
        readMeta();
        model_.bounds = computeBounds(model_.vertices);
        const float height = model_.bounds.max.y - model_.bounds.min.y;
        for (const cgltf_size index : screenOutlines_) {
            model_.materials[index].outlineWidth *= height;
        }
        return std::move(model_);
    }

private:
    template <class T>
    int indexOf(const T* item, const T* first) const {
        return item != nullptr ? static_cast<int>(item - first) : -1;
    }

    // 메시가 달린 노드를 차례로 읽음. 소품 메시는 이름만 남기고 건너뜀
    bool readMeshes(std::string& error) {
        for (cgltf_size i = 0; i < data_.nodes_count; ++i) {
            const cgltf_node& node = data_.nodes[i];
            if (node.mesh == nullptr) {
                continue;
            }
            if (isProp(node)) {
                model_.droppedProps.emplace_back(node.mesh->name != nullptr ? node.mesh->name : "");
            } else if (!readMesh(node, error)) {
                return false;
            }
        }
        if (model_.vertices.empty()) {
            error = "그릴 수 있는 메시가 없습니다";
            return false;
        }
        return true;
    }

    // VRM 0.x 정면은 -Z. Y축 180° 회전 = (x, z) 부호 반전
    void faceVrm0Forward() {
        for (Vertex& v : model_.vertices) {
            v.position = {-v.position.x, v.position.y, -v.position.z};
            v.normal = {-v.normal.x, v.normal.y, -v.normal.z};
        }
        for (Bone& bone : model_.bones) {
            bone.position = {-bone.position.x, bone.position.y, -bone.position.z};
        }
        for (auto& [node, morphs] : nodeMorphs_) {
            for (Morph& morph : morphs) {
                for (Vec3& d : morph.deltas) {
                    d = {-d.x, d.y, -d.z};
                }
            }
        }
    }

    // VRM은 표정(expressions)으로만 씀. 일반 glTF는 애니메이션 weights 채널용으로 보관 (ADR-0015)
    void keepNamedMorphs() {
        for (const auto& [node, morphs] : nodeMorphs_) {
            const char* name = data_.nodes[static_cast<std::size_t>(node)].name;
            for (std::size_t t = 0; t < morphs.size(); ++t) {
                model_.morphs.push_back({name != nullptr ? name : "", t, morphs[t],
                                         firstKeyWeight(static_cast<std::size_t>(node), t)});
            }
        }
    }

    // 파일 안 첫 애니메이션이 이 노드·타깃에 주는 첫 키 가중치, 없으면 0. 입 모양 메시마다 "숨기기"
    // 타깃을 두고 애니메이션으로 하나만 보이게 하는 모델은 가중치 0(모두 보임)이면 입이 겹치므로,
    // 클립이 가중치를 주지 않을 때의 기본값으로 씀
    [[nodiscard]] float firstKeyWeight(std::size_t node, std::size_t target) const {
        if (data_.animations_count == 0) {
            return 0.0f;
        }
        const cgltf_animation& animation = data_.animations[0];
        for (cgltf_size c = 0; c < animation.channels_count; ++c) {
            const cgltf_animation_channel& channel = animation.channels[c];
            if (channel.target_path != cgltf_animation_path_type_weights ||
                channel.target_node != &data_.nodes[node] || channel.sampler == nullptr) {
                continue;
            }
            const cgltf_size targets = morphTargetCount(data_.nodes[node]);
            // CUBICSPLINE은 키마다 (들어오는 접선, 값, 나가는 접선) 묶음이라 값은 가운데 묶음
            const cgltf_size first =
                channel.sampler->interpolation == cgltf_interpolation_type_cubic_spline ? targets
                                                                                        : 0;
            float weight = 0.0f;
            if (target < targets && cgltf_accessor_read_float(channel.sampler->output,
                                                              first + target, &weight, 1) != 0) {
                return weight;
            }
        }
        return 0.0f;
    }

    // 노드가 엉덩이(hips) 본이거나 그 아래에 있는지
    [[nodiscard]] bool inBody(const cgltf_node* node, int hips) const {
        for (std::size_t depth = 0; node != nullptr && depth < data_.nodes_count;
             node = node->parent, ++depth) {
            if (indexOf(node, data_.nodes) == hips) {
                return true;
            }
        }
        return false;
    }

    // 소품 메시 (ADR-0015): VRM이 아닌 glTF에서 몸(엉덩이 아래)과 관계없는 뼈에만 붙은 메시.
    // 게임에서 내보낸 모델의 총·이펙트 소품은 뼈대 맨 위나 장면 최상위의 별도 뼈에 붙어 있어,
    // 몸 뼈대로 움직이는 모션에서 엉뚱한 곳에 남음. 헤일로처럼 머리 등에 붙은 장신구는 몸 안이라
    // 유지. 휴머노이드를 못 찾은 모델은 기준이 없으므로 아무것도 버리지 않음
    [[nodiscard]] bool isProp(const cgltf_node& node) const {
        const int hips = model_.version == VrmVersion::None ? model_.findBone(HumanBone::Hips) : -1;
        if (hips < 0) {
            return false;
        }
        if (node.skin == nullptr) {
            return !inBody(&node, hips);
        }
        const std::span<cgltf_node* const> joints(node.skin->joints, node.skin->joints_count);
        return !joints.empty() && std::ranges::none_of(joints, [&](const cgltf_node* joint) {
            return inBody(joint, hips);
        });
    }

    void readTextures() {
        model_.textures.reserve(data_.images_count);
        for (cgltf_size i = 0; i < data_.images_count; ++i) {
            const cgltf_image& image = data_.images[i];
            const std::string name = image.name != nullptr ? image.name : std::format("image{}", i);
            Texture texture;
            if (const cgltf_buffer_view* view = image.buffer_view;
                view != nullptr && view->buffer->data != nullptr) {
                // VRM(GLB)은 이미지가 버퍼 뷰에 들어 있음
                const auto* begin =
                    static_cast<const std::uint8_t*>(view->buffer->data) + view->offset;
                texture = makeTexture({begin, begin + view->size}, name);
            } else if (image.uri != nullptr &&
                       std::string_view(image.uri).find(':') == std::string_view::npos) {
                // .gltf의 외부 이미지 파일 (data: URI는 위 버퍼 뷰 경로로 오지 않으므로 미지원)
                texture = loadTextureFile(context_.baseDirectory, image.uri);
            }
            if (image.mime_type != nullptr) {
                texture.mimeType = image.mime_type;
            }
            model_.textures.push_back(std::move(texture));
        }
    }

    // 모든 노드를 본으로 둡니다 (VRM humanBones가 노드 인덱스로 가리키므로 인덱스를 같게 유지)
    void readBones() {
        model_.bones.resize(data_.nodes_count);
        for (cgltf_size i = 0; i < data_.nodes_count; ++i) {
            const cgltf_node& node = data_.nodes[i];
            Bone& bone = model_.bones[i];
            bone.name = node.name != nullptr ? node.name : "";
            bone.parent = indexOf(node.parent, data_.nodes);
            const Mat4 world = worldMatrix(node);
            bone.position = {world.m[3][0], world.m[3][1], world.m[3][2]};
        }
        if (model_.version == VrmVersion::None) {
            mapHumanoidByName(model_.bones);
        } else {
            mapVrmHumanoid(data_, model_.bones);  // VRM은 humanBones만 (이름은 보지 않음)
        }
    }

    void readMaterials() {
        model_.materials.resize(data_.materials_count);
        for (cgltf_size i = 0; i < data_.materials_count; ++i) {
            const cgltf_material& src = data_.materials[i];
            Material& dst = model_.materials[i];
            if (src.name != nullptr) {
                dst.name = src.name;
            }
            if (src.has_pbr_metallic_roughness != 0) {
                const auto& pbr = src.pbr_metallic_roughness;
                dst.baseColor = {pbr.base_color_factor[0], pbr.base_color_factor[1],
                                 pbr.base_color_factor[2], pbr.base_color_factor[3]};
                if (const cgltf_texture* texture = pbr.base_color_texture.texture;
                    texture != nullptr) {
                    dst.baseColorTexture = indexOf(texture->image, data_.images);
                }
            }
            dst.alphaMode = toAlphaMode(src.alpha_mode);
            dst.alphaCutoff = src.alpha_cutoff;
            dst.doubleSided = src.double_sided != 0;
            // KHR_materials_emissive_strength: 색 × 세기. 세기 0으로 발광을 끈 재질(VRM Add-on for
            // Blender의 MToon 변환 기본값)을 무시하면 흰색 발광이 되어 모델이 하얗게 보임
            const float emissiveStrength =
                src.has_emissive_strength != 0 ? src.emissive_strength.emissive_strength : 1.0f;
            dst.emissiveColor =
                Vec3{src.emissive_factor[0], src.emissive_factor[1], src.emissive_factor[2]} *
                emissiveStrength;
            if (const cgltf_texture* texture = src.emissive_texture.texture; texture != nullptr) {
                dst.emissiveTexture = indexOf(texture->image, data_.images);
            }
            dst.unlit = src.unlit != 0;
            readMToon1(src, i);
        }
        readMToon0();
    }

    // glTF 텍스처 번호 → Model::textures(= 이미지) 번호, 없으면 -1
    [[nodiscard]] int imageOfTexture(std::optional<int> texture) const {
        if (!texture || *texture < 0 || static_cast<cgltf_size>(*texture) >= data_.textures_count) {
            return -1;
        }
        return indexOf(data_.textures[*texture].image, data_.images);
    }

    // {"index": n} 형태의 텍스처 참조
    [[nodiscard]] int textureInfo(std::string_view json, std::string_view key) const {
        const auto info = valueSpan(json, key);
        return info ? imageOfTexture(intMember(*info, "index")) : -1;
    }

    // VRM 0.x: extensions.VRM.meta {title, author, licenseName}
    // VRM 1.0: extensions.VRMC_vrm.meta {name, authors[], licenseUrl}
    void readMeta() {
        ModelMeta& meta = model_.meta;
        if (const std::string_view v1 = extensionJson(data_, "VRMC_vrm"); !v1.empty()) {
            const auto object = childSpan(v1, "meta");
            if (!object) {
                return;
            }
            meta.name = std::string(stringMember(*object, "name").value_or(""));
            meta.author = joinStrings(valueSpan(*object, "authors"));
            meta.license = licenseName(stringMember(*object, "licenseUrl").value_or(""));
        } else if (const std::string_view v0 = extensionJson(data_, "VRM"); !v0.empty()) {
            const auto object = childSpan(v0, "meta");
            if (!object) {
                return;
            }
            meta.name = std::string(stringMember(*object, "title").value_or(""));
            meta.author = std::string(stringMember(*object, "author").value_or(""));
            meta.license = std::string(stringMember(*object, "licenseName").value_or(""));
            if (meta.license == "Other") {  // 0.x "Other"는 otherLicenseUrl에 실제 조건
                meta.license = std::string(stringMember(*object, "otherLicenseUrl").value_or(""));
            }
        }
    }

    // ["A", "B"] → "A, B"
    static std::string joinStrings(std::optional<std::string_view> array) {
        std::string joined;
        if (!array) {
            return joined;
        }
        for (std::size_t pos = array->find('"'); pos != std::string_view::npos;) {
            const std::size_t end = array->find('"', pos + 1);
            if (end == std::string_view::npos) {
                break;
            }
            if (!joined.empty()) {
                joined += ", ";
            }
            joined += array->substr(pos + 1, end - pos - 1);
            pos = array->find('"', end + 1);
        }
        return joined;
    }

    // VRM 1.0 라이선스는 URL. 알려진 VRM 공개 라이선스는 이름으로
    static std::string licenseName(std::string_view url) {
        if (url.find("vrm.dev/licenses/1.0") != std::string_view::npos) {
            return "VRM Public License 1.0";
        }
        return std::string(url);
    }

    // VRM 1.0: materials[i].extensions.VRMC_materials_mtoon (값이 이미 VRM 1.0 정의·선형 색)
    void readMToon1(const cgltf_material& src, cgltf_size index) {
        for (cgltf_size e = 0; e < src.extensions_count; ++e) {
            const cgltf_extension& extension = src.extensions[e];
            if (extension.name == nullptr || extension.data == nullptr ||
                std::string_view(extension.name) != "VRMC_materials_mtoon") {
                continue;
            }
            const std::string_view json = extension.data;
            Material& dst = model_.materials[index];
            dst.mtoon = true;
            dst.shadeColor = rgbOr(valueSpan(json, "shadeColorFactor"), dst.shadeColor);
            dst.shadeTexture = textureInfo(json, "shadeMultiplyTexture");
            dst.shadingShift = floatMember(json, "shadingShiftFactor").value_or(dst.shadingShift);
            dst.shadingToony = floatMember(json, "shadingToonyFactor").value_or(dst.shadingToony);
            dst.matcapColor = rgbOr(valueSpan(json, "matcapFactor"), dst.matcapColor);
            dst.matcapTexture = textureInfo(json, "matcapTexture");
            dst.rimColor = rgbOr(valueSpan(json, "parametricRimColorFactor"), dst.rimColor);
            dst.rimFresnelPower =
                floatMember(json, "parametricRimFresnelPowerFactor").value_or(dst.rimFresnelPower);
            dst.rimLift = floatMember(json, "parametricRimLiftFactor").value_or(dst.rimLift);
            dst.rimLightingMix =
                floatMember(json, "rimLightingMixFactor").value_or(dst.rimLightingMix);
            readMToon1Outline(json, index);
        }
    }

    // outlineWidthMode: "none" | "worldCoordinates"(m) | "screenCoordinates"(화면 높이 비율)
    void readMToon1Outline(std::string_view json, cgltf_size index) {
        const auto mode = stringMember(json, "outlineWidthMode");
        const float width = floatMember(json, "outlineWidthFactor").value_or(0.0f);
        if (!mode || *mode == "none" || width <= 0.0f) {
            return;
        }
        Material& dst = model_.materials[index];
        dst.outlineWidth = width;
        dst.outlineColor = colorOr(valueSpan(json, "outlineColorFactor"), dst.outlineColor);
        if (*mode == "screenCoordinates") {
            // 화면 높이 비율: 이 앱은 모델 키에 맞춰 창을 채우므로 모델 키를 곱해 m로 근사
            screenOutlines_.push_back(index);
        }
    }

    // VRM 0.x: extensions.VRM.secondaryAnimation
    //   colliderGroups[{node, colliders[{offset{x,y,z}, radius}]}]
    //   boneGroups[{stiffiness(철자 그대로), gravityPower, gravityDir, dragForce, hitRadius,
    //               bones[루트 노드], colliderGroups[인덱스]}]
    // VRM 0.x 노드는 회전이 없는 T포즈라 오프셋을 모델 공간 그대로 쓰고, 정면 -Z → +Z 회전만 반영
    void readVrm0SpringBones() {
        const std::string_view json = extensionJson(data_, "VRM");
        const auto secondary = json.empty() ? std::nullopt : valueSpan(json, "secondaryAnimation");
        if (!secondary) {
            return;
        }
        const std::vector<std::vector<int>> groupSpheres = readSpringColliderGroups(*secondary);
        if (const auto groups = childSpan(*secondary, "boneGroups")) {
            forEachObject(*groups, [&](std::string_view object) {
                SpringGroup group = readSpringGroup(object, groupSpheres);
                if (!group.roots.empty()) {
                    model_.springGroups.push_back(std::move(group));
                }
            });
        }
    }

    [[nodiscard]] Vec3 flipV0(Vec3 v) const {
        return model_.version == VrmVersion::V0 ? Vec3{-v.x, v.y, -v.z} : v;
    }

    [[nodiscard]] bool validBone(std::optional<int> node) const {
        return node && *node >= 0 && static_cast<std::size_t>(*node) < model_.bones.size();
    }

    // 충돌체 그룹 → 구 목록. 반환값[그룹 번호] = 그 그룹의 구 인덱스들 (본 그룹이 번호로 참조)
    std::vector<std::vector<int>> readSpringColliderGroups(std::string_view secondary) {
        std::vector<std::vector<int>> groupSpheres;
        const auto groups = childSpan(secondary, "colliderGroups");
        if (!groups) {
            return groupSpheres;
        }
        forEachObject(*groups, [&](std::string_view group) {
            std::vector<int>& spheres = groupSpheres.emplace_back();
            const auto node = intMember(group, "node");
            const auto colliders = valueSpan(group, "colliders");
            if (!validBone(node) || !colliders) {
                return;
            }
            forEachObject(*colliders, [&](std::string_view collider) {
                spheres.push_back(static_cast<int>(model_.springColliders.size()));
                SpringCollider sphere;
                sphere.bone = *node;
                sphere.offset = flipV0(vec3Member(collider, "offset", {}));
                sphere.radius = floatMember(collider, "radius").value_or(0.0f);
                model_.springColliders.push_back(sphere);
            });
        });
        return groupSpheres;
    }

    SpringGroup readSpringGroup(std::string_view object,
                                const std::vector<std::vector<int>>& groupSpheres) const {
        SpringGroup group;
        group.stiffness = floatMember(object, "stiffiness").value_or(group.stiffness);
        group.gravityPower = floatMember(object, "gravityPower").value_or(0.0f);
        group.gravityDir = flipV0(vec3Member(object, "gravityDir", {0.0f, -1.0f, 0.0f}));
        group.dragForce = floatMember(object, "dragForce").value_or(group.dragForce);
        group.hitRadius = floatMember(object, "hitRadius").value_or(group.hitRadius);
        if (const auto bones = valueSpan(object, "bones")) {
            for (const float node : floatArray(*bones)) {
                if (validBone(static_cast<int>(node))) {
                    group.roots.push_back(static_cast<int>(node));
                }
            }
        }
        if (const auto refs = valueSpan(object, "colliderGroups")) {
            for (const float ref : floatArray(*refs)) {
                const auto index = static_cast<std::size_t>(ref);
                if (ref >= 0.0f && index < groupSpheres.size()) {
                    group.colliders.insert(group.colliders.end(), groupSpheres[index].begin(),
                                           groupSpheres[index].end());
                }
            }
        }
        return group;
    }

    // VRM 1.0: extensions.VRMC_springBone
    //   colliders[{node, shape{sphere{offset[3], radius} | capsule{offset[3], radius, tail[3]}}}]
    //   colliderGroups[{colliders[인덱스]}]
    //   springs[{joints[{node, hitRadius, stiffness, gravityPower, gravityDir[3], dragForce}],
    //            colliderGroups[인덱스]}]
    // 0.x와 달리 노드에 회전이 있을 수 있어, 충돌체 오프셋(노드 로컬 좌표)을 바인드 포즈의 노드
    // 변환으로 모델 공간 방향으로 바꿈. 정면은 이미 +Z라 부호 반전 없음
    void readVrm1SpringBones() {
        const std::string_view json = extensionJson(data_, "VRMC_springBone");
        if (json.empty()) {
            return;
        }
        std::vector<int> colliders;  // VRM 충돌체 번호 → Model::springColliders 인덱스 (-1 = 무시)
        if (const auto list = childSpan(json, "colliders")) {
            forEachObject(
                *list, [&](std::string_view object) { colliders.push_back(readCollider(object)); });
        }
        std::vector<std::vector<int>> groupColliders;  // 충돌체 그룹 번호 → 충돌체 인덱스들
        if (const auto groups = childSpan(json, "colliderGroups")) {
            forEachObject(*groups, [&](std::string_view group) {
                std::vector<int>& members = groupColliders.emplace_back();
                if (const auto refs = childSpan(group, "colliders")) {
                    for (const std::size_t index : indexArray(*refs, colliders.size())) {
                        if (colliders[index] >= 0) {
                            members.push_back(colliders[index]);
                        }
                    }
                }
            });
        }
        if (const auto springs = childSpan(json, "springs")) {
            forEachObject(*springs,
                          [&](std::string_view spring) { readSpring(spring, groupColliders); });
        }
    }

    // 충돌체 하나 → Model::springColliders 인덱스, 노드나 모양이 잘못되면 -1
    int readCollider(std::string_view object) {
        const auto node = intMember(object, "node");
        const auto shape = childSpan(object, "shape");
        // optional을 직접 검사 (함수 안의 검사는 unchecked-optional-access가 추적 못 함)
        if (!node || !shape || !validBone(node)) {
            return -1;
        }
        const auto sphere = childSpan(*shape, "sphere");
        const auto capsule = childSpan(*shape, "capsule");
        const auto found = sphere ? sphere : capsule;
        if (!found) {
            return -1;
        }
        const std::string_view body = *found;
        const Mat4 world = worldMatrix(data_.nodes[static_cast<std::size_t>(*node)]);
        const auto toModel = [&](std::string_view key) {
            return core::transformDirection(vec3Array(childSpan(body, key), {}), world);
        };
        SpringCollider collider;
        collider.bone = *node;
        collider.offset = toModel("offset");
        collider.radius = floatMember(body, "radius").value_or(0.0f);
        if (!sphere) {
            collider.capsule = true;
            collider.tail = toModel("tail");
        }
        model_.springColliders.push_back(collider);
        return static_cast<int>(model_.springColliders.size()) - 1;
    }

    // 관절 i의 값은 i → i+1 구간에 쓰임 (마지막 관절의 값은 쓰지 않음). 값이 같은 구간이 이어지면
    // SpringGroup 하나로 묶고, 바뀌는 관절에서 나눔 (앞 그룹의 마지막 관절 = 뒤 그룹의 첫 관절)
    void readSpring(std::string_view spring, const std::vector<std::vector<int>>& groupColliders) {
        std::vector<int> colliders;
        if (const auto refs = childSpan(spring, "colliderGroups")) {
            for (const std::size_t index : indexArray(*refs, groupColliders.size())) {
                colliders.insert(colliders.end(), groupColliders[index].begin(),
                                 groupColliders[index].end());
            }
        }
        std::vector<int> bones;
        std::vector<SpringGroup> values;
        if (const auto joints = childSpan(spring, "joints")) {
            forEachObject(*joints, [&](std::string_view joint) {
                const auto node = intMember(joint, "node");
                if (!node || !validBone(node)) {
                    return;
                }
                SpringGroup v;  // VRM 1.0 기본값
                v.stiffness = floatMember(joint, "stiffness").value_or(1.0f);
                v.gravityPower = floatMember(joint, "gravityPower").value_or(0.0f);
                v.gravityDir = vec3Array(childSpan(joint, "gravityDir"), {0.0f, -1.0f, 0.0f});
                v.dragForce = floatMember(joint, "dragForce").value_or(0.5f);
                v.hitRadius = floatMember(joint, "hitRadius").value_or(0.0f);
                bones.push_back(*node);
                values.push_back(v);
            });
        }
        for (std::size_t first = 0; first + 1 < bones.size();) {
            std::size_t last = first + 1;
            while (last + 1 < bones.size() && sameSpringValues(values[last], values[first])) {
                ++last;
            }
            SpringGroup group = values[first];
            group.joints.assign(bones.begin() + static_cast<std::ptrdiff_t>(first),
                                bones.begin() + static_cast<std::ptrdiff_t>(last) + 1);
            group.colliders = colliders;
            model_.springGroups.push_back(std::move(group));
            first = last;
        }
    }

    static bool sameSpringValues(const SpringGroup& a, const SpringGroup& b) {
        return a.stiffness == b.stiffness && a.gravityPower == b.gravityPower &&
               a.gravityDir == b.gravityDir && a.dragForce == b.dragForce &&
               a.hitRadius == b.hitRadius;
    }

    // 번호 배열에서 0 ≤ 번호 < count인 것만 (음수 float를 size_t로 바꾸면 정의되지 않은 동작)
    static std::vector<std::size_t> indexArray(std::string_view array, std::size_t count) {
        std::vector<std::size_t> indices;
        for (const float value : floatArray(array)) {
            if (value >= 0.0f && value < static_cast<float>(count)) {
                indices.push_back(static_cast<std::size_t>(value));
            }
        }
        return indices;
    }

    // [x, y, z] 배열. 없거나 짧으면 fallback
    static Vec3 vec3Array(std::optional<std::string_view> array, Vec3 fallback) {
        if (!array) {
            return fallback;
        }
        const std::vector<float> v = floatArray(*array);
        return v.size() >= 3 ? Vec3{v[0], v[1], v[2]} : fallback;
    }

    // VRM 0.x: extensions.VRM.materialProperties[{name, shader, floatProperties,
    //   vectorProperties, textureProperties}]. 이름으로 재질을 찾고, shader가 "VRM/MToon"일 때만
    void readMToon0() {
        const std::string_view json = extensionJson(data_, "VRM");
        const auto properties = json.empty() ? std::nullopt : valueSpan(json, "materialProperties");
        if (!properties) {
            return;
        }
        forEachObject(*properties, [&](std::string_view object) {
            const auto name = stringMember(object, "name");
            const auto floats = valueSpan(object, "floatProperties");
            const auto it = std::ranges::find_if(
                model_.materials, [&](const Material& m) { return name && m.name == *name; });
            if (!floats || it == model_.materials.end() ||
                stringMember(object, "shader") != std::optional<std::string_view>("VRM/MToon")) {
                return;
            }
            const std::string_view vectors =
                valueSpan(object, "vectorProperties").value_or(std::string_view{});
            const std::string_view textures =
                valueSpan(object, "textureProperties").value_or(std::string_view{});
            readMToon0Shading(*it, *floats, vectors, textures);
            readMToon0Outline(*it, *floats, vectors);
        });
    }

    void readMToon0Shading(Material& dst, std::string_view floats, std::string_view vectors,
                           std::string_view textures) const {
        const auto color = [&](std::string_view key, Vec3 fallback) {
            return rgbOr(vectors.empty() ? std::nullopt : valueSpan(vectors, key), fallback);
        };
        const auto texture = [&](std::string_view key) {
            return imageOfTexture(textures.empty() ? std::nullopt : intMember(textures, key));
        };
        dst.mtoon = true;
        dst.shadeColor = linearRgb(color("_ShadeColor", {1.0f, 1.0f, 1.0f}));
        dst.shadeTexture = texture("_ShadeTexture");
        // 0.x: 밝기 = saturate((N·L − min) / (max − min)), min = shift, max = lerp(1, shift, toony)
        // → VRM 1.0의 linearstep(−1 + toony, 1 − toony, N·L + shift)로 바꾸면
        //   shift = −(min + max) / 2, toony = 1 − (max − min) / 2 (경계의 중심과 반폭이 같음)
        const float minThreshold = floatMember(floats, "_ShadeShift").value_or(0.0f);
        const float toony = floatMember(floats, "_ShadeToony").value_or(0.9f);
        const float maxThreshold = 1.0f + (minThreshold - 1.0f) * toony;
        dst.shadingShift = -(minThreshold + maxThreshold) * 0.5f;
        dst.shadingToony = 1.0f - (maxThreshold - minThreshold) * 0.5f;
        dst.rimColor = linearRgb(color("_RimColor", {}));
        dst.rimFresnelPower = floatMember(floats, "_RimFresnelPower").value_or(1.0f);
        dst.rimLift = floatMember(floats, "_RimLift").value_or(0.0f);
        dst.rimLightingMix = floatMember(floats, "_RimLightingMix").value_or(0.0f);
        dst.matcapTexture = texture("_SphereAdd");  // 0.x MatCap(더하기)은 색 없이 텍스처만
        // 0.x는 glTF emissive(대체 셰이더용 값)가 아니라 이 값을 씀. HDR 색이라 선형 그대로
        dst.emissiveColor = color("_EmissionColor", {});
        dst.emissiveTexture = texture("_EmissionMap");
    }

    //   _OutlineWidthMode 0 = 없음, 1 = 월드, 2 = 화면 / _OutlineWidth는 cm (둘 다 cm로 취급)
    static void readMToon0Outline(Material& dst, std::string_view floats,
                                  std::string_view vectors) {
        const float mode = floatMember(floats, "_OutlineWidthMode").value_or(0.0f);
        const float width = floatMember(floats, "_OutlineWidth").value_or(0.0f);
        if (mode <= 0.0f || width <= 0.0f) {
            return;
        }
        dst.outlineWidth = width * 0.01f;  // cm → m
        const core::Vec4 srgb = colorOr(
            vectors.empty() ? std::nullopt : valueSpan(vectors, "_OutlineColor"), dst.outlineColor);
        const Vec3 rgb = linearRgb({srgb.x, srgb.y, srgb.z});
        dst.outlineColor = {rgb.x, rgb.y, rgb.z, srgb.w};
    }

    // 표정 = VRM이 지정한 (메시, 모프 타깃, 가중치) 묶음의 합 (ADR-0010)
    // VRM 1.0: expressions.preset.<이름>.morphTargetBinds[{node, index, weight 0~1}]
    // VRM 0.x: blendShapeMaster.blendShapeGroups[{presetName, binds[{mesh, index, weight 0~100}]}]
    void readExpressions() {
        if (const std::string_view json = extensionJson(data_, "VRMC_vrm"); !json.empty()) {
            const auto expressions = valueSpan(json, "expressions");
            const auto preset = expressions ? valueSpan(*expressions, "preset") : std::nullopt;
            constexpr std::array<std::pair<std::string_view, Expression>, 3> kNames = {{
                {"blink", Expression::Blink},
                {"happy", Expression::Happy},
                {"surprised", Expression::Surprised},
            }};
            for (const auto& [name, expression] : kNames) {
                const auto object = preset ? valueSpan(*preset, name) : std::nullopt;
                const auto binds = object ? valueSpan(*object, "morphTargetBinds") : std::nullopt;
                if (binds) {
                    forEachObject(*binds, [&](std::string_view bind) {
                        addNodeMorph(expression, intMember(bind, "node"), intMember(bind, "index"),
                                     floatMember(bind, "weight").value_or(1.0f));
                    });
                }
            }
        }
        if (const std::string_view json = extensionJson(data_, "VRM"); !json.empty()) {
            if (const auto groups = valueSpan(json, "blendShapeGroups")) {
                forEachObject(*groups, [&](std::string_view group) { readVrm0Group(group); });
            }
        }
    }

    void readVrm0Group(std::string_view group) {
        const std::string_view preset = stringMember(group, "presetName").value_or("");
        Expression expression = Expression::Count;
        if (preset == "blink") {
            expression = Expression::Blink;
        } else if (preset == "joy") {
            expression = Expression::Happy;
        } else if (stringMember(group, "name").value_or("") == "Surprised") {
            expression = Expression::Surprised;  // 0.x에는 놀람 프리셋이 없어 이름으로 찾음
        }
        const auto binds = valueSpan(group, "binds");
        if (expression == Expression::Count || !binds) {
            return;
        }
        forEachObject(*binds, [&](std::string_view bind) {
            const std::optional<int> mesh = intMember(bind, "mesh");
            const float weight = floatMember(bind, "weight").value_or(100.0f) / 100.0f;
            // 0.x는 노드가 아니라 메시 번호로 가리키므로, 그 메시를 단 노드를 모두 찾음
            for (cgltf_size n = 0; n < data_.nodes_count; ++n) {
                if (mesh && data_.nodes[n].mesh != nullptr &&
                    indexOf(data_.nodes[n].mesh, data_.meshes) == *mesh) {
                    addNodeMorph(expression, static_cast<int>(n), intMember(bind, "index"), weight);
                }
            }
        });
    }

    void addNodeMorph(Expression expression, std::optional<int> node, std::optional<int> target,
                      float weight) {
        if (!node || !target || weight == 0.0f) {
            return;
        }
        const auto it = nodeMorphs_.find(*node);
        if (it == nodeMorphs_.end() || *target < 0 ||
            static_cast<std::size_t>(*target) >= it->second.size()) {
            return;
        }
        const Morph& morph = it->second[static_cast<std::size_t>(*target)];
        Morph& out = model_.expression(expression);
        for (std::size_t i = 0; i < morph.vertices.size(); ++i) {
            out.add(morph.vertices[i], morph.deltas[i] * weight);
        }
    }

    bool readMesh(const cgltf_node& node, std::string& error) {
        // glTF 규칙: 스킨이 있으면 메시 노드 자신의 변환은 무시하고 관절 행렬만 씁니다.
        const Mat4 nodeWorld = worldMatrix(node);
        const std::vector<Mat4> joints =
            node.skin != nullptr ? jointMatrices(*node.skin) : std::vector<Mat4>{};
        // 스킨의 관절 번호 → 본(= 노드) 번호
        std::vector<int> jointBones;
        if (node.skin != nullptr) {
            for (cgltf_size j = 0; j < node.skin->joints_count; ++j) {
                jointBones.push_back(indexOf(node.skin->joints[j], data_.nodes));
            }
        }
        const int nodeIndex = indexOf(&node, data_.nodes);

        const cgltf_mesh& mesh = *node.mesh;
        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            const cgltf_primitive& primitive = mesh.primitives[p];
            if (primitive.type != cgltf_primitive_type_triangles) {
                continue;  // 선·점은 그리지 않음
            }
            if (!readPrimitive(primitive, {nodeWorld, joints, jointBones, nodeIndex}, error)) {
                return false;
            }
        }
        return true;
    }

    struct MeshNode {
        const Mat4& world;
        const std::vector<Mat4>& joints;  // 바인드 포즈 굽기용 관절 행렬
        const std::vector<int>& jointBones;
        int index = -1;
    };

    bool readPrimitive(const cgltf_primitive& primitive, const MeshNode& node, std::string& error) {
        const Mat4& nodeWorld = node.world;
        const std::vector<Mat4>& joints = node.joints;
        const cgltf_accessor* positions = nullptr;
        const cgltf_accessor* normals = nullptr;
        const cgltf_accessor* uvs = nullptr;
        const cgltf_accessor* jointIndices = nullptr;
        const cgltf_accessor* weights = nullptr;
        for (cgltf_size a = 0; a < primitive.attributes_count; ++a) {
            const cgltf_attribute& attribute = primitive.attributes[a];
            if (attribute.index != 0) {
                continue;  // TEXCOORD_1, JOINTS_1 등은 사용하지 않음
            }
            switch (attribute.type) {
                case cgltf_attribute_type_position: positions = attribute.data; break;
                case cgltf_attribute_type_normal: normals = attribute.data; break;
                case cgltf_attribute_type_texcoord: uvs = attribute.data; break;
                case cgltf_attribute_type_joints: jointIndices = attribute.data; break;
                case cgltf_attribute_type_weights: weights = attribute.data; break;
                default: break;
            }
        }
        if (positions == nullptr) {
            return true;  // 위치가 없는 프리미티브는 건너뜀
        }

        const bool skinned = !joints.empty() && jointIndices != nullptr && weights != nullptr;
        const std::size_t base = model_.vertices.size();
        const std::size_t count = positions->count;
        if (base + count > std::numeric_limits<std::uint32_t>::max()) {
            error = "정점이 너무 많습니다";
            return false;
        }

        model_.vertices.resize(base + count);
        for (cgltf_size i = 0; i < count; ++i) {
            Vertex& v = model_.vertices[base + i];
            std::array<float, 4> f{};
            cgltf_accessor_read_float(positions, i, f.data(), 3);
            v.position = {f[0], f[1], f[2]};
            if (normals != nullptr) {
                cgltf_accessor_read_float(normals, i, f.data(), 3);
                v.normal = {f[0], f[1], f[2]};
            }
            if (uvs != nullptr) {
                cgltf_accessor_read_float(uvs, i, f.data(), 2);
                v.u = f[0];
                v.v = f[1];
            }

            Mat4 transform = nodeWorld;
            if (skinned) {
                transform = blendJoints(joints, *jointIndices, *weights, i).value_or(nodeWorld);
                readSkinWeights(v, *jointIndices, *weights, i, node.jointBones);
            }
            if (v.weights[0] == 0.0f) {
                // 스킨이 없는 메시(액세서리 등)는 자기 노드에 고정되어 함께 움직임
                const std::array<std::pair<int, float>, 1> rigid = {{{node.index, 1.0f}}};
                setSkinWeights(v, rigid);
            }
            v.position = core::transformPoint(v.position, transform);
            v.normal = core::normalize(core::transformDirection(v.normal, transform));
            readMorphDeltas(primitive, i, base, transform, node.index);
        }

        Primitive out;
        out.firstIndex = static_cast<std::uint32_t>(model_.indices.size());
        out.material = indexOf(primitive.material, data_.materials);
        if (primitive.indices != nullptr) {
            for (cgltf_size i = 0; i < primitive.indices->count; ++i) {
                const cgltf_size index = cgltf_accessor_read_index(primitive.indices, i);
                if (index >= count) {
                    error = std::format("인덱스 {}가 정점 수 {}를 넘습니다", index, count);
                    return false;
                }
                model_.indices.push_back(static_cast<std::uint32_t>(base + index));
            }
        } else {
            for (std::size_t i = 0; i < count; ++i) {
                model_.indices.push_back(static_cast<std::uint32_t>(base + i));
            }
        }
        out.indexCount = static_cast<std::uint32_t>(model_.indices.size()) - out.firstIndex;
        if (out.indexCount > 0) {
            model_.primitives.push_back(out);
        }
        return true;
    }

    static void readSkinWeights(Vertex& v, const cgltf_accessor& jointIndices,
                                const cgltf_accessor& weights, cgltf_size vertex,
                                const std::vector<int>& jointBones) {
        std::array<cgltf_uint, 4> index{};
        std::array<float, 4> weight{};
        cgltf_accessor_read_uint(&jointIndices, vertex, index.data(), 4);
        cgltf_accessor_read_float(&weights, vertex, weight.data(), 4);
        std::array<std::pair<int, float>, 4> influences{};
        for (std::size_t k = 0; k < 4; ++k) {
            influences[k] = {index[k] < jointBones.size() ? jointBones[index[k]] : -1, weight[k]};
        }
        setSkinWeights(v, influences);
    }

    // 모프 타깃의 위치 델타를 정점과 같은 변환으로 모델 공간에 옮겨, 노드별로 모아 둠
    void readMorphDeltas(const cgltf_primitive& primitive, cgltf_size vertex, std::size_t base,
                         const Mat4& transform, int nodeIndex) {
        if (primitive.targets_count == 0) {
            return;
        }
        std::vector<Morph>& morphs = nodeMorphs_[nodeIndex];
        if (morphs.size() < primitive.targets_count) {
            morphs.resize(primitive.targets_count);
        }
        for (cgltf_size t = 0; t < primitive.targets_count; ++t) {
            const cgltf_morph_target& target = primitive.targets[t];
            for (cgltf_size a = 0; a < target.attributes_count; ++a) {
                if (target.attributes[a].type != cgltf_attribute_type_position) {
                    continue;
                }
                std::array<float, 3> d{};
                cgltf_accessor_read_float(target.attributes[a].data, vertex, d.data(), 3);
                if (d[0] != 0.0f || d[1] != 0.0f || d[2] != 0.0f) {  // 희소하게 저장
                    morphs[t].add(static_cast<std::uint32_t>(base + vertex),
                                  core::transformDirection({d[0], d[1], d[2]}, transform));
                }
            }
        }
    }

    // 선형 블렌드 스키닝: Σ wᵢ·Jᵢ. 가중치 합이 0이면 nullopt
    static std::optional<Mat4> blendJoints(const std::vector<Mat4>& joints,
                                           const cgltf_accessor& jointIndices,
                                           const cgltf_accessor& weights, cgltf_size vertex) {
        std::array<cgltf_uint, 4> index{};
        std::array<float, 4> weight{};
        cgltf_accessor_read_uint(&jointIndices, vertex, index.data(), 4);
        cgltf_accessor_read_float(&weights, vertex, weight.data(), 4);

        Mat4 sum;  // 영행렬
        float total = 0.0f;
        for (std::size_t k = 0; k < 4; ++k) {
            if (weight[k] > 0.0f && index[k] < joints.size()) {
                sum = sum + joints[index[k]] * weight[k];
                total += weight[k];
            }
        }
        if (total <= 0.0f) {
            return std::nullopt;
        }
        return sum * (1.0f / total);  // 합이 1이 아닌 파일 보정
    }

    const cgltf_data& data_;
    std::vector<cgltf_size> screenOutlines_;  // 화면 비율 외곽선 → 모델 키를 곱할 재질
    const ImportContext& context_;
    Model model_;
    std::map<int, std::vector<Morph>> nodeMorphs_;  // 노드 → 모프 타깃 번호별 델타
};

LoadResult convert(cgltf_data* raw, const ImportContext& context) {
    DataPtr data(raw);
    cgltf_options options{};

    LoadResult result;
    // 기준 경로 "": 외부 버퍼 파일(.bin)은 지원하지 않음 (GLB와 data URI만)
    cgltf_result status = cgltf_load_buffers(&options, data.get(), "");
    if (status == cgltf_result_success) {
        status = cgltf_validate(data.get());  // 범위 밖 접근자 등 손상된 파일 차단
    }
    if (status != cgltf_result_success) {
        result.error = resultToString(status);
        return result;
    }

    result.model = Converter(*data, context).run(result.error);
    return result;
}

// glTF 애니메이션 → 모션 클립. 휴머노이드 본은 VRMA면 VRMC_vrm_animation.humanoid (ADR-0013),
// 애니메이션이 든 VRM 모델이면 그 humanBones, 일반 glTF면 본 이름 (ADR-0015).
// 좌표는 VRM 1.0과 같음 (정면 +Z). 노드는 TRS만 지원 (matrix 노드는 회전 없음으로 봄)
class GltfMotionConverter {
public:
    GltfMotionConverter(const cgltf_data& data, std::string_view clipName)
        : data_(data), clipName_(clipName) {}

    std::optional<MotionClip> run(std::string& error) {
        if (!readHumanoid()) {
            error =
                "휴머노이드 본이 없습니다 (VRMA의 VRMC_vrm_animation, 또는 Mixamo·3ds Max "
                "Biped·MMD 본 이름이 필요합니다)";
            return std::nullopt;
        }
        const cgltf_animation* animation = findAnimation(error);
        if (animation == nullptr) {
            return std::nullopt;
        }
        readChannels(*animation);
        orderNodes();

        MotionClip clip;
        clip.format = format_;
        clip.frameCount =
            static_cast<std::size_t>(std::floor(duration_ * MotionClip::kFramesPerSecond + 1e-3f)) +
            1;
        // 노드마다 기본 자세 → 지금: W(t)·W₀⁻¹ (모델 축 기준 회전). 휴머노이드가 아닌 노드도
        // 부모와 비교하려고 전부 계산
        std::vector<core::Quat> rest = worldRotations(-1.0f);
        std::vector<core::Quat> world;
        std::vector<std::vector<core::Quat>> deltas(data_.nodes_count);
        for (std::size_t frame = 0; frame < clip.frameCount; ++frame) {
            world = worldRotations(static_cast<float>(frame) / MotionClip::kFramesPerSecond);
            for (std::size_t n = 0; n < world.size(); ++n) {
                deltas[n].push_back(world[n] * rest[n].conjugate());
            }
        }
        std::array<std::optional<Vec3>, kHumanBoneCount> positions{};
        std::vector<motion::SourceNode> nodes(data_.nodes_count);
        for (std::size_t n = 0; n < human_.size(); ++n) {
            const cgltf_node& node = data_.nodes[n];
            nodes[n] = {node.name != nullptr ? std::string_view(node.name) : std::string_view{},
                        indexOf(node.parent), human_[n] != HumanBone::None};
            if (human_[n] != HumanBone::None) {
                clip.track(human_[n]) = deltas[n];
                const Mat4 m = worldMatrix(node);
                positions[static_cast<std::size_t>(human_[n])] =
                    Vec3{m.m[3][0], m.m[3][1], m.m[3][2]};
            }
        }
        motion::setRestDirections(clip, positions);
        motion::addBoneTracks(clip, nodes, deltas, nodeOffsets(clip.frameCount));
        readHipsTranslation(clip);
        readHiddenBones(clip);
        readWeightTracks(clip);
        return clip;
    }

private:
    template <class T>
    struct Channel {
        std::vector<float> times;
        std::vector<T> values;
        bool step = false;
    };

    // 휴머노이드가 아닌 노드의 프레임별 이동 변화 (ADR-0015): (로컬 이동 − 기본 자세 로컬 이동)을
    // 부모의 기본 자세 월드 행렬(크기 포함)로 모델 축에 옮김. 이동 채널이 없는 노드는 비움
    [[nodiscard]] std::vector<std::vector<Vec3>> nodeOffsets(std::size_t frameCount) const {
        std::vector<std::vector<Vec3>> offsets(data_.nodes_count);
        for (std::size_t n = 0; n < data_.nodes_count; ++n) {
            if (translations_[n].times.empty() || human_[n] != HumanBone::None) {
                continue;
            }
            const cgltf_node& node = data_.nodes[n];
            const Vec3 rest =
                node.has_translation != 0
                    ? Vec3{node.translation[0], node.translation[1], node.translation[2]}
                    : Vec3{};
            const Mat4 parent =
                node.parent != nullptr ? worldMatrix(*node.parent) : Mat4::identity();
            offsets[n].reserve(frameCount);
            for (std::size_t frame = 0; frame < frameCount; ++frame) {
                const float time = static_cast<float>(frame) / MotionClip::kFramesPerSecond;
                offsets[n].push_back(
                    core::transformDirection(sample(translations_[n], time) - rest, parent));
            }
        }
        return offsets;
    }

    // weights 채널 → 타깃별 가중치 트랙 (입 모양 메시 바꾸기 등, ADR-0015)
    void readWeightTracks(MotionClip& clip) const {
        for (const WeightChannel& channel : weights_) {
            const char* name = data_.nodes[channel.node].name;
            WeightTrack track{name != nullptr ? name : "", channel.target, {}};
            track.weights.reserve(clip.frameCount);
            for (std::size_t frame = 0; frame < clip.frameCount; ++frame) {
                track.weights.push_back(sample(
                    channel.values, static_cast<float>(frame) / MotionClip::kFramesPerSecond));
            }
            clip.weightTracks.push_back(std::move(track));
        }
    }

    // 크기 키가 모두 0.1배 미만인 휴머노이드가 아닌 노드 (게임의 소품 숨기기, ADR-0015).
    // 키 사이는 보간이라 키가 모두 작으면 클립 내내 작음
    void readHiddenBones(MotionClip& clip) const {
        constexpr float kHiddenScale = 0.1f;
        for (std::size_t n = 0; n < scales_.size(); ++n) {
            const std::vector<Vec3>& keys = scales_[n].values;
            const char* name = data_.nodes[n].name;
            if (keys.empty() || human_[n] != HumanBone::None || name == nullptr) {
                continue;
            }
            if (std::ranges::all_of(keys, [](Vec3 s) {
                    return std::max({std::abs(s.x), std::abs(s.y), std::abs(s.z)}) < kHiddenScale;
                })) {
                clip.hiddenBones.emplace_back(name);
            }
        }
    }

    // 엉덩이 노드의 위치 변화 (조상의 이동·회전 포함)
    void readHipsTranslation(MotionClip& clip) const {
        const auto hips = std::ranges::find(human_, HumanBone::Hips);
        if (hips == human_.end()) {
            return;
        }
        const auto n = static_cast<std::size_t>(hips - human_.begin());
        std::vector<Vec3> frames;
        frames.reserve(clip.frameCount);
        for (std::size_t frame = 0; frame < clip.frameCount; ++frame) {
            const float time = static_cast<float>(frame) / MotionClip::kFramesPerSecond;
            frames.push_back(worldPosition(n, time, worldRotations(time)));
        }
        motion::setHipsTranslation(clip, frames, worldPosition(n, -1.0f, worldRotations(-1.0f)));
    }

    // 노드 → 휴머노이드 본. 하나라도 있으면 true
    bool readHumanoid() {
        std::vector<Bone> bones(data_.nodes_count);
        for (cgltf_size n = 0; n < data_.nodes_count; ++n) {
            bones[n].name = data_.nodes[n].name != nullptr ? data_.nodes[n].name : "";
        }
        const std::string_view json = extensionJson(data_, "VRMC_vrm_animation");
        const std::size_t start =
            json.empty() ? std::string_view::npos
                         : valueOf(json, "humanBones").value_or(std::string_view::npos);
        const std::size_t end =
            start < json.size() ? skipBalanced(json, start) : std::string_view::npos;
        if (end != std::string_view::npos) {
            format_ = MotionFormat::Vrma;
            mapHumanBoneEntries(json.substr(start + 1, end - start - 2), bones);  // 괄호 안쪽
        } else if (detectVersion(data_) != VrmVersion::None) {
            mapVrmHumanoid(data_, bones);
        } else {
            mapHumanoidByName(bones);
        }
        human_.resize(bones.size());
        std::ranges::transform(bones, human_.begin(), &Bone::human);
        return std::ranges::any_of(human_, [](HumanBone h) { return h != HumanBone::None; });
    }

    // clipName_과 이름이 같은 애니메이션 (비어 있으면 첫 번째). 없으면 error에 있는 이름을 나열
    const cgltf_animation* findAnimation(std::string& error) const {
        if (data_.animations_count == 0) {
            error = "애니메이션이 없습니다";
            return nullptr;
        }
        if (clipName_.empty()) {
            return &data_.animations[0];
        }
        std::string names;
        for (cgltf_size i = 0; i < data_.animations_count; ++i) {
            const std::string_view name =
                data_.animations[i].name != nullptr ? data_.animations[i].name : "";
            if (name == clipName_) {
                return &data_.animations[i];
            }
            names += std::format("{}'{}'", i > 0 ? ", " : "", name);
        }
        error =
            std::format("애니메이션 '{}'이(가) 없습니다. 있는 애니메이션: {}", clipName_, names);
        return nullptr;
    }

    // 회전·이동·크기 채널. 이동은 엉덩이 위치에만, 크기는 숨김 본을 고를 때만 씀
    void readChannels(const cgltf_animation& animation) {
        channels_.resize(data_.nodes_count);
        translations_.resize(data_.nodes_count);
        scales_.resize(data_.nodes_count);
        for (cgltf_size c = 0; c < animation.channels_count; ++c) {
            const cgltf_animation_channel& channel = animation.channels[c];
            if (channel.target_node == nullptr || channel.sampler == nullptr) {
                continue;
            }
            const auto node = static_cast<std::size_t>(channel.target_node - data_.nodes);
            if (channel.target_path == cgltf_animation_path_type_rotation) {
                readSampler(*channel.sampler, channels_[node], [](std::array<float, 4> v) {
                    return core::Quat{v[0], v[1], v[2], v[3]}.normalized();
                });
            } else if (channel.target_path == cgltf_animation_path_type_translation) {
                readSampler(*channel.sampler, translations_[node],
                            [](std::array<float, 4> v) { return Vec3{v[0], v[1], v[2]}; });
            } else if (channel.target_path == cgltf_animation_path_type_scale) {
                readSampler(*channel.sampler, scales_[node],
                            [](std::array<float, 4> v) { return Vec3{v[0], v[1], v[2]}; });
            } else if (channel.target_path == cgltf_animation_path_type_weights) {
                readWeights(*channel.sampler, node);
            }
        }
    }

    // weights 채널: 키마다 타깃 수만큼 값이 이어짐 (CUBICSPLINE은 키마다 접선·값·접선 묶음)
    void readWeights(const cgltf_animation_sampler& sampler, std::size_t node) {
        const cgltf_size targets = morphTargetCount(data_.nodes[node]);
        const cgltf_size count = sampler.input->count;
        const cgltf_size stride =
            sampler.interpolation == cgltf_interpolation_type_cubic_spline ? 3 : 1;
        if (targets == 0 || sampler.output->count < count * stride * targets) {
            return;
        }
        for (cgltf_size t = 0; t < targets; ++t) {
            WeightChannel& channel = weights_.emplace_back();
            channel.node = node;
            channel.target = t;
            channel.values.step = sampler.interpolation == cgltf_interpolation_type_step;
            for (cgltf_size k = 0; k < count; ++k) {
                float time = 0.0f;
                float value = 0.0f;
                cgltf_accessor_read_float(sampler.input, k, &time, 1);
                cgltf_accessor_read_float(sampler.output,
                                          (k * stride + (stride - 1) / 2) * targets + t, &value, 1);
                channel.values.times.push_back(time);
                channel.values.values.push_back(value);
                duration_ = std::max(duration_, time);
            }
        }
    }

    template <class T, class Convert>
    void readSampler(const cgltf_animation_sampler& sampler, Channel<T>& out, Convert convert) {
        const cgltf_size count = sampler.input->count;
        // CUBICSPLINE은 키마다 (들어오는 접선, 값, 나가는 접선): 값만 쓰고 선형으로 근사
        const cgltf_size stride =
            sampler.interpolation == cgltf_interpolation_type_cubic_spline ? 3 : 1;
        if (sampler.output->count < count * stride) {
            return;
        }
        const cgltf_size components = cgltf_num_components(sampler.output->type);
        out = {};
        out.step = sampler.interpolation == cgltf_interpolation_type_step;
        for (cgltf_size k = 0; k < count; ++k) {
            float t = 0.0f;
            std::array<float, 4> v{};
            cgltf_accessor_read_float(sampler.input, k, &t, 1);
            cgltf_accessor_read_float(sampler.output, k * stride + (stride - 1) / 2, v.data(),
                                      std::min<cgltf_size>(components, v.size()));
            out.times.push_back(t);
            out.values.push_back(convert(v));
            duration_ = std::max(duration_, t);
        }
    }

    // 부모가 먼저 오는 노드 순서 (glTF는 자식 번호가 부모보다 작을 수 있음)
    void orderNodes() {
        std::vector<bool> placed(data_.nodes_count, false);
        while (order_.size() < data_.nodes_count) {
            const std::size_t before = order_.size();
            for (cgltf_size n = 0; n < data_.nodes_count; ++n) {
                const cgltf_node* parent = data_.nodes[n].parent;
                if (!placed[n] &&
                    (parent == nullptr || placed[static_cast<std::size_t>(parent - data_.nodes)])) {
                    placed[n] = true;
                    order_.push_back(n);
                }
            }
            if (order_.size() == before) {
                break;  // 순환 (손상된 파일): 남은 노드는 무시
            }
        }
    }

    [[nodiscard]] int indexOf(const cgltf_node* node) const {
        return node != nullptr ? static_cast<int>(node - data_.nodes) : -1;
    }

    // 회전은 slerp, 이동은 선형 보간
    template <class T>
    [[nodiscard]] static T sample(const Channel<T>& channel, float time) {
        const auto& t = channel.times;
        if (time <= t.front()) {
            return channel.values.front();
        }
        if (time >= t.back()) {
            return channel.values.back();
        }
        const auto k = static_cast<std::size_t>(std::ranges::upper_bound(t, time) - t.begin()) - 1;
        if (channel.step || t[k + 1] <= t[k]) {
            return channel.values[k];
        }
        const float w = (time - t[k]) / (t[k + 1] - t[k]);
        if constexpr (std::is_same_v<T, core::Quat>) {
            return core::slerp(channel.values[k], channel.values[k + 1], w);
        } else {
            return channel.values[k] + (channel.values[k + 1] - channel.values[k]) * w;
        }
    }

    // 노드의 월드 위치 = Σ (조상마다 부모의 월드 회전 · 로컬 이동). world는 같은 시각의
    // worldRotations. 크기는 무시 (VRMA 노드는 크기 1). time < 0이면 기본 자세
    [[nodiscard]] Vec3 worldPosition(std::size_t n, float time,
                                     const std::vector<core::Quat>& world) const {
        Vec3 position;
        std::size_t depth = 0;
        for (const cgltf_node* node = &data_.nodes[n]; node != nullptr && depth < data_.nodes_count;
             node = node->parent, ++depth) {
            const auto i = static_cast<std::size_t>(node - data_.nodes);
            Vec3 local =
                node->has_translation != 0
                    ? Vec3{node->translation[0], node->translation[1], node->translation[2]}
                    : Vec3{};
            if (time >= 0.0f && !translations_[i].times.empty()) {
                local = sample(translations_[i], time);
            }
            const core::Quat parent =
                node->parent != nullptr
                    ? world[static_cast<std::size_t>(node->parent - data_.nodes)]
                    : core::Quat{};
            position = position + parent.rotate(local);
        }
        return position;
    }

    // time < 0이면 애니메이션 없는 기본 자세
    [[nodiscard]] std::vector<core::Quat> worldRotations(float time) const {
        std::vector<core::Quat> world(data_.nodes_count);
        for (const std::size_t n : order_) {
            const cgltf_node& node = data_.nodes[n];
            core::Quat local{};
            if (time >= 0.0f && !channels_[n].times.empty()) {
                local = sample(channels_[n], time);
            } else if (node.has_rotation != 0) {
                local = core::Quat{node.rotation[0], node.rotation[1], node.rotation[2],
                                   node.rotation[3]}
                            .normalized();
            }
            const core::Quat parent =
                node.parent != nullptr ? world[static_cast<std::size_t>(node.parent - data_.nodes)]
                                       : core::Quat{};
            world[n] = parent * local;  // 로컬을 먼저 적용한 뒤 부모 (W = W부모 · L)
        }
        return world;
    }

    const cgltf_data& data_;
    std::string_view clipName_;
    MotionFormat format_ = MotionFormat::Gltf;
    std::vector<HumanBone> human_;  // 노드 → 휴머노이드 본
    std::vector<Channel<core::Quat>> channels_;
    std::vector<Channel<Vec3>> translations_;
    std::vector<Channel<Vec3>> scales_;
    struct WeightChannel {
        std::size_t node = 0;
        std::size_t target = 0;
        Channel<float> values;
    };
    std::vector<WeightChannel> weights_;
    std::vector<std::size_t> order_;
    float duration_ = 0.0f;
};

}  // namespace

MotionLoadResult importGltfMotion(std::span<const std::uint8_t> bytes, std::string_view clipName) {
    cgltf_options options{};
    cgltf_data* raw = nullptr;
    cgltf_result status = cgltf_parse(&options, bytes.data(), bytes.size(), &raw);
    if (status != cgltf_result_success) {
        return {std::nullopt, resultToString(status)};
    }
    const DataPtr data(raw);
    status = cgltf_load_buffers(&options, data.get(), "");
    if (status == cgltf_result_success) {
        status = cgltf_validate(data.get());
    }
    if (status != cgltf_result_success) {
        return {std::nullopt, resultToString(status)};
    }
    MotionLoadResult result;
    result.clip = GltfMotionConverter(*data, clipName).run(result.error);
    return result;
}

LoadResult importGltf(std::span<const std::uint8_t> bytes, const ImportContext& context) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    const cgltf_result status = cgltf_parse(&options, bytes.data(), bytes.size(), &data);
    if (status != cgltf_result_success) {
        return {std::nullopt, resultToString(status)};
    }
    // 주의: GLB의 BIN 청크는 복사되지 않고 bytes를 가리키므로, convert가 끝날 때까지 bytes가 살아
    // 있어야 함
    return convert(data, context);
}

}  // namespace deskpet::model::detail
