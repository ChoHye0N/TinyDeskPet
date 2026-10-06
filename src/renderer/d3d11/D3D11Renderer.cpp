#include "renderer/d3d11/D3D11Renderer.h"

#include "core/Log.h"
#include "renderer/SampleCount.h"
#include "renderer/d3d11/DxCheck.h"
#include "renderer/d3d11/MeshPass.h"
#include "renderer/d3d11/PlaceholderPass.h"

#include <cstdint>
#include <iterator>

namespace deskpet::renderer::d3d11 {

D3D11Renderer::D3D11Renderer() = default;

D3D11Renderer::~D3D11Renderer() {
    shutdown();
}

bool D3D11Renderer::initialize(void* nativeWindow, core::SizeI size,
                               const RendererOptions& options) {
    hwnd_ = static_cast<HWND>(nativeWindow);
    size_ = size;
    options_ = options;

    if (hwnd_ == nullptr || size.width <= 0 || size.height <= 0) {
        core::logging::error("렌더러 초기화 인자가 잘못되었습니다 (hwnd={}, {}x{})",
                             static_cast<void*>(hwnd_), size.width, size.height);
        return false;
    }

    // 패스 실행 순서 = 등록 순서 (docs/02-architecture/SAD.md §7.2)
    scenePasses_.clear();
    overlayPasses_.clear();
    scenePasses_.push_back(
        std::make_unique<MeshPass>(options_.outline));  // 3D를 먼저 그림 (ADR-0008)
    overlayPasses_.push_back(std::make_unique<PlaceholderPass>());

    initialized_ = createDeviceResources();
    if (!initialized_) {
        releaseDeviceResources();
        return false;
    }

    core::logging::info("렌더러 초기화 완료: {}x{}, vsync={}, debugLayer={}, msaa={}", size_.width,
                        size_.height, options_.vsync, options_.debugLayer, sampleCount_);
    return true;
}

FrameResult D3D11Renderer::render(const RenderScene& scene) {
    if (!initialized_) {
        return FrameResult::Fatal;
    }

    // 0) 직전 프레임과 입력이 같으면 아무것도 하지 않음. DirectComposition은 마지막으로 Present한
    //    내용을 계속 합성하므로 화면은 그대로 유지되고, GPU·CPU는 쉴 수 있음
    if (!needsPresent_ && scene == lastScene_) {
        return FrameResult::Skipped;
    }

    // 1) 프레임 텍스처를 완전 투명(premultiplied alpha이므로 RGB도 0)으로 지움
    ID3D11RenderTargetView* renderTarget = renderTarget_.Get();
    context_->OMSetRenderTargets(1, &renderTarget, nullptr);
    constexpr float kTransparent[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context_->ClearRenderTargetView(renderTarget, kTransparent);
    if (msaaView_) {
        context_->ClearRenderTargetView(msaaView_.Get(), kTransparent);
    }

    const D3D11_VIEWPORT viewport{
        0.0f, 0.0f, static_cast<float>(size_.width), static_cast<float>(size_.height), 0.0f, 1.0f};
    context_->RSSetViewports(1, &viewport);

    // 2) 3D 패스 → MSAA면 샘플 평균을 프레임 텍스처로 resolve → 2D(Direct2D) 패스.
    //    D2D는 프레임 텍스처에 직접 그리므로 resolve보다 뒤여야 덮어쓰이지 않음
    if (!runPasses(scenePasses_, scene)) {
        return handleDeviceLost() ? FrameResult::DeviceRecovered : FrameResult::Fatal;
    }
    if (msaaTarget_) {
        // premultiplied alpha라 샘플을 단순 평균해도 가장자리 반투명이 올바름
        context_->ResolveSubresource(frameTexture_.Get(), 0, msaaTarget_.Get(), 0,
                                     DXGI_FORMAT_B8G8R8A8_UNORM);
        context_->OMSetRenderTargets(1, &renderTarget, nullptr);
    }
    if (!runPasses(overlayPasses_, scene)) {
        return handleDeviceLost() ? FrameResult::DeviceRecovered : FrameResult::Fatal;
    }

    // 3) 완성된 프레임을 백버퍼로 복사(GPU 안에서)하고 출력.
    //    VSync(1)면 다음 수직 동기화까지 대기하므로 CPU가 쉽니다.
    context_->CopyResource(backBuffer_.Get(), frameTexture_.Get());
    const HRESULT hr = swapChain_->Present(options_.vsync ? 1U : 0U, 0U);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        return handleDeviceLost() ? FrameResult::DeviceRecovered : FrameResult::Fatal;
    }
    if (!check(hr, "Present")) {
        return FrameResult::Fatal;
    }
    lastScene_ = scene;
    needsPresent_ = false;
    return FrameResult::Ok;
}

void D3D11Renderer::resize(core::SizeI size) {
    if (!initialized_ || size == size_ || size.width <= 0 || size.height <= 0) {
        return;
    }

    // 백버퍼를 참조하는 RTV와 D2D 비트맵을 모두 놓아야 ResizeBuffers가 성공합니다.
    releaseRenderTargets();
    size_ = size;
    needsPresent_ = true;

    const HRESULT hr = swapChain_->ResizeBuffers(
        0, static_cast<UINT>(size.width), static_cast<UINT>(size.height), DXGI_FORMAT_UNKNOWN, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        if (!handleDeviceLost()) {
            initialized_ = false;
        }
        return;
    }
    if (!check(hr, "ResizeBuffers") || !createRenderTargets()) {
        initialized_ = false;
    }
}

void D3D11Renderer::shutdown() {
    releaseDeviceResources();
    scenePasses_.clear();
    overlayPasses_.clear();
    d2dFactory_.Reset();
    initialized_ = false;
}

// ---------------------------------------------------------------------------
// 디바이스 종속 리소스 (docs/03-detailed-design/renderer.md §4.1)
// ---------------------------------------------------------------------------

bool D3D11Renderer::createDeviceResources() {
    if (!createDevice() || !createSwapChain() || !createDirect2D() || !createComposition() ||
        !createRenderTargets()) {
        return false;
    }

    const D3D11Context ctx = makeContext();
    for (const auto* passes : {&scenePasses_, &overlayPasses_}) {
        for (const auto& pass : *passes) {
            if (!pass->create(ctx)) {
                core::logging::error("{} 리소스 생성 실패", pass->name());
                return false;
            }
        }
    }
    needsPresent_ = true;  // 새 스왑체인은 비어 있음
    return true;
}

void D3D11Renderer::releaseDeviceResources() {
    // 생성의 역순으로 해제
    for (const auto* passes : {&overlayPasses_, &scenePasses_}) {
        for (const auto& pass : *passes) {
            pass->release();
        }
    }
    releaseRenderTargets();

    dcompVisual_.Reset();
    dcompTarget_.Reset();
    dcompDevice_.Reset();

    d2dContext_.Reset();
    d2dDevice_.Reset();

    swapChain_.Reset();

    if (context_) {
        context_->ClearState();
        context_->Flush();
    }
    context_.Reset();
    dxgiDevice_.Reset();
    device_.Reset();
}

bool D3D11Renderer::createDevice() {
    // BGRA 지원은 Direct2D와 디바이스를 공유하기 위해 필수입니다.
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (options_.debugLayer) {
        flags |= D3D11_CREATE_DEVICE_DEBUG;
    }

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL featureLevel{};

    const auto tryCreate = [&](D3D_DRIVER_TYPE driverType, UINT creationFlags) {
        return D3D11CreateDevice(nullptr, driverType, nullptr, creationFlags, levels,
                                 static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                                 device_.ReleaseAndGetAddressOf(), &featureLevel,
                                 context_.ReleaseAndGetAddressOf());
    };

    HRESULT hr = tryCreate(D3D_DRIVER_TYPE_HARDWARE, flags);

    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG) != 0) {
        // 디버그 레이어는 Windows "그래픽 도구" 선택적 기능이 있어야 동작합니다 (RISK-04)
        core::logging::warn(
            "D3D11 디버그 레이어를 사용할 수 없어 끄고 다시 시도합니다. "
            "설정 > 시스템 > 선택적 기능 > '그래픽 도구'를 설치하세요.");
        flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
        hr = tryCreate(D3D_DRIVER_TYPE_HARDWARE, flags);
    }

    if (FAILED(hr)) {
        core::logging::warn("하드웨어 디바이스 생성 실패({}). WARP 소프트웨어 렌더러로 대체합니다.",
                            hresultToString(hr));
        hr = tryCreate(D3D_DRIVER_TYPE_WARP, flags);
    }

    if (!check(hr, "D3D11CreateDevice")) {
        return false;
    }
    core::logging::info("D3D11 디바이스 생성: feature level {:#x}",
                        static_cast<unsigned>(featureLevel));
    sampleCount_ = chooseSupportedSampleCount();
    if (static_cast<int>(sampleCount_) < options_.msaaSamples) {
        core::logging::warn("MSAA {}x를 지원하지 않아 {}x로 낮춥니다", options_.msaaSamples,
                            sampleCount_);
    }

#ifndef NDEBUG
    // 디버거가 붙어 있으면 D3D 오류 지점에서 바로 멈춥니다.
    ComPtr<ID3D11InfoQueue> infoQueue;
    if (IsDebuggerPresent() != FALSE && SUCCEEDED(device_.As(&infoQueue))) {
        infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
    }
#endif

    return check(device_.As(&dxgiDevice_), "IDXGIDevice 조회");
}

bool D3D11Renderer::createSwapChain() {
    // 디바이스가 실제로 사용하는 어댑터의 팩토리를 씁니다 (다중 GPU에서 어댑터 불일치 방지).
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    if (!check(dxgiDevice_->GetAdapter(&adapter), "GetAdapter") ||
        !check(adapter->GetParent(IID_PPV_ARGS(&factory)), "IDXGIFactory2 조회")) {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(size_.width);
    desc.Height = static_cast<UINT>(size_.height);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count =
        1;  // flip 모델 스왑체인은 멀티샘플 불가 → MSAA는 별도 텍스처에서 resolve
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;  // 컴포지션 스왑체인은 flip 모델 필수
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;  // 투명 배경의 핵심 (ADR-0001)
    desc.Scaling = DXGI_SCALING_STRETCH;

    return check(factory->CreateSwapChainForComposition(device_.Get(), &desc, nullptr,
                                                        swapChain_.ReleaseAndGetAddressOf()),
                 "CreateSwapChainForComposition");
}

bool D3D11Renderer::createDirect2D() {
    if (!d2dFactory_) {
        const D2D1_FACTORY_OPTIONS factoryOptions{};
        if (!check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
                                     &factoryOptions,
                                     reinterpret_cast<void**>(d2dFactory_.GetAddressOf())),
                   "D2D1CreateFactory")) {
            return false;
        }
    }

    return check(d2dFactory_->CreateDevice(dxgiDevice_.Get(), d2dDevice_.ReleaseAndGetAddressOf()),
                 "ID2D1Factory1::CreateDevice") &&
           check(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                 d2dContext_.ReleaseAndGetAddressOf()),
                 "CreateDeviceContext");
}

bool D3D11Renderer::createComposition() {
    // 창(HWND) → 타깃 → 비주얼 → 스왑체인 (docs/02-architecture/SAD.md §7.1)
    return check(DCompositionCreateDevice(dxgiDevice_.Get(),
                                          IID_PPV_ARGS(dcompDevice_.ReleaseAndGetAddressOf())),
                 "DCompositionCreateDevice") &&
           check(dcompDevice_->CreateTargetForHwnd(hwnd_, TRUE,
                                                   dcompTarget_.ReleaseAndGetAddressOf()),
                 "CreateTargetForHwnd") &&
           check(dcompDevice_->CreateVisual(dcompVisual_.ReleaseAndGetAddressOf()),
                 "CreateVisual") &&
           check(dcompVisual_->SetContent(swapChain_.Get()), "SetContent") &&
           check(dcompTarget_->SetRoot(dcompVisual_.Get()), "SetRoot") &&
           check(dcompDevice_->Commit(), "DComp Commit");
}

bool D3D11Renderer::createRenderTargets() {
    // flip 모델 + D3D11에서는 0번 버퍼가 항상 "현재 백버퍼"를 가리킵니다.
    if (!check(swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer_.ReleaseAndGetAddressOf())),
               "GetBuffer") ||
        !createFrameTexture() || !createMsaaTarget()) {
        return false;
    }

    ComPtr<IDXGISurface> surface;
    if (!check(frameTexture_.As(&surface), "IDXGISurface 조회")) {
        return false;
    }

    const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (!check(d2dContext_->CreateBitmapFromDxgiSurface(surface.Get(), &properties,
                                                        d2dTarget_.ReleaseAndGetAddressOf()),
               "CreateBitmapFromDxgiSurface")) {
        return false;
    }
    d2dContext_->SetTarget(d2dTarget_.Get());
    return true;
}

bool D3D11Renderer::createFrameTexture() {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(size_.width);
    desc.Height = static_cast<UINT>(size_.height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // CopyResource하려면 백버퍼와 형식·크기가 같아야 함
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;  // Direct2D 타깃으로도 씀
    if (!check(device_->CreateTexture2D(&desc, nullptr, frameTexture_.ReleaseAndGetAddressOf()),
               "프레임 텍스처") ||
        !check(device_->CreateRenderTargetView(frameTexture_.Get(), nullptr,
                                               renderTarget_.ReleaseAndGetAddressOf()),
               "프레임 텍스처 RTV")) {
        return false;
    }

    // 알파 읽기용 1×1 스테이징 (CPU가 Map으로 읽을 수 있는 유일한 종류)
    D3D11_TEXTURE2D_DESC staging = desc;
    staging.Width = staging.Height = 1;
    staging.Usage = D3D11_USAGE_STAGING;
    staging.BindFlags = 0;
    staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (AlphaSlot& slot : alphaSlots_) {
        slot.pending = false;
        if (!check(
                device_->CreateTexture2D(&staging, nullptr, slot.staging.ReleaseAndGetAddressOf()),
                "알파 스테이징 텍스처")) {
            return false;
        }
    }
    lastAlpha_.reset();
    return true;
}

std::optional<AlphaSample> D3D11Renderer::sampleAlpha(core::PointI local) {
    if (!initialized_ || !frameTexture_) {
        return std::nullopt;
    }
    // 1) 앞서 요청한 복사 중 끝난 것을 오래된 순서로 읽음. DO_NOT_WAIT: 아직이면 기다리지 않고
    //    DXGI_ERROR_WAS_STILL_DRAWING을 돌려받음 (Map이 GPU를 기다리면 프레임이 멈춤)
    for (std::size_t i = 0; i < kAlphaSlots; ++i) {
        AlphaSlot& slot = alphaSlots_[(nextAlphaSlot_ + i) % kAlphaSlots];
        if (!slot.pending) {
            continue;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT hr = context_->Map(slot.staging.Get(), 0, D3D11_MAP_READ,
                                         D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING) {
            break;  // 뒤의 것은 더 늦게 요청했으므로 역시 아직
        }
        slot.pending = false;
        if (SUCCEEDED(hr)) {
            const auto* bgra = static_cast<const std::uint8_t*>(mapped.pData);
            lastAlpha_ = AlphaSample{slot.at, static_cast<float>(bgra[3]) / 255.0f};
            context_->Unmap(slot.staging.Get(), 0);
        }
    }

    // 2) 이번 위치의 1픽셀 복사를 요청 (창 밖이면 요청하지 않음)
    if (local.x < 0 || local.y < 0 || local.x >= size_.width || local.y >= size_.height) {
        return std::nullopt;
    }
    AlphaSlot& slot = alphaSlots_[nextAlphaSlot_];
    if (!slot.pending) {
        const auto x = static_cast<UINT>(local.x);
        const auto y = static_cast<UINT>(local.y);
        const D3D11_BOX box{x, y, 0, x + 1, y + 1, 1};
        context_->CopySubresourceRegion(slot.staging.Get(), 0, 0, 0, 0, frameTexture_.Get(), 0,
                                        &box);
        slot.at = local;
        slot.pending = true;
        nextAlphaSlot_ = (nextAlphaSlot_ + 1) % kAlphaSlots;
    }
    return lastAlpha_;
}

bool D3D11Renderer::createMsaaTarget() {
    if (sampleCount_ <= 1) {
        return true;
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(size_.width);
    desc.Height = static_cast<UINT>(size_.height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // resolve하려면 백버퍼와 형식이 같아야 함
    desc.SampleDesc.Count = sampleCount_;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    return check(device_->CreateTexture2D(&desc, nullptr, msaaTarget_.ReleaseAndGetAddressOf()),
                 "MSAA 텍스처") &&
           check(device_->CreateRenderTargetView(msaaTarget_.Get(), nullptr,
                                                 msaaView_.ReleaseAndGetAddressOf()),
                 "MSAA 렌더 타깃 뷰");
}

unsigned D3D11Renderer::chooseSupportedSampleCount() const {
    // 색(백버퍼 형식)과 깊이 형식 모두 품질 수준이 1 이상이어야 그 샘플 수를 쓸 수 있음
    const auto supported = [this](int n) {
        const auto count = static_cast<UINT>(n);
        for (const DXGI_FORMAT format : {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_D32_FLOAT}) {
            UINT quality = 0;
            if (FAILED(device_->CheckMultisampleQualityLevels(format, count, &quality)) ||
                quality == 0) {
                return false;
            }
        }
        return true;
    };
    return static_cast<unsigned>(chooseSampleCount(options_.msaaSamples, supported));
}

bool D3D11Renderer::runPasses(const std::vector<std::unique_ptr<IRenderPass>>& passes,
                              const RenderScene& scene) {
    const D3D11Context ctx = makeContext();
    for (const auto& pass : passes) {
        if (!pass->execute(ctx, scene)) {
            core::logging::warn("{} 실행 실패 → 디바이스 리소스를 다시 만듭니다", pass->name());
            return false;
        }
    }
    return true;
}

void D3D11Renderer::releaseRenderTargets() {
    if (d2dContext_) {
        d2dContext_->SetTarget(nullptr);
    }
    d2dTarget_.Reset();

    if (context_) {
        context_->OMSetRenderTargets(0, nullptr, nullptr);
    }
    renderTarget_.Reset();
    msaaView_.Reset();
    msaaTarget_.Reset();
    frameTexture_.Reset();
    for (AlphaSlot& slot : alphaSlots_) {
        slot.staging.Reset();
        slot.pending = false;
    }
    lastAlpha_.reset();
    backBuffer_.Reset();  // 백버퍼 참조가 남아 있으면 ResizeBuffers 실패

    if (context_) {
        context_->Flush();
    }
}

bool D3D11Renderer::handleDeviceLost() {
    const HRESULT reason = device_ ? device_->GetDeviceRemovedReason() : E_FAIL;
    core::logging::warn("GPU 디바이스 손실 감지 (원인: {}). 리소스를 다시 만듭니다.",
                        hresultToString(reason));

    releaseDeviceResources();
    if (!createDeviceResources()) {
        core::logging::error("GPU 디바이스 복구 실패");
        releaseDeviceResources();
        initialized_ = false;
        return false;
    }

    core::logging::info("GPU 디바이스 복구 완료");
    return true;
}

D3D11Context D3D11Renderer::makeContext() const {
    D3D11Context ctx;
    ctx.device = device_.Get();
    ctx.context = context_.Get();
    ctx.renderTarget = renderTarget_.Get();
    ctx.sceneTarget = msaaView_ ? msaaView_.Get() : renderTarget_.Get();
    ctx.sampleCount = sampleCount_;
    ctx.d2d = d2dContext_.Get();
    ctx.viewport = size_;
    return ctx;
}

}  // namespace deskpet::renderer::d3d11
