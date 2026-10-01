#pragma once

// 렌더러 입력 데이터. 렌더러는 이 데이터만 보고 그리며, 캐릭터 로직을 모릅니다 (SAD 규칙 R3).

#include "core/Math.h"
#include "core/Math3D.h"
#include "model/Model.h"

#include <array>
#include <cstddef>
#include <vector>

namespace deskpet::renderer {

// M0 임시 캐릭터 (타원 몸통 + 눈 + 볼)
struct PlaceholderCharacter {
    bool visible = true;
    core::Vec2 center;  // 창 내부 좌표 (px)
    float radiusX = 0.0f;
    float radiusY = 0.0f;
    bool eyesClosed = false;

    bool operator==(const PlaceholderCharacter&) const = default;
};

// VRM 캐릭터 (ADR-0008). model이 nullptr이면 그리지 않습니다.
struct MeshCharacter {
    const model::Model* model = nullptr;  // 소유하지 않음. 렌더러는 주소가 바뀌면 GPU에 다시 올림
    core::Mat4 viewProjection = core::Mat4::identity();  // 모델 공간 → 클립 공간
    core::Vec3 lightDirection{0.3f, -0.5f, -1.0f};  // 빛이 진행하는 방향 (모델 공간)
    // 애니메이션 (ADR-0010). 스킨 행렬이 비어 있으면 바인드 포즈 그대로
    std::vector<core::Mat4> skinMatrices;  // Model::bones 순서
    std::array<float, static_cast<std::size_t>(model::Expression::Count)> expressionWeights{};

    bool operator==(const MeshCharacter&) const = default;
};

struct RenderScene {
    core::SizeI viewport;
    PlaceholderCharacter placeholder;
    MeshCharacter character;

    // 렌더러가 "변화 없음"을 판단할 때 사용 (DEBT-02)
    bool operator==(const RenderScene&) const = default;
    // TODO(M4): 본 행렬, 모프 가중치
};

}  // namespace deskpet::renderer
