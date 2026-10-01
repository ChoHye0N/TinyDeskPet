#pragma once

// VRM 정적 메시를 D3D11로 그리는 패스 (ADR-0008, renderer.md §4.5)
// 불투명·마스크 프리미티브를 먼저, 반투명(BLEND)을 나중에 그립니다.

#include "renderer/d3d11/IRenderPass.h"

#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <vector>
#include <wincodec.h>

namespace deskpet::renderer::d3d11 {

class MeshPass final : public IRenderPass {
public:
    [[nodiscard]] std::string_view name() const override { return "MeshPass"; }
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

    // 업로드한 모델
    const model::Model* uploadedModel_ = nullptr;
    ComPtr<ID3D11Buffer> vertexBuffer_;
    ComPtr<ID3D11Buffer> indexBuffer_;
    std::vector<ComPtr<ID3D11ShaderResourceView>> textures_;  // model.textures와 같은 순서
    std::vector<GpuPrimitive> drawOrder_;                     // 불투명 → 반투명

    // 애니메이션 (ADR-0010)
    ComPtr<ID3D11Buffer> skinBuffer_;  // 본별 스킨 행렬 (구조화 버퍼, 매 프레임 갱신)
    ComPtr<ID3D11ShaderResourceView> skinView_;
    std::uint32_t skinCapacity_ = 0;
    ComPtr<ID3D11Buffer> morphBuffer_;  // 정점별 표정 오프셋 합 (두 번째 정점 스트림)
    std::vector<core::Vec3> morphOffsets_;  // CPU 쪽 계산 공간 (할당 재사용)
    std::array<float, static_cast<std::size_t>(model::Expression::Count)> lastExpressions_{};
    bool morphDirty_ = true;
};

}  // namespace deskpet::renderer::d3d11
