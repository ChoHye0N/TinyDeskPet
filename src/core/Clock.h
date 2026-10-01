#pragma once

#include <functional>

namespace deskpet::core {

// 초 단위의 단조 증가 시간을 돌려주는 함수.
// 실제 시계 대신 함수를 주입받으면 테스트에서 "가짜 시간"을 넣을 수 있습니다.
using TimeSource = std::function<double()>;

// std::chrono::steady_clock 기반 시간 소스. 호출한 시점을 0초로 합니다.
[[nodiscard]] TimeSource makeSteadyTimeSource();

}  // namespace deskpet::core
