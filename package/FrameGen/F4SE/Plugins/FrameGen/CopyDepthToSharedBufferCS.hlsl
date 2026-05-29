Texture2D<float> InputTexture : register(t0);
RWTexture2D<float> OutputTexture : register(u0);

[numthreads(8, 8, 1)] void main(uint3 DTid
								: SV_DispatchThreadID) {
	uint inputWidth = 0;
	uint inputHeight = 0;
	uint outputWidth = 0;
	uint outputHeight = 0;
	InputTexture.GetDimensions(inputWidth, inputHeight);
	OutputTexture.GetDimensions(outputWidth, outputHeight);
	if (DTid.x >= inputWidth || DTid.y >= inputHeight || DTid.x >= outputWidth || DTid.y >= outputHeight) {
		return;
	}

	OutputTexture[DTid.xy] = InputTexture[DTid.xy];
}
