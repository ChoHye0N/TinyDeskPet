#pragma once

// IWindow의 Win32 구현.
// 창 스타일과 메시지 변환 규칙: docs/03-detailed-design/platform.md §4

#include "platform/IWindow.h"

#include <windows.h>

#include <string>

namespace deskpet::platform::win32 {

class Win32Window final : public IWindow {
public:
    explicit Win32Window(HINSTANCE instance);
    ~Win32Window() override;

    [[nodiscard]] bool create(const WindowDesc& desc) override;
    void show() override;
    void hide() override;
    void pollEvents(std::vector<core::Event>& out) override;
    void waitForEvents(int timeoutMs) override;

    void setPosition(core::PointI topLeft) override;
    [[nodiscard]] core::PointI position() const override { return position_; }
    void setSize(core::SizeI size) override;
    [[nodiscard]] core::SizeI size() const override { return size_; }
    [[nodiscard]] core::RectI workAreaAt(core::PointI screen) const override;
    [[nodiscard]] core::RectI desktopBounds() const override;
    [[nodiscard]] float dpiScale() const override;
    bool showTrayIcon(const std::string& tooltip) override;

    void setClickThrough(bool enabled) override;
    [[nodiscard]] core::PointI cursorPosition() const override;
    [[nodiscard]] bool fullscreenAppActive(core::PointI screen) const override;
    [[nodiscard]] int showContextMenu(const std::vector<MenuItem>& items,
                                      core::PointI screen) override;

    [[nodiscard]] void* nativeHandle() const override { return hwnd_; }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    // 메시지가 발생한 시점의 커서 화면 좌표 (ADR-0003)
    [[nodiscard]] static core::Vec2 messageScreenPosition();

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    bool clickThrough_ = true;  // 만들 때 WS_EX_TRANSPARENT로 시작
    ATOM classAtom_ = 0;

    core::SizeI size_;
    core::PointI position_;

    bool capturing_ = false;
    bool trayAdded_ = false;
    UINT taskbarCreatedMessage_ = 0;  // 탐색기가 재시작되면 트레이 아이콘을 다시 등록해야 함
    std::wstring trayTooltip_;
    std::vector<core::Event> pending_;  // windowProc가 채우고 pollEvents가 비움
};

}  // namespace deskpet::platform::win32
