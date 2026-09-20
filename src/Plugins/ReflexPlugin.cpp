#include "Platform/PluginCommon.h"

#include "Platform/ModulePaths.h"
#include "Render/DX11Hooks.h"
#include "Upscaling/FeaturePanels.h"
#include "Upscaling/Upscaler.h"

namespace
{
	// Upscaler or FrameGen, when present, owns the D3D12 proxy that Reflex rides on.
	bool HasExternalProxyOwner(const F4SE::LoadInterface* a_f4se)
	{
		return fo4cs::platform::IsSiblingPluginAvailable(a_f4se, "Upscaler") ||
		       fo4cs::platform::IsSiblingPluginAvailable(a_f4se, "FrameGen");
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
	F4SE::Init(a_f4se);
	fo4cs::WaitForDebuggerIfNeeded();
	fo4cs::InitializeLog();

	auto upscaling = Upscaling::GetSingleton();
	upscaling->pluginMode = Upscaling::PluginMode::kReflex;
	upscaling->LoadReflexSettings();

	const auto registerPanel = [] {
		fo4cs::panels::RegisterWithOverlayHost(fo4cs::panels::ReflexPanel());
	};

	if (!upscaling->UsesReflex()) {
		logger::info("[Reflex] Disabled by settings; D3D12 proxy hooks not installed");
		registerPanel();
		return true;
	}

	if (HasExternalProxyOwner(a_f4se)) {
		logger::info("[Reflex] Upscaler/FrameGen plugin detected, leaving D3D12 proxy ownership to that plugin");
		registerPanel();
		return true;
	}

	logger::info("[Reflex] Installing D3D12 proxy hooks");
	DX11Hooks::Install();

	registerPanel();
	return true;
}
