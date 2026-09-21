// Composites the redirected UI layer back over the presented frame.
//
// While the UI render target is redirected (see RedirectUIRenderTarget) the engine draws
// the whole interface into our own texture instead of the frame buffer, so the frame the
// proxy swap chain hands us has no UI on it at all. This pass puts it back, so what the
// player sees is unchanged while DLSS-G gets the UI as a separate layer.
//
// The engine draws into a target cleared to (0,0,0,0) with its normal alpha blending, which
// accumulates premultiplied colour -- so this is a straight "over" of a premultiplied
// source. Scene alpha is preserved; the swap chain does not use it, and overwriting it
// would be a silent change to what gets presented.
//
// If nothing is visibly composited, the UI layer is empty: the engine masked alpha writes
// while drawing the interface, and the redirect cannot work without a further change. That
// failure is deliberately loud -- the HUD disappears -- rather than subtly wrong.

Texture2D<float4> UILayer : register(t0);
RWTexture2D<float4> Backbuffer : register(u0);

[numthreads(8, 8, 1)] void main(uint3 DTid
								: SV_DispatchThreadID) {
	uint uiWidth = 0;
	uint uiHeight = 0;
	uint outWidth = 0;
	uint outHeight = 0;
	UILayer.GetDimensions(uiWidth, uiHeight);
	Backbuffer.GetDimensions(outWidth, outHeight);
	if (DTid.x >= uiWidth || DTid.y >= uiHeight || DTid.x >= outWidth || DTid.y >= outHeight) {
		return;
	}

	float4 ui = UILayer[DTid.xy];
	float4 scene = Backbuffer[DTid.xy];

	Backbuffer[DTid.xy] = float4(ui.rgb + (1.0 - ui.a) * scene.rgb, scene.a);
}
