#include "PluginCommon.h"

#include "Core/CommunityShaders.h"

#include "Core/Globals.h"
#include "Core/Menu.h"
#include "Diagnostics/HangTrace.h"
#include "DX11Hooks.h"
#include "DX12SwapChain.h"
#include "Overlay/Overlay.h"

#include <d3d11.h>
#include <dxgi.h>

namespace
{
	ID3D11Device* g_device = nullptr;

	void OnD3D11DeviceCreated(ID3D11Device* a_device)
	{
		fo4cs::Diagnostics::WriteHangTraceLine("D3D11DeviceCreated:enter");
		if (!a_device || g_device) {
			fo4cs::Diagnostics::WriteHangTraceLine("D3D11DeviceCreated:skip");
			return;
		}

		g_device = a_device;

		fo4cs::Diagnostics::WriteHangTraceLine("Runtime:OnD3D11DeviceCreated:begin");
		CommunityShaders::Runtime::GetSingleton()->OnD3D11DeviceCreated(g_device);
		fo4cs::Diagnostics::WriteHangTraceLine("Runtime:OnD3D11DeviceCreated:end");
	}

	void OnPresent(IDXGISwapChain* a_swapChain)
	{
#if !defined(FALLOUT_PRE_NG)
		(void)a_swapChain;
#endif
		fo4cs::Diagnostics::WriteHangTraceLine("Present:enter");
		auto* runtime = CommunityShaders::Runtime::GetSingleton();
		if (runtime->IsLoaded()) {
			fo4cs::Diagnostics::WriteHangTraceLine("Runtime:OnFrame:begin");
			runtime->OnFrame();
			fo4cs::Diagnostics::WriteHangTraceLine("Runtime:OnFrame:end");
		}

#if defined(FALLOUT_PRE_NG)
		if (!a_swapChain || !g_device) {
			fo4cs::Diagnostics::WriteHangTraceLine("Present:exit:no-swapchain-or-device");
			return;
		}

		if (DX12SwapChain::GetSingleton()->swapChain) {
			fo4cs::Diagnostics::WriteHangTraceLine("Present:skip-d3d11-menu-dx12-proxy");
			return;
		}

		ID3D11DeviceContext* context = nullptr;
		fo4cs::Diagnostics::WriteHangTraceLine("D3D11:GetImmediateContext:begin");
		g_device->GetImmediateContext(&context);
		fo4cs::Diagnostics::WriteHangTraceLine("D3D11:GetImmediateContext:end");
		if (context) {
			fo4cs::Diagnostics::WriteHangTraceLine("Menu:RenderD3D11:begin");
			CommunityShaders::Menu::Render(g_device, context, a_swapChain);
			fo4cs::Diagnostics::WriteHangTraceLine("Menu:RenderD3D11:end");
			context->Release();
		}
#endif
		fo4cs::Diagnostics::WriteHangTraceLine("Present:exit");
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
	fo4cs::Diagnostics::ResetHangTrace();

	logger::info("[CommunityShaders] Initializing unified Feature framework...");

	DX11Hooks::SetDeviceCreatedCallback(OnD3D11DeviceCreated);
	DX11Hooks::SetPresentCallback(OnPresent);

	CommunityShaders::Runtime::GetSingleton()->Load();

	auto messaging = F4SE::GetMessagingInterface();
	messaging->RegisterListener(MessageHandler);

	logger::info("[CommunityShaders] Plugin loaded with unified Features");
	return true;
}
