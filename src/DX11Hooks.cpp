#include "DX11Hooks.h"

#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

#include "Upscaler.h"
#include "DX12SwapChain.h"
#include "FidelityFX.h"
#include "Streamline.h"
#include "Core/CommunityShaders.h"

#include "ENB/ENBSeriesAPI.h"

bool enbLoaded = false;

decltype(&D3D11CreateDeviceAndSwapChain) ptrD3D11CreateDeviceAndSwapChain;
using CreateSwapChainFn = HRESULT(WINAPI*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
CreateSwapChainFn ptrCreateSwapChain;
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
PresentFn ptrPresent;

namespace
{
	bool ShouldLoadStreamline()
	{
		auto upscaling = Upscaling::GetSingleton();
		return upscaling->UsesDLSSUpscaling() || upscaling->UsesDLSSFrameGeneration() || upscaling->UsesReflex();
	}

	bool ShouldLoadFidelityFX()
	{
		auto upscaling = Upscaling::GetSingleton();
		return upscaling->UsesFSRUpscaling() || upscaling->UsesFSRFrameGeneration();
	}

	template <class T>
	void ReleaseAndNull(T** a_value)
	{
		if (a_value && *a_value) {
			(*a_value)->Release();
			*a_value = nullptr;
		}
	}

	HRESULT STDMETHODCALLTYPE hk_IDXGISwapChain_Present(IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags)
	{
		CommunityShaders::Runtime::GetSingleton()->OnFrame();
		return ptrPresent(a_swapChain, a_syncInterval, a_flags);
	}

	void InstallSwapChainPresentHook(IDXGISwapChain* a_swapChain)
	{
		if (!a_swapChain || ptrPresent) {
			return;
		}

		*(uintptr_t*)&ptrPresent = Detours::X64::DetourClassVTable(*(uintptr_t*)a_swapChain, &hk_IDXGISwapChain_Present, 8);
		logger::info("[CommunityShaders] D3D11 Present hook installed");
	}
}

HRESULT WINAPI hk_IDXGIFactory_CreateSwapChain(IDXGIFactory2* This, _In_ ID3D11Device* a_device, _In_ DXGI_SWAP_CHAIN_DESC* pDesc, _COM_Outptr_ IDXGISwapChain** ppSwapChain)
{
	try {
		IDXGIDevice* dxgiDevice = nullptr;
		DX::ThrowIfFailed(a_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice));

		IDXGIAdapter* adapter = nullptr;
		DX::ThrowIfFailed(dxgiDevice->GetAdapter(&adapter));

		auto proxy = DX12SwapChain::GetSingleton();

		proxy->SetD3D11Device(a_device);

		ID3D11DeviceContext* context = nullptr;
		a_device->GetImmediateContext(&context);
		proxy->SetD3D11DeviceContext(context);

		IDXGIFactory4* dxgiFactory = nullptr;
		DX::ThrowIfFailed(This->QueryInterface(IID_PPV_ARGS(&dxgiFactory)));

		proxy->CreateD3D12Device(adapter);
		Streamline::GetSingleton()->PostDevice(proxy->d3d12Device.get(), adapter);
		proxy->CreateSwapChain(dxgiFactory, *pDesc);
		proxy->CreateInterop();

		Upscaling::GetSingleton()->OnD3D11DeviceCreated(a_device, adapter);
		DX11Hooks::NotifyD3D11DeviceCreated(a_device);

		*ppSwapChain = proxy->GetSwapChainProxy();

		if (context)
			context->Release();
		dxgiFactory->Release();
		adapter->Release();
		dxgiDevice->Release();

		return S_OK;
	} catch (const std::exception& e) {
		logger::error("[FrameGen] D3D12 proxy swap chain creation failed: {}; falling back to D3D11", e.what());
		Upscaling::GetSingleton()->d3d12Interop = false;
		const auto result = ptrCreateSwapChain(reinterpret_cast<IDXGIFactory*>(This), a_device, pDesc, ppSwapChain);
		if (SUCCEEDED(result) && ppSwapChain && *ppSwapChain) {
			InstallSwapChainPresentHook(*ppSwapChain);
		}
		return result;
	}
}

HRESULT WINAPI hk_D3D11CreateDeviceAndSwapChain(
	IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	const D3D_FEATURE_LEVEL* pFeatureLevels,
	UINT FeatureLevels,
	UINT SDKVersion,
	DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
	IDXGISwapChain** ppSwapChain,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext)
{
	auto upscaling = Upscaling::GetSingleton();
	const auto originalFeatureLevels = pFeatureLevels;
	const auto originalFeatureLevelCount = FeatureLevels;

	if (pSwapChainDesc->Windowed) {
		logger::debug("[FrameGen] Using D3D12 proxy");
		
		try {
			upscaling->d3d12Interop = true;
			upscaling->refreshRate = Upscaling::GetRefreshRate(pSwapChainDesc->OutputWindow);

			IDXGIFactory4* dxgiFactory = nullptr;
			if (!pAdapter)
				DX::ThrowIfFailed(E_POINTER);
			DX::ThrowIfFailed(pAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory)));

			const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;
			pFeatureLevels = &featureLevel;
			FeatureLevels = 1;

			if (enbLoaded) {
				*(uintptr_t*)&ptrCreateSwapChain = Detours::X64::DetourClassVTable(*(uintptr_t*)dxgiFactory, &hk_IDXGIFactory_CreateSwapChain, 10);
			}
			else {
				DX::ThrowIfFailed(D3D11CreateDevice(
					pAdapter,
					DriverType,
					Software,
					Flags,
					pFeatureLevels,
					FeatureLevels,
					SDKVersion,
					ppDevice,
					pFeatureLevel,
					ppImmediateContext));

				IDXGIDevice* dxgiDevice = nullptr;
				DX::ThrowIfFailed((*ppDevice)->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice));

				IDXGIAdapter* adapter = nullptr;
				DX::ThrowIfFailed(dxgiDevice->GetAdapter(&adapter));

				auto proxy = DX12SwapChain::GetSingleton();

				proxy->SetD3D11Device(*ppDevice);

				ID3D11DeviceContext* context = nullptr;
				(*ppDevice)->GetImmediateContext(&context);
				proxy->SetD3D11DeviceContext(context);

				proxy->CreateD3D12Device(adapter);
				Streamline::GetSingleton()->PostDevice(proxy->d3d12Device.get(), adapter);
				proxy->CreateSwapChain(dxgiFactory, *pSwapChainDesc);
				proxy->CreateInterop();

				upscaling->OnD3D11DeviceCreated(*ppDevice, adapter);
				DX11Hooks::NotifyD3D11DeviceCreated(*ppDevice);

				*ppSwapChain = proxy->GetSwapChainProxy();

				if (context)
					context->Release();
				adapter->Release();
				dxgiDevice->Release();
				dxgiFactory->Release();

				return S_OK;
			}

			dxgiFactory->Release();
		} catch (const std::exception& e) {
			logger::error("[FrameGen] D3D12 proxy initialization failed: {}; falling back to D3D11", e.what());
			upscaling->d3d12Interop = false;
			ReleaseAndNull(ppImmediateContext);
			ReleaseAndNull(ppDevice);
			pFeatureLevels = originalFeatureLevels;
			FeatureLevels = originalFeatureLevelCount;
		}
	}

	auto ret = ptrD3D11CreateDeviceAndSwapChain(
		pAdapter,
		DriverType,
		Software,
		Flags,
		pFeatureLevels,
		FeatureLevels,
		SDKVersion,
		pSwapChainDesc,
		ppSwapChain,
		ppDevice,
		pFeatureLevel,
		ppImmediateContext);

	if (SUCCEEDED(ret) && ppDevice && *ppDevice) {
		IDXGIDevice* dxgiDevice = nullptr;
		if (SUCCEEDED((*ppDevice)->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice))) {
			IDXGIAdapter* adapter = nullptr;
			if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) {
				upscaling->OnD3D11DeviceCreated(*ppDevice, adapter);
				DX11Hooks::NotifyD3D11DeviceCreated(*ppDevice);
				adapter->Release();
			}
			dxgiDevice->Release();
		}
	}
	if (SUCCEEDED(ret) && ppSwapChain && *ppSwapChain) {
		InstallSwapChainPresentHook(*ppSwapChain);
	}

	return ret;
}

void DX11Hooks::Install()
{
	if (ShouldLoadStreamline()) {
		Streamline::GetSingleton()->LoadAndInit();
	} else {
		logger::info("[Streamline] Runtime not required for current settings");
	}

	if (ENB_API::RequestENBAPI()) {
		logger::info("[DX12SwapChain] ENB detected, using alternative swap chain hook");
		enbLoaded = true;
	} else {
		logger::debug("[DX12SwapChain] ENB not detected, using standard swap chain hook");
	}

	if (ShouldLoadFidelityFX()) {
		FidelityFX::GetSingleton()->LoadFFX();
	} else {
		logger::info("[FidelityFX] Runtime not required for current settings");
	}
	uintptr_t moduleBase = (uintptr_t)GetModuleHandle(nullptr);

	(uintptr_t&)ptrD3D11CreateDeviceAndSwapChain = Detours::IATHook(moduleBase, "d3d11.dll", "D3D11CreateDeviceAndSwapChain", (uintptr_t)hk_D3D11CreateDeviceAndSwapChain);
}

void DX11Hooks::NotifyD3D11DeviceCreated(ID3D11Device* a_device)
{
	CommunityShaders::Runtime::GetSingleton()->OnD3D11DeviceCreated(a_device);
}
