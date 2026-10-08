// 캐릭터 메시 셰이더: GPU 스키닝 + 표정(모프) + MToon 툰 셰이딩 (ADR-0008, ADR-0010, ADR-0012).
// 빌드 시 fxc로 바이트코드 헤더가 됩니다 (d3d11/CMakeLists.txt).
// 색공간: 텍스처는 _SRGB 뷰로 읽어 선형 값으로 계산하고, 출력 직전에 sRGB로 바꿉니다.
// 렌더 타깃을 _SRGB로 두지 않는 이유: 하드웨어 변환은 알파를 곱한 값(c·a)을 변환해
// DWM이 기대하는 "sRGB 색 × a"와 달라짐 → 반투명 가장자리가 밝게 뜸 (ADR-0012)

// row_major: C++ 쪽 행렬이 행 우선 저장 + 행 벡터 규약(core/Math3D.h)이라 mul(v, M)로 그대로 씁니다.
// (HLSL 상수 버퍼의 기본 패킹은 column_major라, 지정하지 않으면 전치된 행렬로 읽힘)
cbuffer FrameConstants : register(b0)
{
    row_major float4x4 viewProjection;
    float3 lightDirection;  // 빛이 진행하는 방향 (모델 공간)
    uint boneCount;         // 0이면 스키닝 없이 바인드 포즈
    float3 viewDirection;   // 모델 → 카메라 방향 (모델 공간). 화각이 좁아 방향 하나로 근사
    float framePadding;
};

// 색은 모두 선형 공간. 값의 의미는 model::Material (VRM 1.0 MToon 정의)
cbuffer MaterialConstants : register(b1)
{
    float4 baseColor;
    float alphaCutoff;  // 0 미만이면 알파 테스트 안 함
    float hasTexture;   // bool 대신 float: 상수 버퍼의 bool은 4바이트라 C++ bool(1바이트)과 어긋나기 쉬움
    float forceOpaque;  // OPAQUE/MASK는 알파를 1로
    float outlineWidth; // 외곽선 패스에서만 0보다 큼: 법선 방향으로 밀어낼 거리 (m)
    float4 outlineColor;
    float3 shadeColor;
    float hasShadeTexture;
    float shadingShift;
    float shadingToony;
    float rimFresnelPower;
    float rimLift;
    float3 rimColor;
    float rimLightingMix;
    float3 matcapColor;
    float hasMatcapTexture;
    float3 emissiveColor;
    float hasEmissiveTexture;
};

// 본별 스킨 행렬. 본 수가 수백 개일 수 있어 상수 버퍼(최대 4096 float4) 대신 구조화 버퍼를 씀.
// 구조체로 감싸야 row_major 지정이 적용됨. 레지스터는 셰이더 단계마다 따로라 VS t0와 PS t0는 별개
struct SkinMatrix
{
    row_major float4x4 m;
};
StructuredBuffer<SkinMatrix> skinMatrices : register(t0);

Texture2D baseTexture : register(t0);
Texture2D shadeTexture : register(t1);
Texture2D matcapTexture : register(t2);
Texture2D emissiveTexture : register(t3);
SamplerState linearWrap : register(s0);

// 선형 → sRGB (IEC 61966-2-1). 벡터에 쓰는 ?:는 성분별로 고름
float3 linearToSrgb(float3 c)
{
    c = saturate(c);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * pow(c, 1.0f / 2.4f) - 0.055f;
}

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    uint4 joints : BLENDINDICES;     // R16G16B16A16_UINT → uint4로 확장되어 들어옴
    float4 weights : BLENDWEIGHT;
    float3 morphOffset : POSITION1;  // 두 번째 정점 스트림: 표정 오프셋 합 (CPU가 갱신)
};

struct PSInput
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

PSInput VSMain(VSInput input)
{
    // 표정(모프)을 먼저 더한 뒤 스키닝: 모프 델타는 바인드 포즈 기준이기 때문
    float4 position = float4(input.position + input.morphOffset, 1.0f);
    float3 normal = input.normal;

    // 선형 블렌드 스키닝: 행렬을 가중 평균한 뒤 한 번 곱함
    const float total = dot(input.weights, float4(1.0f, 1.0f, 1.0f, 1.0f));
    if (boneCount > 0 && total > 0.0f)
    {
        const float4x4 skin = input.weights.x * skinMatrices[input.joints.x].m +
                              input.weights.y * skinMatrices[input.joints.y].m +
                              input.weights.z * skinMatrices[input.joints.z].m +
                              input.weights.w * skinMatrices[input.joints.w].m;
        position = mul(position, skin);
        normal = mul(normal, (float3x3)skin);  // 균등 회전만 있으므로 역전치 없이 사용
    }

    // 외곽선 패스(반전 헐): 법선 방향으로 부풀린 껍데기를 뒷면만 그림 → 원래 몸 바깥으로
    // 삐져나온 테두리만 보임. 메인 패스는 outlineWidth = 0. 길이 0인 법선은 NaN 방지
    position.xyz += normal * rsqrt(max(dot(normal, normal), 1e-8f)) * outlineWidth;

    PSInput output;
    output.position = mul(position, viewProjection);
    output.normal = normal;
    output.uv = input.uv;
    return output;
}

float4 PSMain(PSInput input, bool frontFace : SV_IsFrontFace) : SV_Target
{
    float4 color = baseColor;
    if (hasTexture > 0.5f)
    {
        color *= baseTexture.Sample(linearWrap, input.uv);
    }
    if (alphaCutoff >= 0.0f && color.a < alphaCutoff)
    {
        discard;
    }
    if (forceOpaque > 0.5f)
    {
        color.a = 1.0f;
    }

    // 양면 머티리얼의 뒷면은 법선을 뒤집어야 조명이 맞음
    const float3 normal = normalize(input.normal) * (frontFace ? 1.0f : -1.0f);
    const float3 view = normalize(viewDirection);

    // MToon 그림자: 밝기 = linearstep(−1 + toony, 1 − toony, N·L + shift)로 그림자 색 ↔ 기본색.
    // MToon이 아닌 재질도 같은 식 (그림자 색 = 기본색을 어둡게, 경계 좁게 → 2단 툰)
    float3 shade = shadeColor;
    if (hasShadeTexture > 0.5f)
    {
        shade *= shadeTexture.Sample(linearWrap, input.uv).rgb;
    }
    const float edge0 = -1.0f + shadingToony;
    const float edge1 = 1.0f - shadingToony;
    const float shading = saturate((dot(normal, -normalize(lightDirection)) + shadingShift - edge0) /
                                   max(edge1 - edge0, 1e-4f));
    float3 lit = lerp(shade, color.rgb, shading);

    // 림: 시선과 비스듬한 면(윤곽 쪽)을 밝힘 + MatCap(시점 기준 법선으로 구 텍스처를 찍음)
    float3 rim = rimColor * pow(saturate(1.0f - dot(normal, view) + rimLift), max(rimFresnelPower, 1e-4f));
    if (hasMatcapTexture > 0.5f)
    {
        const float3 right = normalize(cross(float3(0.0f, 1.0f, 0.0f), view));
        const float3 up = cross(view, right);
        const float2 uv = float2(dot(normal, right), -dot(normal, up)) * 0.5f + 0.5f;
        rim += matcapColor * matcapTexture.Sample(linearWrap, uv).rgb;
    }
    lit += rim * lerp(1.0f, shading, rimLightingMix);

    float3 emissive = emissiveColor;
    if (hasEmissiveTexture > 0.5f)
    {
        emissive *= emissiveTexture.Sample(linearWrap, input.uv).rgb;
    }
    lit += emissive;

    // 스왑체인이 premultiplied alpha(ADR-0001)이므로 sRGB로 바꾼 RGB에 알파를 곱해 출력
    return float4(linearToSrgb(lit) * color.a, color.a);
}

// 외곽선 색 (조명 없음). 마스크 재질(머리카락 끝 등)은 잘린 모양대로 외곽선도 잘라야
// 사각형 카드 모양 테두리가 생기지 않음
float4 PSOutline(PSInput input) : SV_Target
{
    if (alphaCutoff >= 0.0f && hasTexture > 0.5f &&
        baseColor.a * baseTexture.Sample(linearWrap, input.uv).a < alphaCutoff)
    {
        discard;
    }
    return float4(linearToSrgb(outlineColor.rgb) * outlineColor.a, outlineColor.a);
}
