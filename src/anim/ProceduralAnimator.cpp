#include "anim/ProceduralAnimator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace deskpet::anim {
namespace {

using model::Expression;
using model::HumanBone;

constexpr float kPi = std::numbers::pi_v<float>;

constexpr float radians(float degrees) {
    return degrees * kPi / 180.0f;
}

constexpr core::Vec3 kAxisX{1.0f, 0.0f, 0.0f};  // 몸의 좌우 축: 앞뒤로 흔드는 회전
constexpr core::Vec3 kAxisY{0.0f, 1.0f, 0.0f};  // 몸통 비틀기
constexpr core::Vec3 kAxisZ{0.0f, 0.0f, 1.0f};  // 옆으로 기울이기

// 동작 파라미터 (각도는 도, 주기는 초). 값은 눈으로 보며 정한 것
constexpr float kBreathPeriod = 2.4f;
constexpr float kHeadSwayPeriod = 5.0f;
constexpr float kWalkCycle = 0.6f;  // 왼발·오른발 한 번씩
constexpr float kDangleCycle = 0.9f;

struct PoseParams {
    float armOutward = 10.0f;   // 팔이 몸에서 벌어지는 각도 (0 = 수직으로 내림)
    float leftArmSwing = 0.0f;  // + = 앞으로
    float rightArmSwing = 0.0f;
    float elbow = 10.0f;        // 팔꿈치 앞으로 굽힘
    float leftLegSwing = 0.0f;  // + = 앞으로
    float rightLegSwing = 0.0f;
    float leftKnee = 0.0f;
    float rightKnee = 0.0f;
    float chestPitch = 0.0f;  // + = 앞으로 숙임
    float spineTwist = 0.0f;
    float headTilt = 0.0f;
};

PoseParams paramsFor(const AnimationInput& in) {
    PoseParams p;
    const float t = in.time;
    switch (in.motion) {
        case Motion::Idle:
            if (in.idleMotion) {
                const float breath = std::sin(2.0f * kPi * t / kBreathPeriod);
                p.chestPitch = 1.2f * breath;
                p.leftArmSwing = p.rightArmSwing = 1.5f * breath;
                p.headTilt = 2.0f * std::sin(2.0f * kPi * t / kHeadSwayPeriod);
            }
            break;
        case Motion::Walk: {
            const float phase = 2.0f * kPi * t / kWalkCycle;
            const float s = std::sin(phase);
            p.leftLegSwing = 25.0f * s;
            p.rightLegSwing = -25.0f * s;
            // 다리가 뒤에서 앞으로 넘어오는 동안 무릎을 굽혀 발이 끌리지 않게 함
            p.leftKnee = 30.0f * std::max(0.0f, -std::cos(phase));
            p.rightKnee = 30.0f * std::max(0.0f, std::cos(phase));
            p.leftArmSwing = -18.0f * s;  // 팔은 같은 쪽 다리와 반대로
            p.rightArmSwing = 18.0f * s;
            p.elbow = 20.0f;
            p.spineTwist = 4.0f * s;
            break;
        }
        case Motion::Dragged: {
            // 들려서 대롱대롱: 팔은 벌리고 다리는 엇갈려 흔들림
            const float s = std::sin(2.0f * kPi * t / kDangleCycle);
            p.armOutward = 35.0f;
            p.leftArmSwing = 10.0f * s;
            p.rightArmSwing = -10.0f * s;
            p.leftLegSwing = 12.0f * s;
            p.rightLegSwing = -12.0f * s;
            p.leftKnee = p.rightKnee = 15.0f;
            p.headTilt = 6.0f * s;
            break;
        }
        case Motion::Airborne:
            p.armOutward = 45.0f;  // 균형 잡듯 벌림 (더 벌리면 창 폭이 커져 캐릭터가 작아짐)
            p.leftLegSwing = p.rightLegSwing = 15.0f;
            p.leftKnee = p.rightKnee = 35.0f;
            break;
    }
    return p;  // 착지 반동은 전환 보간 뒤에 더함 (applySquash)
}

// 표정 (깜빡임 제외): 들려 있거나 공중이면 놀람, 걸을 때는 살짝 웃음
Expressions expressionsFor(Motion motion) {
    Expressions e{};
    const bool surprised = motion == Motion::Dragged || motion == Motion::Airborne;
    e[static_cast<std::size_t>(Expression::Surprised)] = surprised ? 1.0f : 0.0f;
    e[static_cast<std::size_t>(Expression::Happy)] = motion == Motion::Walk ? 0.6f : 0.0f;
    return e;
}

}  // namespace

ProceduralAnimator::ProceduralAnimator(const model::Model& model)
    : model_(model),
      skeleton_(model),
      leftArm_(makeLimb(HumanBone::LeftUpperArm, HumanBone::LeftLowerArm)),
      rightArm_(makeLimb(HumanBone::RightUpperArm, HumanBone::RightLowerArm)) {}

ProceduralAnimator::Limb ProceduralAnimator::makeLimb(HumanBone upper, HumanBone lower) const {
    Limb limb;
    limb.upper = skeleton_.find(upper);
    limb.lower = skeleton_.find(lower);
    if (limb.upper < 0 || limb.lower < 0) {
        limb.upper = -1;  // 방향을 알 수 없으면 이 팔은 움직이지 않음
        return limb;
    }
    limb.bindDirection =
        core::normalize(skeleton_.bindPosition(limb.lower) - skeleton_.bindPosition(limb.upper));
    limb.side = limb.bindDirection.x >= 0.0f ? 1.0f : -1.0f;
    return limb;
}

void ProceduralAnimator::setRotation(int bone, const core::Quat& q) const {
    if (bone >= 0 && static_cast<std::size_t>(bone) < rotations_.size()) {
        rotations_[static_cast<std::size_t>(bone)] = q;
    }
}

void ProceduralAnimator::poseArm(const Limb& arm, float outward, float swing, float elbow) const {
    if (arm.upper < 0) {
        return;
    }
    // 목표 방향: 수직 아래에서 바깥으로 outward만큼, 그다음 앞뒤로 swing만큼.
    // 바인드 방향(T포즈든 A포즈든)에서 목표로 바로 돌리므로 원본 자세와 무관하게 같은 결과
    const float a = radians(outward);
    const core::Vec3 down{arm.side * std::sin(a), -std::cos(a), 0.0f};
    const core::Vec3 target = core::Quat::axisAngle(kAxisX, -radians(swing)).rotate(down);
    setRotation(arm.upper, core::Quat::fromTo(arm.bindDirection, target));
    // 아래팔: 위팔 자세 위에서 모델 X축 기준으로 앞으로 굽힘 (−각도 = 손이 +Z 쪽)
    setRotation(arm.lower, core::Quat::axisAngle(kAxisX, -radians(elbow)));
}

void ProceduralAnimator::poseLeg(int upper, int lower, float swing, float knee) const {
    setRotation(upper, core::Quat::axisAngle(kAxisX, -radians(swing)));  // + = 발이 앞(+Z)으로
    setRotation(lower, core::Quat::axisAngle(kAxisX, radians(knee)));  // + = 발이 뒤로 (무릎 굽힘)
}

void ProceduralAnimator::computeRotations(const AnimationInput& input) const {
    rotations_.assign(skeleton_.size(), core::Quat{});
    const PoseParams p = paramsFor(input);

    poseArm(leftArm_, p.armOutward, p.leftArmSwing, p.elbow);
    poseArm(rightArm_, p.armOutward, p.rightArmSwing, p.elbow);
    poseLeg(skeleton_.find(HumanBone::LeftUpperLeg), skeleton_.find(HumanBone::LeftLowerLeg),
            p.leftLegSwing, p.leftKnee);
    poseLeg(skeleton_.find(HumanBone::RightUpperLeg), skeleton_.find(HumanBone::RightLowerLeg),
            p.rightLegSwing, p.rightKnee);

    int chest = skeleton_.find(HumanBone::Chest);
    if (chest < 0) {
        chest = skeleton_.find(HumanBone::Spine);
    }
    if (p.chestPitch != 0.0f || p.spineTwist != 0.0f) {
        setRotation(chest, core::Quat::axisAngle(kAxisX, radians(p.chestPitch)) *
                               core::Quat::axisAngle(kAxisY, radians(p.spineTwist)));
    }
    if (p.headTilt != 0.0f) {
        setRotation(skeleton_.find(HumanBone::Head),
                    core::Quat::axisAngle(kAxisZ, radians(p.headTilt)));
    }
}

// 착지 반동: 무릎을 굽히고 허벅지를 앞으로 (squash 최대 약 0.18 → 무릎 약 27°).
// 다리 회전은 모두 X축이라 뒤에 곱해도 각도를 더한 것과 같음
void ProceduralAnimator::applySquash(float squash) const {
    if (squash == 0.0f) {
        return;
    }
    const core::Quat thigh = core::Quat::axisAngle(kAxisX, -radians(60.0f * squash));
    const core::Quat knee = core::Quat::axisAngle(kAxisX, radians(150.0f * squash));
    for (const auto& [upper, lower] :
         {std::pair{HumanBone::LeftUpperLeg, HumanBone::LeftLowerLeg},
          std::pair{HumanBone::RightUpperLeg, HumanBone::RightLowerLeg}}) {
        const int u = skeleton_.find(upper);
        const int l = skeleton_.find(lower);
        if (u >= 0) {
            setRotation(u, rotations_[static_cast<std::size_t>(u)] * thigh);
        }
        if (l >= 0) {
            setRotation(l, rotations_[static_cast<std::size_t>(l)] * knee);
        }
    }
}

void ProceduralAnimator::writeOutput(const AnimationInput& input, const Expressions& base,
                                     AnimationOutput& out) const {
    skeleton_.computeSkinMatrices(rotations_, out.skin);
    out.expressions = base;
    // 놀란 정도만큼 깜빡임을 줄임 (완전히 놀라면 깜빡이지 않음)
    const float surprised = base[static_cast<std::size_t>(Expression::Surprised)];
    out.expressions[static_cast<std::size_t>(Expression::Blink)] =
        input.blink ? 1.0f - surprised : 0.0f;
}

void ProceduralAnimator::evaluate(const AnimationInput& input, AnimationOutput& out) const {
    computeRotations(input);
    applySquash(input.squash);
    writeOutput(input, expressionsFor(input.motion), out);
}

void ProceduralAnimator::animate(const AnimationInput& input, float dt, AnimationOutput& out) {
    computeRotations(input);
    Expressions expressions = expressionsFor(input.motion);

    if (!hasPose_) {
        hasPose_ = true;  // 첫 프레임은 넘어올 자세가 없음
        motion_ = input.motion;
    } else if (input.motion != motion_) {
        // 직전에 그린 자세(전환 도중이면 섞인 자세)에서 출발하므로 전환이 끊겨도 튀지 않음
        motion_ = input.motion;
        fromRotations_ = lastRotations_;
        fromExpressions_ = lastExpressions_;
        elapsed_ = 0.0f;
    }

    if (elapsed_ < kTransitionSeconds) {
        elapsed_ = std::min(elapsed_ + dt, kTransitionSeconds);
        const float x = elapsed_ / kTransitionSeconds;
        const float w = x * x * (3.0f - 2.0f * x);  // smoothstep: 시작·끝에서 속도 0
        for (std::size_t i = 0; i < rotations_.size(); ++i) {
            rotations_[i] = core::slerp(fromRotations_[i], rotations_[i], w);
        }
        for (std::size_t i = 0; i < expressions.size(); ++i) {
            expressions[i] = fromExpressions_[i] + (expressions[i] - fromExpressions_[i]) * w;
        }
    }

    lastRotations_ = rotations_;
    lastExpressions_ = expressions;
    applySquash(input.squash);
    writeOutput(input, expressions, out);
}

model::Bounds ProceduralAnimator::displayBounds() const {
    const bool skinned = std::ranges::any_of(
        model_.vertices, [](const model::Vertex& v) { return v.weights[0] > 0.0f; });
    if (!skinned) {
        return model_.bounds;  // 움직일 정점이 없으면 원래 경계 그대로
    }
    constexpr float kMax = std::numeric_limits<float>::max();
    model::Bounds bounds{{kMax, kMax, kMax}, {-kMax, -kMax, -kMax}};
    AnimationOutput out;
    for (const Motion motion : {Motion::Idle, Motion::Dragged, Motion::Airborne}) {
        AnimationInput input;
        input.motion = motion;
        input.idleMotion = false;
        evaluate(input, out);
        includePosedVertices(out.skin, bounds);
    }
    return bounds;
}

void ProceduralAnimator::includePosedVertices(const std::vector<core::Mat4>& skin,
                                              model::Bounds& bounds) const {
    for (const model::Vertex& v : model_.vertices) {
        core::Vec3 p = v.position;
        if (v.weights[0] > 0.0f) {
            p = {};
            for (std::size_t k = 0; k < model::kMaxInfluences; ++k) {
                if (v.weights[k] > 0.0f && v.joints[k] < skin.size()) {
                    p = p + core::transformPoint(v.position, skin[v.joints[k]]) * v.weights[k];
                }
            }
        }
        bounds.min = {std::min(bounds.min.x, p.x), std::min(bounds.min.y, p.y),
                      std::min(bounds.min.z, p.z)};
        bounds.max = {std::max(bounds.max.x, p.x), std::max(bounds.max.y, p.y),
                      std::max(bounds.max.z, p.z)};
    }
}

}  // namespace deskpet::anim
