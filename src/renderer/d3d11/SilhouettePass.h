#pragma once

// 화면 실루엣 외곽선 (renderer.md §4.x). 3D 영역 텍스처를 프레임 텍스처로 옮기는 복사 대신,
// 셰이더로 옮기면서 캐릭터 바깥 테두리에만 외곽선을 깔아 줌. 장면 패스(IRenderPass)가 아니라
// 렌더러가 영역을 그린 뒤 직접 부름 (입력이 영역 텍스처, 출력이 프레임 텍스처)

#include "core/Math.h"
#include "core/Math3D.h"

#include <d3d11.h>
#include <wrl/client.h>

namespace deskpet::renderer::d3d11 {

class SilhouettePass {
public:
    [[nodiscard]] bool create(ID3D11Device* device);
    void release();

    // region(프레임 텍스처의 영역 자리)에 regionTexture(왼쪽 위 region 크기만 유효)를 옮기며 두께
    // width(px)의 외곽선을 그림. frameTarget이 출력, 블렌드 없이 덮어씀 (복사와 같은 결과 + 외곽선)
    void draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* frameTarget,
              ID3D11ShaderResourceView* regionTexture, const core::RectI& region, float width,
              core::Vec3 color) const;

private:
    template <class T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    ComPtr<ID3D11VertexShader> vertexShader_;
    ComPtr<ID3D11PixelShader> pixelShader_;
    ComPtr<ID3D11Buffer> constants_;
    ComPtr<ID3D11BlendState> noBlend_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11DepthStencilState> noDepth_;
};

}  // namespace deskpet::renderer::d3d11
