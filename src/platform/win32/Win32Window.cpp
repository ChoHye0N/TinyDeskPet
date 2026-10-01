#include "platform/win32/Win32Window.h"

#include "core/Log.h"
#include "platform/win32/Win32Strings.h"

#include <windowsx.h>  // GET_X_LPARAM, GET_Y_LPARAM

#include <algorithm>
#include <iterator>
#include <shellapi.h>  // Shell_NotifyIconW

namespace deskpet::platform::win32 {
namespace {

constexpr wchar_t kClassName[] = L"DeskPetWindow";

// 트레이 아이콘 알림은 우리가 정한 사용자 메시지로 창에 전달됩니다 (WM_APP 이상은 앱 전용 범위).
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTrayIconId = 1;

core::RectI toRect(const RECT& rect) {
    return {rect.left, rect.top, rect.right, rect.bottom};
}

}  // namespace

Win32Window::Win32Window(HINSTANCE instance) : instance_(instance) {}

Win32Window::~Win32Window() {
    if (trayAdded_ && hwnd_ != nullptr) {
        // 제거하지 않으면 마우스를 올릴 때까지 "유령 아이콘"이 트레이에 남음
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = hwnd_;
        data.uID = kTrayIconId;
        Shell_NotifyIconW(NIM_DELETE, &data);
    }
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
    }
    if (classAtom_ != 0) {
        UnregisterClassW(kClassName, instance_);
    }
}

bool Win32Window::create(const WindowDesc& desc) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_DBLCLKS;  // WM_LBUTTONDBLCLK 수신 (FR-13)
    windowClass.lpfnWndProc = &Win32Window::windowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
    windowClass.lpszClassName = kClassName;

    classAtom_ = RegisterClassExW(&windowClass);
    if (classAtom_ == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        core::logging::error("RegisterClassExW 실패 (GetLastError={})", GetLastError());
        return false;
    }

    // 스타일별 목적: docs/03-detailed-design/platform.md §4.1
    DWORD exStyle = WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW;
    if (desc.alwaysOnTop) {
        exStyle |= WS_EX_TOPMOST;
    }

    const std::wstring title = widen(desc.title);
    // 마지막 인자 this → WM_NCCREATE에서 GWLP_USERDATA에 저장 (§4.5)
    const HWND hwnd = CreateWindowExW(exStyle, kClassName, title.c_str(), WS_POPUP, desc.position.x,
                                      desc.position.y, desc.size.width, desc.size.height, nullptr,
                                      nullptr, instance_, this);
    if (hwnd == nullptr) {
        core::logging::error("CreateWindowExW 실패 (GetLastError={})", GetLastError());
        return false;
    }

    size_ = desc.size;
    position_ = desc.position;
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    core::logging::info("창 생성: {}x{}, 항상 위={}", size_.width, size_.height, desc.alwaysOnTop);
    return true;
}

void Win32Window::show() {
    // 시작할 때도 사용자가 쓰던 창의 포커스를 빼앗지 않습니다.
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
}

void Win32Window::hide() {
    ShowWindow(hwnd_, SW_HIDE);
}

void Win32Window::waitForEvents(int timeoutMs) {
    // 메시지 큐에 입력(마우스·키보드·게시된 메시지 등)이 들어오면 즉시 깨어나는 대기.
    // MWMO_INPUTAVAILABLE: 이미 큐에 있지만 아직 꺼내지 않은 입력이 있어도 바로 반환
    MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(std::max(timeoutMs, 0)), QS_ALLINPUT,
                                MWMO_INPUTAVAILABLE);
}

void Win32Window::setSize(core::SizeI size) {
    if (hwnd_ == nullptr || size == size_) {
        return;
    }
    SetWindowPos(hwnd_, nullptr, 0, 0, size.width, size.height,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    size_ = size;
}

core::RectI Win32Window::desktopBounds() const {
    // 가상 데스크톱: 모든 모니터를 감싸는 사각형. 주 모니터 왼쪽에 모니터가 있으면 left가 음수
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    return {left, top, left + GetSystemMetrics(SM_CXVIRTUALSCREEN),
            top + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
}

float Win32Window::dpiScale() const {
    // 매니페스트가 PerMonitorV2라 창마다 자기 모니터의 DPI를 받음
    const UINT dpi = hwnd_ != nullptr ? GetDpiForWindow(hwnd_) : GetDpiForSystem();
    return dpi > 0 ? static_cast<float>(dpi) / static_cast<float>(USER_DEFAULT_SCREEN_DPI) : 1.0f;
}

bool Win32Window::showTrayIcon(const std::string& tooltip) {
    if (hwnd_ == nullptr) {
        return false;
    }
    trayTooltip_ = widen(tooltip);

    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd_;
    data.uID = kTrayIconId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = kTrayMessage;
    // 실행 파일 리소스에 아이콘이 없으므로 시스템 기본 아이콘 사용 (TODO(M6): 전용 아이콘)
    data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(data.szTip, trayTooltip_.c_str(), _TRUNCATE);

    Shell_NotifyIconW(NIM_DELETE, &data);  // 탐색기 재시작 후 재등록 시 중복 방지
    if (Shell_NotifyIconW(NIM_ADD, &data) == FALSE) {
        core::logging::warn("Shell_NotifyIconW(NIM_ADD) 실패");
        return false;
    }
    // 버전 4: lParam 하위 워드에 실제 마우스 메시지, wParam에 커서 좌표가 옴
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
    trayAdded_ = true;
    return true;
}

void Win32Window::pollEvents(std::vector<core::Event>& out) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            pending_.emplace_back(core::QuitRequestedEvent{});
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);  // → windowProc → handleMessage → pending_
    }

    out.insert(out.end(), std::make_move_iterator(pending_.begin()),
               std::make_move_iterator(pending_.end()));
    pending_.clear();
}

void Win32Window::setPosition(core::PointI topLeft) {
    if (hwnd_ == nullptr || topLeft == position_) {
        return;  // 같은 위치면 SetWindowPos 호출 생략
    }
    SetWindowPos(hwnd_, nullptr, topLeft.x, topLeft.y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    position_ = topLeft;
}

core::RectI Win32Window::workArea() const {
    const HMONITOR monitor = hwnd_ != nullptr
                                 ? MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY)
                                 : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info) != FALSE) {
        return toRect(info.rcWork);
    }

    RECT fallback{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &fallback, 0);
    return toRect(fallback);
}

void Win32Window::setHitRegionEllipse(const core::RectI& local) {
    if (hwnd_ == nullptr) {
        return;
    }
    const HRGN region = CreateEllipticRgn(local.left, local.top, local.right, local.bottom);
    if (region == nullptr) {
        core::logging::warn("CreateEllipticRgn 실패");
        return;
    }
    // 성공하면 리전 소유권이 OS로 넘어갑니다. 실패했을 때만 직접 삭제합니다.
    if (SetWindowRgn(hwnd_, region, TRUE) == 0) {
        DeleteObject(region);
        core::logging::warn("SetWindowRgn 실패");
    }
    // TODO(M5): 알파 기반 클릭 통과로 교체 (커서 아래 픽셀 알파 검사 + WS_EX_TRANSPARENT 토글)
}

int Win32Window::showContextMenu(const std::vector<MenuItem>& items, core::PointI screen) {
    const HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return 0;
    }

    for (const MenuItem& item : items) {
        if (item.separator) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        const std::wstring label = widen(item.label);
        const UINT flags = MF_STRING | (item.enabled ? MF_ENABLED : MF_GRAYED);
        AppendMenuW(menu, flags, static_cast<UINT_PTR>(item.id), label.c_str());
    }

    // 팝업 메뉴는 소유 창이 전경이어야 바깥을 클릭했을 때 정상적으로 닫힙니다.
    SetForegroundWindow(hwnd_);
    const BOOL selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                         screen.x, screen.y, 0, hwnd_, nullptr);
    // 알려진 문제 회피: 메뉴가 두 번째부터 바로 닫히는 현상 방지 (KB135788)
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);

    return static_cast<int>(selected);
}

LRESULT CALLBACK Win32Window::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    Win32Window* self = nullptr;

    if (message == WM_NCCREATE) {
        const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<Win32Window*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (message == WM_NCDESTROY && self != nullptr) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->hwnd_ = nullptr;
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    if (self != nullptr) {
        return self->handleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT Win32Window::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    // 메시지 → 이벤트 변환표: docs/03-detailed-design/platform.md §4.2
    switch (message) {
        case WM_NCHITTEST: return HTCLIENT;  // 모양은 SetWindowRgn이 결정합니다.

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;  // 클릭해도 사용 중인 창의 포커스를 빼앗지 않음

        case WM_LBUTTONDOWN:
            SetCapture(hwnd_);  // 커서가 창 밖으로 나가도 이동 메시지를 받음
            capturing_ = true;
            pending_.emplace_back(
                core::PointerDownEvent{messageScreenPosition(), core::MouseButton::Left});
            return 0;

        case WM_MOUSEMOVE:
            pending_.emplace_back(core::PointerMoveEvent{messageScreenPosition()});
            return 0;

        case WM_LBUTTONUP:
            if (capturing_) {
                capturing_ = false;  // ReleaseCapture가 보내는 WM_CAPTURECHANGED와 구분
                ReleaseCapture();
            }
            pending_.emplace_back(
                core::PointerUpEvent{messageScreenPosition(), core::MouseButton::Left});
            return 0;

        case WM_CAPTURECHANGED:
            if (capturing_) {
                // 우리가 놓지 않았는데 캡처를 잃음 (Alt+Tab 등) → 드래그를 끝내 줌
                capturing_ = false;
                pending_.emplace_back(
                    core::PointerUpEvent{messageScreenPosition(), core::MouseButton::Left});
            }
            return 0;

        case WM_LBUTTONDBLCLK:
            pending_.emplace_back(
                core::DoubleClickEvent{messageScreenPosition(), core::MouseButton::Left});
            return 0;

        case WM_RBUTTONUP:
            pending_.emplace_back(
                core::PointerUpEvent{messageScreenPosition(), core::MouseButton::Right});
            return 0;

        case WM_DISPLAYCHANGE: pending_.emplace_back(core::WorkAreaChangedEvent{}); break;

        case WM_SETTINGCHANGE:
            if (wParam == SPI_SETWORKAREA) {
                pending_.emplace_back(core::WorkAreaChangedEvent{});
            }
            break;

        case WM_DPICHANGED: {
            // wParam 상위/하위 워드 = 새 DPI(X/Y). 크기·위치는 앱이 발 위치 기준으로 다시 정하므로
            // OS가 제안하는 사각형(lParam)은 쓰지 않습니다.
            const auto dpi = static_cast<float>(HIWORD(wParam));
            pending_.emplace_back(
                core::DpiChangedEvent{dpi / static_cast<float>(USER_DEFAULT_SCREEN_DPI)});
            return 0;
        }

        case kTrayMessage:
            // NOTIFYICON_VERSION_4: LOWORD(lParam) = 이벤트, wParam = 커서 화면 좌표
            switch (LOWORD(lParam)) {
                case WM_CONTEXTMENU:  // 오른쪽 클릭 (또는 키보드 메뉴 키)
                case NIN_SELECT:      // 왼쪽 클릭
                case NIN_KEYSELECT:
                    pending_.emplace_back(
                        core::TrayMenuRequestedEvent{{GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)}});
                    break;
                default: break;
            }
            return 0;

        case WM_CLOSE:
            pending_.emplace_back(core::QuitRequestedEvent{});
            return 0;  // 창 파괴는 소멸자에서

        case WM_PAINT:
            ValidateRect(hwnd_, nullptr);  // 그리기는 렌더러가 담당
            return 0;

        case WM_ERASEBKGND: return 1;

        default:
            if (message == taskbarCreatedMessage_ && taskbarCreatedMessage_ != 0 && trayAdded_) {
                // 탐색기가 다시 시작되면 기존 트레이 아이콘이 사라지므로 재등록
                (void)showTrayIcon(narrow(trayTooltip_));
                return 0;
            }
            break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

core::Vec2 Win32Window::messageScreenPosition() {
    const DWORD position = GetMessagePos();
    const auto packed = static_cast<LPARAM>(position);
    return {static_cast<float>(GET_X_LPARAM(packed)), static_cast<float>(GET_Y_LPARAM(packed))};
}

}  // namespace deskpet::platform::win32
