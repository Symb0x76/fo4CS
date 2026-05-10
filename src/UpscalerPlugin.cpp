#include "PluginCommon.h"

#include "DX11Hooks.h"
#include "Upscaler.h"

#include <OverlayAPI.h>
#include <SimpleIni.h>
#include <imgui.h>

namespace
{
	void MessageHandler(F4SE::MessagingInterface::Message* message)
	{
		switch (message->type) {
		case F4SE::MessagingInterface::kPostPostLoad:
			Upscaling::GetSingleton()->PostPostLoad();
			break;
		default:
			break;
		}
	}
}

// Panel callbacks for Overlay.dll registration
namespace UpscalerOverlay
{
	int RenderPanel(void* userData)
	{
		auto& s = *static_cast<Upscaling::Settings*>(userData);
		int changed = 0;

		if (ImGui::CollapsingHeader("Upscaler")) {
			const char* upscaleMethods[] = { "Disabled", "FSR", "DLSS" };
			changed |= ImGui::Combo("Method", &s.upscaleMethodPreference, upscaleMethods, IM_ARRAYSIZE(upscaleMethods)) ? 1 : 0;

			const char* qualityModes[] = { "Ultra Quality", "Quality", "Balanced", "Performance", "Ultra Performance" };
			changed |= ImGui::Combo("Quality", &s.qualityMode, qualityModes, IM_ARRAYSIZE(qualityModes)) ? 1 : 0;

			const int validPresetValues[] = { 0, 10, 11, 12, 13 };
			const char* validPresetNames[] = { "Default", "Preset J", "Preset K", "Preset L", "Preset M" };
			int presetComboIdx = 0;
			for (int i = 0; i < IM_ARRAYSIZE(validPresetValues); ++i) {
				if (s.dlssPreset == validPresetValues[i]) { presetComboIdx = i; break; }
			}
			if (ImGui::Combo("DLSS Preset", &presetComboIdx, validPresetNames, IM_ARRAYSIZE(validPresetNames))) {
				s.dlssPreset = validPresetValues[presetComboIdx];
				changed = 1;
			}
		}
		return changed;
	}

	void SavePanel(void* userData)
	{
		auto& s = *static_cast<Upscaling::Settings*>(userData);
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.SetValue("Settings", "iUpscaleMethodPreference", std::to_string(s.upscaleMethodPreference).c_str());
		ini.SetValue("Settings", "iQualityMode", std::to_string(s.qualityMode).c_str());
		ini.SetValue("Settings", "iDLSSPreset", std::to_string(s.dlssPreset).c_str());
		std::error_code ec;
		std::filesystem::create_directories("Data\\F4SE\\Plugins\\Upscaler", ec);
		if (!ec) ini.SaveFile("Data\\F4SE\\Plugins\\Upscaler\\Upscaler.ini");
	}

	void TryRegister()
	{
		HMODULE overlay = GetModuleHandleW(L"Overlay.dll");
		if (!overlay) return;

		auto registerFn = reinterpret_cast<decltype(&Overlay_RegisterPanel)>(
			GetProcAddress(overlay, "Overlay_RegisterPanel"));
		if (!registerFn) return;

		static OverlayPanelCallbacks cbs;
		cbs.render = RenderPanel;
		cbs.save = SavePanel;
		cbs.userData = &Upscaling::GetSingleton()->settings;
		registerFn("Upscaler", kOverlayCategory_Rendering, &cbs);
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

	DX11Hooks::Install();

	auto messaging = F4SE::GetMessagingInterface();
	messaging->RegisterListener(MessageHandler);

	UpscalerOverlay::TryRegister();
	return true;
}
