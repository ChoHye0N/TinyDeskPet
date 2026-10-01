#pragma once

// 코드로 계산하는 휴머노이드 동작 (모션 파일 없음, ADR-0010). 명세: docs/03-detailed-design/anim.md
// 휴머노이드 본 이름(model::HumanBone)만 쓰므로 VRM·PMX·FBX 모두 같은 동작을 합니다.
// 없는 본은 건너뛰므로, 본이 부족한 모델도 할 수 있는 만큼만 움직입니다.

#include "anim/Skeleton.h"
#include "model/Model.h"

#include <array>
#include <cstdint>
#include <vector>

namespace deskpet::anim {

// 캐릭터 상태를 애니메이션 관점으로 옮긴 것 (character 모듈에 의존하지 않도록 app이 변환)
enum class Motion : std::uint8_t { Idle, Walk, Dragged, Airborne };

struct AnimationInput {
    Motion motion = Motion::Idle;
    float time = 0.0f;    // 누적 시간 (s). 주기 동작의 위상
    float squash = 0.0f;  // 착지 반동 (0 ~ 약 0.2) → 무릎 굽힘
    bool blink = false;   // 눈 감는 순간
    bool idleMotion = true;  // false면 대기 중 숨쉬기 동작을 끔 → 화면이 멈춰 Present 생략 가능
};

struct AnimationOutput {
    std::vector<core::Mat4> skin;  // 본별 스킨 행렬 (Model::bones 순서)
    std::array<float, static_cast<std::size_t>(model::Expression::Count)> expressions{};
};

class ProceduralAnimator {
public:
    explicit ProceduralAnimator(const model::Model& model);

    void evaluate(const AnimationInput& input, AnimationOutput& out) const;

    // 대기·매달림·공중 자세를 모두 담는 경계 상자. T포즈 경계 대신 카메라 맞춤에 씀
    // (팔을 내린 대기 자세만 쓰면 매달림·공중에서 벌린 팔이 창 밖으로 잘림)
    [[nodiscard]] model::Bounds displayBounds() const;

private:
    struct Limb {
        int upper = -1;
        int lower = -1;
        core::Vec3 bindDirection;  // 바인드 포즈에서 upper → lower 방향 (단위 벡터)
        float side = 1.0f;         // +1 = 모델 +X 쪽(캐릭터 왼쪽)
    };

    [[nodiscard]] Limb makeLimb(model::HumanBone upper, model::HumanBone lower) const;
    void setRotation(int bone, const core::Quat& q) const;
    void poseArm(const Limb& arm, float outward, float swing, float elbow) const;
    void poseLeg(int upper, int lower, float swing, float knee) const;
    void includePosedVertices(const std::vector<core::Mat4>& skin, model::Bounds& bounds) const;

    const model::Model& model_;
    Skeleton skeleton_;
    Limb leftArm_;
    Limb rightArm_;
    mutable std::vector<core::Quat> rotations_;  // evaluate 중간 결과 (할당 재사용)
};

}  // namespace deskpet::anim
