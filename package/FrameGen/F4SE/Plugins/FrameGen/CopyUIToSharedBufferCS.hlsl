// Copies the engine's own UI render target (RenderTarget::kUI, slot 17) into the shared
// buffer DLSS-G reads as kBufferTypeUIColorAndAlpha.
//
// This replaces BuildUIColorAndAlphaCS, which reconstructed the UI layer by differencing
// the presented backbuffer against the HUDLess capture and thresholding the result. That
// reconstruction was a classifier, and DLSS-G treats its alpha as "do not interpolate", so
// each class of error became a distinct artifact: a missed UI pixel flickered, and a scene
// pixel wrongly tagged as UI froze at frame N's position against the interpolated
// background. No threshold separates the two. The engine's target carries the real UI with
// the game's own alpha, so there is nothing left to classify.
//
// Alpha is forwarded unchanged. The engine draws UI into a target cleared to (0,0,0,0),
// which accumulates premultiplied colour -- the convention DLSS-G expects. If in-game
// verification shows dark haloes around HUD edges, the source is straight alpha instead and
// the store below becomes float4(ui.rgb * ui.a, ui.a).

Texture2D<float4> UISource : register(t0);
RWTexture2D<float4> OutputUIColorAndAlpha : register(u0);

[numthreads(8, 8, 1)] void main(uint3 DTid
								: SV_DispatchThreadID) {
	uint inputWidth = 0;
	uint inputHeight = 0;
	uint outputWidth = 0;
	uint outputHeight = 0;
	UISource.GetDimensions(inputWidth, inputHeight);
	OutputUIColorAndAlpha.GetDimensions(outputWidth, outputHeight);
	if (DTid.x >= inputWidth || DTid.y >= inputHeight || DTid.x >= outputWidth || DTid.y >= outputHeight) {
		return;
	}

	OutputUIColorAndAlpha[DTid.xy] = UISource[DTid.xy];
}
