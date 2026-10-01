#pragma once

// 고정 시간 간격 누적기 (ADR-0004)
//
//   double frame = now - last;
//   int steps = timestep.advance(frame);
//   for (int i = 0; i < steps; ++i) update(timestep.step());
//
// 프레임 시간이 들쭉날쭉해도 update는 항상 같은 dt로 호출되므로
// 물리 결과가 실행 환경과 무관하게 같아집니다.

namespace deskpet::core {

class FixedTimestep {
public:
    explicit FixedTimestep(double stepSeconds = 1.0 / 60.0, int maxStepsPerFrame = 5);

    // 이번 프레임 경과 시간을 누적하고, 실행할 고정 업데이트 횟수를 반환합니다.
    // 음수·NaN·무한대는 0으로 취급합니다.
    // 횟수가 maxStepsPerFrame을 넘으면 잘라내고 남은 누적 시간은 버립니다.
    [[nodiscard]] int advance(double frameSeconds);

    [[nodiscard]] double step() const noexcept { return step_; }

    // 다음 스텝까지 진행된 비율 [0, 1). 렌더링 보간에 사용할 수 있습니다.
    [[nodiscard]] double alpha() const noexcept { return accumulator_ / step_; }

    void reset() noexcept { accumulator_ = 0.0; }

private:
    double step_;
    int maxStepsPerFrame_;
    double accumulator_ = 0.0;
};

}  // namespace deskpet::core
