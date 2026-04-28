cbuffer HDRColorSpaceCB : register(b0)
{
    uint2 dimensions;
    float peakNits;
    float paperWhiteNits;
    float scRGBReferenceNits;
    uint hdrMode;
    float padding;
};

Texture2D<float4> SourceTexture : register(t0);

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint vertexID : SV_VertexID)
{
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0,  3.0),
        float2( 3.0, -1.0)
    };
    const float2 uvs[3] = {
        float2(0.0, 1.0),
        float2(0.0, -1.0),
        float2(2.0, 1.0)
    };

    VSOut output;
    output.position = float4(positions[vertexID], 0.0, 1.0);
    output.uv = uvs[vertexID];
    return output;
}

float3 sRGBToLinear(float3 srgb)
{
    float3 lo = srgb / 12.92;
    float3 hi = pow((srgb + 0.055) / 1.055, 2.4);
    return lerp(lo, hi, step(0.04045, srgb));
}

static const float3x3 Rec709ToRec2020 = {
    { 0.6274, 0.3293, 0.0433 },
    { 0.0691, 0.9195, 0.0114 },
    { 0.0164, 0.0880, 0.8956 }
};

float3 LinearToPQ(float3 linearNits, float peakNitsValue)
{
    float3 y = saturate(linearNits / max(peakNitsValue, 1.0));
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    float3 yPow = pow(y, m1);
    return pow((c1 + c2 * yPow) / (1.0 + c3 * yPow), m2);
}

float4 PSMain(VSOut input) : SV_Target
{
    uint2 coord = uint2(input.uv * float2(dimensions));
    float4 source = SourceTexture.Load(int3(coord, 0));

    float3 linearColor = sRGBToLinear(saturate(source.rgb));

    if (hdrMode == 1)
    {
        float scRGBScale = paperWhiteNits / max(scRGBReferenceNits, 1.0);
        return float4(linearColor * scRGBScale, source.a);
    }

    float3 rec2020 = mul(Rec709ToRec2020, linearColor);
    float3 pq = LinearToPQ(rec2020 * paperWhiteNits, peakNits);
    return float4(pq, source.a);
}
