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

private:
	FfxInterface m_backendInterface{};
	FfxFrameInterpolationContext m_fiContext{};
	void* m_scratchBuffer = nullptr;
	size_t m_scratchBufferSize = 0;

	ID3D11Device* m_device = nullptr;

	// Shared resources created per the SDK's resource descriptions
	winrt::com_ptr<ID3D11Texture2D> m_reconstructedPrevNearestDepth;
	winrt::com_ptr<ID3D11Texture2D> m_dilatedDepth;
	winrt::com_ptr<ID3D11Texture2D> m_dilatedMotionVectors;

	uint32_t m_displayWidth = 0;
	uint32_t m_displayHeight = 0;
	uint32_t m_renderWidth = 0;
	uint32_t m_renderHeight = 0;
	DXGI_FORMAT m_backBufferFormat = DXGI_FORMAT_UNKNOWN;

	bool m_initialized = false;

	void DestroySharedResources();
	winrt::com_ptr<ID3D11Texture2D> CreateSharedResource(
		const FfxCreateResourceDescription& desc, const wchar_t* name);
};
