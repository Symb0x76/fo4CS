#include "Platform/PluginCommon.h"

#include "Platform/ModulePaths.h"
#include "Render/DX11Hooks.h"
#include "Upscaling/FeaturePanels.h"
#include "Upscaling/Upscaler.h"

using fo4cs::diagnostics::Event;
using fo4cs::diagnostics::LogEvent;

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
	F4SE::Init(a_f4se);
	fo4cs::WaitForDebuggerIfNeeded();
	fo4cs::InitializeLog();

	// The Upscaler plugin, when present, owns the D3D hooks and the D3D12 proxy;
	// FrameGen then only contributes its settings and panel.
	const bool upscalerPluginAvailable = fo4cs::platform::IsSiblingPluginAvailable(a_f4se, "Upscaler");
	if (upscalerPluginAvailable) {
		auto upscaling = Upscaling::GetSingleton();
		upscaling->LoadFrameGenerationSettings();
		LogEvent(Event::FeatureState, "[Settings] FrameGen(enabled={}, limiter={}), Debug(enabled={}, streamlineLogLevel={}, frames={})",
			upscaling->settings.frameGenerationMode,
			upscaling->settings.frameLimitMode,
			upscaling->settings.debugLogging,
			upscaling->settings.streamlineLogLevel,
			upscaling->settings.debugFrameLogCount);
		logger::info("[FrameGen] Upscaler plugin available, leaving DX hooks to Upscaler");
	} else {
		Upscaling::GetSingleton()->LoadSettings();
		logger::info("[FrameGen] Upscaler plugin not available, installing FrameGen DX hooks");
		DX11Hooks::Install();
	}

	fo4cs::panels::RegisterWithOverlayHost(fo4cs::panels::FrameGenerationPanel());
	return true;
}
