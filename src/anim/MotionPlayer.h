#pragma once

// 모션 클립 재생과 리타기팅 (ADR-0013). 명세: docs/03-detailed-design/anim.md §4.6
//
// 클립은 휴머노이드 본마다 "원본 기본 자세 → 지금"의 모델 공간 회전(월드 델타 Δ)을 갖습니다.
// 대상 모델의 본 j에 대해:
//   목표 누적 회전 Aⱼ = Δ(그 본) · C(그 본)       트랙이 있으면
//                     = A부모                       없으면 (부모를 그대로 따라감)
//   Skeleton 델타      = Aⱼ · A부모⁻¹
// C는 기본 자세 차이 보정: 대상의 팔다리 방향을 원본 기본 자세 방향으로 돌리는 회전
// (T포즈 모델에 A포즈 기준 모션을 입혀도 팔이 같은 방향). 방향을 모르는 본은 부모 쪽 C를 씀.
// 휴머노이드가 아닌 본은 이름이 같은 트랙이 있으면 같은 방식으로 재생하고, 엉덩이 이동은
// rootOffset으로 몸 전체를 옮김 (ADR-0014)

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
    // 처음 한 번은 끝까지, 그 뒤로는 클립의 loopStart부터 끝까지 반복 (loopStart가 길이 이상이면
    // 마지막 자세 유지)
    void sample(float time, std::vector<core::Quat>& rotations) const;

    // time(초)에 몸 전체를 옮길 거리 (모델 공간 m, computePose의 rootOffset, ADR-0014).
    // 클립의 엉덩이 이동을 대상 엉덩이 높이에 맞춰 줄이거나 늘림. 수평은 시작 → 끝 이동(루트
    // 모션)과 처음 위치를 빼고 흔들림만 남김 (캐릭터를 옮기는 것은 앱). 이동이 없는 클립은 0
    [[nodiscard]] core::Vec3 rootOffset(float time) const;

private:
    struct FramePosition {
        std::size_t i0 = 0;
        std::size_t i1 = 0;
        float weight = 0.0f;  // i0 → i1 보간 비율
        float phase = 0.0f;   // 클립 안의 진행률 (0 ~ 1)
    };
    [[nodiscard]] FramePosition locate(float time) const;

    std::shared_ptr<const model::MotionClip> clip_;
    float hipsScale_ = 0.0f;  // 대상 엉덩이 높이 / 원본 엉덩이 높이. 0이면 이동 안 씀
    std::vector<int> parents_;
    std::vector<int> order_;
    // 본 → 클립 트랙 (휴머노이드 트랙 또는 이름이 같은 트랙), 없으면 nullptr. clip_이 소유
    std::vector<const std::vector<core::Quat>*> tracks_;
    std::vector<core::Quat> corrections_;          // 본별 기본 자세 보정 C
    mutable std::vector<core::Quat> accumulated_;  // sample 중간 결과 (할당 재사용)
};

}  // namespace deskpet::anim
