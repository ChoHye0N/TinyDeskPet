#include "renderer/d3d11/MeshPass.h"

#include "core/Log.h"
#include "renderer/d3d11/DxCheck.h"
#include "renderer/d3d11/WicTexture.h"

// fxc가 빌드 시 생성하는 바이트코드 배열 (d3d11/CMakeLists.txt)
#include "g_MeshOutlinePS.h"
#include "g_MeshPS.h"
#include "g_MeshVS.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <limits>

namespace deskpet::renderer::d3d11 {
namespace {

// HLSL cbuffer와 1:1로 맞춰야 하는 구조체. 상수 버퍼 크기는 16바이트의 배수여야 합니다.
struct FrameConstants {
    core::Mat4 viewProjection;
    core::Vec3 lightDirection;
    std::uint32_t boneCount = 0;  // 0이면 셰이더가 스키닝을 건너뜀
    core::Vec3 viewDirection;
    float padding = 0.0f;
};
static_assert(sizeof(core::Mat4) == 64);
static_assert(sizeof(FrameConstants) % 16 == 0);

// float3 다음에 float 하나를 붙여 16바이트 경계를 맞춤 (HLSL 패킹 규칙: 4성분이 16바이트를
// 넘어가는 멤버는 다음 레지스터로 넘어감)
struct MaterialConstants {
    core::Vec4 baseColor;
    float alphaCutoff = -1.0f;
    float hasTexture = 0.0f;
    float forceOpaque = 1.0f;
    float outlineWidth = 0.0f;  // 외곽선 패스에서만 0보다 큼 (m)
    core::Vec4 outlineColor;
    core::Vec3 shadeColor;
    float hasShadeTexture = 0.0f;
    float shadingShift = 0.0f;
    float shadingToony = 0.0f;
    float rimFresnelPower = 1.0f;
    float rimLift = 0.0f;
    core::Vec3 rimColor;
    float rimLightingMix = 0.0f;
    core::Vec3 matcapColor;
    float hasMatcapTexture = 0.0f;
    core::Vec3 emissiveColor;
    float hasEmissiveTexture = 0.0f;
};
static_assert(sizeof(MaterialConstants) == 128);

// MToon이 아닌 재질의 기본 2단 툰: 그림자 면 = 기본색 × 0.53 (선형) ≈ 예전 감마 공간 0.75배,
// 경계 폭 ±0.05 (toony 0.95)
constexpr float kDefaultShade = 0.53f;
constexpr float kDefaultToony = 0.95f;

// 입력 레이아웃은 model::Vertex 메모리 배치와 같아야 합니다.
static_assert(sizeof(model::Vertex) == 56);
static_assert(offsetof(model::Vertex, normal) == 12);
static_assert(offsetof(model::Vertex, u) == 24);
static_assert(offsetof(model::Vertex, joints) == 32);
static_assert(offsetof(model::Vertex, weights) == 40);
static_assert(sizeof(core::Vec3) == 12);

// 스트림 0: 정점 데이터(불변), 스트림 1: 표정 오프셋(표정이 바뀔 때만 CPU가 다시 씀).
// 자주 바뀌는 데이터를 별도 버퍼로 나누면 큰 정점 버퍼는 IMMUTABLE로 둘 수 있음
const D3D11_INPUT_ELEMENT_DESC kInputLayout[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 1, DXGI_FORMAT_R32G32B32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

template <class T>
bool writeConstants(ID3D11DeviceContext* context, ID3D11Buffer* buffer, const T& value) {
    // WRITE_DISCARD: GPU가 이전 내용을 쓰는 중이어도 기다리지 않고 새 메모리를 받음
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return false;
    }
    std::memcpy(mapped.pData, &value, sizeof(T));
    context->Unmap(buffer, 0);
    return true;
}

}  // namespace

bool MeshPass::create(const D3D11Context& ctx) {
    ID3D11Device* device = ctx.device;

    if (!wic_ && !check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                         IID_PPV_ARGS(&wic_)),
                        "WIC 팩토리 생성")) {
        return false;
    }

    if (!check(device->CreateVertexShader(g_MeshVS, sizeof(g_MeshVS), nullptr, &vertexShader_),
               "CreateVertexShader") ||
        !check(device->CreatePixelShader(g_MeshPS, sizeof(g_MeshPS), nullptr, &pixelShader_),
               "CreatePixelShader") ||
        !check(device->CreatePixelShader(g_MeshOutlinePS, sizeof(g_MeshOutlinePS), nullptr,
                                         &outlineShader_),
               "CreatePixelShader(외곽선)") ||
        // 입력 레이아웃은 정점 셰이더 입력 시그니처와 대조되므로 VS 바이트코드가 필요합니다.
        !check(device->CreateInputLayout(kInputLayout, static_cast<UINT>(std::size(kInputLayout)),
                                         g_MeshVS, sizeof(g_MeshVS), &inputLayout_),
               "CreateInputLayout")) {
        return false;
    }

    D3D11_BUFFER_DESC cb{};
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cb.ByteWidth = sizeof(FrameConstants);
    if (!check(device->CreateBuffer(&cb, nullptr, &frameConstants_), "프레임 상수 버퍼")) {
        return false;
    }
    cb.ByteWidth = sizeof(MaterialConstants);
    if (!check(device->CreateBuffer(&cb, nullptr, &materialConstants_), "머티리얼 상수 버퍼")) {
        return false;
    }

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (!check(device->CreateSamplerState(&sampler, &sampler_), "샘플러")) {
        return false;
    }

    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_BACK;
    // glTF는 반시계(CCW)가 앞면. 오른손 뷰·투영 행렬을 쓰면 화면에서도 CCW로 남습니다.
    // (D3D 기본값은 시계 방향이 앞면이라, 그대로 두면 앞면이 잘려 나감)
    raster.FrontCounterClockwise = TRUE;
    raster.DepthClipEnable = TRUE;
    if (!check(device->CreateRasterizerState(&raster, &cullBack_), "래스터라이저(컬링)")) {
        return false;
    }
    raster.CullMode = D3D11_CULL_NONE;  // doubleSided 머티리얼 (머리카락, 치마 등)
    if (!check(device->CreateRasterizerState(&raster, &cullNone_), "래스터라이저(양면)")) {
        return false;
    }
    // 외곽선: 앞면을 잘라 부풀린 껍데기의 안쪽(뒷면)만 그림. 몸 앞쪽은 원래 메시가 깊이로
    // 가리므로 실루엣과 겹친 경계에서만 테두리가 보임
    raster.CullMode = D3D11_CULL_FRONT;
    if (!check(device->CreateRasterizerState(&raster, &cullFront_), "래스터라이저(외곽선)")) {
        return false;
    }

    // premultiplied alpha 합성: out = src + dst × (1 - srcA). 알파 채널도 같은 식으로 누적해야
    // DirectComposition이 바탕화면과 올바르게 섞습니다 (ADR-0001).
    D3D11_BLEND_DESC blend{};
    auto& rt = blend.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlend = rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (!check(device->CreateBlendState(&blend, &premultipliedBlend_), "블렌드 상태")) {
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D11_COMPARISON_LESS;
    if (!check(device->CreateDepthStencilState(&depth, &depthWrite_), "깊이 상태(쓰기)")) {
        return false;
    }
    // 반투명은 뒤의 물체를 가리지 않도록 깊이를 쓰지 않음 (테스트만)
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    return check(device->CreateDepthStencilState(&depth, &depthReadOnly_), "깊이 상태(읽기)");
}

void MeshPass::release() {
    releaseModel();
    depthView_.Reset();
    depthSize_ = {};
    depthReadOnly_.Reset();
    depthWrite_.Reset();
    premultipliedBlend_.Reset();
    cullFront_.Reset();
    cullNone_.Reset();
    cullBack_.Reset();
    outlineShader_.Reset();
    sampler_.Reset();
    materialConstants_.Reset();
    frameConstants_.Reset();
    inputLayout_.Reset();
    pixelShader_.Reset();
    vertexShader_.Reset();
}

void MeshPass::releaseModel() {
    uploadedModel_ = nullptr;
    skinView_.Reset();
    skinBuffer_.Reset();
    skinCapacity_ = 0;
    morphBuffer_.Reset();
    morphOffsets_.clear();
    morphDirty_ = true;
    vertexBuffer_.Reset();
    indexBuffer_.Reset();
    textures_.clear();
    drawOrder_.clear();
}

bool MeshPass::upload(const D3D11Context& ctx, const model::Model& model) {
    constexpr std::size_t kMaxBytes = std::numeric_limits<UINT>::max();
    if (model.vertices.size() * sizeof(model::Vertex) > kMaxBytes ||
        model.indices.size() * sizeof(std::uint32_t) > kMaxBytes) {
        core::logging::error("모델이 너무 커서 GPU 버퍼를 만들 수 없습니다");
        return false;
    }

    // IMMUTABLE: 생성 후 바뀌지 않는 데이터. 드라이버가 GPU 전용 메모리에 둘 수 있습니다.
    D3D11_BUFFER_DESC desc{};
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.ByteWidth = static_cast<UINT>(model.vertices.size() * sizeof(model::Vertex));
    D3D11_SUBRESOURCE_DATA data{model.vertices.data(), 0, 0};
    if (!check(ctx.device->CreateBuffer(&desc, &data, &vertexBuffer_), "정점 버퍼")) {
        return false;
    }
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    desc.ByteWidth = static_cast<UINT>(model.indices.size() * sizeof(std::uint32_t));
    data.pSysMem = model.indices.data();
    if (!check(ctx.device->CreateBuffer(&desc, &data, &indexBuffer_), "인덱스 버퍼")) {
        return false;
    }

    textures_.resize(model.textures.size());
    for (std::size_t i = 0; i < model.textures.size(); ++i) {
        const model::Texture& texture = model.textures[i];
        const std::vector<model::UvTriangle> islands =
            model::collectUvTriangles(model, static_cast<int>(i));
        textures_[i] = createTexture(wic_.Get(), ctx.device, ctx.context, texture, islands);
        if (!textures_[i]) {
            core::logging::warn("텍스처 {} ({}) {} → 흰색으로 대체", i, texture.name,
                                texture.empty() ? "파일 없음" : "디코딩 실패");
        }
    }

    // 불투명·마스크를 먼저, 반투명을 나중에 (반투명은 뒤에 있는 것이 먼저 그려져 있어야 섞임)
    for (const bool blendPass : {false, true}) {
        for (const model::Primitive& p : model.primitives) {
            const model::Material* material =
                p.material >= 0 && static_cast<std::size_t>(p.material) < model.materials.size()
                    ? &model.materials[static_cast<std::size_t>(p.material)]
                    : nullptr;
            const bool blend =
                material != nullptr && material->alphaMode == model::AlphaMode::Blend;
            if (blend == blendPass) {
                drawOrder_.push_back(
                    {p.firstIndex, p.indexCount, material != nullptr ? p.material : -1, blend,
                     material != nullptr && material->doubleSided, outlineWidthFor(material)});
            }
        }
    }

    if (!createAnimationBuffers(ctx, model)) {
        return false;
    }

    const auto outlined = std::ranges::count_if(
        drawOrder_, [](const GpuPrimitive& p) { return p.outlineWidth > 0.0f; });
    core::logging::info("모델 GPU 업로드: 정점 {}, 삼각형 {}, 텍스처 {}, 드로우 {} (외곽선 {})",
                        model.vertices.size(), model.indices.size() / 3, textures_.size(),
                        drawOrder_.size(), outlined);
    return true;
}

bool MeshPass::createAnimationBuffers(const D3D11Context& ctx, const model::Model& model) {
    // 본이 없는 모델도 셰이더가 같은 경로를 타도록 최소 1개
    skinCapacity_ = static_cast<std::uint32_t>(std::max<std::size_t>(model.bones.size(), 1));

    D3D11_BUFFER_DESC skin{};
    skin.Usage = D3D11_USAGE_DYNAMIC;
    skin.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    skin.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    skin.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;  // HLSL StructuredBuffer<T>
    skin.StructureByteStride = sizeof(core::Mat4);
    skin.ByteWidth = skinCapacity_ * static_cast<UINT>(sizeof(core::Mat4));

    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_UNKNOWN;  // 구조화 버퍼는 형식 없음
    view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    view.Buffer.NumElements = skinCapacity_;

    D3D11_BUFFER_DESC morph{};
    morph.Usage = D3D11_USAGE_DYNAMIC;
    morph.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    morph.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    morph.ByteWidth = static_cast<UINT>(model.vertices.size() * sizeof(core::Vec3));

    morphOffsets_.assign(model.vertices.size(), core::Vec3{});
    const D3D11_SUBRESOURCE_DATA zeros{morphOffsets_.data(), 0, 0};
    morphDirty_ = true;
    return check(ctx.device->CreateBuffer(&skin, nullptr, &skinBuffer_), "스킨 버퍼") &&
           check(ctx.device->CreateShaderResourceView(skinBuffer_.Get(), &view, &skinView_),
                 "스킨 SRV") &&
           check(ctx.device->CreateBuffer(&morph, &zeros, &morphBuffer_), "표정 버퍼");
}

bool MeshPass::updateSkin(ID3D11DeviceContext* context, const MeshCharacter& character) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(skinBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return false;
    }
    auto* out = static_cast<core::Mat4*>(mapped.pData);
    const std::size_t count = std::min<std::size_t>(character.skinMatrices.size(), skinCapacity_);
    std::copy_n(character.skinMatrices.begin(), count, out);
    std::fill(out + count, out + skinCapacity_, core::Mat4::identity());  // 부족분은 바인드 포즈
    context->Unmap(skinBuffer_.Get(), 0);
    return true;
}

// 표정 가중치가 바뀐 프레임에만 오프셋 = Σ 가중치 × 델타를 다시 계산해 올림 (깜빡임은 가끔)
bool MeshPass::updateMorphs(ID3D11DeviceContext* context, const model::Model& model,
                            const MeshCharacter& character) {
    if (!morphDirty_ && character.expressionWeights == lastExpressions_) {
        return true;
    }
    std::ranges::fill(morphOffsets_, core::Vec3{});
    for (std::size_t e = 0; e < character.expressionWeights.size(); ++e) {
        const float weight = character.expressionWeights[e];
        const model::Morph& morph = model.expressions[e];
        if (weight == 0.0f) {
            continue;
        }
        for (std::size_t i = 0; i < morph.vertices.size(); ++i) {
            if (morph.vertices[i] < morphOffsets_.size()) {
                core::Vec3& offset = morphOffsets_[morph.vertices[i]];
                offset = offset + morph.deltas[i] * weight;
            }
        }
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(morphBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return false;
    }
    std::memcpy(mapped.pData, morphOffsets_.data(), morphOffsets_.size() * sizeof(core::Vec3));
    context->Unmap(morphBuffer_.Get(), 0);
    lastExpressions_ = character.expressionWeights;
    morphDirty_ = false;
    return true;
}

bool MeshPass::ensureDepthBuffer(const D3D11Context& ctx) {
    if (depthView_ && depthSize_ == ctx.viewport && depthSamples_ == ctx.sampleCount) {
        return true;
    }
    depthView_.Reset();

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(ctx.viewport.width);
    desc.Height = static_cast<UINT>(ctx.viewport.height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = ctx.sampleCount;  // 렌더 타깃과 샘플 수가 다르면 그리기 실패
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ComPtr<ID3D11Texture2D> texture;
    if (!check(ctx.device->CreateTexture2D(&desc, nullptr, &texture), "깊이 버퍼") ||
        !check(ctx.device->CreateDepthStencilView(texture.Get(), nullptr, &depthView_),
               "깊이 뷰")) {
        return false;
    }
    depthSize_ = ctx.viewport;
    depthSamples_ = ctx.sampleCount;
    return true;
}

bool MeshPass::execute(const D3D11Context& ctx, const RenderScene& scene) {
    const model::Model* model = scene.character.model;
    if (model == nullptr) {
        return true;
    }
    if (model != uploadedModel_) {
        releaseModel();
        uploadedModel_ = model;  // 실패해도 매 프레임 재시도하지 않도록 기록
        if (!upload(ctx, *model)) {
            core::logging::error("모델을 GPU에 올리지 못했습니다");
            vertexBuffer_.Reset();
        }
    }
    if (!vertexBuffer_) {
        return true;
    }
    if (!ensureDepthBuffer(ctx)) {
        return false;
    }

    ID3D11DeviceContext* context = ctx.context;
    FrameConstants frame;
    frame.viewProjection = scene.character.viewProjection * ctx.clipTransform;
    frame.lightDirection = scene.character.lightDirection;
    frame.boneCount = scene.character.skinMatrices.empty() ? 0U : skinCapacity_;
    frame.viewDirection = scene.character.viewDirection;
    if (!writeConstants(context, frameConstants_.Get(), frame)) {
        return false;
    }

    ID3D11RenderTargetView* renderTarget = ctx.sceneTarget;
    context->OMSetRenderTargets(1, &renderTarget, depthView_.Get());
    context->ClearDepthStencilView(depthView_.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    context->OMSetBlendState(premultipliedBlend_.Get(), nullptr, 0xFFFFFFFFU);

    if (!updateSkin(context, scene.character) || !updateMorphs(context, *model, scene.character)) {
        return false;
    }

    const std::array<UINT, 2> strides = {sizeof(model::Vertex), sizeof(core::Vec3)};
    const std::array<UINT, 2> offsets = {0, 0};
    const std::array<ID3D11Buffer*, 2> vertexBuffers = {vertexBuffer_.Get(), morphBuffer_.Get()};
    context->IASetInputLayout(inputLayout_.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetVertexBuffers(0, 2, vertexBuffers.data(), strides.data(), offsets.data());
    context->IASetIndexBuffer(indexBuffer_.Get(), DXGI_FORMAT_R32_UINT, 0);

    ID3D11Buffer* constants[] = {frameConstants_.Get(), materialConstants_.Get()};
    ID3D11SamplerState* sampler = sampler_.Get();
    context->VSSetShader(vertexShader_.Get(), nullptr, 0);
    // VS도 b1(재질)이 필요: 외곽선 굵기만큼 정점을 밀어냄. 빠지면 굵기 0으로 읽혀 외곽선이 안 보임
    context->VSSetConstantBuffers(0, 2, constants);
    ID3D11ShaderResourceView* skinView = skinView_.Get();
    context->VSSetShaderResources(0, 1, &skinView);
    context->PSSetShader(pixelShader_.Get(), nullptr, 0);
    context->PSSetConstantBuffers(0, 2, constants);
    context->PSSetSamplers(0, 1, &sampler);

    // 불투명·마스크 → 외곽선 → 반투명. 외곽선은 깊이를 쓰므로 반투명보다 먼저 그려야
    // 반투명 뒤에 비쳐 보임
    for (const GpuPrimitive& primitive : drawOrder_) {
        if (!primitive.blend) {
            drawPrimitive(ctx, *model, primitive);
        }
    }
    const bool anyOutline = std::ranges::any_of(
        drawOrder_, [](const GpuPrimitive& p) { return p.outlineWidth > 0.0f; });
    if (anyOutline) {
        context->PSSetShader(outlineShader_.Get(), nullptr, 0);
        for (const GpuPrimitive& primitive : drawOrder_) {
            if (primitive.outlineWidth > 0.0f) {
                drawOutline(ctx, *model, primitive);
            }
        }
        context->PSSetShader(pixelShader_.Get(), nullptr, 0);
    }
    for (const GpuPrimitive& primitive : drawOrder_) {
        if (primitive.blend) {
            drawPrimitive(ctx, *model, primitive);
        }
    }

    // 다음 패스(Direct2D)가 깊이 버퍼에 묶이지 않도록 원래 상태로 되돌림
    const std::array<ID3D11ShaderResourceView*, kMaterialTextures> nullViews{};
    context->PSSetShaderResources(0, kMaterialTextures, nullViews.data());
    context->VSSetShaderResources(0, 1, nullViews.data());
    context->OMSetRenderTargets(1, &renderTarget, nullptr);
    return true;
}

void MeshPass::drawPrimitive(const D3D11Context& ctx, const model::Model& model,
                             const GpuPrimitive& primitive) {
    ID3D11DeviceContext* context = ctx.context;

    static const model::Material kDefaultMaterial;
    const model::Material& material =
        primitive.material >= 0 ? model.materials[static_cast<std::size_t>(primitive.material)]
                                : kDefaultMaterial;

    // t0 기본색, t1 그림자, t2 MatCap, t3 발광
    std::array<ID3D11ShaderResourceView*, kMaterialTextures> views{
        textureView(material.baseColorTexture), textureView(material.shadeTexture),
        textureView(material.matcapTexture), textureView(material.emissiveTexture)};

    MaterialConstants constants;
    constants.baseColor = material.baseColor;
    constants.alphaCutoff =
        material.alphaMode == model::AlphaMode::Mask ? material.alphaCutoff : -1.0f;
    constants.forceOpaque = material.alphaMode == model::AlphaMode::Blend ? 0.0f : 1.0f;
    if (material.mtoon) {
        constants.shadeColor = material.shadeColor;
        constants.shadingShift = material.shadingShift;
        constants.shadingToony = material.shadingToony;
        constants.rimColor = material.rimColor;
        constants.rimFresnelPower = material.rimFresnelPower;
        constants.rimLift = material.rimLift;
        constants.rimLightingMix = material.rimLightingMix;
        constants.matcapColor = material.matcapColor;
    } else {
        // 그림자 = 기본색(텍스처 포함)을 어둡게. unlit이면 그림자 없음
        const float shade = material.unlit ? 1.0f : kDefaultShade;
        constants.shadeColor =
            core::Vec3{material.baseColor.x, material.baseColor.y, material.baseColor.z} * shade;
        constants.shadingToony = kDefaultToony;
        views[1] = views[0];
        views[2] = nullptr;
    }
    constants.emissiveColor = material.emissiveColor;
    constants.hasTexture = views[0] != nullptr ? 1.0f : 0.0f;
    constants.hasShadeTexture = views[1] != nullptr ? 1.0f : 0.0f;
    constants.hasMatcapTexture = views[2] != nullptr ? 1.0f : 0.0f;
    constants.hasEmissiveTexture = views[3] != nullptr ? 1.0f : 0.0f;
    if (!writeConstants(context, materialConstants_.Get(), constants)) {
        return;
    }

    context->PSSetShaderResources(0, kMaterialTextures, views.data());
    context->RSSetState(primitive.doubleSided ? cullNone_.Get() : cullBack_.Get());
    context->OMSetDepthStencilState(primitive.blend ? depthReadOnly_.Get() : depthWrite_.Get(), 0);
    context->DrawIndexed(primitive.indexCount, primitive.firstIndex, 0);
}

// Model::textures 번호 → SRV. 없거나 읽기에 실패했으면 nullptr
ID3D11ShaderResourceView* MeshPass::textureView(int index) const {
    return index >= 0 && static_cast<std::size_t>(index) < textures_.size()
               ? textures_[static_cast<std::size_t>(index)].Get()
               : nullptr;
}

// 재질별 외곽선 굵기 (m). 반투명은 뒤가 비쳐야 하므로 외곽선 없음
float MeshPass::outlineWidthFor(const model::Material* material) const {
    // outline = all일 때 모델에 정보가 없는 재질에 쓰는 굵기 (MToon 기본값과 비슷한 4mm)
    constexpr float kDefaultOutlineWidth = 0.004f;
    if (material == nullptr || material->alphaMode == model::AlphaMode::Blend) {
        return 0.0f;
    }
    switch (outlineMode_) {
        case core::OutlineMode::Off: return 0.0f;
        case core::OutlineMode::All:
            return material->outlineWidth > 0.0f ? material->outlineWidth : kDefaultOutlineWidth;
        case core::OutlineMode::Model:
        default: return material->outlineWidth;
    }
}

void MeshPass::drawOutline(const D3D11Context& ctx, const model::Model& model,
                           const GpuPrimitive& primitive) {
    ID3D11DeviceContext* context = ctx.context;
    const model::Material& material = model.materials[static_cast<std::size_t>(primitive.material)];

    MaterialConstants constants;
    constants.baseColor = material.baseColor;
    constants.alphaCutoff =
        material.alphaMode == model::AlphaMode::Mask ? material.alphaCutoff : -1.0f;
    constants.outlineWidth = primitive.outlineWidth;
    constants.outlineColor = material.outlineColor;
    ID3D11ShaderResourceView* texture = textureView(material.baseColorTexture);
    constants.hasTexture = texture != nullptr ? 1.0f : 0.0f;
    if (!writeConstants(context, materialConstants_.Get(), constants)) {
        return;
    }
    context->PSSetShaderResources(0, 1, &texture);
    context->RSSetState(cullFront_.Get());
    context->OMSetDepthStencilState(depthWrite_.Get(), 0);
    context->DrawIndexed(primitive.indexCount, primitive.firstIndex, 0);
}

}  // namespace deskpet::renderer::d3d11
