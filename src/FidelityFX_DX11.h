#pragma once

#include <cstdint>
#include <d3d11.h>

#include "Buffer.h"

// D3D11-native FFX FrameInterpolation wrapper using the Fidelity-SDK-DX11-PreNG
// (FFX SDK v1.x).  This is the PreNG frame-generation path — no D3D12 proxy needed.
//
// API surface: Context → Prepare → Dispatch → Destroy
// Backend:     ffxGetInterfaceDX11 (static link, no runtime DLL loading)

#include <fsr3.0/ffx_frameinterpolation.h>
#include <fsr3.0/ffx_fsr3upscaler.h>

class FidelityFX_DX11
{
public:
	static FidelityFX_DX11* GetSingleton()
	{
		static FidelityFX_DX11 singleton;
		return &singleton;
	}

	~FidelityFX_DX11();

	bool Initialize(ID3D11Device* device, uint32_t displayWidth, uint32_t displayHeight,
		uint32_t renderWidth, uint32_t renderHeight, DXGI_FORMAT backBufferFormat);

	void Shutdown();

	bool Prepare(ID3D11DeviceContext* context,
		ID3D11Resource* depth,
		ID3D11Resource* motionVectors,
		float2 jitter,
		float2 renderSize,
		float frameTimeDelta,
		float cameraNear,
		float cameraFar,
		uint64_t frameID);

	bool Dispatch(ID3D11DeviceContext* context,
		ID3D11Resource* currentBackBuffer,
		ID3D11Resource* hudLessBackBuffer,
		ID3D11Resource* output,
		float frameTimeDelta,
		bool reset,
		uint64_t frameID);

	[[nodiscard]] bool IsReady() const noexcept { return m_initialized; }
	[[nodiscard]] bool IsUpscalingReady() const noexcept { return m_upscaleInitialized; }

	bool InitializeUpscaling(ID3D11Device* device, uint32_t maxRenderWidth, uint32_t maxRenderHeight,
		uint32_t outputWidth, uint32_t outputHeight);
	bool Upscale(ID3D11DeviceContext* context,
		ID3D11Resource* color,
		ID3D11Resource* output,
		ID3D11Resource* depth,
		ID3D11Resource* motionVectors,
		float2 jitter,
		float2 renderSize,
		float2 displaySize,
		uint qualityMode);
	void DestroyUpscaling();

private:
	FfxInterface m_backendInterface{};
	FfxFrameInterpolationContext m_fiContext{};
	FfxFsr3UpscalerContext m_upscaleContext{};
	void* m_scratchBuffer = nullptr;
	size_t m_scratchBufferSize = 0;

	ID3D11Device* m_device = nullptr;

	// Shared resources created per the SDK's resource descriptions
	winrt::com_ptr<ID3D11Texture2D> m_reconstructedPrevNearestDepth;
	winrt::com_ptr<ID3D11Texture2D> m_dilatedDepth;
	winrt::com_ptr<ID3D11Texture2D> m_dilatedMotionVectors;
	winrt::com_ptr<ID3D11Texture2D> m_upscaleReconstructedPrevNearestDepth;
	winrt::com_ptr<ID3D11Texture2D> m_upscaleDilatedDepth;
	winrt::com_ptr<ID3D11Texture2D> m_upscaleDilatedMotionVectors;

	uint32_t m_displayWidth = 0;
	uint32_t m_displayHeight = 0;
	uint32_t m_renderWidth = 0;
	uint32_t m_renderHeight = 0;
	uint32_t m_upscaleMaxRenderWidth = 0;
	uint32_t m_upscaleMaxRenderHeight = 0;
	uint32_t m_upscaleOutputWidth = 0;
	uint32_t m_upscaleOutputHeight = 0;
	uint32_t m_upscaleLastRenderWidth = 0;
	uint32_t m_upscaleLastRenderHeight = 0;
	uint32_t m_upscaleLastOutputWidth = 0;
	uint32_t m_upscaleLastOutputHeight = 0;
	uint32_t m_upscaleLastQualityMode = 0xFFFFFFFFu;
	DXGI_FORMAT m_backBufferFormat = DXGI_FORMAT_UNKNOWN;

	bool m_backendInitialized = false;
	bool m_initialized = false;
	bool m_upscaleInitialized = false;
	bool m_upscaleNeedsReset = true;

	bool EnsureBackend(ID3D11Device* device);
	void DestroySharedResources();
	winrt::com_ptr<ID3D11Texture2D> CreateSharedResource(
		const FfxCreateResourceDescription& desc, const wchar_t* name);
};
