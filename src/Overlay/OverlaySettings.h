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
		auto* overlay = Overlay::GetSingleton();

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
			ini.SetValue("Settings", "showIntro", overlay->IsIntroEnabled() ? "true" : "false");
			ini.SetValue("Settings", "iOverlayHotkey", std::to_string(overlay->GetHotkey()).c_str());
				ini.SetValue("Settings", "fUIScale", std::to_string(overlay->GetUIScaleOverride()).c_str());

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
		const float uiScale = Overlay::GetSingleton()->GetUIScale();
		ImGui::SetNextWindowSize(ImVec2(480.0f * uiScale, 560.0f * uiScale), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("fo4CS Settings", nullptr)) {
			ImGui::End();
			return;
		}

		auto& s = Upscaling::GetSingleton()->settings;

		if (ImGui::CollapsingHeader("Frame Generation")) {
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
			if (ImGui::Combo("Mode", &s.hdrMode, hdrModes, IM_ARRAYSIZE(hdrModes))) {
				if (s.hdrMode == 2 && s.peakLuminance < 400.0f) s.peakLuminance = 1000.0f;
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered()) {
				ImGui::BeginTooltip();
				ImGui::TextWrapped("scRGB: HDR via linear FP16 framebuffer. Recommended for monitors with native scRGB support and GPU hardware calibration.");
				ImGui::TextWrapped("HDR10: Standard PQ (ST 2084) pipeline. Recommended for most HDR TVs and monitors.");
				ImGui::EndTooltip();
			}

			if (s.hdrMode > 0) {
				ImGui::Separator();
				if (s.hdrCalibrationActive) {
					ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Calibration active - adjust in the fullscreen overlay");
				} else {
					ImGui::TextWrapped(
						"HDR luminance values are adjusted using the fullscreen calibration test pattern."
					);
					ImGui::Spacing();
					ImGui::BulletText("The test pattern shows 4 regions: black ramp, luminance ramp, color bars, and a clipping zone");
					ImGui::BulletText("Adjust Peak Luminance until the luminance ramp shows smooth steps without hard clipping at the right edge");
					ImGui::BulletText("Adjust Paper White until the center white patch is comfortably visible against the gradient background");
					ImGui::BulletText("The calibration numbers are what matter - not your monitor's advertised specs");

					ImGui::Spacing();
					ImGui::Separator();
					ImGui::Text("Current:  Peak=%.0f nits  |  Paper White=%.0f nits", s.peakLuminance, s.paperWhiteLuminance);
					if (s.hdrMode == 1) {
						ImGui::Text("scRGB Reference=%.0f nits", s.scRGBReferenceLuminance);
					}
					ImGui::Spacing();

					if (ImGui::Button("Start HDR Calibration")) {
						s.hdrCalibrationActive = true;
						if (s.peakLuminance < 80.0f) s.peakLuminance = 1000.0f;
						if (s.paperWhiteLuminance < 20.0f) s.paperWhiteLuminance = 200.0f;
						SaveToINI();
					}
					ImGui::SameLine();
					ImGui::TextDisabled("(opens fullscreen test pattern)");
				}
			}
		}

		if (ImGui::CollapsingHeader("Upscaler")) {
			const char* upscaleMethods[] = { "Disabled", "FSR", "DLSS" };
			ImGui::Combo("Method", &s.upscaleMethodPreference, upscaleMethods, IM_ARRAYSIZE(upscaleMethods));

			const char* qualityModes[] = { "Ultra Quality", "Quality", "Balanced", "Performance", "Ultra Performance" };
			ImGui::Combo("Quality", &s.qualityMode, qualityModes, IM_ARRAYSIZE(qualityModes));

			// Only show non-deprecated presets. Presets A-D (1-4) removed, E-F (5-6) deprecated,
			// G-I (7-9) and N-O (14-15) revert to default — all excluded.
			const int validPresetValues[] = { 0, 10, 11, 12, 13 };
			const char* validPresetNames[] = { "Default", "Preset J", "Preset K", "Preset L", "Preset M" };
			const char* validPresetDescs[] = {
				"Default behavior, may change after an OTA update.",
				"Similar to Preset K. Might exhibit slightly less ghosting at the cost of extra flickering. Preset K is generally recommended over Preset J.",
				"Default preset for DLAA / Balanced / Quality modes. Transformer-based, best image quality at a higher performance cost.",
				"Default preset for Ultra Performance mode. Delivers a sharper, more stable image with less ghosting than Preset J / K, but is more expensive performance-wise.",
				"Default preset for Performance mode. Delivers similar image quality improvements as Preset L, but closer in speed to Presets J / K."
			};

			int presetComboIdx = 0;
			for (int i = 0; i < IM_ARRAYSIZE(validPresetValues); ++i) {
				if (s.dlssPreset == validPresetValues[i]) { presetComboIdx = i; break; }
			}
			if (ImGui::Combo("DLSS Preset", &presetComboIdx, validPresetNames, IM_ARRAYSIZE(validPresetNames))) {
				s.dlssPreset = validPresetValues[presetComboIdx];
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", validPresetDescs[presetComboIdx]);
			}
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
				const UINT scEx = MapVirtualKeyW(static_cast<UINT>(currentHotkey), MAPVK_VK_TO_VSC_EX);
				if (scEx != 0) {
					LONG lParam = static_cast<LONG>(scEx & 0xFF) << 16;
					if (scEx & 0x100) lParam |= 0x01000000;
					if (GetKeyNameTextA(lParam, hotkeyName, sizeof(hotkeyName)) == 0 || hotkeyName[0] == 0) {
						snprintf(hotkeyName, sizeof(hotkeyName), "VK_0x%02X", currentHotkey);
					}
				} else {
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

				bool showIntro = overlay->IsIntroEnabled();
				if (ImGui::Checkbox("Show Intro Hint", &showIntro)) {
					overlay->SetIntroEnabled(showIntro);
				}

				static const float kUIScales[] = { 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f, 4.0f };
				static const char* kUIScaleNames[] = { "0.75x", "1.0x (auto)", "1.25x", "1.5x", "1.75x", "2.0x", "2.5x", "3.0x", "4.0x" };
				float curScale = overlay->GetUIScaleOverride();
				int scaleIdx = 1;
				float best = 999.0f;
				for (int i = 0; i < IM_ARRAYSIZE(kUIScales); ++i) {
					float d = (curScale - kUIScales[i]) * (curScale - kUIScales[i]);
					if (d < best) { best = d; scaleIdx = i; }
				}
				if (ImGui::Combo("UI Scale", &scaleIdx, kUIScaleNames, IM_ARRAYSIZE(kUIScaleNames)))
					overlay->SetUIScaleOverride(kUIScales[scaleIdx]);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Overlay UI size relative to automatic DPI scaling.");
			}

		ImGui::Separator();
		if (ImGui::Button("Save Settings to INI")) {
			SaveToINI();
		}
		ImGui::SameLine();
		{
			auto* overlay = Overlay::GetSingleton();
			char hotkeyName[32];
			const int currentHotkey = overlay->GetHotkey();
			const UINT scEx = MapVirtualKeyW(static_cast<UINT>(currentHotkey), MAPVK_VK_TO_VSC_EX);
			if (scEx != 0) {
				LONG lParam = static_cast<LONG>(scEx & 0xFF) << 16;
				if (scEx & 0x100) lParam |= 0x01000000;
				if (GetKeyNameTextA(lParam, hotkeyName, sizeof(hotkeyName)) == 0 || hotkeyName[0] == 0)
					snprintf(hotkeyName, sizeof(hotkeyName), "VK_0x%02X", currentHotkey);
			} else {
				snprintf(hotkeyName, sizeof(hotkeyName), "VK_0x%02X", currentHotkey);
			}
			ImGui::TextDisabled("Press %s to toggle overlay", hotkeyName);
		}

		ImGui::End();
	}
}
