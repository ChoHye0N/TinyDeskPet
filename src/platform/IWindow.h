#pragma once

// 운영체제 창 추상화. 구현은 platform/win32/Win32Window (ADR-0002)
// 명세: docs/03-detailed-design/platform.md

#include "core/Events.h"
#include "core/Math.h"

#include <string>
#include <vector>

namespace deskpet::platform {

struct WindowDesc {
    std::string title = "DeskPet";  // UTF-8
    core::SizeI size{200, 200};
    core::PointI position{0, 0};  // 화면 좌표, 창 좌상단
    bool alwaysOnTop = true;
};

struct MenuItem {
    int id = 0;         // 0은 "선택 안 함"으로 예약
    std::string label;  // UTF-8
    bool enabled = true;
    bool separator = false;

    [[nodiscard]] static MenuItem makeSeparator() {
        MenuItem item;
        item.separator = true;
        return item;
    }
};

class IWindow {
public:
    IWindow() = default;
    virtual ~IWindow() = default;

    IWindow(const IWindow&) = delete;
    IWindow& operator=(const IWindow&) = delete;
    IWindow(IWindow&&) = delete;
    IWindow& operator=(IWindow&&) = delete;

    [[nodiscard]] virtual bool create(const WindowDesc& desc) = 0;
    virtual void show() = 0;
    virtual void hide() = 0;

    // 쌓인 OS 메시지를 처리해 out 뒤에 이벤트를 추가합니다. 절대 대기하지 않습니다.
    virtual void pollEvents(std::vector<core::Event>& out) = 0;

    // 새 OS 메시지가 오거나 timeoutMs가 지날 때까지 잠듭니다 (Present를 생략한 프레임의 CPU 휴식).
    virtual void waitForEvents(int timeoutMs) = 0;

    virtual void setPosition(core::PointI topLeft) = 0;
    [[nodiscard]] virtual core::PointI position() const = 0;
    virtual void setSize(core::SizeI size) = 0;
    [[nodiscard]] virtual core::SizeI size() const = 0;

    // 창이 있는 모니터의 작업 영역 (작업 표시줄 제외, 화면 좌표)
    [[nodiscard]] virtual core::RectI workArea() const = 0;

    // 모든 모니터를 합친 가상 데스크톱 영역 (화면 좌표, FR-17)
    [[nodiscard]] virtual core::RectI desktopBounds() const = 0;

    // 창이 있는 모니터의 DPI 배율 (1.0 = 96 DPI)
    [[nodiscard]] virtual float dpiScale() const = 0;

    // 알림 영역(트레이)에 아이콘을 추가합니다. 클릭하면 TrayMenuRequestedEvent (FR-18).
    // 아이콘은 창이 파괴될 때 함께 제거됩니다.
    virtual bool showTrayIcon(const std::string& tooltip) = 0;

    // 창 내부 좌표의 타원 바깥은 클릭이 아래 창으로 통과합니다 (FR-04)
    virtual void setHitRegionEllipse(const core::RectI& local) = 0;

    // 선택한 항목의 id를 반환합니다. 취소하면 0.
    [[nodiscard]] virtual int showContextMenu(const std::vector<MenuItem>& items,
                                              core::PointI screen) = 0;

    // 렌더러에 넘길 OS 창 핸들 (Win32에서는 HWND)
    [[nodiscard]] virtual void* nativeHandle() const = 0;
};

}  // namespace deskpet::platform
