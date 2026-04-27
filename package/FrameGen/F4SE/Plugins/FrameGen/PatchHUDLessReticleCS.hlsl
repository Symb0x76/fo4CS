Texture2D<float4> HUDLessColor : register(t0);
Texture2D<float4> ReticleBackgroundAndAlpha : register(t1);

RWTexture2D<float4> OutputHUDLessColor : register(u0);

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
	int2 pixel = int2(DTid.xy);
	float4 hudlessColor = HUDLessColor[pixel];
	float4 reticleBackgroundAndAlpha = ReticleBackgroundAndAlpha[pixel];
	float alpha = saturate(reticleBackgroundAndAlpha.a);

	OutputHUDLessColor[DTid.xy] = float4(lerp(hudlessColor.rgb, reticleBackgroundAndAlpha.rgb, alpha), hudlessColor.a);
}
