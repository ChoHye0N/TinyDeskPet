#pragma once

// 렌더링용 캐릭터 모델 (플랫폼 독립). 명세: docs/03-detailed-design/model.md
// 모든 형식(VRM/glTF, PMX, FBX)을 같은 규약으로 통일합니다 (ADR-0009):
//   미터 단위, 오른손 좌표계, +Y 위, 캐릭터 정면 +Z, 반시계(CCW)가 앞면, UV 원점은 이미지 왼쪽 위

#include "core/Math3D.h"
#include "model/Humanoid.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace deskpet::model {

// 정점 하나에 영향을 주는 최대 본 수 (GPU 스키닝 관례, ADR-0010)
inline constexpr std::size_t kMaxInfluences = 4;

struct Vertex {
    core::Vec3 position;
    core::Vec3 normal{0.0f, 1.0f, 0.0f};
    float u = 0.0f;
    float v = 0.0f;
    // 스킨: Model::bones 인덱스와 가중치. 가중치 큰 순서, 합이 1 (모두 0이면 움직이지 않음)
    std::array<std::uint16_t, kMaxInfluences> joints{};
    std::array<float, kMaxInfluences> weights{};
};

enum class AlphaMode : std::uint8_t { Opaque, Mask, Blend };

struct Material {
    std::string name;
    core::Vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    int baseColorTexture = -1;  // Model::textures 인덱스, 없으면 -1
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    // 외곽선 (반전 헐): 법선 방향으로 밀어낼 굵기 (m, 0이면 없음)와 색.
    // VRM MToon의 _OutlineWidth / outlineWidthFactor, PMX 에지에서 읽음
    float outlineWidth = 0.0f;
    core::Vec4 outlineColor{0.0f, 0.0f, 0.0f, 1.0f};
    // 발광 (glTF emissive, VRM 0.x는 MToon _EmissionColor). 텍스처 × 색을 더함
    core::Vec3 emissiveColor;
    int emissiveTexture = -1;
    bool unlit = false;  // KHR_materials_unlit: 그림자 없이 기본색 그대로 (MToon이 아닐 때)

    // MToon (VRM 툰 셰이딩, renderer.md §4.6). 값은 VRM 1.0 정의, 색은 모두 선형 공간.
    // mtoon = false면 렌더러가 기본 2단 툰(그림자 = 기본색을 어둡게)으로 그림 (PMX, FBX 등)
    bool mtoon = false;
    core::Vec3 shadeColor{1.0f, 1.0f, 1.0f};  // 그림자 면 색 = shadeColor × shadeTexture
    int shadeTexture = -1;                    // 없으면 흰색 (색만)
    // 밝기 = linearstep(−1 + toony, 1 − toony, N·L + shift): 0이면 그림자 색, 1이면 기본색
    float shadingShift = 0.0f;
    float shadingToony = 0.9f;
    // 림 = rimColor × (1 − N·V + rimLift)^rimFresnelPower + matcapColor × MatCap 텍스처.
    // rimLightingMix: 1이면 그림자 쪽에서 림이 약해짐
    core::Vec3 rimColor;
    float rimFresnelPower = 5.0f;
    float rimLift = 0.0f;
    float rimLightingMix = 1.0f;
    core::Vec3 matcapColor{1.0f, 1.0f, 1.0f};
    int matcapTexture = -1;  // 없으면 MatCap 없음
};

// 텍스처 이미지. 둘 중 하나만 채워지며, 둘 다 비어 있으면 읽기에 실패한 것(렌더러가 흰색 처리)
//   - encoded: PNG/JPEG/BMP 등 WIC가 읽는 형식의 원본 바이트 (디코딩은 렌더러)
//   - rgba:    WIC가 못 읽는 형식(TGA)을 미리 푼 RGBA8 픽셀 (width × height × 4)
struct Texture {
    std::string name;      // 원본 파일 이름 또는 경로 (로그용)
    std::string mimeType;  // 비어 있을 수 있음
    std::vector<std::uint8_t> encoded;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;

    [[nodiscard]] bool empty() const noexcept { return encoded.empty() && rgba.empty(); }
};

// 뼈대의 한 본 (바인드 포즈). 애니메이션(M4)의 기반 데이터
struct Bone {
    std::string name;     // 원본 이름 (UTF-8)
    int parent = -1;      // Model::bones 인덱스, 루트면 -1
    core::Vec3 position;  // 바인드 포즈에서의 모델 공간 위치
    HumanBone human = HumanBone::None;
};

// SpringBone 충돌체 (구). 본을 따라 움직임
struct SpringCollider {
    int bone = -1;        // Model::bones 인덱스
    core::Vec3 offset;    // 본 위치에서의 오프셋 (바인드 포즈 모델 공간, m)
    float radius = 0.0f;  // m
};

// SpringBone 그룹: roots의 자손 본 전체가 같은 파라미터로 흔들림 (VRM secondaryAnimation)
struct SpringGroup {
    float stiffness = 1.0f;     // 원래 방향으로 돌아가려는 힘
    float gravityPower = 0.0f;  // 중력 세기
    core::Vec3 gravityDir{0.0f, -1.0f, 0.0f};
    float dragForce = 0.4f;      // 0 ~ 1. 클수록 흔들림이 빨리 줄어듦
    float hitRadius = 0.02f;     // 본 끝의 충돌 반지름 (m)
    std::vector<int> roots;      // 흔들림이 시작되는 본
    std::vector<int> colliders;  // Model::springColliders 인덱스
};

// 같은 머티리얼로 그리는 인덱스 구간
struct Primitive {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    int material = -1;  // Model::materials 인덱스, 없으면 -1 (기본 흰색)
};

struct Bounds {
    core::Vec3 min;
    core::Vec3 max;

    [[nodiscard]] constexpr core::Vec3 size() const noexcept { return max - min; }
};

// 애니메이션이 쓰는 표정 (ADR-0010). 형식마다 다른 이름을 여기로 모읍니다:
//   VRM 1.0 blink/happy/surprised, VRM 0.x blink/joy, MMD まばたき/笑い/びっくり
enum class Expression : std::uint8_t { Blink, Happy, Surprised, Count };

// 표정 하나 = 정점 위치 오프셋의 희소 목록 (가중치 1일 때의 변위, 모델 공간)
struct Morph {
    std::vector<std::uint32_t> vertices;
    std::vector<core::Vec3> deltas;

    void add(std::uint32_t vertex, core::Vec3 delta) {
        vertices.push_back(vertex);
        deltas.push_back(delta);
    }
};

enum class VrmVersion : std::uint8_t { None, V0, V1 };  // None = VRM 확장 없는 일반 glTF

enum class ModelFormat : std::uint8_t { Unknown, Gltf, Pmx, Fbx };  // Gltf = glTF/GLB/VRM

struct Model {
    ModelFormat format = ModelFormat::Unknown;
    VrmVersion version = VrmVersion::None;
    std::vector<Vertex> vertices;  // 바인드 포즈를 적용한 모델 공간 좌표
    std::vector<std::uint32_t> indices;
    std::vector<Primitive> primitives;
    std::vector<Material> materials;
    std::vector<Texture> textures;
    std::vector<Bone> bones;
    std::array<Morph, static_cast<std::size_t>(Expression::Count)> expressions;
    std::vector<SpringGroup> springGroups;  // 비어 있으면 흔들림 없음 (VRM 0.x만 읽음)
    std::vector<SpringCollider> springColliders;
    Bounds bounds;

    [[nodiscard]] const Morph& expression(Expression e) const noexcept {
        return expressions[static_cast<std::size_t>(e)];
    }
    [[nodiscard]] Morph& expression(Expression e) noexcept {
        return expressions[static_cast<std::size_t>(e)];
    }

    // 휴머노이드 본의 인덱스, 없으면 -1
    [[nodiscard]] int findBone(HumanBone human) const noexcept {
        for (std::size_t i = 0; i < bones.size(); ++i) {
            if (bones[i].human == human) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

// 정점에서 경계 상자를 계산합니다 (각 로더가 마지막에 호출)
[[nodiscard]] Bounds computeBounds(const std::vector<Vertex>& vertices);

// (본, 가중치) 후보들 중 큰 것부터 최대 4개를 골라 합이 1이 되도록 정규화해 정점에 기록합니다.
// 같은 본이 여러 번 나오면 합칩니다. 가중치 합이 0이면 정점은 움직이지 않음
void setSkinWeights(Vertex& vertex, std::span<const std::pair<int, float>> influences);

}  // namespace deskpet::model
