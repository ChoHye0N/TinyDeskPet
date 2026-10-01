#pragma once

// 렌더러 인터페이스. 구현은 renderer/d3d11/D3D11Renderer (ADR-0002)

#include "core/Math.h"
#include "renderer/RenderScene.h"

#include <cstdint>

namespace deskpet::renderer {

struct RendererOptions {
    bool vsync = true;        // true면 Present가 VSync까지 대기 → CPU 휴식
    bool debugLayer = false;  // D3D11 디버그 레이어 (Windows "그래픽 도구" 필요)
};

enum class FrameResult : std::uint8_t {
    Ok,
    DeviceRecovered,  // 디바이스 손실이 있었지만 복구됨 (이번 프레임은 버려짐)
    Skipped,  // 직전 프레임과 같아 Present를 생략함 (VSync 대기가 없으므로 호출자가 쉬어야 함)
    Fatal,    // 복구 불가
};

class IRenderer {
public:
    IRenderer() = default;
    virtual ~IRenderer() = default;

    IRenderer(const IRenderer&) = delete;
    IRenderer& operator=(const IRenderer&) = delete;
    IRenderer(IRenderer&&) = delete;
    IRenderer& operator=(IRenderer&&) = delete;

    // nativeWindow: IWindow::nativeHandle() (Win32에서는 HWND)
    [[nodiscard]] virtual bool initialize(void* nativeWindow, core::SizeI size,
                                          const RendererOptions& options) = 0;
    [[nodiscard]] virtual FrameResult render(const RenderScene& scene) = 0;
    virtual void resize(core::SizeI size) = 0;
    virtual void shutdown() = 0;
};

}  // namespace deskpet::renderer
