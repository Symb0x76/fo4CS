#include "DX11Hooks.h"

#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

#include "Upscaler.h"
#include "DX12SwapChain.h"
#include "FidelityFX.h"
#include "Streamline.h"

#include "ENB/ENBSeriesAPI.h"

bool enbLoaded = false;

decltype(&D3D11CreateDeviceAndSwapChain) ptrD3D11CreateDeviceAndSwapChain;
decltype(&D3D11CreateDevice) ptrD3D11CreateDevice;
using CreateSwapChainFn = HRESULT(WINAPI*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
CreateSwapChainFn ptrCreateSwapChain;
using CreateSwapChainForHwndFn = HRESULT(WINAPI*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
CreateSwapChainForHwndFn ptrCreateSwapChainForHwnd;
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
PresentFn ptrPresent;
bool g_swapChainVTableHooked = false;
DX11Hooks::DeviceCreatedCallback g_deviceCreatedCallback = nullptr;
DX11Hooks::PresentCallback g_presentCallback = nullptr;

namespace
{
	bool ShouldLoadStreamline()
	{
#ifdef FALLOUT_PRE_NG
		logger::info("[Streamline] Pre-NG runtime detected, skipping Streamline initialization");
		return false;
#endif
		auto upscaling = Upscaling::GetSingleton();
		return upscaling->UsesDLSSUpscaling() || upscaling->UsesDLSSFrameGeneration() || upscaling->UsesReflex();
	}

	bool ShouldLoadFidelityFX()
	{
		auto upscaling = Upscaling::GetSingleton();
		return upscaling->UsesFSRUpscaling() || upscaling->UsesFSRFrameGeneration();
	}

	bool ShouldCreateD3D12Proxy()
	{
#ifdef FALLOUT_PRE_NG
		return false;
#else
		return true;
#endif
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
		if (g_presentCallback) {
			g_presentCallback(a_swapChain);
		}

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
	if (!ShouldCreateD3D12Proxy()) {
		auto ret = ptrCreateSwapChain(reinterpret_cast<IDXGIFactory*>(This), a_device, pDesc, ppSwapChain);
		if (SUCCEEDED(ret) && ppSwapChain && *ppSwapChain) {
			InstallSwapChainPresentHook(*ppSwapChain);
			Upscaling::GetSingleton()->OnD3D11DeviceCreated(a_device, nullptr);
			DX11Hooks::NotifyD3D11DeviceCreated(a_device);
		}
		return ret;
	}

	if (DX12SwapChain::GetSingleton()->swapChain) {
		logger::debug("[FrameGen] D3D12 proxy already exists, delegating to original CreateSwapChain");
		return ptrCreateSwapChain(reinterpret_cast<IDXGIFactory*>(This), a_device, pDesc, ppSwapChain);
	}

	logger::info("[FrameGen] vtable CreateSwapChain hook fired ({}x{} fmt={})", pDesc->BufferDesc.Width, pDesc->BufferDesc.Height, static_cast<uint32_t>(pDesc->BufferDesc.Format));
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

HRESULT WINAPI hk_IDXGIFactory2_CreateSwapChainForHwnd(
	IDXGIFactory2* This, IUnknown* pDevice, HWND hWnd,
	const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
	IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain)
{
	if (!ShouldCreateD3D12Proxy()) {
		auto ret = ptrCreateSwapChainForHwnd(This, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
		if (SUCCEEDED(ret) && ppSwapChain && *ppSwapChain) {
			InstallSwapChainPresentHook(reinterpret_cast<IDXGISwapChain*>(*ppSwapChain));
		}
		return ret;
	}

	if (DX12SwapChain::GetSingleton()->swapChain) {
		logger::debug("[FrameGen] D3D12 proxy already exists, delegating to original CreateSwapChainForHwnd");
		return ptrCreateSwapChainForHwnd(This, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
	}

	ID3D11Device* d3d11Device = nullptr;
	if (FAILED(pDevice->QueryInterface(__uuidof(ID3D11Device), (void**)&d3d11Device)) || !d3d11Device) {
		logger::error("[FrameGen] CreateSwapChainForHwnd: pDevice is not an ID3D11Device");
		return ptrCreateSwapChainForHwnd(This, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
	}

	DXGI_SWAP_CHAIN_DESC desc = {};
	desc.BufferDesc.Width = pDesc->Width;
	desc.BufferDesc.Height = pDesc->Height;
	desc.BufferDesc.Format = pDesc->Format;
	desc.BufferDesc.RefreshRate = { 0, 0 };
	desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	desc.SampleDesc = pDesc->SampleDesc;
	desc.BufferUsage = pDesc->BufferUsage;
	desc.BufferCount = pDesc->BufferCount;
	desc.OutputWindow = hWnd;
	desc.Windowed = !pFullscreenDesc || pFullscreenDesc->Windowed;
	desc.SwapEffect = pDesc->SwapEffect;
	desc.Flags = pDesc->Flags;

	logger::info("[FrameGen] vtable CreateSwapChainForHwnd hook fired ({}x{} fmt={})",
		desc.BufferDesc.Width, desc.BufferDesc.Height, static_cast<uint32_t>(desc.BufferDesc.Format));
	try {
		IDXGIDevice* dxgiDevice = nullptr;
		DX::ThrowIfFailed(d3d11Device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice));

		IDXGIAdapter* adapter = nullptr;
		DX::ThrowIfFailed(dxgiDevice->GetAdapter(&adapter));

		auto proxy = DX12SwapChain::GetSingleton();

		proxy->SetD3D11Device(d3d11Device);

		ID3D11DeviceContext* context = nullptr;
		d3d11Device->GetImmediateContext(&context);
		proxy->SetD3D11DeviceContext(context);

		IDXGIFactory4* dxgiFactory = nullptr;
		DX::ThrowIfFailed(This->QueryInterface(IID_PPV_ARGS(&dxgiFactory)));

		proxy->CreateD3D12Device(adapter);
		Streamline::GetSingleton()->PostDevice(proxy->d3d12Device.get(), adapter);
		proxy->CreateSwapChain(dxgiFactory, desc);
		proxy->CreateInterop();

		Upscaling::GetSingleton()->OnD3D11DeviceCreated(d3d11Device, adapter);
		DX11Hooks::NotifyD3D11DeviceCreated(d3d11Device);

		*ppSwapChain = reinterpret_cast<IDXGISwapChain1*>(proxy->GetSwapChainProxy());

		if (context)
			context->Release();
		dxgiFactory->Release();
		adapter->Release();
		dxgiDevice->Release();
		d3d11Device->Release();

		return S_OK;
	} catch (const std::exception& e) {
		logger::error("[FrameGen] D3D12 proxy via CreateSwapChainForHwnd failed: {}; falling back to D3D11", e.what());
		Upscaling::GetSingleton()->d3d12Interop = false;
		d3d11Device->Release();
		const auto result = ptrCreateSwapChainForHwnd(This, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
		if (SUCCEEDED(result) && ppSwapChain && *ppSwapChain) {
			InstallSwapChainPresentHook(*ppSwapChain);
		}
		return result;
	}
}

HRESULT WINAPI hk_D3D11CreateDevice(
	IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	const D3D_FEATURE_LEVEL* pFeatureLevels,
	UINT FeatureLevels,
	UINT SDKVersion,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext)
{
	const auto result = ptrD3D11CreateDevice(
		pAdapter, DriverType, Software, Flags,
		pFeatureLevels, FeatureLevels, SDKVersion,
		ppDevice, pFeatureLevel, ppImmediateContext);

	if (SUCCEEDED(result) && ppDevice && *ppDevice) {
		DX11Hooks::NotifyD3D11DeviceCreated(*ppDevice);

		if (!g_swapChainVTableHooked) {
		IDXGIDevice* dxgiDevice = nullptr;
		if (SUCCEEDED((*ppDevice)->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice))) {
			IDXGIAdapter* adapter = nullptr;
			if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) {
				IDXGIFactory4* dxgiFactory = nullptr;
				if (SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&dxgiFactory)))) {
					logger::info("[DX11Hooks] Installing CreateSwapChain vtable hook via D3D11CreateDevice fallback");
					(uintptr_t&)ptrCreateSwapChain = Detours::X64::DetourClassVTable(
						*(uintptr_t*)dxgiFactory, &hk_IDXGIFactory_CreateSwapChain, 10);
						(uintptr_t&)ptrCreateSwapChainForHwnd = Detours::X64::DetourClassVTable(
							*(uintptr_t*)dxgiFactory, &hk_IDXGIFactory2_CreateSwapChainForHwnd, 13);
					g_swapChainVTableHooked = true;
					dxgiFactory->Release();
				}
				adapter->Release();
			}
			dxgiDevice->Release();
		}
		}
	}

	return result;
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
	logger::info("[FrameGen] D3D11CreateDeviceAndSwapChain hook fired (windowed={}, enb={})", pSwapChainDesc->Windowed, enbLoaded);
	auto upscaling = Upscaling::GetSingleton();
	const auto originalFeatureLevels = pFeatureLevels;
	const auto originalFeatureLevelCount = FeatureLevels;

	if (pSwapChainDesc->Windowed && ShouldCreateD3D12Proxy()) {
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

			if (enbLoaded && !g_swapChainVTableHooked) {
				(uintptr_t&)ptrCreateSwapChain = Detours::X64::DetourClassVTable(*(uintptr_t*)dxgiFactory, &hk_IDXGIFactory_CreateSwapChain, 10);
				(uintptr_t&)ptrCreateSwapChainForHwnd = Detours::X64::DetourClassVTable(
					*(uintptr_t*)dxgiFactory, &hk_IDXGIFactory2_CreateSwapChainForHwnd, 13);
				g_swapChainVTableHooked = true;
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
#if defined(FALLOUT_POST_NG)
	if (ShouldLoadStreamline()) {
		Streamline::GetSingleton()->LoadAndInit();
	} else {
		logger::info("[Streamline] Runtime not required for current settings");
	}
#else
	logger::info("[Streamline] Skipped for PreNG (blocking slInit)");
#endif

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
	(uintptr_t&)ptrD3D11CreateDevice = Detours::IATHook(moduleBase, "d3d11.dll", "D3D11CreateDevice", (uintptr_t)hk_D3D11CreateDevice);
	logger::info("[FrameGen] D3D11 IAT hooks installed (enb={}, createDeviceAndSwapChain={}, createDevice={})",
		enbLoaded,
		ptrD3D11CreateDeviceAndSwapChain != nullptr,
		ptrD3D11CreateDevice != nullptr);
}

void DX11Hooks::SetDeviceCreatedCallback(DeviceCreatedCallback a_callback)
{
	g_deviceCreatedCallback = a_callback;
}

void DX11Hooks::SetPresentCallback(PresentCallback a_callback)
{
	g_presentCallback = a_callback;
}

void DX11Hooks::NotifyD3D11DeviceCreated(ID3D11Device* a_device)
{
	if (g_deviceCreatedCallback) {
		g_deviceCreatedCallback(a_device);
	}
}
