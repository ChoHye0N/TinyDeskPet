#pragma once

// 캐릭터 상태 머신 + 물리 + 포즈 계산.
// 창이나 렌더러를 전혀 모르는 순수 로직이므로 Linux에서도 테스트됩니다.
// 명세: docs/03-detailed-design/character.md

#include "character/CharacterState.h"
#include "core/Config.h"
#include "core/Math.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>

namespace deskpet::character {

struct Params {
    float gravity = 2400.0f;              // px/s² (화면 좌표이므로 아래 방향이 +)
    float jumpSpeed = 900.0f;             // px/s
    float dragThreshold = 4.0f;           // px
    float maxFallSpeed = 4000.0f;         // px/s
    float breathPeriod = 2.4f;            // s
    float breathAmplitude = 3.0f;         // px
    float blinkInterval = 3.5f;           // s
    float blinkDuration = 0.12f;          // s
    float landingSquashDuration = 0.18f;  // s
    float landingSquashAmount = 0.18f;    // 0 ~ 1

    // 던지기 (FR-16)
    float throwSampleWindow = 0.1f;  // s, 마지막 이동 직전 이 시간 동안의 평균 속도를 씀
    float maxThrowSpeed = 3000.0f;   // px/s
    float airDrag = 0.8f;            // 1/s, 공중 수평 속도 감쇠율 (v *= e^(-k·dt))
    float groundFriction = 1500.0f;  // px/s², 바닥에서 미끄러질 때 감속
    float wallRestitution = 0.5f;    // 벽에 부딪힌 뒤 남는 속도 비율 (FR-17)

    // 걷기
    float walkSpeed = 60.0f;   // px/s, 0이면 걷지 않음
    float idleTimeMin = 6.0f;  // s, 걷기 전 대기 시간 범위
    float idleTimeMax = 15.0f;
    float walkTimeMin = 2.0f;  // s, 한 번 걷는 시간 범위
    float walkTimeMax = 5.0f;
    std::uint32_t randomSeed = 0;  // 난수 시드 (테스트에서 결정적으로 만들기 위해 주입)

    [[nodiscard]] static Params fromConfig(const core::CharacterConfig& config);
};

// 렌더링에 필요한 "모습" 정보. 렌더러 타입과 무관한 순수 데이터입니다.
struct Pose {
    State state = State::Idle;
    float breathOffset = 0.0f;  // px, 위쪽이 음수
    bool eyesClosed = false;
    float squash = 0.0f;  // 0 = 원래 모양, 양수 = 세로로 눌림
    int facing = 0;       // 걷는 방향: -1 왼쪽, +1 오른쪽, 0 정면
    float time = 0.0f;    // 누적 시간 (s). 애니메이션 주기 동작의 위상
    // TODO(M4): 애니메이션 클립, 표정 가중치
    // TODO(M6): 시선 목표 좌표
};

class CharacterController {
public:
    explicit CharacterController(Params params = {});

    void setGround(float groundY) noexcept { ground_ = groundY; }
    [[nodiscard]] float ground() const noexcept { return ground_; }

    // 발 x 좌표의 허용 범위 (가상 데스크톱 좌우 끝, FR-17). 현재 위치도 범위 안으로 맞춥니다.
    void setHorizontalBounds(float minX, float maxX) noexcept;

    // 위치를 강제로 지정합니다. 속도 0, 진행 중인 드래그 취소, 상태 재판정.
    void teleport(core::Vec2 feet);

    [[nodiscard]] core::Vec2 position() const noexcept { return position_; }
    [[nodiscard]] core::Vec2 velocity() const noexcept { return velocity_; }
    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] const Params& params() const noexcept { return params_; }

    // 입력 (화면 좌표)
    void onPointerDown(core::Vec2 screen);
    void onPointerMove(core::Vec2 screen);
    void onPointerUp(core::Vec2 screen);

    // Idle, Walk 상태에서만 성공합니다.
    bool jump();

    // 고정 시간 간격으로 호출합니다 (ADR-0004).
    void update(float dt);

    [[nodiscard]] Pose pose() const;

private:
    struct PointerSample {
        core::Vec2 position;
        float time = 0.0f;
    };

    void transitionTo(State next);
    void settleOnGroundOrFall();
    void recordSample(core::Vec2 screen);
    [[nodiscard]] core::Vec2 throwVelocity() const;
    void applyWalls();  // 벽 충돌: 위치를 범위 안으로, 수평 속도 반사
    void updateIdle(float dt);
    void updateWalk(float dt);
    [[nodiscard]] float randomBetween(float lo, float hi);

    Params params_;
    State state_ = State::Idle;

    core::Vec2 position_;  // 발 위치 (화면 좌표)
    core::Vec2 velocity_;
    float ground_ = 0.0f;

    // 드래그
    bool pointerDown_ = false;
    core::Vec2 pressPoint_;
    core::Vec2 grabOffset_;  // 발 위치 - 커서 위치

    // 던지기용 최근 포인터 샘플 링 버퍼. 60Hz 기준 8개 ≈ 133ms로 throwSampleWindow를 덮음
    std::array<PointerSample, 8> samples_{};
    std::size_t sampleCount_ = 0;
    std::size_t sampleHead_ = 0;  // 다음에 쓸 칸

    // 화면 경계 (발 x)
    float minX_ = -1.0e9f;
    float maxX_ = 1.0e9f;

    // 걷기
    std::mt19937 random_;
    float idleTimer_ = 0.0f;  // 0이 되면 걷기 시작
    float walkTimer_ = 0.0f;  // 0이 되면 Idle로
    int walkDirection_ = 0;

    // 애니메이션 시간
    float time_ = 0.0f;
    float landingTimer_ = 0.0f;
};

}  // namespace deskpet::character
