#include "renderer/d3d11/WicTexture.h"

#include "renderer/d3d11/DxCheck.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace deskpet::renderer::d3d11 {
namespace {

// 창이 수백 px라 원본(최대 2048²)을 그대로 올리면 메모리만 차지합니다 (NFR-PERF-02).
// 긴 변을 이 크기 이하로 줄여 디코딩합니다. RGBA8 512² + 밉맵 ≈ 1.3MB.
constexpr UINT kMaxTextureSize = 512;

// 섬 바깥 여백을 채울 폭 (축소된 텍스처 기준 텍셀). 밉 단계 k는 2^k 텍셀을 평균하므로
// 16이면 밉 4단계(512 → 32)까지 여백 색이 섞이지 않음. 더 작은 밉은 화면에서 몇 픽셀이라 무시
constexpr int kPaddingTexels = 16;

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

namespace {

struct Pixels {
    UINT width = 0;
    UINT height = 0;
    std::vector<BYTE> rgba;
    int keptTexels = 0;  // 여백 채우기에서 점·선 삼각형 때문에 보존한 텍셀 수
};

// CPU 단계: 디코딩 → 긴 변 512 이하로 축소 → RGBA8 → UV 섬 바깥 여백 채우기. GPU를 쓰지 않으므로
// 작업 스레드에서도 부를 수 있음 (그 스레드에서 COM 초기화 필요)
std::optional<Pixels> preparePixels(IWICImagingFactory* wic, const model::Texture& image,
                                    std::span<const model::UvTriangle> islands,
                                    core::TexturePadding padding) {
    const ComPtr<IWICBitmapSource> decoded =
        image.rgba.empty() ? decode(wic, image.encoded) : wrapPixels(wic, image);
    if (!decoded) {
        return std::nullopt;
    }

    Pixels out;
    if (!check(decoded->GetSize(&out.width, &out.height), "이미지 크기") || out.width == 0 ||
        out.height == 0) {
        return std::nullopt;
    }

    IWICBitmapSource* source = decoded.Get();
    ComPtr<IWICBitmapScaler> scaler;
    if (out.width > kMaxTextureSize || out.height > kMaxTextureSize) {
        const double scale = static_cast<double>(kMaxTextureSize) /
                             static_cast<double>(std::max(out.width, out.height));
        out.width = std::max(1U, static_cast<UINT>(out.width * scale));
        out.height = std::max(1U, static_cast<UINT>(out.height * scale));
        if (!check(wic->CreateBitmapScaler(&scaler), "스케일러 생성") ||
            !check(scaler->Initialize(decoded.Get(), out.width, out.height,
                                      WICBitmapInterpolationModeFant),
                   "이미지 축소")) {
            return std::nullopt;
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
        return std::nullopt;
    }
    source = toRgba.Get();

    const UINT stride = out.width * 4;
    out.rgba.resize(static_cast<std::size_t>(stride) * out.height);
    if (!check(source->CopyPixels(nullptr, stride, static_cast<UINT>(out.rgba.size()),
                                  out.rgba.data()),
               "CopyPixels")) {
        return std::nullopt;
    }
    // 밉맵을 만들기 전에 해야 작은 밉 단계에도 여백 색(보통 검정)이 섞이지 않음
    out.keptTexels =
        model::padUvIslands(out.rgba, static_cast<int>(out.width), static_cast<int>(out.height),
                            islands, kPaddingTexels, padding);
    return out;
}

// GPU 단계: 텍스처 생성 + 0번 밉 업로드 + 밉맵 생성 (즉시 컨텍스트라 렌더 스레드에서만)
ComPtr<ID3D11ShaderResourceView> upload(ID3D11Device* device, ID3D11DeviceContext* context,
                                        UINT width, UINT height, const BYTE* rgba) {
    // GenerateMips를 쓰려면 RENDER_TARGET 바인딩과 GENERATE_MIPS 플래그가 필요합니다.
    // 밉맵이 없으면 큰 텍스처를 작은 창에 그릴 때 지글거림(앨리어싱)이 심합니다.
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 0;  // 0 = 전체 밉 체인
    desc.ArraySize = 1;
    // _SRGB: 샘플링할 때 하드웨어가 선형 값으로 바꿔 줌 (셰이더는 선형 공간에서 계산, ADR-0012).
    // 밉맵도 선형 공간에서 평균해 작은 밉이 어두워지지 않음. 모델 텍스처는 모두 색 텍스처
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
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
    context->UpdateSubresource(texture.Get(), 0, nullptr, rgba, width * 4, 0);
    context->GenerateMips(view.Get());
    return view;
}

}  // namespace

ComPtr<ID3D11ShaderResourceView> createTexture(IWICImagingFactory* wic, ID3D11Device* device,
                                               ID3D11DeviceContext* context,
                                               const model::Texture& image,
                                               std::span<const model::UvTriangle> islands) {
    if (image.prepared) {  // 작업 스레드에서 이미 CPU 단계를 끝냄 (prepareTextures)
        const auto width = static_cast<UINT>(image.width);
        const auto height = static_cast<UINT>(image.height);
        if (width == 0 || height == 0 ||
            image.rgba.size() != static_cast<std::size_t>(width) * height * 4U) {
            return nullptr;
        }
        return upload(device, context, width, height, image.rgba.data());
    }
    const std::optional<Pixels> pixels =
        preparePixels(wic, image, islands, core::TexturePadding::Auto);
    if (!pixels) {
        return nullptr;
    }
    return upload(device, context, pixels->width, pixels->height, pixels->rgba.data());
}

int prepareTextures(model::Model& model, core::TexturePadding padding) {
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&wic)))) {
        return 0;  // 이 스레드에 COM이 없음 → 렌더러가 그릴 때 처리
    }
    int prepared = 0;
    for (std::size_t i = 0; i < model.textures.size(); ++i) {
        model::Texture& texture = model.textures[i];
        if (texture.prepared || texture.empty()) {
            continue;
        }
        const std::vector<model::UvTriangle> islands =
            model::collectUvTriangles(model, static_cast<int>(i));
        std::optional<Pixels> pixels = preparePixels(wic.Get(), texture, islands, padding);
        if (!pixels) {
            continue;  // 렌더러가 다시 시도하고 실패를 로그에 남김
        }
        if (pixels->keptTexels > 0) {
            core::logging::info("텍스처 {}: 점·선으로 찍어 쓰는 색 {}텍셀을 여백 채우기에서 보존",
                                texture.name, pixels->keptTexels);
        }
        texture.width = static_cast<int>(pixels->width);
        texture.height = static_cast<int>(pixels->height);
        texture.rgba = std::move(pixels->rgba);
        texture.encoded.clear();
        texture.encoded.shrink_to_fit();
        texture.prepared = true;
        ++prepared;
    }
    return prepared;
}

}  // namespace deskpet::renderer::d3d11
