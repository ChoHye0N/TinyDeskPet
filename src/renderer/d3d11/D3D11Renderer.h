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

#include <array>
#include <memory>
#include <optional>
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
    [[nodiscard]] std::optional<AlphaSample> sampleAlpha(core::PointI local) override;
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
    [[nodiscard]] bool createFrameTexture();
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
    ComPtr<ID3D11Texture2D> backBuffer_;  // 완성된 프레임을 복사해 Present
    // 프레임을 먼저 그리는 우리 소유 텍스처. 스왑체인 버퍼는 Present 뒤 내용이 정해지지 않지만
    // 이 텍스처는 남아 있어, Present를 생략한 동안에도 커서 아래 알파를 읽을 수 있음 (FR-04)
    ComPtr<ID3D11Texture2D> frameTexture_;
    ComPtr<ID3D11RenderTargetView> renderTarget_;  // frameTexture_의 RTV

    // 커서 아래 1픽셀 알파를 GPU를 기다리지 않고 읽는 스테이징 링 (복사 요청 → 2~3프레임 뒤 Map)
    static constexpr std::size_t kAlphaSlots = 3;
    struct AlphaSlot {
        ComPtr<ID3D11Texture2D> staging;
        core::PointI at;
        bool pending = false;
    };
    std::array<AlphaSlot, kAlphaSlots> alphaSlots_;
    std::size_t nextAlphaSlot_ = 0;
    std::optional<AlphaSample> lastAlpha_;
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
