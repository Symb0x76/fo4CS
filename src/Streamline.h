#pragma once

#include <d3d12.h>
#include <dxgi.h>

#define NV_WINDOWS
#pragma warning(push)
#pragma warning(disable : 4471)
#include <sl.h>
#include <sl_dlss_g.h>
#pragma warning(pop)

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
	bool featureDLSSG = false;

	sl::ViewportHandle viewport{ 0 };

	PFun_slInit* slInit{};
	PFun_slShutdown* slShutdown{};
	PFun_slIsFeatureSupported* slIsFeatureSupported{};
	PFun_slIsFeatureLoaded* slIsFeatureLoaded{};
	PFun_slSetTagForFrame* slSetTagForFrame{};
	PFun_slGetFeatureRequirements* slGetFeatureRequirements{};
	PFun_slGetFeatureFunction* slGetFeatureFunction{};
	PFun_slGetNewFrameToken* slGetNewFrameToken{};
	PFun_slSetD3DDevice* slSetD3DDevice{};
	PFun_slUpgradeInterface* slUpgradeInterface{};

	PFun_slDLSSGGetState* slDLSSGGetState{};
	PFun_slDLSSGSetOptions* slDLSSGSetOptions{};

	sl::FrameToken* frameToken = nullptr;
	uint64_t frameID = 0;

	// Call before D3D device creation
	void LoadAndInit();

	// Call after D3D12 device is created
	void PostDevice(ID3D12Device* device, IDXGIAdapter* adapter);

	// Call each frame before Present — tags resources and configures DLSS-G
	void TagResourcesAndConfigure(
		ID3D12Resource* hudless,
		ID3D12Resource* depth,
		ID3D12Resource* motionVectors,
		bool enable);

	// Call after Present to advance frame
	void AdvanceFrame();
};
