#include "renderer/d3d11/SilhouettePass.h"

#include "renderer/d3d11/DxCheck.h"

// fxc가 빌드 때 만든 셰이더 바이트코드
#include "g_SilhouettePS.h"
#include "g_SilhouetteVS.h"

#include <cstring>

namespace deskpet::renderer::d3d11 {
namespace {

// Silhouette.hlsl의 cbuffer와 같은 배치 (16바이트 정렬)
struct SilhouetteConstants {
    std::int32_t regionOffset[2];
    std::int32_t regionSize[2];
    float width;
    float color[3];
};
static_assert(sizeof(SilhouetteConstants) % 16 == 0);

}  // namespace

bool SilhouettePass::create(ID3D11Device* device) {
    if (!check(device->CreateVertexShader(g_SilhouetteVS, sizeof(g_SilhouetteVS), nullptr,
                                          &vertexShader_),
               "실루엣 정점 셰이더") ||
        !check(device->CreatePixelShader(g_SilhouettePS, sizeof(g_SilhouettePS), nullptr,
                                         &pixelShader_),
               "실루엣 픽셀 셰이더")) {
        return false;
    }
    D3D11_BUFFER_DESC cb{};
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cb.ByteWidth = sizeof(SilhouetteConstants);
    if (!check(device->CreateBuffer(&cb, nullptr, &constants_), "실루엣 상수 버퍼")) {
        return false;
    }
    // 블렌드 없이 덮어씀: 영역 복사(CopySubresourceRegion)와 같은 결과를 내야 지난 프레임이 남지
    // 않음
    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    D3D11_DEPTH_STENCIL_DESC depth{};  // 깊이 끔 (프레임 텍스처에는 깊이 버퍼가 없음)
    return check(device->CreateBlendState(&blend, &noBlend_), "실루엣 블렌드") &&
           check(device->CreateRasterizerState(&raster, &raster_), "실루엣 래스터라이저") &&
           check(device->CreateDepthStencilState(&depth, &noDepth_), "실루엣 깊이");
}

void SilhouettePass::release() {
    noDepth_.Reset();
    raster_.Reset();
    noBlend_.Reset();
    constants_.Reset();
    pixelShader_.Reset();
    vertexShader_.Reset();
}

void SilhouettePass::draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* frameTarget,
                          ID3D11ShaderResourceView* regionTexture, const core::RectI& region,
                          float width, core::Vec3 color) const {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    const SilhouetteConstants constants{{region.left, region.top},
                                        {region.width(), region.height()},
                                        width,
                                        {color.x, color.y, color.z}};
    std::memcpy(mapped.pData, &constants, sizeof(constants));
    context->Unmap(constants_.Get(), 0);

    // 뷰포트를 영역으로: 화면 전체 삼각형이 영역 안 픽셀만 칠함
    const D3D11_VIEWPORT viewport{static_cast<float>(region.left),
                                  static_cast<float>(region.top),
                                  static_cast<float>(region.width()),
                                  static_cast<float>(region.height()),
                                  0.0f,
                                  1.0f};
    context->RSSetViewports(1, &viewport);
    context->OMSetRenderTargets(1, &frameTarget, nullptr);
    context->OMSetBlendState(noBlend_.Get(), nullptr, 0xFFFFFFFF);
    context->OMSetDepthStencilState(noDepth_.Get(), 0);
    context->RSSetState(raster_.Get());
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertexShader_.Get(), nullptr, 0);
    context->PSSetShader(pixelShader_.Get(), nullptr, 0);
    ID3D11Buffer* buffer = constants_.Get();
    context->PSSetConstantBuffers(0, 1, &buffer);
    context->PSSetShaderResources(0, 1, &regionTexture);
    context->Draw(3, 0);
    // 다음 프레임에 영역 텍스처를 렌더 타깃으로 쓰려면 셰이더 입력에서 빼야 함 (동시 바인딩 금지)
    ID3D11ShaderResourceView* none = nullptr;
    context->PSSetShaderResources(0, 1, &none);
}

}  // namespace deskpet::renderer::d3d11
