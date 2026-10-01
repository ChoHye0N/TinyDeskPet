#include "renderer/d3d11/WicTexture.h"

#include "renderer/d3d11/DxCheck.h"

#include <algorithm>
#include <vector>

namespace deskpet::renderer::d3d11 {
namespace {

// 창이 수백 px라 원본(최대 2048²)을 그대로 올리면 메모리만 차지합니다 (NFR-PERF-02).
// 긴 변을 이 크기 이하로 줄여 디코딩합니다. RGBA8 512² + 밉맵 ≈ 1.3MB.
constexpr UINT kMaxTextureSize = 512;

}  // namespace

using Microsoft::WRL::ComPtr;

namespace {

// 인코딩된 이미지 → RGBA 8비트 WIC 소스. 메모리 스트림 → 디코더 → 첫 프레임 → 포맷 변환
ComPtr<IWICBitmapSource> decode(IWICImagingFactory* wic, const std::vector<std::uint8_t>& encoded) {
    if (encoded.empty() || encoded.size() > UINT32_MAX) {
        return nullptr;
    }
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (!check(wic->CreateStream(&stream), "IWICStream 생성") ||
        !check(stream->InitializeFromMemory(const_cast<BYTE*>(encoded.data()),
                                            static_cast<DWORD>(encoded.size())),
               "InitializeFromMemory") ||
        !check(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand,
                                            &decoder),
               "이미지 디코더 생성") ||
        !check(decoder->GetFrame(0, &frame), "이미지 프레임") ||
        !check(wic->CreateFormatConverter(&converter), "포맷 변환기 생성") ||
        !check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom),
               "RGBA 변환")) {
        return nullptr;
    }
    return converter;  // 팔레트·그레이 PNG 등도 RGBA로 통일됨
}

// 이미 RGBA로 풀린 픽셀 → WIC 비트맵 (축소 경로를 인코딩된 이미지와 공유하기 위해)
ComPtr<IWICBitmapSource> wrapPixels(IWICImagingFactory* wic, const model::Texture& texture) {
    const auto width = static_cast<UINT>(texture.width);
    const auto height = static_cast<UINT>(texture.height);
    if (width == 0 || height == 0 ||
        texture.rgba.size() != static_cast<std::size_t>(width) * height * 4U) {
        return nullptr;
    }
    ComPtr<IWICBitmap> bitmap;
    if (!check(wic->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppRGBA, width * 4,
                                           static_cast<UINT>(texture.rgba.size()),
                                           const_cast<BYTE*>(texture.rgba.data()), &bitmap),
               "CreateBitmapFromMemory")) {
        return nullptr;
    }
    return bitmap;
}

}  // namespace

ComPtr<ID3D11ShaderResourceView> createTexture(IWICImagingFactory* wic, ID3D11Device* device,
                                               ID3D11DeviceContext* context,
                                               const model::Texture& image) {
    const ComPtr<IWICBitmapSource> decoded =
        image.rgba.empty() ? decode(wic, image.encoded) : wrapPixels(wic, image);
    if (!decoded) {
        return nullptr;
    }

    UINT width = 0;
    UINT height = 0;
    if (!check(decoded->GetSize(&width, &height), "이미지 크기") || width == 0 || height == 0) {
        return nullptr;
    }

    IWICBitmapSource* source = decoded.Get();
    ComPtr<IWICBitmapScaler> scaler;
    if (width > kMaxTextureSize || height > kMaxTextureSize) {
        const double scale =
            static_cast<double>(kMaxTextureSize) / static_cast<double>(std::max(width, height));
        width = std::max(1U, static_cast<UINT>(width * scale));
        height = std::max(1U, static_cast<UINT>(height * scale));
        if (!check(wic->CreateBitmapScaler(&scaler), "스케일러 생성") ||
            !check(scaler->Initialize(decoded.Get(), width, height, WICBitmapInterpolationModeFant),
                   "이미지 축소")) {
            return nullptr;
        }
        source = scaler.Get();
    }

    // 스케일러(특히 Fant)는 내부 처리용으로 픽셀 형식을 BGRA 계열로 바꿔 내보낼 수 있습니다.
    // 그대로 복사하면 R과 B가 뒤바뀌므로(파란 피부) 마지막에 RGBA로 한 번 더 고정합니다.
    ComPtr<IWICFormatConverter> toRgba;
    if (!check(wic->CreateFormatConverter(&toRgba), "포맷 변환기 생성") ||
        !check(toRgba->Initialize(source, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                  nullptr, 0.0, WICBitmapPaletteTypeCustom),
               "RGBA 변환")) {
        return nullptr;
    }
    source = toRgba.Get();

    const UINT stride = width * 4;
    std::vector<BYTE> pixels(static_cast<std::size_t>(stride) * height);
    if (!check(source->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data()),
               "CopyPixels")) {
        return nullptr;
    }

    // GenerateMips를 쓰려면 RENDER_TARGET 바인딩과 GENERATE_MIPS 플래그가 필요합니다.
    // 밉맵이 없으면 큰 텍스처를 작은 창에 그릴 때 지글거림(앨리어싱)이 심합니다.
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 0;  // 0 = 전체 밉 체인
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    if (!check(device->CreateTexture2D(&desc, nullptr, &texture), "텍스처 생성") ||
        !check(device->CreateShaderResourceView(texture.Get(), nullptr, &view), "SRV 생성")) {
        return nullptr;
    }
    // 밉 체인이 있는 텍스처는 생성 시 초기 데이터로 0번 밉만 줄 수 없어 UpdateSubresource로 올림
    context->UpdateSubresource(texture.Get(), 0, nullptr, pixels.data(), stride, 0);
    context->GenerateMips(view.Get());
    return view;
}

}  // namespace deskpet::renderer::d3d11
