#pragma once

// VRM 정적 메시를 D3D11로 그리는 패스 (ADR-0008, renderer.md §4.5)
// 불투명·마스크 프리미티브를 먼저, 반투명(BLEND)을 나중에 그립니다.

#include "core/Config.h"
#include "renderer/d3d11/IRenderPass.h"

#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
#include <wincodec.h>

namespace deskpet::renderer::d3d11 {

class MeshPass final : public IRenderPass {
public:
    explicit MeshPass(core::OutlineMode outline) : outlineMode_(outline) {}

    [[nodiscard]] std::string_view name() const override { return "MeshPass"; }
    // 외곽선 대상 바꾸기 (설정 창). 다음 execute에서 재질별 굵기를 다시 계산 (다시 올리지 않음)
    void setOutlineMode(core::OutlineMode mode) noexcept { outlineMode_ = mode; }
    // 외곽선 두께 배율 (설정 창). 그릴 때 곱하므로 바로 적용
    void setOutlineScale(float scale) noexcept { outlineScale_ = std::max(scale, 0.0f); }
    [[nodiscard]] bool create(const D3D11Context& ctx) override;
    [[nodiscard]] bool execute(const D3D11Context& ctx, const RenderScene& scene) override;
    void release() override;

private:
    template <class T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct GpuPrimitive {
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
        int material = -1;
        bool blend = false;
        bool doubleSided = false;
        float outlineWidth = 0.0f;  // 0이면 외곽선 없음 (outlineMode_ 반영 후)
        model::OutlineSkip outlineSkip = model::OutlineSkip::None;
    };

    [[nodiscard]] bool upload(const D3D11Context& ctx, const model::Model& model);
    [[nodiscard]] bool createAnimationBuffers(const D3D11Context& ctx, const model::Model& model);
    [[nodiscard]] bool updateSkin(ID3D11DeviceContext* context, const MeshCharacter& character);
    [[nodiscard]] bool updateMorphs(ID3D11DeviceContext* context, const model::Model& model,
                                    const MeshCharacter& character);
    [[nodiscard]] bool ensureDepthBuffer(const D3D11Context& ctx);
    void releaseModel();
    void drawPrimitive(const D3D11Context& ctx, const model::Model& model,
                       const GpuPrimitive& primitive);
    void drawOutline(const D3D11Context& ctx, const model::Model& model,
                     const GpuPrimitive& primitive);
    // skip: "모든 재질"에서 뺄 primitive (눈·입·헤일로, model::markOutlineSkips)
    [[nodiscard]] float outlineWidthFor(const model::Material* material,
                                        model::OutlineSkip skip) const;
    [[nodiscard]] ID3D11ShaderResourceView* textureView(int index) const;

    static constexpr UINT kMaterialTextures = 4;  // 기본색, 그림자, MatCap, 발광 (t0 ~ t3)

    // 디바이스 독립
    ComPtr<IWICImagingFactory> wic_;

    // 파이프라인 상태
    ComPtr<ID3D11VertexShader> vertexShader_;
    ComPtr<ID3D11PixelShader> pixelShader_;
    ComPtr<ID3D11InputLayout> inputLayout_;
    ComPtr<ID3D11Buffer> frameConstants_;
    ComPtr<ID3D11Buffer> materialConstants_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11RasterizerState> cullBack_;
    ComPtr<ID3D11RasterizerState> cullNone_;
    ComPtr<ID3D11BlendState> premultipliedBlend_;
    ComPtr<ID3D11DepthStencilState> depthWrite_;
    ComPtr<ID3D11DepthStencilState> depthReadOnly_;

    // 깊이 버퍼 (창 크기를 따라감)
    ComPtr<ID3D11DepthStencilView> depthView_;
    core::SizeI depthSize_;
    unsigned depthSamples_ = 0;  // 깊이 버퍼 샘플 수 (sceneTarget과 같아야 함)

    // 업로드한 모델
    const model::Model* uploadedModel_ = nullptr;
    ComPtr<ID3D11Buffer> vertexBuffer_;
    ComPtr<ID3D11Buffer> indexBuffer_;
    std::vector<ComPtr<ID3D11ShaderResourceView>> textures_;  // model.textures와 같은 순서
    core::OutlineMode outlineMode_;
    float outlineScale_ = 1.0f;
    core::OutlineMode drawOrderOutline_ = core::OutlineMode::Model;  // drawOrder_ 굵기를 계산한 값
    ComPtr<ID3D11PixelShader> outlineShader_;
    ComPtr<ID3D11RasterizerState> cullFront_;  // 외곽선(반전 헐): 부풀린 껍데기의 뒷면만
    std::vector<GpuPrimitive> drawOrder_;  // 불투명 → 반투명

    // 애니메이션 (ADR-0010)
    ComPtr<ID3D11Buffer> skinBuffer_;  // 본별 스킨 행렬 (구조화 버퍼, 매 프레임 갱신)
    ComPtr<ID3D11ShaderResourceView> skinView_;
    std::uint32_t skinCapacity_ = 0;
    ComPtr<ID3D11Buffer> morphBuffer_;  // 정점별 표정 오프셋 합 (두 번째 정점 스트림)
    std::vector<core::Vec3> morphOffsets_;  // CPU 쪽 계산 공간 (할당 재사용)
    std::array<float, static_cast<std::size_t>(model::Expression::Count)> lastExpressions_{};
    std::vector<float> lastMorphWeights_;
    bool morphDirty_ = true;
};

}  // namespace deskpet::renderer::d3d11
