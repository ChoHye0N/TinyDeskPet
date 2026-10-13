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

// 착지 동작: 가장 깊이 웅크렸을 때의 각도(도)와 시간(초)
constexpr float kFullSquash = 0.18f;   // 캐릭터의 착지 squash 최댓값 (= 웅크림 1)
constexpr float kCrouchKnee = 50.0f;   // 무릎 굽힘 (허벅지는 그 절반만큼 앞으로)
constexpr float kCrouchLean = 15.0f;   // 상체 숙임
constexpr float kCrouchArms = 12.0f;   // 팔 벌림
constexpr float kLandingDown = 0.07f;  // 내려가는 시간
constexpr float kLandingUp = 0.38f;    // 일어나는 시간

// 바라보기: 목·머리가 나눠 돌고(합 = 목표 각도), 눈은 그 위에 조금 더 (머리보다 먼저 가는 느낌).
// 목표로 다가가는 빠르기는 초당 비율 (지수 감쇠, 약 0.3초에 95%)
constexpr float kLookNeckShare = 0.4f;
constexpr float kLookEyeShare = 0.3f;
constexpr float kLookEyeMaxDegrees = 12.0f;
constexpr float kLookRate = 10.0f;
constexpr float kLookSettleRadians = 1e-4f;  // 이보다 가까우면 목표에 맞춤 (Present 생략 유지)

// 착지 후 시간 → 웅크림 정도 (0 ~ 1). 빠르게 내려갔다가 천천히 일어남, 양 끝 속도 0
float landingCurve(float t) {
    const auto smooth = [](float x) {
        x = std::clamp(x, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    };
    return t < kLandingDown ? smooth(t / kLandingDown)
                            : 1.0f - smooth((t - kLandingDown) / kLandingUp);
}

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
    return p;  // 착지 웅크림은 전환 보간 뒤에 더함 (applyCrouch)
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
      springs_(model),
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

void ProceduralAnimator::setClip(Motion motion, std::shared_ptr<const model::MotionClip> clip) {
    auto& slot = clips_[static_cast<std::size_t>(motion)];
    slot.reset();
    if (clip && clip->frameCount > 0) {
        slot.emplace(skeleton_, std::move(clip), model_.morphs);
    }
    // 클립이 없는 동작에서 숨길 본 = 어느 클립이든 숨기는 본 (소품은 코드로 만든 자세를 모르므로
    // 그대로 두면 기본 자세 위치에 떠 있음)
    proceduralHidden_.assign(skeleton_.size(), 0);
    for (const auto& player : clips_) {
        if (!player) {
            continue;
        }
        const std::span<const std::uint8_t> hidden = player->hidden();
        for (std::size_t j = 0; j < hidden.size() && j < proceduralHidden_.size(); ++j) {
            proceduralHidden_[j] = proceduralHidden_[j] != 0 || hidden[j] != 0 ? 1 : 0;
        }
    }
}

void ProceduralAnimator::computeRotations(const AnimationInput& input) const {
    rotations_.assign(skeleton_.size(), core::Quat{});
    clipOffset_ = {};
    offsets_.clear();
    morphWeights_.clear();
    if (const auto& clip = clips_[static_cast<std::size_t>(input.motion)]) {
        // 대기 동작을 끄면 첫 프레임에 멈춤 (화면이 안 바뀌어 Present 생략, DEBT-02)
        const bool still = input.motion == Motion::Idle && !input.idleMotion;
        float time = input.motionTime >= 0.0f ? input.motionTime : input.time;
        if (still) {
            time = 0.0f;
        }
        clip->sample(time, rotations_);
        clip->sampleOffsets(time, offsets_);
        clip->sampleWeights(time, morphWeights_);
        clipOffset_ = clip->rootOffset(time);
        return;
    }
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
    drapeNamedBones();
}

// 휴머노이드가 아닌 이름 본(머플러·꼬리 등)은 코드로 만든 자세가 움직이지 않으므로, 클립(대기
// 우선)의 첫 프레임 모양을 부모 기준으로 씀 (ADR-0015). 게임에서 추출한 모델은 기본 자세에서 이런
// 본이 수평으로 뻗어 있어, 그대로 두면 뻣뻣하게 튀어나오고 화면 맞춤 영역도 넓어짐
void ProceduralAnimator::drapeNamedBones() const {
    const MotionPlayer* source = nullptr;
    for (const std::optional<MotionPlayer>& clip : clips_) {
        if (clip.has_value()) {
            source = &*clip;
            break;
        }
    }
    if (source == nullptr) {
        return;
    }
    const MotionPlayer& player = *source;
    player.sample(0.0f, drape_);
    player.sampleOffsets(0.0f, offsets_);  // 눈꺼풀 등 이동, 입 모양도 첫 프레임 그대로
    player.sampleWeights(0.0f, morphWeights_);
    const std::span<const std::uint8_t> named = player.named();
    for (std::size_t j = 0; j < named.size() && j < rotations_.size(); ++j) {
        if (named[j] != 0) {
            rotations_[j] = drape_[j];  // 부모에 대한 델타라 몸 자세가 달라도 붙어 있음
        }
    }
}

// 착지 웅크림 (amount 0 ~ 1). 슬라임처럼 몸 전체를 납작하게 늘이면 사람 모델은 비율이 깨지므로
// 관절로 표현: 무릎을 굽히고 허벅지를 그 절반만큼 앞으로 → 정강이가 반대로 같은 각도만큼 기울어
// 발이 엉덩이 바로 아래에 옴 (허벅지·정강이 길이가 비슷할 때). 몸이 내려가는 높이는 rootOffset이
// 발을 바닥에 맞추며 정함. 상체는 숙이고, 고개는 반쯤 되돌려 시선을 유지, 팔은 균형 잡듯 벌림
void ProceduralAnimator::applyCrouch(float amount) const {
    if (amount <= 0.0f) {
        return;
    }
    const float knee = kCrouchKnee * amount;
    const auto addDelta = [this](int bone, const core::Quat& delta) {
        if (bone >= 0) {
            // 모델 축 기준 델타를 기존 자세 위에 덧붙임 (같은 X축 회전끼리는 각도가 더해짐)
            setRotation(bone, delta * rotations_[static_cast<std::size_t>(bone)]);
        }
    };
    for (const auto& [upper, lower] :
         {std::pair{HumanBone::LeftUpperLeg, HumanBone::LeftLowerLeg},
          std::pair{HumanBone::RightUpperLeg, HumanBone::RightLowerLeg}}) {
        addDelta(skeleton_.find(upper), core::Quat::axisAngle(kAxisX, -radians(knee * 0.5f)));
        addDelta(skeleton_.find(lower), core::Quat::axisAngle(kAxisX, radians(knee)));
    }
    int chest = skeleton_.find(HumanBone::Chest);
    if (chest < 0) {
        chest = skeleton_.find(HumanBone::Spine);
    }
    addDelta(chest, core::Quat::axisAngle(kAxisX, radians(kCrouchLean * amount)));
    addDelta(skeleton_.find(HumanBone::Head),
             core::Quat::axisAngle(kAxisX, -radians(kCrouchLean * 0.5f * amount)));
    for (const Limb* arm : {&leftArm_, &rightArm_}) {
        if (arm->upper >= 0) {
            addDelta(arm->upper,
                     core::Quat::axisAngle(kAxisZ, arm->side * radians(kCrouchArms * amount)));
        }
    }
}

// 머리 위치(기본 자세)에서 목표를 향하는 각도. 제한 각도 안으로 자름
ProceduralAnimator::LookAngles ProceduralAnimator::lookAnglesFor(
    const AnimationInput& input) const {
    const int head = skeleton_.find(HumanBone::Head);
    if (!input.lookTarget || head < 0) {
        return {};
    }
    const core::Vec3 d = *input.lookTarget - skeleton_.bindPosition(head);
    const float horizontal = std::hypot(d.x, d.z);
    if (horizontal < 1e-6f && std::abs(d.y) < 1e-6f) {
        return {};
    }
    const float yawLimit = radians(kLookMaxYawDegrees);
    return {std::clamp(std::atan2(d.x, d.z), -yawLimit, yawLimit),
            std::clamp(std::atan2(d.y, horizontal), -radians(kLookMaxDownDegrees),
                       radians(kLookMaxUpDegrees))};
}

// 목·머리에 모델 축 기준으로 나눠 덧붙임 (목이 없으면 머리가 전부). 숙임(X) 먼저, 돌림(Y) 나중:
// 고개를 숙인 채 옆을 보는 자연스러운 순서. 눈 본이 있으면 같은 방향으로 조금 더
void ProceduralAnimator::applyLook(LookAngles look) const {
    if (look.yaw == 0.0f && look.pitch == 0.0f) {
        return;
    }
    const auto turn = [&](int bone, float share) {
        if (bone >= 0) {
            const core::Quat delta = core::Quat::axisAngle(kAxisY, look.yaw * share) *
                                     core::Quat::axisAngle(kAxisX, -look.pitch * share);
            setRotation(bone, delta * rotations_[static_cast<std::size_t>(bone)]);
        }
    };
    const int neck = skeleton_.find(HumanBone::Neck);
    turn(neck, neck >= 0 ? kLookNeckShare : 0.0f);
    turn(skeleton_.find(HumanBone::Head), neck >= 0 ? 1.0f - kLookNeckShare : 1.0f);
    const float eyeLimit = radians(kLookEyeMaxDegrees);
    const LookAngles eyes{std::clamp(look.yaw * kLookEyeShare, -eyeLimit, eyeLimit),
                          std::clamp(look.pitch * kLookEyeShare, -eyeLimit, eyeLimit)};
    for (const HumanBone eye : {HumanBone::LeftEye, HumanBone::RightEye}) {
        const int bone = skeleton_.find(eye);
        if (bone >= 0) {
            const core::Quat delta = core::Quat::axisAngle(kAxisY, eyes.yaw) *
                                     core::Quat::axisAngle(kAxisX, -eyes.pitch);
            setRotation(bone, delta * rotations_[static_cast<std::size_t>(bone)]);
        }
    }
}

// 클립의 엉덩이 이동으로 몸을 옮긴 뒤, 서 있거나 걷는 동안 낮은 쪽 발이 바닥(바인드 포즈 발
// 높이)보다 떠 있으면 닿도록 더 내림. 엉덩이를 고정한 채 다리를 굽히거나 흔들면 발이 바닥에서 뜨기
// 때문 (걸을 때 몸이 오르내림). 엉덩이 이동을 넣은 자세로 재므로, 클립이 이미 몸을 낮췄으면 두 번
// 내리지 않음
core::Vec3 ProceduralAnimator::rootOffset(Motion motion) const {
    if (motion != Motion::Idle && motion != Motion::Walk) {
        return clipOffset_;
    }
    const std::array<int, 2> feet = {skeleton_.find(HumanBone::LeftFoot),
                                     skeleton_.find(HumanBone::RightFoot)};
    skeleton_.computePose(rotations_, poseRotations_, posePositions_, clipOffset_, offsets_);
    float bindLowest = std::numeric_limits<float>::max();
    float posedLowest = std::numeric_limits<float>::max();
    for (const int foot : feet) {
        if (foot >= 0) {
            bindLowest = std::min(bindLowest, skeleton_.bindPosition(foot).y);
            posedLowest = std::min(posedLowest, posePositions_[static_cast<std::size_t>(foot)].y);
        }
    }
    if (bindLowest == std::numeric_limits<float>::max()) {
        return clipOffset_;  // 발 본이 없는 모델
    }
    return clipOffset_ +
           core::Vec3{0.0f, std::min(bindLowest - posedLowest, 0.0f), 0.0f};  // 내리기만
}

void ProceduralAnimator::writeOutput(const AnimationInput& input, const Expressions& base,
                                     AnimationOutput& out, core::Vec3 offset) const {
    // 재생 중인 클립이 숨기는 본 (게임의 소품 숨기기, ADR-0015). 전환 중에도 새 동작 기준
    const auto& clip = clips_[static_cast<std::size_t>(input.motion)];
    skeleton_.computeSkinMatrices(rotations_, out.skin, offset,
                                  clip ? clip->hidden() : std::span(proceduralHidden_), offsets_);
    out.morphWeights = morphWeights_;
    out.expressions = base;
    // 놀란 정도만큼 깜빡임을 줄임 (완전히 놀라면 깜빡이지 않음)
    const float surprised = base[static_cast<std::size_t>(Expression::Surprised)];
    out.expressions[static_cast<std::size_t>(Expression::Blink)] =
        input.blink ? 1.0f - surprised : 0.0f;
}

void ProceduralAnimator::evaluate(const AnimationInput& input, AnimationOutput& out) const {
    computeRotations(input);
    // 상태가 없으므로 squash 값을 그대로 웅크림 정도로 씀
    applyCrouch(std::clamp(input.squash / kFullSquash, 0.0f, 1.0f));
    applyLook(lookAnglesFor(input));
    writeOutput(input, expressionsFor(input.motion), out, rootOffset(input.motion));
}

void ProceduralAnimator::animate(const AnimationInput& input, float dt, AnimationOutput& out) {
    if (!hasPose_) {
        hasPose_ = true;  // 첫 프레임은 넘어올 자세가 없음
        motion_ = input.motion;
        motionStart_ = input.time;
    } else if (input.motion != motion_) {
        // 직전에 그린 자세(전환 도중이면 섞인 자세)에서 출발하므로 전환이 끊겨도 튀지 않음
        motion_ = input.motion;
        fromRotations_ = lastRotations_;
        fromExpressions_ = lastExpressions_;
        fromClipOffset_ = lastClipOffset_;
        elapsed_ = 0.0f;
        motionStart_ = input.time;
    }

    // 클립은 동작이 시작될 때 처음부터 (집어 드는 동작 등이 중간부터 나오지 않게)
    AnimationInput timed = input;
    timed.motionTime = std::max(input.time - motionStart_, 0.0f);
    computeRotations(timed);
    Expressions expressions = expressionsFor(input.motion);

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
        clipOffset_ = fromClipOffset_ + (clipOffset_ - fromClipOffset_) * w;
    }

    lastRotations_ = rotations_;
    lastExpressions_ = expressions;
    lastClipOffset_ = clipOffset_;

    // 착지: squash가 커지는 순간 시작. 캐릭터의 squash(0.18초 직선 감소)는 사람 동작엔 짧고
    // 갑작스러워서, 시작 신호로만 쓰고 곡선은 따로 만듦 (빠르게 내려갔다 천천히 일어남)
    if (input.squash > lastSquash_ + 1e-4f) {
        landingTime_ = 0.0f;
        landingStrength_ = std::clamp(input.squash / kFullSquash, 0.0f, 1.0f);
    }
    lastSquash_ = input.squash;
    float crouch = 0.0f;
    if (landingTime_ >= 0.0f) {
        landingTime_ += dt;
        crouch = landingStrength_ * landingCurve(landingTime_);
        if (landingTime_ >= kLandingDown + kLandingUp) {
            landingTime_ = -1.0f;
        }
    }
    applyCrouch(crouch);

    // 바라보기: 전환 보간과 별개로 목표 각도로 서서히 (커서가 움직일 때마다 튀지 않게)
    const LookAngles target = lookAnglesFor(input);
    const float follow = 1.0f - std::exp(-kLookRate * dt);
    const auto approach = [&](float current, float goal) {
        const float next = current + (goal - current) * follow;
        return std::abs(goal - next) < kLookSettleRadians ? goal : next;
    };
    look_ = {approach(look_.yaw, target.yaw), approach(look_.pitch, target.pitch)};
    applyLook(look_);

    const core::Vec3 offset = rootOffset(input.motion);
    // 흔들림은 몸 자세가 정해진 뒤 그 위에서 계산 (보간 대상이 아님: 스스로 연속적임)
    springs_.update(skeleton_, rotations_, input.movement, dt, offset);
    writeOutput(input, expressions, out, offset);
}

namespace {

// a·cos θ + b·sin θ (θ ∈ [−T, T], 0 ≤ T ≤ π/2)의 최소·최대.
// = r·cos(θ − φ) (r = |(a, b)|, φ = atan2(b, a)) 이므로 양 끝값과, 구간 안에 있으면 ±r.
// 양 끝 각도만 보면 비스듬히 뒤로 뻗은 점의 최대(θ = φ)를 놓침
std::pair<float, float> rotatedRange(float a, float b, float turn) {
    const float c = std::cos(turn);
    const float s = std::sin(turn);
    float lo = std::min(a * c - b * s, a * c + b * s);
    float hi = std::max(a * c - b * s, a * c + b * s);
    if (turn > 0.0f) {
        const float r = std::hypot(a, b);
        const float phi = std::atan2(b, a);
        if (std::abs(phi) <= turn) {
            hi = r;
        }
        if (kPi - std::abs(phi) <= turn) {  // θ = φ ± π가 구간 안
            lo = -r;
        }
    }
    return {lo, hi};
}

// 몸을 Y축으로 θ ∈ [−turn, turn] 돌린 p를 모두 포함 (Mat4::rotationY, 행 벡터):
// x' = x·cos θ + z·sin θ, z' = z·cos θ − x·sin θ, y는 그대로
void includeTurnedPoint(core::Vec3 p, float turn, model::Bounds& bounds) {
    const auto [minX, maxX] = rotatedRange(p.x, p.z, turn);
    const auto [minZ, maxZ] = rotatedRange(p.z, -p.x, turn);
    bounds.min = {std::min(bounds.min.x, minX), std::min(bounds.min.y, p.y),
                  std::min(bounds.min.z, minZ)};
    bounds.max = {std::max(bounds.max.x, maxX), std::max(bounds.max.y, p.y),
                  std::max(bounds.max.z, maxZ)};
}

}  // namespace

model::Bounds ProceduralAnimator::displayBounds(float maxTurnRadians) const {
    constexpr float kMax = std::numeric_limits<float>::max();
    model::Bounds bounds{{kMax, kMax, kMax}, {-kMax, -kMax, -kMax}};
    const bool skinned = std::ranges::any_of(
        model_.vertices, [](const model::Vertex& v) { return v.weights[0] > 0.0f; });
    if (!skinned) {
        if (maxTurnRadians == 0.0f) {
            return model_.bounds;  // 움직일 정점도, 돌릴 각도도 없으면 원래 경계 그대로
        }
        // 원래 경계 상자의 꼭짓점을 돌려서 포함 (정점보다 model.bounds가 기준)
        const model::Bounds& b = model_.bounds;
        for (unsigned i = 0; i < 8; ++i) {
            includeTurnedPoint(
                {(i & 1U) != 0 ? b.max.x : b.min.x, (i & 2U) != 0 ? b.max.y : b.min.y,
                 (i & 4U) != 0 ? b.max.z : b.min.z},
                maxTurnRadians, bounds);
        }
        return bounds;
    }

    AnimationOutput out;
    for (const AnimationInput& input : boundsSamples()) {
        evaluate(input, out);
        includePosedVertices(out.skin, maxTurnRadians, bounds);
    }
    return bounds;
}

// 경계 상자를 잴 자세들
std::vector<AnimationInput> ProceduralAnimator::boundsSamples() const {
    // 모션 파일은 길이 전체에서 고르게 최대 kClipSamples개 (30fps 프레임을 모두 보면 느림)
    constexpr std::size_t kClipSamples = 24;
    // 걷기는 한 주기를 8등분해 샘플링: 다리를 가장 멀리 뻗는 순간(1/4, 3/4)과
    // 무릎을 가장 굽히는 순간(0, 1/2)이 모두 포함됨
    constexpr int kWalkSamples = 8;
    std::vector<AnimationInput> inputs;
    for (const Motion motion : {Motion::Idle, Motion::Walk, Motion::Dragged, Motion::Airborne}) {
        const auto& clip = clips_[static_cast<std::size_t>(motion)];
        if (clip) {
            const std::size_t samples = std::min(clip->frameCount(), kClipSamples);
            for (std::size_t i = 0; i < samples; ++i) {
                AnimationInput& input = inputs.emplace_back();
                input.motion = motion;
                input.time = samples > 1 ? clip->duration() * static_cast<float>(i) /
                                               static_cast<float>(samples - 1)
                                         : 0.0f;
            }
        } else if (motion == Motion::Walk) {
            for (int i = 0; i < kWalkSamples; ++i) {
                AnimationInput& input = inputs.emplace_back();
                input.motion = Motion::Walk;
                input.time = kWalkCycle * static_cast<float>(i) / kWalkSamples;
            }
        } else {
            AnimationInput& input = inputs.emplace_back();
            input.motion = motion;
            input.idleMotion = false;
        }
    }
    return inputs;
}

void ProceduralAnimator::includePosedVertices(const std::vector<core::Mat4>& skin,
                                              float maxTurnRadians, model::Bounds& bounds) const {
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
        includeTurnedPoint(p, maxTurnRadians, bounds);
    }
}

}  // namespace deskpet::anim
