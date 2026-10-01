#pragma once

// 플랫폼이 애플리케이션에 전달하는 입력 이벤트.
// 모든 포인터 좌표는 화면 좌표(물리 픽셀)입니다. (ADR-0003)
//
// 새 이벤트를 추가할 때:
//   1. 아래에 구조체를 추가하고 Event variant에 넣습니다.
//   2. Application::handleEvent의 std::visit에 처리를 추가합니다.
//      (처리가 빠지면 컴파일 오류가 나므로 누락을 막을 수 있습니다.)

#include "core/Math.h"

#include <cstdint>
#include <variant>

namespace deskpet::core {

enum class MouseButton : std::uint8_t { Left, Right, Middle };

struct PointerDownEvent {
    Vec2 screen;
    MouseButton button = MouseButton::Left;
};

struct PointerUpEvent {
    Vec2 screen;
    MouseButton button = MouseButton::Left;
};

struct PointerMoveEvent {
    Vec2 screen;
};

struct DoubleClickEvent {
    Vec2 screen;
    MouseButton button = MouseButton::Left;
};

// 작업 표시줄 이동, 해상도 변경 등으로 모니터 작업 영역이 바뀌었을 때
struct WorkAreaChangedEvent {};

// 창 닫기 요청 또는 WM_QUIT
struct QuitRequestedEvent {};

// 트레이 아이콘 클릭 → 메뉴 요청 (FR-18). screen: 메뉴를 띄울 화면 좌표
struct TrayMenuRequestedEvent {
    PointI screen;
};

// 창이 있는 모니터의 DPI 배율이 바뀜 (DEBT-01). 1.0 = 96 DPI
struct DpiChangedEvent {
    float scale = 1.0f;
};

using Event =
    std::variant<PointerDownEvent, PointerUpEvent, PointerMoveEvent, DoubleClickEvent,
                 WorkAreaChangedEvent, QuitRequestedEvent, TrayMenuRequestedEvent, DpiChangedEvent>;

}  // namespace deskpet::core
