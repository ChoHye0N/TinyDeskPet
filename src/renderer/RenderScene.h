#pragma once

// 렌더러 입력 데이터. 렌더러는 이 데이터만 보고 그리며, 캐릭터 로직을 모릅니다 (SAD 규칙 R3).

#include "core/Math.h"
#include "core/Math3D.h"
#include "model/Model.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

struct ImDrawData;  // Dear ImGui (renderer API는 ImGui 헤더에 의존하지 않음)

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
    core::Vec3 viewDirection{0.0f, 0.0f, 1.0f};  // 모델 → 카메라 방향 (모델 공간, 림·MatCap)
    // 애니메이션 (ADR-0010). 스킨 행렬이 비어 있으면 바인드 포즈 그대로
    std::vector<core::Mat4> skinMatrices;  // Model::bones 순서
    std::array<float, static_cast<std::size_t>(model::Expression::Count)> expressionWeights{};
    std::vector<float> morphWeights;  // Model::morphs 순서 (비면 모두 0, ADR-0015)

    bool operator==(const MeshCharacter&) const = default;
};

struct RenderScene {
    core::SizeI viewport;
    // 3D 캐릭터가 그려질 수 있는 영역 (뷰포트 px). 렌더러는 3D·MSAA를 이 영역 크기로만 그리고
    // 화면 전체 프레임의 그 자리에 복사함 (화면 전체 MSAA는 비쌈, ADR-0011). 비면 화면 전체
    core::RectI sceneRegion;
    PlaceholderCharacter placeholder;
    MeshCharacter character;
    // 설정 창 (Dear ImGui, ADR-0016). 닫혀 있으면 nullptr. 소유하지 않음 (ui 모듈의 이번 프레임
    // 데이터). 열려 있는 동안 앱이 uiFrame을 매 프레임 올려 Present를 생략하지 않게 함
    ImDrawData* ui = nullptr;
    std::uint64_t uiFrame = 0;

    // 렌더러가 "변화 없음"을 판단할 때 사용 (DEBT-02)
    bool operator==(const RenderScene&) const = default;
};

}  // namespace deskpet::renderer
