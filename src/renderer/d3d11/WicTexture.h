#pragma once

// model::Texture → D3D11 텍스처 (WIC 디코딩·축소 + 밉맵 생성)
// 전제: 호출 스레드에서 COM이 초기화되어 있어야 합니다 (main_win32.cpp의 CoInitializeEx).

#include "model/Model.h"
#include "model/TexturePadding.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <span>
#include <wincodec.h>

namespace deskpet::renderer::d3d11 {

// encoded(PNG/JPEG 등)는 WIC로 디코딩하고, rgba(TGA 등 미리 푼 픽셀)는 그대로 씁니다.
// islands: 이 텍스처를 쓰는 삼각형의 UV — 축소 후 밉맵 전에 섬 바깥 여백을 채움 (이음매 선 방지)
// 실패하거나 비어 있으면 nullptr (원인은 로그에 남김)
[[nodiscard]] Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> createTexture(
    IWICImagingFactory* wic, ID3D11Device* device, ID3D11DeviceContext* context,
    const model::Texture& image, std::span<const model::UvTriangle> islands);

}  // namespace deskpet::renderer::d3d11
