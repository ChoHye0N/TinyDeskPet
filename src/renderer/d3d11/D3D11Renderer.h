#pragma once

// Direct3D 11 + DirectComposition 렌더러 (ADR-0001)
// 리소스 수명, 프레임 흐름, 디바이스 손실 복구: docs/03-detailed-design/renderer.md §4

#include "renderer/IRenderer.h"
#include "renderer/d3d11/IRenderPass.h"

#include <d2d1_1.h>
#include <d3d11.h>
#include <d3d11_1.h>
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
    // 3D 영역 텍스처(일반 + MSAA)를 needed 이상 크기로 준비. 커질 때만 다시 만듦
    [[nodiscard]] bool ensureRegionTargets(core::SizeI needed);
    [[nodiscard]] bool createFrameTexture();
    void releaseRenderTargets();
    [[nodiscard]] unsigned chooseSupportedSampleCount() const;
    [[nodiscard]] bool runPasses(const std::vector<std::unique_ptr<IRenderPass>>& passes,
                                 const RenderScene& scene, const D3D11Context& ctx);
    // 3D를 영역에 그려 프레임 텍스처에 복사하고, 실제로 쓴 영역(화면 좌표)을 돌려줌
    [[nodiscard]] std::optional<core::RectI> drawSceneRegion(const RenderScene& scene);
    [[nodiscard]] HRESULT present(const core::RectI& changed);

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
    ComPtr<ID3D11DeviceContext1> context1_;  // ClearView(부분 지우기). 없으면 전체 지우기
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
    unsigned sampleCount_ = 1;  // 실제로 쓰는 MSAA 샘플 수 (1 = 끔)
    // 3D 영역 텍스처 (용량 = regionCapacity_). MSAA면 msaaTarget_에 그리고 regionTexture_로 resolve
    core::SizeI regionCapacity_;
    int regionSmallFrames_ = 0;
    // 바뀐 부분만 지우기·복사·합성 (Present1 dirty rect). 플립 모델 백버퍼 2개라 이번에 받은
    // 백버퍼는 2프레임 전 내용 → 직전·이번 프레임에 바뀐 곳을 모두 다시 복사해야 함
    core::RectI lastRegion_;  // 프레임 텍스처에 지난번 3D를 복사한 곳 (이번에 지울 곳)
    core::RectI lastChanged_;  // 직전 Present에서 바뀐 곳
    bool partialReady_ = false;  // false면 전체 지우기·복사 (첫 프레임, 크기 변경, 슬라임)  //
                                 // 용량보다 많이 작은 영역만 연속으로 쓴 프레임 수
    ComPtr<ID3D11Texture2D> regionTexture_;
    ComPtr<ID3D11RenderTargetView> regionView_;
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
