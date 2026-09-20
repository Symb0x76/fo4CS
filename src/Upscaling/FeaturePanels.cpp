#include "Upscaling/FeaturePanels.h"

#include "Upscaling/Upscaler.h"

#include <SimpleIni.h>
#include <imgui.h>

#include <filesystem>
#include <string>

namespace fo4cs::panels
{
	namespace
	{
		Upscaling::Settings& SettingsOf(void* userData)
		{
			return *static_cast<Upscaling::Settings*>(userData);
		}

		void DrawDLSSRuntimeNotice()
		{
			auto* upscaling = Upscaling::GetSingleton();
			upscaling->ApplyRuntimeFallbacks();
			if (const char* reason = upscaling->GetDLSSUnavailableReason()) {
				ImGui::TextWrapped("%s", reason);
			}
		}

		int DrawFrameGenerationBackendCombo(int& backend)
		{
			const char* fgBackends[] = { "NVIDIA DLSS-G", "AMD FSR FG" };
			int backendIndex = backend == Upscaling::kFrameGenerationBackendFSR ? 1 : 0;
			if (ImGui::Combo("Backend", &backendIndex, fgBackends, IM_ARRAYSIZE(fgBackends))) {
				backend = backendIndex == 0 ? Upscaling::kFrameGenerationBackendDLSS : Upscaling::kFrameGenerationBackendFSR;
				return 1;
			}
			return 0;
		}

		int FinishPanel(int changed)
		{
			if (changed) {
				Upscaling::GetSingleton()->ApplyRuntimeFallbacks();
			}
			return changed;
		}

		// Writes one feature INI under Data\F4SE\Plugins\<feature>\, creating the
		// directory when the package layout is incomplete.
		void SaveIni(CSimpleIniA& ini, const char* directory, const char* file)
		{
			std::error_code ec;
			std::filesystem::create_directories(directory, ec);
			if (!ec) {
				ini.SaveFile(file);
			}
		}

		int RenderFrameGeneration(void* userData)
		{
			auto& s = SettingsOf(userData);
			int changed = 0;
			if (ImGui::CollapsingHeader("Frame Generation")) {
				changed |= ImGui::Checkbox("Enabled", &s.frameGenerationMode) ? 1 : 0;
				ImGui::SameLine();
				changed |= ImGui::Checkbox("Frame Limit", &s.frameLimitMode) ? 1 : 0;
				DrawDLSSRuntimeNotice();
				changed |= DrawFrameGenerationBackendCombo(s.frameGenerationBackend);
			}
			return FinishPanel(changed);
		}

		void SaveFrameGeneration(void* userData)
		{
			auto& s = SettingsOf(userData);
			Upscaling::GetSingleton()->ApplyRuntimeFallbacks();
			CSimpleIniA ini;
			ini.SetUnicode();
			ini.SetValue("Settings", "bFrameGenerationMode", s.frameGenerationMode ? "true" : "false");
			ini.SetValue("Settings", "bFrameLimitMode", s.frameLimitMode ? "true" : "false");
			ini.SetValue("Settings", "iFrameGenerationBackend", std::to_string(s.frameGenerationBackend).c_str());
			SaveIni(ini, "Data\\F4SE\\Plugins\\FrameGen", "Data\\F4SE\\Plugins\\FrameGen\\FrameGen.ini");
		}

		int RenderReflex(void* userData)
		{
			auto& s = SettingsOf(userData);
			int changed = 0;
			if (ImGui::CollapsingHeader("Reflex")) {
				DrawDLSSRuntimeNotice();
				const char* reflexModes[] = { "Off", "Low Latency", "Low Latency + Boost" };
				changed |= ImGui::Combo("Mode", &s.reflexMode, reflexModes, IM_ARRAYSIZE(reflexModes)) ? 1 : 0;
				changed |= ImGui::Checkbox("Reflex Sleep Mode", &s.reflexSleepMode) ? 1 : 0;
			}
			return FinishPanel(changed);
		}

		void SaveReflex(void* userData)
		{
			auto& s = SettingsOf(userData);
			Upscaling::GetSingleton()->ApplyRuntimeFallbacks();
			CSimpleIniA ini;
			ini.SetUnicode();
			ini.SetValue("Settings", "iReflexMode", std::to_string(s.reflexMode).c_str());
			ini.SetValue("Settings", "bReflexSleepMode", s.reflexSleepMode ? "true" : "false");
			SaveIni(ini, "Data\\F4SE\\Plugins\\Reflex", "Data\\F4SE\\Plugins\\Reflex\\Reflex.ini");
		}

		int RenderUpscaler(void* userData)
		{
			auto& s = SettingsOf(userData);
			int changed = 0;
			if (ImGui::CollapsingHeader("Upscaler")) {
				DrawDLSSRuntimeNotice();
				const char* upscaleMethods[] = { "Disabled", "FSR", "DLSS" };
				changed |= ImGui::Combo("Method", &s.upscaleMethodPreference, upscaleMethods, IM_ARRAYSIZE(upscaleMethods)) ? 1 : 0;

				const char* qualityModes[] = { "Native AA", "Quality", "Balanced", "Performance", "Ultra Performance" };
				changed |= ImGui::Combo("Quality", &s.qualityMode, qualityModes, IM_ARRAYSIZE(qualityModes)) ? 1 : 0;

				const int validPresetValues[] = { 0, 10, 11, 12, 13 };
				const char* validPresetNames[] = { "Default", "Preset J", "Preset K", "Preset L", "Preset M" };
				int presetComboIdx = 0;
				for (int i = 0; i < IM_ARRAYSIZE(validPresetValues); ++i) {
					if (s.dlssPreset == validPresetValues[i]) {
						presetComboIdx = i;
						break;
					}
				}
				if (ImGui::Combo("DLSS Preset", &presetComboIdx, validPresetNames, IM_ARRAYSIZE(validPresetNames))) {
					s.dlssPreset = validPresetValues[presetComboIdx];
					changed = 1;
				}
			}
			return FinishPanel(changed);
		}

		void SaveUpscaler(void* userData)
		{
			auto& s = SettingsOf(userData);
			Upscaling::GetSingleton()->ApplyRuntimeFallbacks();
			CSimpleIniA ini;
			ini.SetUnicode();
			ini.SetValue("Settings", "iUpscaleMethodPreference", std::to_string(s.upscaleMethodPreference).c_str());
			ini.SetValue("Settings", "iQualityMode", std::to_string(s.qualityMode).c_str());
			ini.SetValue("Settings", "iDLSSPreset", std::to_string(s.dlssPreset).c_str());
			SaveIni(ini, "Data\\F4SE\\Plugins\\Upscaler", "Data\\F4SE\\Plugins\\Upscaler\\Upscaler.ini");
		}

		OverlayPanelCallbacks MakeCallbacks(int (*render)(void*), void (*save)(void*))
		{
			OverlayPanelCallbacks cbs{};
			cbs.render = render;
			cbs.save = save;
			cbs.userData = &Upscaling::GetSingleton()->settings;
			return cbs;
		}
	}

	PanelSpec FrameGenerationPanel()
	{
		static OverlayPanelCallbacks cbs = MakeCallbacks(RenderFrameGeneration, SaveFrameGeneration);
		return { "Frame Generation", kOverlayCategory_Rendering, &cbs };
	}

	PanelSpec ReflexPanel()
	{
		static OverlayPanelCallbacks cbs = MakeCallbacks(RenderReflex, SaveReflex);
		return { "Reflex", kOverlayCategory_Latency, &cbs };
	}

	PanelSpec UpscalerPanel()
	{
		static OverlayPanelCallbacks cbs = MakeCallbacks(RenderUpscaler, SaveUpscaler);
		return { "Upscaler", kOverlayCategory_Rendering, &cbs };
	}

	bool RegisterWithOverlayHost(const PanelSpec& panel)
	{
		HMODULE host = GetModuleHandleW(L"Overlay.dll");
		if (!host) {
			host = GetModuleHandleW(L"NuclearGFX.dll");
		}
		if (!host) {
			logger::info("[Overlay] No overlay host loaded; '{}' panel not registered", panel.name);
			return false;
		}

		auto registerFn = reinterpret_cast<decltype(&Overlay_RegisterPanel)>(GetProcAddress(host, "Overlay_RegisterPanel"));
		if (!registerFn) {
			logger::warn("[Overlay] Overlay host does not export Overlay_RegisterPanel; '{}' panel not registered", panel.name);
			return false;
		}

		registerFn(panel.name, panel.category, panel.callbacks);
		return true;
	}
}
