Texture2D<float4> ReticleColor : register(t0);
Texture2D<float4> PreReticleColor : register(t1);

RWTexture2D<float4> OutputUIColorAndAlpha : register(u0);

// Between PreAlpha and PostAlpha only the reticle is drawn, so the per-pixel
// difference IS the reticle.  Smooth detection catches thin anti-aliased
// lines (bracket corners) that a binary threshold would miss.
static const float kUILo = 3.0 / 255.0;
static const float kUIHi = 12.0 / 255.0;

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
	int2 pixel = int2(DTid.xy);
	uint width, height;
	ReticleColor.GetDimensions(width, height);
	if (DTid.x >= width || DTid.y >= height) {
		return;
	}

	float3 diff = abs(ReticleColor[pixel].rgb - PreReticleColor[pixel].rgb);
	float maxDiff = max(diff.r, max(diff.g, diff.b));

	// The crosshair transitions between a compact dot and a wide bracket
	// (corners up to ~350 px from centre at 1080p).  Use a generous mask
	// so the bracket arms are never clipped during the morph animation.
	float2 center = (float2(width, height) - 1.0) * 0.5;
	float2 centerDist = abs(float2(pixel) - center);
	float centerHalf = max(384.0, min((float)width, (float)height) * 0.35);
	float centerMask = (centerDist.x <= centerHalf && centerDist.y <= centerHalf) ? 1.0 : 0.0;

	float alpha = smoothstep(kUILo, kUIHi, maxDiff) * centerMask;

	// RGB = 0 — the main UI shader computes PM colour from Final / HUDLess.
	OutputUIColorAndAlpha[DTid.xy] = float4(0.0, 0.0, 0.0, alpha);
}
