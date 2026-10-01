#pragma once

// 뼈대 정방향 운동학(FK)과 스키닝 행렬. 명세: docs/03-detailed-design/anim.md, ADR-0010
//
// 회전 규약: 본마다 "모델 공간 축 기준 델타 회전"을 줍니다. 부모의 자세가 먼저 적용된 뒤
// 그 위에 델타가 더해집니다 (누적 회전 Aⱼ = Δⱼ × A부모). 원본 본의 로컬 축 방향을 몰라도 되므로
// VRM·PMX·FBX를 같은 코드로 움직일 수 있습니다.
//
// 스킨 행렬 Sⱼ = T(-pⱼ) · R(Aⱼ) · T(Pⱼ)   (행 벡터 규약)
//   pⱼ: 바인드 포즈 관절 위치, Pⱼ: 자세를 적용한 관절 위치 = P부모 + A부모(pⱼ - p부모)

#include "core/Math3D.h"
#include "model/Model.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace deskpet::anim {

class Skeleton {
public:
    explicit Skeleton(const model::Model& model);

    [[nodiscard]] std::size_t size() const noexcept { return positions_.size(); }
    [[nodiscard]] int find(model::HumanBone bone) const noexcept;
    [[nodiscard]] core::Vec3 bindPosition(int bone) const noexcept;

    // rotations[본] = 델타 회전 (크기는 size()). out은 재사용되며 size()개로 맞춰짐
    void computeSkinMatrices(std::span<const core::Quat> rotations,
                             std::vector<core::Mat4>& out) const;

private:
    std::vector<core::Vec3> positions_;
    std::vector<int> parents_;
    std::vector<int> order_;       // 부모가 자식보다 먼저 오는 계산 순서
    std::array<int, 32> human_{};  // HumanBone → 본 인덱스 (-1 = 없음)

    // computeSkinMatrices의 중간 결과 (할당 재사용)
    mutable std::vector<core::Quat> accumulated_;
    mutable std::vector<core::Vec3> posed_;
};

}  // namespace deskpet::anim
