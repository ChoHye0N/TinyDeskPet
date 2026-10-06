#pragma once

// 렌더러 인터페이스. 구현은 renderer/d3d11/D3D11Renderer (ADR-0002)

#include "core/Config.h"
#include "core/Math.h"
#include "renderer/RenderScene.h"

#include <cstdint>
#include <optional>

namespace deskpet::renderer {

struct RendererOptions {
    bool vsync = true;        // true면 Present가 VSync까지 대기 → CPU 휴식
    bool debugLayer = false;  // D3D11 디버그 레이어 (Windows "그래픽 도구" 필요)
    int msaaSamples = 4;
    core::OutlineMode outline =
        core::OutlineMode::Model;  // 외곽선 대상 재질  // 1이면 MSAA 끔. 지원하지 않으면
                                   // chooseSampleCount로 낮춤
};

// 창 내부 좌표 at 픽셀의 알파 (0 ~ 1)
struct AlphaSample {
    core::PointI at;
    float alpha = 0.0f;
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
    // local 픽셀의 알파 읽기를 요청하고, 지금까지 도착한 가장 최근 결과를 돌려줌 (FR-04 클릭 통과).
    // GPU를 기다리지 않아 결과는 2~3프레임 늦음. 아직 결과가 없거나 창 밖이면 nullopt
    [[nodiscard]] virtual std::optional<AlphaSample> sampleAlpha(core::PointI local) = 0;
    virtual void shutdown() = 0;
};

}  // namespace deskpet::renderer
