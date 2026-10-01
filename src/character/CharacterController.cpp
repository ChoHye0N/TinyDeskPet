#include "character/CharacterController.h"

#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace deskpet::character {

Params Params::fromConfig(const core::CharacterConfig& config) {
    Params params;
    params.gravity = config.gravity;
    params.jumpSpeed = config.jumpSpeed;
    params.dragThreshold = config.dragThreshold;
    params.maxFallSpeed = config.maxFallSpeed;
    return params;
}

CharacterController::CharacterController(Params params)
    : params_(params), random_(params.randomSeed) {
    idleTimer_ = randomBetween(params_.idleTimeMin, params_.idleTimeMax);
}

void CharacterController::setHorizontalBounds(float minX, float maxX) noexcept {
    minX_ = std::min(minX, maxX);
    maxX_ = std::max(minX, maxX);
    position_.x = std::clamp(position_.x, minX_, maxX_);
}

void CharacterController::teleport(core::Vec2 feet) {
    pointerDown_ = false;
    position_ = {std::clamp(feet.x, minX_, maxX_), feet.y};
    velocity_ = {};
    idleTimer_ = randomBetween(params_.idleTimeMin, params_.idleTimeMax);
    settleOnGroundOrFall();
}

void CharacterController::onPointerDown(core::Vec2 screen) {
    pointerDown_ = true;
    pressPoint_ = screen;
    grabOffset_ = position_ - screen;
    sampleCount_ = 0;
    recordSample(screen);

    if (state_ == State::Airborne) {
        // 떨어지는 캐릭터를 공중에서 붙잡음
        velocity_ = {};
        transitionTo(State::Dragged);
    }
}

void CharacterController::onPointerMove(core::Vec2 screen) {
    if (!pointerDown_) {
        return;
    }

    if (state_ != State::Dragged) {
        // 클릭과 드래그를 구분: 임계 거리 이상 움직여야 드래그로 판정 (FR-11)
        if ((screen - pressPoint_).length() < params_.dragThreshold) {
            return;
        }
        velocity_ = {};
        transitionTo(State::Dragged);
    }

    position_ = screen + grabOffset_;
    recordSample(screen);
}

void CharacterController::onPointerUp(core::Vec2 screen) {
    if (!pointerDown_) {
        return;  // 누르지 않은 상태의 UP (더블클릭 직후 등)은 무시
    }
    pointerDown_ = false;

    if (state_ != State::Dragged) {
        return;  // 단순 클릭
    }

    position_ = screen + grabOffset_;
    recordSample(screen);
    velocity_ = throwVelocity();
    applyWalls();
    if (position_.y < ground_) {
        transitionTo(State::Airborne);
    } else {
        // 바닥 높이에서 놓으면 수평 속도만 남아 미끄러짐
        position_.y = ground_;
        velocity_.y = 0.0f;
        transitionTo(State::Idle);
    }
}

void CharacterController::recordSample(core::Vec2 screen) {
    // 같은 고정 스텝 안의 이동은 마지막 샘플을 덮어씀 (시간 차가 0이면 속도를 구할 수 없으므로)
    if (sampleCount_ > 0) {
        PointerSample& last = samples_[(sampleHead_ + samples_.size() - 1) % samples_.size()];
        if (last.time == time_) {
            last.position = screen;
            return;
        }
        if (last.position == screen) {
            return;  // 움직이지 않음 → 마지막 "이동" 시각을 유지해야 멈춤을 감지할 수 있음
        }
    }
    samples_[sampleHead_] = {screen, time_};
    sampleHead_ = (sampleHead_ + 1) % samples_.size();
    sampleCount_ = std::min(sampleCount_ + 1, samples_.size());
}

core::Vec2 CharacterController::throwVelocity() const {
    if (sampleCount_ < 2) {
        return {};
    }
    const auto at = [this](std::size_t back) -> const PointerSample& {
        return samples_[(sampleHead_ + samples_.size() - 1 - back) % samples_.size()];
    };
    const PointerSample& newest = at(0);

    // 놓기 전 한동안 멈춰 있었다면(마지막 이동이 오래됨) 던지지 않음
    if (time_ - newest.time > params_.throwSampleWindow) {
        return {};
    }

    // 마지막 이동 기준 throwSampleWindow 안에서 가장 오래된 샘플과의 평균 속도
    const PointerSample* oldest = &newest;
    for (std::size_t back = 1; back < sampleCount_; ++back) {
        if (newest.time - at(back).time > params_.throwSampleWindow + 1e-4f) {
            break;
        }
        oldest = &at(back);
    }
    const float elapsed = newest.time - oldest->time;
    if (!(elapsed > 0.0f)) {
        return {};
    }

    core::Vec2 velocity = (newest.position - oldest->position) * (1.0f / elapsed);
    const float speed = velocity.length();
    if (speed > params_.maxThrowSpeed) {
        velocity = velocity * (params_.maxThrowSpeed / speed);
    }
    return velocity;
}

void CharacterController::applyWalls() {
    if (position_.x < minX_) {
        position_.x = minX_;
        velocity_.x = std::abs(velocity_.x) * params_.wallRestitution;
    } else if (position_.x > maxX_) {
        position_.x = maxX_;
        velocity_.x = -std::abs(velocity_.x) * params_.wallRestitution;
    }
}

float CharacterController::randomBetween(float lo, float hi) {
    if (!(hi > lo)) {
        return lo;
    }
    return std::uniform_real_distribution<float>(lo, hi)(random_);
}

bool CharacterController::jump() {
    if (state_ != State::Idle && state_ != State::Walk) {
        return false;
    }
    velocity_ = {0.0f, -params_.jumpSpeed};
    transitionTo(State::Airborne);
    return true;
}

void CharacterController::update(float dt) {
    if (!(dt > 0.0f)) {
        return;  // 0, 음수, NaN 무시
    }

    time_ += dt;
    landingTimer_ = std::max(0.0f, landingTimer_ - dt);

    switch (state_) {
        case State::Idle: updateIdle(dt); break;
        case State::Walk: updateWalk(dt); break;

        case State::Dragged: break;  // 위치는 포인터 이벤트가 결정

        case State::Airborne:
            // 반암시적 오일러: 속도 먼저, 새 속도로 위치 갱신
            velocity_.y = std::min(velocity_.y + params_.gravity * dt, params_.maxFallSpeed);
            velocity_.x *= std::exp(-params_.airDrag * dt);  // 공기 저항 (지수 감쇠는 dt에 안정적)
            position_ += velocity_ * dt;
            applyWalls();

            if (position_.y >= ground_) {
                position_.y = ground_;
                velocity_.y = 0.0f;  // 수평 속도는 남겨 바닥에서 미끄러지게 함
                landingTimer_ = params_.landingSquashDuration;
                transitionTo(State::Idle);
            }
            break;
    }
}

void CharacterController::updateIdle(float dt) {
    if (position_.y < ground_) {
        transitionTo(State::Airborne);  // 바닥이 내려감 (작업 표시줄 숨김 등)
        return;
    }
    position_.y = ground_;  // 바닥이 올라감

    if (velocity_.x != 0.0f) {
        // 던져진 뒤 미끄러짐: 일정한 감속, 부호가 바뀌기 전에 멈춤
        const float slowed = std::max(0.0f, std::abs(velocity_.x) - params_.groundFriction * dt);
        velocity_.x = std::copysign(slowed, velocity_.x);
        position_.x += velocity_.x * dt;
        applyWalls();
        return;
    }

    if (params_.walkSpeed <= 0.0f || pointerDown_) {
        return;
    }
    idleTimer_ -= dt;
    if (idleTimer_ <= 0.0f) {
        walkDirection_ = std::bernoulli_distribution(0.5)(random_) ? 1 : -1;
        walkTimer_ = randomBetween(params_.walkTimeMin, params_.walkTimeMax);
        transitionTo(State::Walk);
    }
}

void CharacterController::updateWalk(float dt) {
    if (position_.y < ground_) {
        transitionTo(State::Airborne);
        return;
    }
    position_.y = ground_;
    position_.x += static_cast<float>(walkDirection_) * params_.walkSpeed * dt;
    if (position_.x <= minX_) {
        position_.x = minX_;
        walkDirection_ = 1;  // 벽에서 뒤돌기
    } else if (position_.x >= maxX_) {
        position_.x = maxX_;
        walkDirection_ = -1;
    }

    walkTimer_ -= dt;
    if (walkTimer_ <= 0.0f) {
        transitionTo(State::Idle);
    }
}

Pose CharacterController::pose() const {
    Pose pose;
    pose.state = state_;
    pose.time = time_;

    if (state_ == State::Idle && params_.breathPeriod > 0.0f) {
        const float phase = 2.0f * std::numbers::pi_v<float> * time_ / params_.breathPeriod;
        pose.breathOffset = -params_.breathAmplitude * std::sin(phase);
    }

    if (state_ != State::Airborne && params_.blinkInterval > 0.0f) {
        const float cycle = std::fmod(time_, params_.blinkInterval);
        pose.eyesClosed = cycle >= params_.blinkInterval - params_.blinkDuration;
    }

    if (state_ == State::Walk) {
        pose.facing = walkDirection_;
    }

    if (landingTimer_ > 0.0f && params_.landingSquashDuration > 0.0f) {
        pose.squash = params_.landingSquashAmount * (landingTimer_ / params_.landingSquashDuration);
    }

    return pose;
}

void CharacterController::transitionTo(State next) {
    if (state_ == next) {
        return;
    }
    core::logging::debug("캐릭터 상태: {} → {}", toString(state_), toString(next));
    state_ = next;
    if (next == State::Idle) {
        idleTimer_ = randomBetween(params_.idleTimeMin, params_.idleTimeMax);
    }
}

void CharacterController::settleOnGroundOrFall() {
    if (position_.y < ground_) {
        transitionTo(State::Airborne);
    } else {
        position_.y = ground_;
        velocity_ = {};
        transitionTo(State::Idle);
    }
}

}  // namespace deskpet::character
