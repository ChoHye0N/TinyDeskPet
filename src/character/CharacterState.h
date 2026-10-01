#pragma once

#include <cstdint>
#include <string_view>

namespace deskpet::character {

// 상태 전이 규칙: docs/03-detailed-design/character.md §4.1
// TODO(M6): Sit, Sleep 상태 추가 — 상태가 더 늘면 상태 패턴(State 클래스)으로 리팩터링 검토
enum class State : std::uint8_t {
    Idle,      // 바닥에 서 있음 (던져진 뒤 미끄러지는 중일 수 있음)
    Dragged,   // 사용자가 끌고 있음
    Airborne,  // 공중 (낙하·점프·던지기)
    Walk,      // 바닥에서 좌우로 천천히 걸음
};

[[nodiscard]] std::string_view toString(State state);

}  // namespace deskpet::character
