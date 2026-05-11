#include "PluginCommon.h"

#include "Core/CommunityShaders.h"

// Overlay stub: CommunityShaders.dll does not link Overlay.dll.
// HDRCalibration handles nullptr → creates its own ImGui context.
class Overlay { public: static Overlay* GetSingleton(); };
Overlay* Overlay::GetSingleton() { return nullptr; }
#include "Core/Menu.h"

#include <d3d11.h>
#include <dxgi.h>

namespace
{
	ID3D11Device* g_device = nullptr;
	ID3D11DeviceContext* g_context = nullptr;
	IDXGISwapChain* g_swapChain = nullptr;

	using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
	PresentFn g_originalPresent = nullptr;
	bool g_presentHooked = false;

	HRESULT STDMETHODCALLTYPE hk_Present(IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags)
	{
		auto* runtime = CommunityShaders::Runtime::GetSingleton();
		if (runtime->IsLoaded()) {
			runtime->OnFrame();
		}

		CommunityShaders::Menu::Render(g_device, g_context);

		return g_originalPresent(a_swapChain, a_syncInterval, a_flags);
	}

	using CreateDeviceAndSwapChainFn = HRESULT(WINAPI*)(
		IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
		const D3D_FEATURE_LEVEL*, UINT, UINT,
		const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**,
		D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

	CreateDeviceAndSwapChainFn g_originalCreateDeviceAndSwapChain = nullptr;

	HRESULT WINAPI hk_D3D11CreateDeviceAndSwapChain(
		IDXGIAdapter* a_adapter, D3D_DRIVER_TYPE a_driverType, HMODULE a_software,
		UINT a_flags, const D3D_FEATURE_LEVEL* a_featureLevels, UINT a_featureLevelsCount,
		UINT a_sdkVersion, const DXGI_SWAP_CHAIN_DESC* a_swapChainDesc,
		IDXGISwapChain** a_swapChain, ID3D11Device** a_device,
		D3D_FEATURE_LEVEL* a_featureLevel, ID3D11DeviceContext** a_immediateContext)
	{
		auto hr = g_originalCreateDeviceAndSwapChain(
			a_adapter, a_driverType, a_software, a_flags, a_featureLevels,
			a_featureLevelsCount, a_sdkVersion, a_swapChainDesc,
			a_swapChain, a_device, a_featureLevel, a_immediateContext);

		if (SUCCEEDED(hr) && a_device && *a_device && !g_device) {
			g_device = *a_device;
			g_context = *a_immediateContext;

			CommunityShaders::Runtime::GetSingleton()->OnD3D11DeviceCreated(g_device);
		}

		if (!g_presentHooked && a_swapChain && *a_swapChain) {
			g_swapChain = *a_swapChain;

			void** vtable = *reinterpret_cast<void***>(g_swapChain);
			g_originalPresent = reinterpret_cast<PresentFn>(vtable[8]);

			DWORD oldProtect;
			VirtualProtect(&vtable[8], sizeof(void*), PAGE_READWRITE, &oldProtect);
			vtable[8] = hk_Present;
			VirtualProtect(&vtable[8], sizeof(void*), oldProtect, &oldProtect);

			g_presentHooked = true;
			logger::info("[CommunityShaders] Present hook installed");
		}

		return hr;
	}

	void InstallD3D11Hooks()
	{
		uintptr_t moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));

		reinterpret_cast<uintptr_t&>(g_originalCreateDeviceAndSwapChain) =
			Detours::IATHook(moduleBase, "d3d11.dll", "D3D11CreateDeviceAndSwapChain",
			                 reinterpret_cast<uintptr_t>(hk_D3D11CreateDeviceAndSwapChain));

		logger::info("[CommunityShaders] D3D11 IAT hooks installed (createDeviceAndSwapChain={})",
		             g_originalCreateDeviceAndSwapChain != nullptr);
	}

	void MessageHandler(F4SE::MessagingInterface::Message* message)
	{
		if (message->type == F4SE::MessagingInterface::kPostPostLoad) {
			CommunityShaders::Runtime::GetSingleton()->PostPostLoad();
		}
	}
}

#if defined(FALLOUT_POST_NG)
extern "C" DLLEXPORT constinit F4SE::PluginVersionData F4SEPlugin_Version = []() consteval {
	F4SE::PluginVersionData data{};
	fo4cs::PopulateVersionData(data);
	return data;
}();
#else
extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface*, F4SE::PluginInfo* a_info)
{
	fo4cs::PopulatePluginInfo(a_info);
	return true;
}
#endif

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
#if defined(FALLOUT_POST_AE)
	F4SE::Init(a_f4se, { .trampoline = true, .trampolineSize = 16 * stl::kThunkCallTrampolineSize });
#else
	F4SE::Init(a_f4se);
	F4SE::AllocTrampoline(16 * stl::kThunkCallTrampolineSize);
#endif
	fo4cs::WaitForDebuggerIfNeeded();
	fo4cs::InitializeLog();

	logger::info("[CommunityShaders] Initializing Feature framework...");

	CommunityShaders::Runtime::GetSingleton()->Load();

	InstallD3D11Hooks();

	auto messaging = F4SE::GetMessagingInterface();
	messaging->RegisterListener(MessageHandler);

	logger::info("[CommunityShaders] Plugin loaded");
	return true;
}
