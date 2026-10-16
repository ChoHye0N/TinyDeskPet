// 화면 실루엣 외곽선 (renderer.md §4.x): 3D 영역 텍스처를 프레임 텍스처의 그 자리로 옮기면서, 캐릭터
// 바깥(알파가 0인 곳)에서 두께 안에 캐릭터 픽셀이 있으면 외곽선 색을 깔아 줌. 캐릭터 안쪽은 그대로라
// 눈·입 같은 안쪽 선이 생기지 않음. 텍스처는 premultiplied alpha (ADR-0001)

cbuffer SilhouetteConstants : register(b0)
{
    int2 regionOffset;  // 프레임 텍스처에서 영역의 왼쪽 위 (px)
    int2 regionSize;    // 영역 크기 (px). 영역 텍스처의 왼쪽 위만 유효
    float width;        // 외곽선 두께 (px)
    float3 color;       // 외곽선 색 (sRGB, 프레임 텍스처와 같은 공간)
};

Texture2D<float4> regionTexture : register(t0);

// 화면 전체를 덮는 삼각형 하나 (정점 버퍼 없이 SV_VertexID로). 뷰포트가 영역으로 잘라 줌
float4 VSMain(uint id : SV_VertexID) : SV_Position
{
    const float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}

// 영역 밖은 투명으로 봄 (영역 텍스처 용량이 더 커도 나머지는 지난 프레임 내용일 수 있음)
float alphaAt(int2 p)
{
    if (any(p < 0) || any(p >= regionSize))
    {
        return 0.0f;
    }
    return regionTexture.Load(int3(p, 0)).a;
}

float4 PSMain(float4 position : SV_Position) : SV_Target
{
    const int2 p = int2(position.xy) - regionOffset;
    const float4 c = regionTexture.Load(int3(p, 0));
    if (c.a >= 0.999f)
    {
        return c;  // 완전히 캐릭터인 픽셀은 그대로 (안쪽에는 선을 그리지 않음)
    }

    // 둘레 16방향 × 고리 셋(두께, 2/3, 1/3)에서 가장 진한 캐릭터 알파. 바깥 고리는 반만 세어
    // 외곽선 바깥 가장자리가 계단처럼 보이지 않게 부드럽게
    static const int kDirections = 16;
    float edge = 0.0f;
    [unroll] for (int i = 0; i < kDirections; ++i)
    {
        float s, cs;
        sincos(6.2831853f * (float)i / (float)kDirections, s, cs);
        const float2 d = float2(cs, s);
        edge = max(edge, alphaAt(p + int2(round(d * width))) * 0.5f);
        edge = max(edge, alphaAt(p + int2(round(d * width * 0.66f))));
        edge = max(edge, alphaAt(p + int2(round(d * width * 0.33f))));
    }
    edge = saturate(edge);

    // 캐릭터(premultiplied) 아래에 외곽선을 깔아 합침: 결과 = 캐릭터 + (1 − 캐릭터 알파) × 선
    const float under = (1.0f - c.a) * edge;
    return float4(c.rgb + color * under, c.a + under);
}
