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
// texture.prepared면 CPU 단계를 건너뛰고 rgba를 그대로 올림
[[nodiscard]] Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> createTexture(
    IWICImagingFactory* wic, ID3D11Device* device, ID3D11DeviceContext* context,
    const model::Texture& image, std::span<const model::UvTriangle> islands);

// CPU 단계(디코딩·축소·여백 채우기)를 미리 해서 텍스처를 최종 RGBA로 바꿈 (prepared = true).
// GPU를 쓰지 않아 모델을 읽는 작업 스레드에서 부름 → 렌더 스레드는 올리기만 해 멈춤이 짧음.
// 호출 스레드에 COM이 초기화되어 있어야 하며, 없거나 실패한 텍스처는 그대로 둠 (렌더러가 처리).
// 반환: 준비한 텍스처 수
// padding: 여백 채우기 방식 (설정 [renderer] texture_padding). 점·선 삼각형 때문에 보존한 텍셀이
// 있으면 로그에 남김
int prepareTextures(model::Model& model, core::TexturePadding padding = core::TexturePadding::Auto);

}  // namespace deskpet::renderer::d3d11
