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

	// D3D12 validation plumbing for the frame-generation device-removal
	// investigation. GetDeviceRemovedReason() reports DXGI_ERROR_INVALID_CALL on the
	// first frame-generation present, which means the runtime rejected an API call
	// rather than the GPU faulting. Only the debug layer's info queue names which
	// call, so this is opt-in via the FO4CS_D3D12_DEBUG_LAYER switch in Debug.ini.
	//
	// Must run before D3D12CreateDevice; the debug layer cannot be attached later.
	void EnableD3D12Diagnostics();

	// Binds the info queue to the freshly created device. No-op unless the debug
	// layer actually attached.
	void ConfigureD3D12InfoQueue(ID3D12Device* a_device);

	// Copies pending validation messages into the plugin log and clears the queue.
	// Cheap no-op when the debug layer is off, so Present can call it every frame.
	void DrainD3D12InfoQueue(const char* a_when);
}
