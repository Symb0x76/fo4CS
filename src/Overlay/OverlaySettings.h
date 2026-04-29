#pragma once

#include "Overlay/Overlay.h"

#include "HDR.h"
#include "Upscaler.h"

#include <SimpleIni.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace OverlaySettings
{
	inline void SaveToINI()
	{
		auto& s = Upscaling::GetSingleton()->settings;

		{
			CSimpleIniA ini;
			ini.SetUnicode();
			ini.SetValue("Settings", "bFrameGenerationMode", s.frameGenerationMode ? "true" : "false");
			ini.SetValue("Settings", "bFrameLimitMode", s.frameLimitMode ? "true" : "false");
			ini.SetValue("Settings", "iFrameGenerationBackend", std::to_string(s.frameGenerationBackend).c_str());
			ini.SetValue("Settings", "bDebugLogging", s.debugLogging ? "true" : "false");
			ini.SetValue("Settings", "iStreamlineLogLevel", std::to_string(s.streamlineLogLevel).c_str());
			ini.SetValue("Settings", "iDebugFrameLogCount", std::to_string(s.debugFrameLogCount).c_str());

			std::error_code ec;
			std::filesystem::create_directories("Data\\F4SE\\Plugins\\FrameGen", ec);
			if (!ec && ini.SaveFile("Data\\F4SE\\Plugins\\FrameGen\\FrameGen.ini") < 0) {
				logger::warn("[Overlay] Failed to save FrameGen.ini");
			}
		}

		{
			CSimpleIniA ini;
			ini.SetUnicode();
			ini.SetValue("Settings", "iReflexMode", std::to_string(s.reflexMode).c_str());
			ini.SetValue("Settings", "bReflexSleepMode", s.reflexSleepMode ? "true" : "false");

			if (ini.SaveFile("Data\\F4SE\\Plugins\\Reflex\\Reflex.ini") < 0) {
				logger::warn("[Overlay] Failed to save Reflex.ini");
			}
		}

		{
			CSimpleIniA ini;
			ini.SetUnicode();
			ini.SetValue("Settings", "iUpscaleMethodPreference", std::to_string(s.upscaleMethodPreference).c_str());
			ini.SetValue("Settings", "iQualityMode", std::to_string(s.qualityMode).c_str());
			ini.SetValue("Settings", "iDLSSPreset", std::to_string(s.dlssPreset).c_str());

			if (ini.SaveFile("Data\\F4SE\\Plugins\\Upscaler\\Upscaler.ini") < 0) {
				logger::warn("[Overlay] Failed to save Upscaler.ini");
			}
		}

		HDRSettings hdr;
		hdr.hdrMode = s.hdrMode;
		hdr.peakLuminance = s.peakLuminance;
		hdr.paperWhiteLuminance = s.paperWhiteLuminance;
		hdr.scRGBReferenceLuminance = s.scRGBReferenceLuminance;
		hdr.calibrationActive = s.hdrCalibrationActive;
		SaveHDRSettingsToINI(hdr);

		logger::info("[Overlay] Settings saved to INI files");
	}

	inline void RenderPanel()
	{
		ImGui::SetNextWindowSize(ImVec2(480, 560), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("fo4CS Settings", nullptr)) {
			ImGui::End();
			return;
		}

		auto& s = Upscaling::GetSingleton()->settings;

		if (ImGui::CollapsingHeader("Frame Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Checkbox("Enabled", &s.frameGenerationMode);
			ImGui::SameLine();
			ImGui::Checkbox("Frame Limit", &s.frameLimitMode);

			const char* fgBackends[] = { "Auto", "NVIDIA DLSS-G", "AMD FSR FG" };
			ImGui::Combo("Backend", &s.frameGenerationBackend, fgBackends, IM_ARRAYSIZE(fgBackends));
		}

		if (ImGui::CollapsingHeader("Reflex")) {
			const char* reflexModes[] = { "Off", "Low Latency", "Low Latency + Boost" };
			ImGui::Combo("Mode", &s.reflexMode, reflexModes, IM_ARRAYSIZE(reflexModes));
			ImGui::Checkbox("Reflex Sleep Mode", &s.reflexSleepMode);
		}

		if (ImGui::CollapsingHeader("HDR")) {
			const char* hdrModes[] = { "Disabled", "scRGB (HDR)", "HDR10 (HDR)" };
			ImGui::Combo("Mode", &s.hdrMode, hdrModes, IM_ARRAYSIZE(hdrModes));

			if (s.hdrMode > 0) {
				float peakLog = std::log10(s.peakLuminance);
				if (ImGui::SliderFloat("Peak Luminance (nits)", &peakLog, std::log10(80.0f), std::log10(10000.0f), "%.0f")) {
					s.peakLuminance = std::pow(10.0f, peakLog);
				}
				ImGui::Text("%.0f nits", s.peakLuminance);

				float paperLog = std::log10(s.paperWhiteLuminance);
				if (ImGui::SliderFloat("Paper White (nits)", &paperLog, std::log10(20.0f), std::log10(1000.0f), "%.0f")) {
					s.paperWhiteLuminance = std::pow(10.0f, paperLog);
				}
				ImGui::Text("%.0f nits", s.paperWhiteLuminance);

				float refLog = std::log10(s.scRGBReferenceLuminance);
				if (ImGui::SliderFloat("scRGB Reference (nits)", &refLog, std::log10(20.0f), std::log10(1000.0f), "%.0f")) {
					s.scRGBReferenceLuminance = std::pow(10.0f, refLog);
				}
				ImGui::Text("%.0f nits", s.scRGBReferenceLuminance);

				ImGui::Separator();
				if (ImGui::Button("Start HDR Calibration")) {
					s.hdrCalibrationActive = true;
					if (s.peakLuminance < 80.0f) s.peakLuminance = 1000.0f;
					if (s.paperWhiteLuminance < 20.0f) s.paperWhiteLuminance = 200.0f;
					SaveToINI();
				}
				ImGui::SameLine();
				ImGui::TextDisabled("(fullscreen test pattern)");
			}
		}

		if (ImGui::CollapsingHeader("Upscaler")) {
			const char* upscaleMethods[] = { "Disabled", "FSR", "DLSS" };
			ImGui::Combo("Method", &s.upscaleMethodPreference, upscaleMethods, IM_ARRAYSIZE(upscaleMethods));

			const char* qualityModes[] = { "Ultra Quality", "Quality", "Balanced", "Performance", "Ultra Performance" };
			ImGui::Combo("Quality", &s.qualityMode, qualityModes, IM_ARRAYSIZE(qualityModes));

			const char* dlssPresets[] = { "Default", "Preset A", "Preset B", "Preset C", "Preset D", "Preset E", "Preset F", "Preset G" };
			ImGui::Combo("DLSS Preset", &s.dlssPreset, dlssPresets, IM_ARRAYSIZE(dlssPresets));
		}

		if (ImGui::CollapsingHeader("Debug")) {
			ImGui::Checkbox("Debug Logging", &s.debugLogging);

			const char* logLevels[] = { "Off", "Errors Only", "Warnings", "Verbose" };
			int logLevelClamped = std::clamp(s.streamlineLogLevel, 0, 3);
			if (ImGui::Combo("Streamline Log Level", &logLevelClamped, logLevels, IM_ARRAYSIZE(logLevels))) {
				s.streamlineLogLevel = logLevelClamped;
			}

			ImGui::SliderInt("Debug Frame Count", &s.debugFrameLogCount, 0, 600);
		}

		if (ImGui::CollapsingHeader("Overlay")) {
			auto* overlay = Overlay::GetSingleton();
			int currentHotkey = overlay->GetHotkey();
			char hotkeyName[32];
			if (overlay->IsCapturingHotkey()) {
				snprintf(hotkeyName, sizeof(hotkeyName), "Press any key...");
			} else {
				const auto scanCode = static_cast<UINT>(MapVirtualKeyW(static_cast<UINT>(currentHotkey), MAPVK_VK_TO_VSC));
				GetKeyNameTextA(scanCode << 16, hotkeyName, sizeof(hotkeyName));
				if (hotkeyName[0] == 0) {
					snprintf(hotkeyName, sizeof(hotkeyName), "VK_0x%02X", currentHotkey);
				}
			}
			ImGui::Text("Toggle Hotkey:");
			ImGui::SameLine();
			if (ImGui::Button(hotkeyName)) {
				overlay->StartCapturingHotkey();
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(click then press key)");
		}

		ImGui::Separator();
		if (ImGui::Button("Save Settings to INI")) {
			SaveToINI();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("Press END to toggle overlay");

		ImGui::End();
	}
}
