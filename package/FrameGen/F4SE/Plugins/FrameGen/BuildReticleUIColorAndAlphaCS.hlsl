Texture2D<float4> ReticleColor    : register(t0);
Texture2D<float4> PreReticleColor : register(t1);

RWTexture2D<float4> OutputUIColorAndAlpha : register(u0);

// FO4's reticle draws as wireframe anti-aliased lines.  The per-pixel
// colour delta on bracket arms can be as low as 2/255 due to AA blending.
// Use a lower floor than the main UI shader because reticle-only frames
// have zero scene motion noise (the game is paused during reticle draw).
static const float kReticleLo = 1.5 / 255.0;
static const float kReticleHi = 8.0 / 255.0;

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
	uint width, height;
	ReticleColor.GetDimensions(width, height);
	if (DTid.x >= width || DTid.y >= height) {
		return;
	}

	int2 pixel = int2(DTid.xy);

	float3 diff = abs(ReticleColor[pixel].rgb - PreReticleColor[pixel].rgb);
	float l1Diff = dot(diff, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0));
	float alpha = smoothstep(kReticleLo, kReticleHi, l1Diff);

	// Crosshair elements are limited to the central region of the screen.
	// The bracket arms extend to ~35% of screen size in either dimension.
	// Use an elliptical mask instead of a box mask for smoother falloff at
	// the bracket arm tips — avoids sharp rectangular cutoff.
	float2 center = (float2(width, height) - 1.0) * 0.5;
	float2 centerDist = abs(float2(pixel) - center);
	float radius = min(float(width), float(height)) * 0.38;
	float distNorm = length(centerDist / radius);
	float centerMask = 1.0 - smoothstep(0.85, 1.0, distNorm);

	alpha *= centerMask;

	// RGB = 0 — main UI shader computes PM colour from Final / HUDLess.
	OutputUIColorAndAlpha[DTid.xy] = float4(0.0, 0.0, 0.0, alpha);
}
