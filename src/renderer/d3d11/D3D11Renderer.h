#pragma once

// Direct3D 11 + DirectComposition 렌더러 (ADR-0001)
// 리소스 수명, 프레임 흐름, 디바이스 손실 복구: docs/03-detailed-design/renderer.md §4

#include "renderer/IRenderer.h"
#include "renderer/d3d11/IRenderPass.h"

#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <memory>
#include <vector>

namespace deskpet::renderer::d3d11 {

class D3D11Renderer final : public IRenderer {
public:
    D3D11Renderer();
    ~D3D11Renderer() override;

    [[nodiscard]] bool initialize(void* nativeWindow, core::SizeI size,
                                  const RendererOptions& options) override;
    [[nodiscard]] FrameResult render(const RenderScene& scene) override;
    void resize(core::SizeI size) override;
    void shutdown() override;

private:
    template <class T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    // 디바이스 종속 리소스 전체: 디바이스 → 스왑체인 → D2D → DComp → 렌더 타깃 → 패스
    [[nodiscard]] bool createDeviceResources();
    void releaseDeviceResources();

    [[nodiscard]] bool createDevice();
    [[nodiscard]] bool createSwapChain();
    [[nodiscard]] bool createDirect2D();
    [[nodiscard]] bool createComposition();
    [[nodiscard]] bool createRenderTargets();
    [[nodiscard]] bool createMsaaTarget();
    void releaseRenderTargets();
    [[nodiscard]] unsigned chooseSupportedSampleCount() const;
    [[nodiscard]] bool runPasses(const std::vector<std::unique_ptr<IRenderPass>>& passes,
                                 const RenderScene& scene);

    [[nodiscard]] bool handleDeviceLost();
    [[nodiscard]] D3D11Context makeContext() const;

    HWND hwnd_ = nullptr;
    core::SizeI size_;
    RendererOptions options_;
    bool initialized_ = false;

    // 변화 없을 때 Present 생략 (DEBT-02). 리소스를 새로 만들거나 크기가 바뀌면 반드시 다시 그림
    RenderScene lastScene_;
    bool needsPresent_ = true;

    // 디바이스 독립 (프로그램 수명 동안 유지)
    ComPtr<ID2D1Factory1> d2dFactory_;

    // 디바이스 종속
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIDevice> dxgiDevice_;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID3D11Texture2D> backBuffer_;  // resolve 대상
    ComPtr<ID3D11RenderTargetView> renderTarget_;
    unsigned sampleCount_ = 1;            // 실제로 쓰는 MSAA 샘플 수 (1 = 끔)
    ComPtr<ID3D11Texture2D> msaaTarget_;  // sampleCount_ > 1일 때만
    ComPtr<ID3D11RenderTargetView> msaaView_;

    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<ID2D1DeviceContext> d2dContext_;
    ComPtr<ID2D1Bitmap1> d2dTarget_;

    ComPtr<IDCompositionDevice> dcompDevice_;
    ComPtr<IDCompositionTarget> dcompTarget_;
    ComPtr<IDCompositionVisual> dcompVisual_;

    // 3D 패스(sceneTarget에 그림) → MSAA resolve → 2D 패스(Direct2D, 백버퍼에 그림)
    std::vector<std::unique_ptr<IRenderPass>> scenePasses_;
    std::vector<std::unique_ptr<IRenderPass>> overlayPasses_;
};

}  // namespace deskpet::renderer::d3d11
