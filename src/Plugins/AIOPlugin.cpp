#include "Platform/PluginCommon.h"

#include "Overlay/Overlay.h"
#include "Render/DX11Hooks.h"
#include "Render/DX12SwapChain.h"
#include "Upscaling/FeaturePanels.h"
#include "Upscaling/Upscaler.h"

namespace
{
	void MessageHandler(F4SE::MessagingInterface::Message* message)
	{
		if (message->type == F4SE::MessagingInterface::kPostPostLoad) {
			Upscaling::GetSingleton()->PostPostLoad();
		}
	}

	// NuclearGFX hosts the overlay in-process, so panels register with the
	// Overlay singleton directly instead of through the Overlay.dll export.
	void RegisterFeaturePanels()
	{
		auto* overlay = Overlay::GetSingleton();
		for (const auto& panel : { fo4cs::panels::FrameGenerationPanel(), fo4cs::panels::ReflexPanel(), fo4cs::panels::UpscalerPanel() }) {
			overlay->RegisterPanel(panel.name, panel.category, panel.callbacks);
		}
	}

	void RegisterHostCallbacks()
	{
		auto* dx12 = DX12SwapChain::GetSingleton();
		dx12->RegisterOverlayInitCallback(Overlay::OnSwapChainCreated);
		dx12->RegisterOverlayPresentCallback(Overlay::OnPresent);
		dx12->RegisterOverlayPollCallback(Overlay::OnPollHotkey);
		logger::info("[AIO] Overlay callbacks registered directly");
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

	auto upscaling = Upscaling::GetSingleton();
	upscaling->pluginMode = Upscaling::PluginMode::kUpscaler;
	upscaling->LoadSettings();

	logger::info("[AIO] Upscaler DLSS={} FrameGen={} Reflex={}",
		static_cast<int>(upscaling->settings.upscaleMethodPreference),
		static_cast<int>(upscaling->settings.frameGenerationMode),
		upscaling->settings.reflexMode);

	RegisterHostCallbacks();
	DX11Hooks::Install();

	auto messaging = F4SE::GetMessagingInterface();
	messaging->RegisterListener(MessageHandler);

	RegisterFeaturePanels();
	return true;
}
