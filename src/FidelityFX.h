#pragma once

#include "Buffer.h"

#if defined(FALLOUT_PRE_NG)
// FSR 3.0 — D3D11-native combined upscale + frame generation
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/backends/dx11/ffx_dx11.h>
#else
// FSR 3.1 — D3D12-interop, separate upscaling + frame generation contexts
#include <dx12/ffx_api_dx12.h>
#include <ffx_api.hpp>
#include <ffx_api_loader.h>
#include <ffx_api_types.h>
#include <ffx_framegeneration.hpp>
#include <ffx_upscale.hpp>
#endif

class FidelityFX
{
public:
	static FidelityFX* GetSingleton()
	{
		static FidelityFX singleton;
		return &singleton;
	}

	HMODULE module = nullptr;

#if defined(FALLOUT_PRE_NG)
	// FSR 3.0 — single context handles upscaling + frame generation
	FfxFsr3Context fsr3Context{};
	void* fsr3ScratchBuffer = nullptr;
	bool fsr3Initialized = false;
#else
	// FSR 3.1 — separate contexts per effect
	ffx::Context swapChainContext{};
	ffx::Context frameGenContext{};
	ffx::Context upscaleContext{};
#endif

	bool featureFSR = false;
	bool featureFrameGen = false;
	uint32_t upscaleMaxRenderWidth = 0;
	uint32_t upscaleMaxRenderHeight = 0;
	uint32_t upscaleMaxOutputWidth = 0;
	uint32_t upscaleMaxOutputHeight = 0;

	void LoadFFX();

#if defined(FALLOUT_PRE_NG)
	// FSR 3.0 D3D11-native API
	void CreateFSR3Context(ID3D11Device* a_device, uint32_t a_maxRenderWidth, uint32_t a_maxRenderHeight, uint32_t a_outputWidth, uint32_t a_outputHeight);
	void DestroyFSR3Context();
#endif

	void SetupFrameGeneration();

#if defined(FALLOUT_PRE_NG)
	// FSR 3.0 combined dispatch — upscale + FG in one call
	bool Upscale(
		ID3D11DeviceContext* a_context,
		ID3D11Texture2D* a_color,
		ID3D11Texture2D* a_output,
		ID3D11Texture2D* a_depth,
		ID3D11Texture2D* a_motionVectors,
		float2 a_jitter,
		float2 a_renderSize,
		float2 a_displaySize,
		uint a_qualityMode);
#else
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
#endif

	void DestroyUpscaling();
	void Present(bool a_useFrameGeneration);
};
