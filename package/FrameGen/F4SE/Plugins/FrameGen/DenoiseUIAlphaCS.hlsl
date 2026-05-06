// Post-processing pass on the UI alpha buffer.
// Suppresses single-frame false positives from TAA jitter / D3D compression
// while preserving persistent UI elements.
//
// Strategy: apply a cross-bilateral-like filter on the alpha channel alone.
// UI elements form spatial clusters of 3+ connected pixels; jitter noise is
// isolated single pixels.  Require at least 1 of the 4 orthogonal neighbours
// to also have UI-level alpha, otherwise zero out the alpha.

RWTexture2D<float4> UIColorAndAlpha : register(u0);

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
	uint width, height;
	UIColorAndAlpha.GetDimensions(width, height);
	if (DTid.x >= width || DTid.y >= height) {
		return;
	}

	int2 pixel = int2(DTid.xy);
	float4 current = UIColorAndAlpha[pixel];

	float nAlpha = 0.0;

	int2 offsets[4] = { int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1) };
	for (int i = 0; i < 4; ++i) {
		int2 np = pixel + offsets[i];
		if (np.x >= 0 && np.y >= 0 && (uint)np.x < width && (uint)np.y < height) {
			float4 neighbour = UIColorAndAlpha[np];
			nAlpha = max(nAlpha, neighbour.a);
		}
	}

	// If no neighbour has UI-level alpha (>kUILo/255), this is isolated noise
	static const float kNoiseFloor = 3.0 / 255.0;
	if (current.a > 0.0 && nAlpha < kNoiseFloor) {
		// Remove the alpha — this pixel was a false positive
		current.a = 0.0;
		// Since PM colour was computed with this false alpha, restore to zero
		current.rgb = float3(0.0, 0.0, 0.0);
	}

	UIColorAndAlpha[DTid.xy] = current;
}
