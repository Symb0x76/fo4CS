#pragma once

#include "Render/Buffer.h"

// FidelityFX runtime API, used through the D3D12 proxy swap chain.
#include <dx12/ffx_api_dx12.h>
#include <ffx_api.hpp>
#include <ffx_api_loader.h>
#include <ffx_api_types.h>
#include <ffx_framegeneration.hpp>
#include <ffx_upscale.hpp>

class FidelityFX
{
public:
	static FidelityFX* GetSingleton()
	{
		static FidelityFX singleton;
		return &singleton;
	}

	HMODULE module = nullptr;

	ffx::Context swapChainContext{};
	ffx::Context frameGenContext{};
	ffx::Context upscaleContext{};

	bool featureFSR = false;
	bool featureFrameGen = false;
	// The hudless surface format the frame generation context was built against, and
	// whether generation was enabled on it by the last ffx::Configure. Both are needed
	// to rebuild the context safely when the HUDLess buffer turns out to use a
	// different format than the backbuffer. See SetupFrameGeneration.
	DXGI_FORMAT frameGenHudlessFormat = DXGI_FORMAT_UNKNOWN;
	bool frameGenEnabled = false;
	uint32_t upscaleMaxRenderWidth = 0;
	uint32_t upscaleMaxRenderHeight = 0;
	uint32_t upscaleMaxOutputWidth = 0;
	uint32_t upscaleMaxOutputHeight = 0;
	uint32_t upscaleLastRenderWidth = 0;
	uint32_t upscaleLastRenderHeight = 0;
	uint32_t upscaleLastOutputWidth = 0;
	uint32_t upscaleLastOutputHeight = 0;
	uint32_t upscaleLastQualityMode = 0xFFFFFFFFu;
	bool upscaleNeedsReset = true;

	void LoadFFX();


	// a_hudlessFormat is the format of the HUDLess buffer that will be handed to
	// frame generation. Pass DXGI_FORMAT_UNKNOWN when it is not known yet (the game's
	// render targets do not exist when the swap chain is created); the context is then
	// built assuming the backbuffer format and rebuilt later if that turns out wrong.
	void SetupFrameGeneration(DXGI_FORMAT a_hudlessFormat = DXGI_FORMAT_UNKNOWN);
	void DestroyFrameGeneration();

	bool SetupUpscaling(ID3D12Device* a_device, uint32_t a_maxRenderWidth, uint32_t a_maxRenderHeight, uint32_t a_outputWidth, uint32_t a_outputHeight);
	bool Upscale(
		ID3D12GraphicsCommandList* a_commandList,
		ID3D12Resource* a_color,
		ID3D12Resource* a_output,
		ID3D12Resource* a_depth,
		ID3D12Resource* a_motionVectors,
		float2 a_jitter,
		float2 a_renderSize,
		float2 a_displaySize,
		uint a_qualityMode);

	void DestroyUpscaling();
	void Present(bool a_useFrameGeneration);
};
