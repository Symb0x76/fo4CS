Texture2D<float4> FinalColor : register(t0);
Texture2D<float4> HUDLessColor : register(t1);
Texture2D<float4> ReticleAlpha : register(t2);

RWTexture2D<float4> OutputUIColorAndAlpha : register(u0);

// HUDLess is captured from kFrameBuffer at PreAlpha time (post-processing
// complete, no reticle/UI drawn yet).  FinalColor is the same buffer at
// Present time.  The difference is UI contribution only — no post-process
// noise.  Low threshold is safe.
static const float kUILo = 4.0 / 255.0;
static const float kUIHi = 16.0 / 255.0;

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
	int2 pixel = int2(DTid.xy);
	uint width, height;
	FinalColor.GetDimensions(width, height);
	if (DTid.x >= width || DTid.y >= height) {
		return;
	}

	float3 diff = abs(FinalColor[pixel].rgb - HUDLessColor[pixel].rgb);
	float maxDiff = max(diff.r, max(diff.g, diff.b));
	float detected = smoothstep(kUILo, kUIHi, maxDiff);

	// Reticle buffer provides backup detection for faint/thin crosshair
	// elements that the per-pixel diff might miss (e.g. 1-px lines).
	float alpha = max(detected, ReticleAlpha[pixel].a);

	// Pre-multiplied UI colour:
	//   Final = UI_pm + (1-a)·HUDLess  ⇒  UI_pm = Final - (1-a)·HUDLess
	// Correct for all pixels on kFrameBuffer (which includes the reticle).
	float3 hudless = HUDLessColor[pixel].rgb;
	float3 premultiplied = FinalColor[pixel].rgb - (1.0 - alpha) * hudless;
	premultiplied = max(premultiplied, 0.0);

	OutputUIColorAndAlpha[DTid.xy] = float4(premultiplied, alpha);
}
