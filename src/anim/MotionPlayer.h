#pragma once

// 모션 클립 재생과 리타기팅 (ADR-0013). 명세: docs/03-detailed-design/anim.md §4.6
//
// 클립은 휴머노이드 본마다 "원본 기본 자세 → 지금"의 모델 공간 회전(월드 델타 Δ)을 갖습니다.
// 대상 모델의 본 j에 대해:
//   목표 누적 회전 Aⱼ = Δ(그 본) · C(그 본)       트랙이 있으면
//                     = A부모                       없으면 (부모를 그대로 따라감)
//   Skeleton 델타      = Aⱼ · A부모⁻¹
// C는 기본 자세 차이 보정: 대상의 팔다리 방향을 원본 기본 자세 방향으로 돌리는 회전
// (T포즈 모델에 A포즈 기준 모션을 입혀도 팔이 같은 방향). 방향을 모르는 본은 부모 쪽 C를 씀

#include "anim/Skeleton.h"
#include "model/Motion.h"

#include <memory>
#include <vector>

namespace deskpet::anim {

class MotionPlayer {
public:
    MotionPlayer(const Skeleton& skeleton, std::shared_ptr<const model::MotionClip> clip);

    [[nodiscard]] float duration() const noexcept { return clip_->duration(); }
    [[nodiscard]] std::size_t frameCount() const noexcept { return clip_->frameCount; }

    // time(초)의 자세를 rotations(Skeleton 규약의 본별 델타, 크기 = 본 수)에 씀.
    // 길이를 넘으면 처음부터 반복
    void sample(float time, std::vector<core::Quat>& rotations) const;

private:
    std::shared_ptr<const model::MotionClip> clip_;
    std::vector<int> parents_;
    std::vector<int> order_;
    std::vector<int> tracks_;              // 본 → 클립 트랙 번호 (HumanBone), 없으면 -1
    std::vector<core::Quat> corrections_;  // 본별 기본 자세 보정 C
    mutable std::vector<core::Quat> accumulated_;  // sample 중간 결과 (할당 재사용)
};

}  // namespace deskpet::anim
