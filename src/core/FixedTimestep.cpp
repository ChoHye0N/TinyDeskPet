#include "core/FixedTimestep.h"

#include <cassert>
#include <cmath>

namespace deskpet::core {

FixedTimestep::FixedTimestep(double stepSeconds, int maxStepsPerFrame)
    : step_(stepSeconds), maxStepsPerFrame_(maxStepsPerFrame) {
    assert(stepSeconds > 0.0);
    assert(maxStepsPerFrame > 0);
}

int FixedTimestep::advance(double frameSeconds) {
    if (!std::isfinite(frameSeconds) || frameSeconds < 0.0) {
        frameSeconds = 0.0;
    }

    accumulator_ += frameSeconds;

    // 부동소수점 오차로 1스텝이 0.9999...로 계산되는 것을 막기 위해 아주 작은 여유를 둡니다.
    constexpr double kEpsilon = 1e-9;
    const double steps = std::floor((accumulator_ + kEpsilon) / step_);

    if (steps > static_cast<double>(maxStepsPerFrame_)) {
        // 죽음의 나선(spiral of death) 방지: 밀린 시간을 따라잡으려 하지 않고 버립니다.
        accumulator_ = 0.0;
        return maxStepsPerFrame_;
    }

    const int count = static_cast<int>(steps);
    accumulator_ -= static_cast<double>(count) * step_;
    if (accumulator_ < 0.0) {
        accumulator_ = 0.0;  // kEpsilon으로 인한 미세한 음수 보정
    }
    return count;
}

}  // namespace deskpet::core
