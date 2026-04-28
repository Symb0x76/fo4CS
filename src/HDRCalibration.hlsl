cbuffer HDRCalibrationCB : register(b0)
{
    float peakNits;
    float paperWhiteNits;
    float scRGBReferenceNits;
    uint hdrMode;
    uint showClippingWarning;
    uint showColorBars;
    float2 padding;
};

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

float3 EncodeNits(float3 nits)
{
    if (hdrMode == 2) {
        return LinearToPQ(nits, peakNits);
    }
    return nits / max(scRGBReferenceNits, 1.0);
}

float GridLine(float2 uv)
{
    float2 grid = abs(frac(uv * float2(10.0, 8.0)) - 0.5);
    return step(0.485, max(grid.x, grid.y)) * 0.08;
}

float4 PSMain(VSOut input) : SV_Target
{
    float2 uv = saturate(input.uv);
    float3 nits = 0.0;

    if (uv.y < 0.22) {
        float blackNits = pow(saturate(uv.x), 2.2) * 5.0;
        nits = blackNits.xxx;
    } else if (uv.y < 0.48) {
        float rampNits = saturate(uv.x) * peakNits;
        nits = rampNits.xxx;
    } else if (uv.y < 0.72 && showColorBars != 0) {
        float bar = floor(saturate(uv.x) * 7.0);
        float3 bars[7] = {
            float3(1.0, 1.0, 1.0),
            float3(1.0, 0.0, 0.0),
            float3(0.0, 1.0, 0.0),
            float3(0.0, 0.0, 1.0),
            float3(0.0, 1.0, 1.0),
            float3(1.0, 0.0, 1.0),
            float3(1.0, 1.0, 0.0)
        };
        nits = bars[(uint)min(bar, 6.0)] * paperWhiteNits;
    } else {
        float2 center = abs(uv - float2(0.5, 0.86));
        float patch = step(center.x, 0.18) * step(center.y, 0.09);
        float background = lerp(20.0, peakNits * 1.1, saturate(uv.x));
        nits = lerp(background.xxx, paperWhiteNits.xxx, patch);
        if (showClippingWarning != 0 && background > peakNits) {
            nits = lerp(nits, float3(peakNits, 0.0, 0.0), 0.75);
        }
    }

    nits += GridLine(uv) * paperWhiteNits;
    return float4(EncodeNits(nits), 1.0);
}
