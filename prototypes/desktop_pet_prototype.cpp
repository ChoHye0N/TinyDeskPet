// desktop_pet.cpp
// C++ + Direct3D11 + Direct2D + DirectComposition 기반 최소 데스크톱 펫 예제
//
// 빌드 (Visual Studio "x64 Native Tools Command Prompt"):
//   cl /EHsc /std:c++17 /O2 desktop_pet.cpp /link /SUBSYSTEM:WINDOWS
//
// 조작:
//   - 캐릭터 드래그: 창 이동
//   - 더블클릭: 점프
//   - 우클릭: 종료 메뉴

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
#include <wrl/client.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <d2d1_2.h>
#include <dcomp.h>
#include <cmath>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dcomp.lib")

// 콘솔 프로젝트로 만들어도 Windows GUI 앱으로 링크되도록 강제
// (없으면 "main 확인할 수 없는 외부 기호(LNK2019)" 에러 발생)
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

using Microsoft::WRL::ComPtr;

constexpr int kSize = 200;          // 창 크기 = 캐릭터 크기 (전체 화면 투명 창을 쓰지 않음)
constexpr UINT kMenuExit = 1;

struct App {
    HWND hwnd = nullptr;

    // D3D / DXGI
    ComPtr<ID3D11Device>       d3dDevice;
    ComPtr<IDXGIDevice>        dxgiDevice;
    ComPtr<IDXGISwapChain1>    swapChain;

    // Direct2D
    ComPtr<ID2D1Factory2>        d2dFactory;
    ComPtr<ID2D1Device1>         d2dDevice;
    ComPtr<ID2D1DeviceContext1>  dc;
    ComPtr<ID2D1SolidColorBrush> bodyBrush, eyeBrush, cheekBrush;

    // DirectComposition
    ComPtr<IDCompositionDevice> dcompDevice;
    ComPtr<IDCompositionTarget> dcompTarget;
    ComPtr<IDCompositionVisual> dcompVisual;

    // 애니메이션 상태
    LARGE_INTEGER freq{}, start{};
    double jumpStart = -10.0;
} g;

static void Check(HRESULT hr, const wchar_t* what) {
    if (FAILED(hr)) {
        wchar_t buf[256];
        wsprintfW(buf, L"%s 실패 (HRESULT 0x%08X)", what, static_cast<unsigned>(hr));
        MessageBoxW(nullptr, buf, L"Desktop Pet", MB_ICONERROR);
        ExitProcess(1);
    }
}

static double Now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart - g.start.QuadPart) / double(g.freq.QuadPart);
}

static void InitGraphics() {
    // 1) D3D11 디바이스 (Direct2D와 공유하려면 BGRA 지원 필수)
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                            D3D11_SDK_VERSION, &g.d3dDevice, nullptr, nullptr),
          L"D3D11CreateDevice");
    Check(g.d3dDevice.As(&g.dxgiDevice), L"IDXGIDevice 조회");

    // 2) 컴포지션용 스왑체인: 알파 모드를 PREMULTIPLIED로 → 배경 투명
    ComPtr<IDXGIFactory2> dxgiFactory;
    Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory)), L"CreateDXGIFactory2");

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width            = kSize;
    desc.Height           = kSize;
    desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount      = 2;
    desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode        = DXGI_ALPHA_MODE_PREMULTIPLIED;
    Check(dxgiFactory->CreateSwapChainForComposition(g.dxgiDevice.Get(), &desc, nullptr, &g.swapChain),
          L"CreateSwapChainForComposition");

    // 3) Direct2D 디바이스 컨텍스트를 같은 GPU 디바이스 위에 생성
    D2D1_FACTORY_OPTIONS opts{};
    Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory2), &opts,
                            reinterpret_cast<void**>(g.d2dFactory.GetAddressOf())),
          L"D2D1CreateFactory");
    Check(g.d2dFactory->CreateDevice(g.dxgiDevice.Get(), &g.d2dDevice), L"D2D CreateDevice");
    Check(g.d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g.dc),
          L"CreateDeviceContext");

    // 4) 스왑체인 백버퍼를 D2D 렌더 타깃으로 연결
    ComPtr<IDXGISurface2> surface;
    Check(g.swapChain->GetBuffer(0, IID_PPV_ARGS(&surface)), L"GetBuffer");
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1Bitmap1> target;
    Check(g.dc->CreateBitmapFromDxgiSurface(surface.Get(), &props, &target), L"CreateBitmapFromDxgiSurface");
    g.dc->SetTarget(target.Get());

    // 5) DirectComposition: 창 → 비주얼 → 스왑체인 연결
    //    (레이어드 윈도우처럼 CPU 메모리로 복사하지 않고 GPU에서 바로 합성됨)
    Check(DCompositionCreateDevice(g.dxgiDevice.Get(), IID_PPV_ARGS(&g.dcompDevice)),
          L"DCompositionCreateDevice");
    Check(g.dcompDevice->CreateTargetForHwnd(g.hwnd, TRUE, &g.dcompTarget), L"CreateTargetForHwnd");
    Check(g.dcompDevice->CreateVisual(&g.dcompVisual), L"CreateVisual");
    Check(g.dcompVisual->SetContent(g.swapChain.Get()), L"SetContent");
    Check(g.dcompTarget->SetRoot(g.dcompVisual.Get()), L"SetRoot");
    Check(g.dcompDevice->Commit(), L"Commit");

    // 브러시는 한 번만 만들고 재사용
    g.dc->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.80f, 1.00f), &g.bodyBrush);
    g.dc->CreateSolidColorBrush(D2D1::ColorF(0.10f, 0.12f, 0.20f), &g.eyeBrush);
    g.dc->CreateSolidColorBrush(D2D1::ColorF(1.00f, 0.55f, 0.65f, 0.6f), &g.cheekBrush);
}

static void Render() {
    const double t = Now();

    // 숨쉬기(위아래 흔들림)
    float bob = static_cast<float>(std::sin(t * 3.0) * 5.0);

    // 점프 (0.5초)
    double jt = t - g.jumpStart;
    float jump = 0.0f;
    if (jt >= 0.0 && jt < 0.5) jump = static_cast<float>(std::sin(jt / 0.5 * 3.14159) * 25.0);

    // 깜빡임: 3초마다 0.12초 동안 눈 감기
    bool blink = std::fmod(t, 3.0) < 0.12;

    const float cx = kSize / 2.0f;
    const float cy = 115.0f + bob - jump;

    g.dc->BeginDraw();
    g.dc->Clear(D2D1::ColorF(0, 0, 0, 0));  // 완전 투명으로 지우기

    // 몸통
    g.dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 65.0f, 55.0f), g.bodyBrush.Get());

    // 눈
    if (blink) {
        g.dc->DrawLine(D2D1::Point2F(cx - 30, cy - 8), D2D1::Point2F(cx - 14, cy - 8), g.eyeBrush.Get(), 4.0f);
        g.dc->DrawLine(D2D1::Point2F(cx + 14, cy - 8), D2D1::Point2F(cx + 30, cy - 8), g.eyeBrush.Get(), 4.0f);
    } else {
        g.dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 22, cy - 8), 7.0f, 10.0f), g.eyeBrush.Get());
        g.dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 22, cy - 8), 7.0f, 10.0f), g.eyeBrush.Get());
    }

    // 볼
    g.dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 38, cy + 10), 10.0f, 6.0f), g.cheekBrush.Get());
    g.dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 38, cy + 10), 10.0f, 6.0f), g.cheekBrush.Get());

    Check(g.dc->EndDraw(), L"EndDraw");

    // VSync에 맞춰 출력 (모니터 주사율만큼만 그리므로 CPU를 낭비하지 않음)
    Check(g.swapChain->Present(1, 0), L"Present");
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST:
        // 창 전체를 "제목 표시줄"로 취급 → 드래그로 창 이동
        return HTCAPTION;

    case WM_NCLBUTTONDBLCLK:
        // 제목 표시줄 더블클릭 기본 동작(최대화)을 막고 점프로 대체
        g.jumpStart = Now();
        return 0;

    case WM_NCRBUTTONUP: {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, kMenuExit, L"종료");
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        SetForegroundWindow(hwnd);
        UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(menu);
        if (cmd == kMenuExit) DestroyWindow(hwnd);
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    QueryPerformanceFrequency(&g.freq);
    QueryPerformanceCounter(&g.start);

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_HAND);
    wc.lpszClassName = L"DesktopPet";
    RegisterClassExW(&wc);

    // 작업 표시줄 위 오른쪽 아래에 배치
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    // WS_EX_NOREDIRECTIONBITMAP : GDI용 리다이렉션 비트맵을 만들지 않음 (DirectComposition 전용 창)
    // WS_EX_TOPMOST             : 항상 위
    // WS_EX_TOOLWINDOW          : 작업 표시줄/Alt+Tab에 표시 안 함
    g.hwnd = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        wc.lpszClassName, L"Desktop Pet", WS_POPUP,
        work.right - kSize - 40, work.bottom - kSize, kSize, kSize,
        nullptr, nullptr, hInst, nullptr);
    if (!g.hwnd) return 1;

    InitGraphics();

    // 캐릭터 바깥 영역은 클릭이 아래 창으로 통과하도록 창 모양을 잘라냄.
    // 실제 스프라이트라면 알파 마스크로 영역을 계산하거나,
    // 커서 위치의 픽셀 알파를 검사해 WS_EX_TRANSPARENT를 토글하는 방식을 씀.
    SetWindowRgn(g.hwnd, CreateRoundRectRgn(30, 25, 171, 181, 80, 80), TRUE);

    ShowWindow(g.hwnd, SW_SHOW);

    MSG msg{};
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }
        Render();
    }
    return 0;
}
