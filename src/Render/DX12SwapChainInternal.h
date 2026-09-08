#pragma once

#include "Render/DX12SwapChain.h"

// State shared by the DX12SwapChain translation units.
//
// Overlay callbacks are resolved from the overlay host DLL at swap-chain creation
// and re-checked every Present. They are file-scope state rather than
// DX12SwapChain members so that whichever DLL creates the DX12SwapChain resolves
// them exactly once (the runtime OBJECT libraries are linked into every plugin DLL).
namespace fo4cs::render
{
	extern OverlayInitCallback s_overlayInitCb;
	extern OverlayPresentCallback s_overlayPresentCb;
	extern OverlayPollCallback s_overlayPollCb;

	// Returns true once all three callbacks are available; logs the first miss.
	bool ResolveOverlayCallbacks();

	// HLSL for the HDR colour-space conversion pass, compiled on demand by
	// DX12SwapChain::EnsureColorSpaceResources.
	extern const char* const kColorSpaceShader;
}
