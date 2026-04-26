#pragma once

#include <dx12/ffx_api_dx12.h>
#include <ffx_api.hpp>
#include <ffx_api_loader.h>
#include <ffx_api_types.h>
#include <ffx_framegeneration.hpp>
#include <ffx_upscale.hpp>

#include "Buffer.h"

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
	uint32_t upscaleMaxRenderWidth = 0;
	uint32_t upscaleMaxRenderHeight = 0;
	uint32_t upscaleMaxOutputWidth = 0;
	uint32_t upscaleMaxOutputHeight = 0;

	void LoadFFX();
	void SetupFrameGeneration();
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
