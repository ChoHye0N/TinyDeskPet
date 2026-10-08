// FBX(바이너리·ASCII) 로더 (ufbx, ADR-0009). 명세: docs/03-detailed-design/model.md §4.5
// 축·단위 변환은 ufbx에 맡겨(target_axes, target_unit_meters) 결과를 공통 규약으로 받습니다.

#include "model/Importers.h"
#include "model/TextureSource.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <string>
#include <ufbx.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace deskpet::model::detail {
namespace {

struct SceneDeleter {
    void operator()(ufbx_scene* scene) const noexcept { ufbx_free_scene(scene); }
};
using ScenePtr = std::unique_ptr<ufbx_scene, SceneDeleter>;

std::string toString(ufbx_string s) {
    return {s.data, s.length};
}

// ufbx_real은 기본이 double
core::Vec3 toVec3(ufbx_vec3 v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

class FbxConverter {
public:
    FbxConverter(const ufbx_scene& scene, const ImportContext& context)
        : scene_(scene), context_(context) {}

    std::optional<Model> run(std::string& error) {
        model_.format = ModelFormat::Fbx;
        readMaterials();
        readBones();  // 메시의 스킨 가중치가 본 번호를 쓰므로 먼저
        for (std::size_t i = 0; i < scene_.nodes.count; ++i) {
            const ufbx_node& node = *scene_.nodes.data[i];
            if (node.mesh != nullptr && !node.is_root && !readMesh(node, *node.mesh, error)) {
                return std::nullopt;
            }
        }
        if (model_.vertices.empty()) {
            error = "그릴 수 있는 메시가 없습니다";
            return std::nullopt;
        }
        model_.bounds = computeBounds(model_.vertices);
        return std::move(model_);
    }

private:
    int textureIndex(const ufbx_texture* texture) {
        if (texture == nullptr) {
            return -1;
        }
        if (const auto it = textureIndices_.find(texture); it != textureIndices_.end()) {
            return it->second;
        }
        Texture result;
        const std::string name = toString(texture->relative_filename);
        if (texture->content.size > 0) {
            // FBX 파일 안에 내장된 이미지 (Mixamo "Embed textures" 등)
            const auto* data = static_cast<const std::uint8_t*>(texture->content.data);
            result = makeTexture({data, data + texture->content.size}, name);
        } else {
            result = loadTextureFile(context_.baseDirectory, name);
            if (result.empty()) {
                // 상대 경로가 맞지 않으면 파일 이름만으로 모델 폴더에서 찾음 (다른 PC에서 만든
                // 파일)
                const std::string full = toString(texture->filename);
                const std::size_t slash = full.find_last_of("/\\");
                result =
                    loadTextureFile(context_.baseDirectory,
                                    slash == std::string::npos ? full : full.substr(slash + 1));
            }
        }
        const int index = static_cast<int>(model_.textures.size());
        model_.textures.push_back(std::move(result));
        textureIndices_.emplace(texture, index);
        return index;
    }

    void readMaterials() {
        for (std::size_t i = 0; i < scene_.materials.count; ++i) {
            const ufbx_material& src = *scene_.materials.data[i];
            Material material;
            material.name = toString(src.name);

            // ufbx가 FBX 고전 재질(Phong 등)도 PBR 값으로 정리해 줌
            const ufbx_material_map& base = src.pbr.base_color;
            ufbx_vec4 color = base.value_vec4;
            if (!base.has_value) {
                color.x = color.y = color.z = color.w = 1.0;
            }
            const double factor =
                src.pbr.base_factor.has_value ? src.pbr.base_factor.value_real : 1.0;
            const double opacity = src.pbr.opacity.has_value ? src.pbr.opacity.value_real : 1.0;
            material.baseColor = {
                static_cast<float>(color.x * factor), static_cast<float>(color.y * factor),
                static_cast<float>(color.z * factor), static_cast<float>(opacity)};
            material.baseColorTexture = textureIndex(
                base.texture != nullptr ? base.texture : src.fbx.diffuse_color.texture);
            material.doubleSided = src.features.double_sided.enabled;
            if (opacity < 1.0) {
                material.alphaMode = AlphaMode::Blend;
            } else if (material.baseColorTexture >= 0) {
                material.alphaMode = AlphaMode::Mask;  // 텍스처 알파로 잘라냄 (머리카락 끝 등)
            }

            materialIndices_.emplace(&src, static_cast<int>(model_.materials.size()));
            model_.materials.push_back(std::move(material));
        }
    }

    bool readMesh(const ufbx_node& node, const ufbx_mesh& mesh, std::string& error) {
        const ufbx_matrix& toWorld = node.geometry_to_world;
        const ufbx_matrix normalMatrix = ufbx_matrix_for_normals(&toWorld);
        std::vector<std::uint32_t> triangle(mesh.max_face_triangles * 3);

        for (std::size_t p = 0; p < mesh.material_parts.count; ++p) {
            const ufbx_mesh_part& part = mesh.material_parts.data[p];
            if (part.num_triangles == 0) {
                continue;
            }
            int material = -1;
            if (part.index < mesh.materials.count) {
                if (const auto it = materialIndices_.find(mesh.materials.data[part.index]);
                    it != materialIndices_.end()) {
                    material = it->second;
                }
            }
            if (model_.vertices.size() + part.num_triangles * 3 >
                std::numeric_limits<std::uint32_t>::max()) {
                error = "정점이 너무 많습니다";
                return false;
            }

            Primitive primitive;
            primitive.firstIndex = static_cast<std::uint32_t>(model_.indices.size());
            primitive.material = material;
            for (std::size_t f = 0; f < part.face_indices.count; ++f) {
                const ufbx_face face = mesh.faces.data[part.face_indices.data[f]];
                // 다각형 → 삼각형 (오목 다각형도 처리). 결과는 면 꼭짓점(인덱스) 번호
                const std::uint32_t count =
                    ufbx_triangulate_face(triangle.data(), triangle.size(), &mesh, face);
                for (std::size_t k = 0; k < std::size_t{count} * 3U; ++k) {
                    model_.indices.push_back(static_cast<std::uint32_t>(model_.vertices.size()));
                    Vertex v = makeVertex(mesh, triangle[k], toWorld, normalMatrix);
                    readSkinWeights(v, mesh, triangle[k]);
                    model_.vertices.push_back(v);
                }
            }
            primitive.indexCount =
                static_cast<std::uint32_t>(model_.indices.size()) - primitive.firstIndex;
            model_.primitives.push_back(primitive);
        }
        return true;
    }

    static Vertex makeVertex(const ufbx_mesh& mesh, std::uint32_t index, const ufbx_matrix& toWorld,
                             const ufbx_matrix& normalMatrix) {
        Vertex v;
        v.position = toVec3(
            ufbx_transform_position(&toWorld, ufbx_get_vertex_vec3(&mesh.vertex_position, index)));
        if (mesh.vertex_normal.exists) {
            v.normal = core::normalize(toVec3(ufbx_transform_direction(
                &normalMatrix, ufbx_get_vertex_vec3(&mesh.vertex_normal, index))));
        }
        if (mesh.vertex_uv.exists) {
            const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh.vertex_uv, index);
            v.u = static_cast<float>(uv.x);
            v.v = static_cast<float>(1.0 - uv.y);  // FBX UV 원점은 왼쪽 아래 → 왼쪽 위로
        }
        return v;
    }

    // 스킨 클러스터의 (본, 가중치) → 정점 가중치. index는 면 꼭짓점 번호라 정점 번호로 바꿔 조회
    void readSkinWeights(Vertex& v, const ufbx_mesh& mesh, std::uint32_t index) const {
        if (mesh.skin_deformers.count == 0) {
            return;  // 스킨 없는 메시는 움직이지 않음 (본이 아닌 노드에 달린 소품 등)
        }
        const ufbx_skin_deformer& skin = *mesh.skin_deformers.data[0];
        const std::uint32_t vertex = mesh.vertex_indices.data[index];
        if (vertex >= skin.vertices.count) {
            return;
        }
        const ufbx_skin_vertex& sv = skin.vertices.data[vertex];
        std::vector<std::pair<int, float>> influences;
        for (std::uint32_t w = 0; w < sv.num_weights; ++w) {
            const ufbx_skin_weight& weight = skin.weights.data[sv.weight_begin + w];
            const ufbx_skin_cluster& cluster = *skin.clusters.data[weight.cluster_index];
            const auto it = boneIndices_.find(cluster.bone_node);
            influences.emplace_back(it != boneIndices_.end() ? it->second : -1,
                                    static_cast<float>(weight.weight));
        }
        setSkinWeights(v, influences);
    }

    // 본 = bone 속성이 있는 노드(LimbNode 등) + 스킨 클러스터가 가리키는 노드.
    // 위치는 스킨의 바인드 포즈(bind_to_world)를 우선 — 파일의 현재 자세가 바인드 포즈와 다를 수
    // 있음
    void readBones() {
        std::unordered_map<const ufbx_node*, ufbx_vec3> bindPositions;
        for (std::size_t i = 0; i < scene_.skin_clusters.count; ++i) {
            const ufbx_skin_cluster& cluster = *scene_.skin_clusters.data[i];
            if (cluster.bone_node != nullptr) {
                bindPositions.emplace(cluster.bone_node, cluster.bind_to_world.cols[3]);
            }
        }
        for (std::size_t i = 0; i < scene_.nodes.count; ++i) {
            const ufbx_node* node = scene_.nodes.data[i];
            const auto bind = bindPositions.find(node);
            if (node->bone == nullptr && bind == bindPositions.end()) {
                continue;
            }
            Bone bone;
            bone.name = toString(node->name);
            // 행렬의 4번째 열 = 이동
            bone.position =
                toVec3(bind != bindPositions.end() ? bind->second : node->node_to_world.cols[3]);
            bone.human = humanBoneFromFbxName(bone.name);
            boneIndices_.emplace(node, static_cast<int>(model_.bones.size()));
            model_.bones.push_back(std::move(bone));
        }
        for (const auto& [node, index] : boneIndices_) {
            for (const ufbx_node* parent = node->parent; parent != nullptr;
                 parent = parent->parent) {
                if (const auto it = boneIndices_.find(parent); it != boneIndices_.end()) {
                    model_.bones[static_cast<std::size_t>(index)].parent = it->second;
                    break;
                }
            }
        }
    }

    const ufbx_scene& scene_;
    const ImportContext& context_;
    Model model_;
    std::unordered_map<const ufbx_texture*, int> textureIndices_;
    std::unordered_map<const ufbx_material*, int> materialIndices_;
    std::unordered_map<const ufbx_node*, int> boneIndices_;
};

core::Quat toQuat(ufbx_quat q) {
    return core::Quat{static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z),
                      static_cast<float>(q.w)}
        .normalized();
}

// 첫 번째 애니메이션 스택 → MotionClip (ADR-0013). 본 이름은 Mixamo 관례로 휴머노이드에 연결.
// 기본 자세 = 애니메이션 없는 노드 값 (Mixamo는 T포즈). 다른 자세여도 기본 자세 방향을 같은
// 값에서 재므로 팔다리 방향은 맞음
class FbxMotionConverter {
public:
    explicit FbxMotionConverter(const ufbx_scene& scene) : scene_(scene) {}

    std::optional<MotionClip> run(std::string& error) {
        if (scene_.anim_stacks.count == 0) {
            error = "애니메이션이 없습니다";
            return std::nullopt;
        }
        std::array<const ufbx_node*, kHumanBoneCount> human{};
        for (const ufbx_node* node : scene_.nodes) {
            const auto bone = static_cast<std::size_t>(humanBoneFromFbxName(toString(node->name)));
            if (bone != 0 && human[bone] == nullptr) {
                human[bone] = node;
            }
        }
        if (std::ranges::all_of(human, [](const ufbx_node* n) { return n == nullptr; })) {
            error = "휴머노이드 본(Mixamo 이름 등)이 없습니다";
            return std::nullopt;
        }

        const ufbx_anim_stack& stack = *scene_.anim_stacks.data[0];
        double begin = stack.time_begin;
        double end = stack.time_end;
        if (end <= begin) {
            // 재생 구간(LocalStart/Stop)이 없는 파일: 키프레임이 있는 구간 전체
            begin = std::numeric_limits<double>::max();
            end = std::numeric_limits<double>::lowest();
            for (const ufbx_anim_curve* curve : scene_.anim_curves) {
                if (curve->keyframes.count > 0) {
                    begin = std::min(begin, curve->min_time);
                    end = std::max(end, curve->max_time);
                }
            }
            if (end < begin) {
                begin = end = 0.0;
            }
        }
        const double length = end - begin;
        MotionClip clip;
        clip.format = MotionFormat::Fbx;
        clip.frameCount =
            static_cast<std::size_t>(std::floor(length * MotionClip::kFramesPerSecond + 1e-3)) + 1;

        const std::vector<core::Quat> rest = worldRotations(nullptr, 0.0);
        std::array<std::optional<core::Vec3>, kHumanBoneCount> positions{};
        for (std::size_t b = 0; b < human.size(); ++b) {
            if (human[b] != nullptr) {
                positions[b] = toVec3(human[b]->node_to_world.cols[3]);
            }
        }
        for (std::size_t frame = 0; frame < clip.frameCount; ++frame) {
            const double time = begin + static_cast<double>(frame) / MotionClip::kFramesPerSecond;
            const std::vector<core::Quat> world = worldRotations(stack.anim, time);
            for (std::size_t b = 0; b < human.size(); ++b) {
                if (human[b] != nullptr) {
                    const std::size_t n = human[b]->typed_id;
                    // 기본 자세 → 지금: W(t)·W₀⁻¹ (모델 축 기준 회전)
                    clip.tracks[b].push_back(world[n] * rest[n].conjugate());
                }
            }
        }
        motion::setRestDirections(clip, positions);
        return clip;
    }

private:
    // 노드별 월드 회전 (typed_id 순). anim이 nullptr이면 애니메이션 없는 값
    [[nodiscard]] std::vector<core::Quat> worldRotations(const ufbx_anim* anim, double time) const {
        std::vector<core::Quat> world(scene_.nodes.count);
        std::vector<bool> done(scene_.nodes.count, false);
        // 부모를 먼저 계산 (scene.nodes의 순서에 기대지 않음). 깊이는 뼈대 깊이 정도
        const auto compute = [&](const auto& self, const ufbx_node* node) -> core::Quat {
            const std::size_t n = node->typed_id;
            if (done[n]) {
                return world[n];
            }
            const ufbx_transform local =
                anim != nullptr ? ufbx_evaluate_transform(anim, node, time) : node->local_transform;
            const core::Quat parent =
                node->parent != nullptr ? self(self, node->parent) : core::Quat{};
            world[n] = parent * toQuat(local.rotation);  // W = W부모 · L
            done[n] = true;
            return world[n];
        };
        for (const ufbx_node* node : scene_.nodes) {
            compute(compute, node);
        }
        return world;
    }

    const ufbx_scene& scene_;
};

}  // namespace

MotionLoadResult importFbxMotion(std::span<const std::uint8_t> bytes) {
    ufbx_load_opts options{};
    options.target_axes = ufbx_axes_right_handed_y_up;
    options.target_unit_meters = 1.0;
    options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    options.ignore_geometry = true;  // 모션만 필요 (메시가 함께 있는 파일도 빠르게)

    ufbx_error error{};
    const ScenePtr scene(ufbx_load_memory(bytes.data(), bytes.size(), &options, &error));
    if (!scene) {
        std::array<char, 512> message{};
        ufbx_format_error(message.data(), message.size(), &error);
        return {std::nullopt, std::format("FBX를 읽을 수 없습니다: {}", message.data())};
    }
    MotionLoadResult result;
    result.clip = FbxMotionConverter(*scene).run(result.error);
    return result;
}

LoadResult importFbx(std::span<const std::uint8_t> bytes, const ImportContext& context) {
    ufbx_load_opts options{};
    // 공통 규약으로 변환: 오른손 Y 위, 미터. MODIFY_GEOMETRY는 루트 변환 대신 정점·노드를 직접 바꿈
    options.target_axes = ufbx_axes_right_handed_y_up;
    options.target_unit_meters = 1.0;
    options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    options.generate_missing_normals = true;
    options.ignore_animation = true;  // 모션은 M4 이후

    ufbx_error error{};
    const ScenePtr scene(ufbx_load_memory(bytes.data(), bytes.size(), &options, &error));
    if (!scene) {
        std::array<char, 512> message{};
        ufbx_format_error(message.data(), message.size(), &error);
        return {std::nullopt, std::format("FBX를 읽을 수 없습니다: {}", message.data())};
    }

    LoadResult result;
    result.model = FbxConverter(*scene, context).run(result.error);
    return result;
}

}  // namespace deskpet::model::detail
