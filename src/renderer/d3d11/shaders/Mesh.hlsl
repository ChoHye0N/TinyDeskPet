// 캐릭터 메시 셰이더: GPU 스키닝 + 표정(모프) + 2단 툰 (ADR-0008, ADR-0010).
// 빌드 시 fxc로 바이트코드 헤더가 됩니다 (d3d11/CMakeLists.txt).
// 색공간: 텍스처를 감마 공간 그대로 계산합니다. 선형 색공간 처리는 M5(MToon)에서.

// row_major: C++ 쪽 행렬이 행 우선 저장 + 행 벡터 규약(core/Math3D.h)이라 mul(v, M)로 그대로 씁니다.
// (HLSL 상수 버퍼의 기본 패킹은 column_major라, 지정하지 않으면 전치된 행렬로 읽힘)
cbuffer FrameConstants : register(b0)
{
    row_major float4x4 viewProjection;
    float3 lightDirection;  // 빛이 진행하는 방향 (모델 공간)
    uint boneCount;         // 0이면 스키닝 없이 바인드 포즈
};

cbuffer MaterialConstants : register(b1)
{
    float4 baseColor;
    float alphaCutoff;  // 0 미만이면 알파 테스트 안 함
    float hasTexture;   // bool 대신 float: 상수 버퍼의 bool은 4바이트라 C++ bool(1바이트)과 어긋나기 쉬움
    float forceOpaque;  // OPAQUE/MASK는 알파를 1로
    float outlineWidth; // 외곽선 패스에서만 0보다 큼: 법선 방향으로 밀어낼 거리 (m)
    float4 outlineColor;
};

// 본별 스킨 행렬. 본 수가 수백 개일 수 있어 상수 버퍼(최대 4096 float4) 대신 구조화 버퍼를 씀.
// 구조체로 감싸야 row_major 지정이 적용됨. 레지스터는 셰이더 단계마다 따로라 VS t0와 PS t0는 별개
struct SkinMatrix
{
    row_major float4x4 m;
};
StructuredBuffer<SkinMatrix> skinMatrices : register(t0);

Texture2D baseTexture : register(t0);
SamplerState linearWrap : register(s0);

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
    float3 normal = normalize(input.normal) * (frontFace ? 1.0f : -1.0f);
    float lit = dot(normal, -normalize(lightDirection));
    // 2단 툰: 밝은 면 1.0, 그림자 면 0.75. smoothstep으로 경계만 살짝 부드럽게
    color.rgb *= lerp(0.75f, 1.0f, smoothstep(-0.05f, 0.05f, lit));

    // 스왑체인이 premultiplied alpha(ADR-0001)이므로 RGB에 알파를 미리 곱해 출력
    color.rgb *= color.a;
    return color;
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
    return float4(outlineColor.rgb * outlineColor.a, outlineColor.a);
}
