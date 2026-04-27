Texture2D<float4> InputTexturePreAlpha : register(t0);
Texture2D<float4> InputTextureAfterAlpha : register(t1);
Texture2D<float2> InputMotionVectors : register(t2);
Texture2D<float> InputDepth : register(t3);

RWTexture2D<float2> OutputMotionVectors : register(u0);
RWTexture2D<float> OutputDepth : register(u1);

[numthreads(8, 8, 1)] void main(uint3 DTid
								: SV_DispatchThreadID) {

	uint width = 0;
	uint height = 0;
	InputMotionVectors.GetDimensions(width, height);
	if (DTid.x >= width || DTid.y >= height) {
		return;
	}

	OutputMotionVectors[DTid.xy] = InputMotionVectors[DTid.xy];
	OutputDepth[DTid.xy] = InputDepth[DTid.xy];
}
