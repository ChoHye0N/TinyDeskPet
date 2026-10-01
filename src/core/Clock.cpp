#include "core/Clock.h"

#include <chrono>

namespace deskpet::core {

TimeSource makeSteadyTimeSource() {
    const auto start = std::chrono::steady_clock::now();
    return [start] {
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
        return elapsed.count();
    };
}

}  // namespace deskpet::core
