Texture2D<float4> FinalColor   : register(t0);
Texture2D<float4> HUDLessColor : register(t1);
Texture2D<float4> ReticleAlpha : register(t2);

RWTexture2D<float4> OutputUIColorAndAlpha : register(u0);

// Tuned for FO4's HDR render targets: scene regions typically have <2/255
// L1-diff from TAA jitter alone, while UI elements produce 8+ difference.
// The wider smoothstep window (2→24) prevents false-positives from scene
// motion while still catching thin UI (compass ticks, ammo counter digits).
static const float kUILo = 2.0 / 255.0;
static const float kUIHi = 24.0 / 255.0;

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
	uint width, height;
	FinalColor.GetDimensions(width, height);
	if (DTid.x >= width || DTid.y >= height) {
		return;
	}

	int2 pixel = int2(DTid.xy);

	// --- Detection ---
	// Use L1-distance (sum of abs) instead of max-component.  For thin,
	// single-channel UI elements (e.g. red compass ticks) max-component can
	// miss detection if the colour change is spread across channels at low
	// intensity.  L1 captures total per-pixel delta more robustly.
	float3 diff = abs(FinalColor[pixel].rgb - HUDLessColor[pixel].rgb);
	float l1Diff = dot(diff, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0));
	float detected = smoothstep(kUILo, kUIHi, l1Diff);

	// --- Spatial neighbourhood check ---
	// Single-pixel detections (e.g. from D3D compression noise or TAA ghost)
	// should NOT produce alpha.  UI elements are at least 2-3 px wide.
	// Sample the 4-neighbour cross and require at least one neighbour also
	// above the LO threshold (not full HI — partial matches count).
	uint2 dims;
	FinalColor.GetDimensions(dims.x, dims.y);
	float nDiff = 0.0;
	[unroll] for (int dy = -1; dy <= 1; dy += 2) {
		int2 np = pixel + int2(0, dy);
		if (np.x >= 0 && np.y >= 0 && (uint)np.x < dims.x && (uint)np.y < dims.y) {
			float3 nd = abs(FinalColor[np].rgb - HUDLessColor[np].rgb);
			nDiff = max(nDiff, dot(nd, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0)));
		}
	}
	[unroll] for (int dx = -1; dx <= 1; dx += 2) {
		int2 np = pixel + int2(dx, 0);
		if (np.x >= 0 && np.y >= 0 && (uint)np.x < dims.x && (uint)np.y < dims.y) {
			float3 nd = abs(FinalColor[np].rgb - HUDLessColor[np].rgb);
			nDiff = max(nDiff, dot(nd, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0)));
		}
	}
	// Require at least one neighbour above kUILo threshold
	float neighbourSupport = smoothstep(kUILo * 0.8, kUILo * 1.5, nDiff);

	// Combine per-pixel detection with spatial confirmation
	float alpha = max(detected * neighbourSupport, ReticleAlpha[pixel].a);

	// --- Reconstruction ---
	// Pre-multiplied UI colour:
	//   Final = UI_pm + (1-a)*HUDLess  ⇒  UI_pm = Final - (1-a)*HUDLess
	float3 premultiplied = FinalColor[pixel].rgb - (1.0 - alpha) * HUDLessColor[pixel].rgb;
	premultiplied = max(premultiplied, 0.0);

	// Clamp alpha to avoid amplifying noise in very dim UI
	alpha = saturate(alpha);

	OutputUIColorAndAlpha[DTid.xy] = float4(premultiplied, alpha);
}
