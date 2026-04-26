#pragma once

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_6.h>

#define NV_WINDOWS
#pragma warning(push)
#pragma warning(disable : 4471)
#include <sl.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>
#pragma warning(pop)

#include "Buffer.h"

using PFun_slSetTag2 = sl::Result(const sl::ViewportHandle& viewport, const sl::ResourceTag* tags, uint32_t numTags, sl::CommandBuffer* cmdBuffer);

class Streamline
{
public:
	static Streamline* GetSingleton()
	{
		static Streamline singleton;
		return &singleton;
	}

	HMODULE interposer = nullptr;
	bool initialized = false;
	bool featureDLSS = false;
	bool featureDLSSG = false;
	bool featureReflex = false;
	bool dlssgDisabledAfterError = false;

	sl::ViewportHandle viewport{ 0 };

	PFun_slInit* slInit{};
	PFun_slShutdown* slShutdown{};
	PFun_slIsFeatureSupported* slIsFeatureSupported{};
	PFun_slIsFeatureLoaded* slIsFeatureLoaded{};
	PFun_slSetFeatureLoaded* slSetFeatureLoaded{};
	PFun_slEvaluateFeature* slEvaluateFeature{};
	PFun_slAllocateResources* slAllocateResources{};
	PFun_slFreeResources* slFreeResources{};
	PFun_slSetTag2* slSetTag{};
	PFun_slSetTagForFrame* slSetTagForFrame{};
	PFun_slGetFeatureRequirements* slGetFeatureRequirements{};
	PFun_slGetFeatureVersion* slGetFeatureVersion{};
	PFun_slGetFeatureFunction* slGetFeatureFunction{};
	PFun_slGetNewFrameToken* slGetNewFrameToken{};
	PFun_slSetD3DDevice* slSetD3DDevice{};
	PFun_slUpgradeInterface* slUpgradeInterface{};
	PFun_slSetConstants* slSetConstants{};
	PFun_slGetNativeInterface* slGetNativeInterface{};

	PFun_slDLSSGetOptimalSettings* slDLSSGetOptimalSettings{};
	PFun_slDLSSGetState* slDLSSGetState{};
	PFun_slDLSSSetOptions* slDLSSSetOptions{};

	PFun_slDLSSGGetState* slDLSSGGetState{};
	PFun_slDLSSGSetOptions* slDLSSGSetOptions{};

	PFun_slReflexGetState* slReflexGetState{};
	PFun_slReflexSleep* slReflexSleep{};
	PFun_slReflexSetOptions* slReflexSetOptions{};

	sl::FrameToken* frameToken = nullptr;
	uint64_t frameID = 0;
	uint64_t frameTokenFrameID = UINT64_MAX;

	bool dlssgOptionsValid = false;
	sl::DLSSGMode dlssgConfiguredMode = sl::DLSSGMode::eOff;
	uint64_t dlssgLastSetOptionsFrame = UINT64_MAX;
	uint32_t dlssgConfiguredWidth = 0;
	uint32_t dlssgConfiguredHeight = 0;
	uint32_t dlssgConfiguredColorFormat = 0;
	uint32_t dlssgConfiguredMvecFormat = 0;
	uint32_t dlssgConfiguredDepthFormat = 0;
	uint32_t dlssgConfiguredHudlessFormat = 0;
	uint32_t dlssgConfiguredBackBuffers = 0;
	bool reflexOptionsValid = false;
	sl::ReflexMode reflexConfiguredMode = sl::ReflexMode::eOff;

	// Call before D3D device creation
	void LoadAndInit();

	// Call after D3D12 device is created
	void PostDevice(ID3D12Device* device, IDXGIAdapter* adapter);

	// Call immediately after native D3D12 swap chain creation and before any swap chain methods.
	bool UpgradeSwapChainForDLSSG(IDXGISwapChain4** swapChain);

	// Call each frame before Present — tags resources and configures DLSS-G
	bool TagResourcesAndConfigure(
		ID3D12Resource* hudless,
		ID3D12Resource* depth,
		ID3D12Resource* motionVectors,
		bool enable);

	// Call after Present to advance frame
	void AdvanceFrame();

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
	bool EnsureFrameToken(const char* caller);
	bool ConfigureDLSSG(ID3D12Resource* hudless, ID3D12Resource* depth, ID3D12Resource* motionVectors, sl::DLSSGMode mode, const char* reason);
	void DisableDLSSGAfterError(const char* reason);
	void ConfigureReflexForDLSSG();
	void UpdateConstants(float2 a_jitter);
	void DestroyDLSSResources();
};
