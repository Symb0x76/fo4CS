#include "Features/HDRDisplay.h"

#include "DX12SwapChain.h"
#include "HDR.h"
#include "Upscaler.h"

#include <SimpleIni.h>
#include <imgui.h>

namespace
{
	float ClampFloat(float value, float minValue, float maxValue, const char* name)
	{
		if (value < minValue || value > maxValue) {
			logger::warn("[HDR] {}={} out of range, clamping to [{}, {}]", name, value, minValue, maxValue);
			return std::clamp(value, minValue, maxValue);
		}
		return value;
	}

	void PushSettingsToRuntime(const FeatureHDR::Settings& settings)
	{
		auto* swapChain = DX12SwapChain::GetSingleton();
		swapChain->hdrSettings.hdrMode = settings.hdrMode;
		swapChain->hdrSettings.peakLuminance = settings.peakLuminance;
		swapChain->hdrSettings.paperWhiteLuminance = settings.paperWhiteLuminance;
		swapChain->hdrSettings.scRGBReferenceLuminance = settings.scRGBReferenceLuminance;
		swapChain->hdrSettings.calibrationActive = settings.calibrationActive;

		auto* upscaling = Upscaling::GetSingleton();
		upscaling->settings.hdrMode = settings.hdrMode;
		upscaling->settings.peakLuminance = settings.peakLuminance;
		upscaling->settings.paperWhiteLuminance = settings.paperWhiteLuminance;
		upscaling->settings.scRGBReferenceLuminance = settings.scRGBReferenceLuminance;
		upscaling->settings.hdrCalibrationActive = settings.calibrationActive;
	}

	void PullSettingsFromRuntime(FeatureHDR::Settings& settings)
	{
		auto* upscaling = Upscaling::GetSingleton();
		settings.hdrMode = upscaling->settings.hdrMode;
		settings.peakLuminance = upscaling->settings.peakLuminance;
		settings.paperWhiteLuminance = upscaling->settings.paperWhiteLuminance;
		settings.scRGBReferenceLuminance = upscaling->settings.scRGBReferenceLuminance;
		settings.calibrationActive = upscaling->settings.hdrCalibrationActive;
	}

	void SyncCalibrationStateFromRuntime(FeatureHDR::Settings& settings)
	{
		auto* upscaling = Upscaling::GetSingleton();
		if (settings.calibrationActive != upscaling->settings.hdrCalibrationActive) {
			PullSettingsFromRuntime(settings);
		}
	}
}

void FeatureHDR::Load()
{
	LoadSettings();

	version = "1.0.0";
	loaded = true;
	logger::info("[Feature::HDR] Loaded");
}

void FeatureHDR::PostPostLoad()
{
	if (!loaded) return;

	PushSettingsToRuntime(settings);
	logger::info("[Feature::HDR] PostPostLoad complete");
}

void FeatureHDR::SetupResources()
{
	if (!loaded) return;

	auto* swapChain = DX12SwapChain::GetSingleton();
	if (swapChain->d3d12Device) {
		swapChain->EnsureColorSpaceResources();
		logger::info("[Feature::HDR] Color space resources created");
	}
}

void FeatureHDR::Prepass()
{
	// HDR settings are applied via DX12SwapChain at present time
}

void FeatureHDR::Reset()
{
	// Color space resources are managed by DX12SwapChain::Present —
	// do not destroy here. Reset is called every frame from OnFrame.
}

void FeatureHDR::LoadSettings()
{
	constexpr const char* section = "Settings";

	CSimpleIniA ini;
	ini.SetUnicode();

	std::error_code ec;
	if (std::filesystem::exists(GetSettingsPath(), ec)) {
		ini.LoadFile(GetSettingsPath().string().c_str());

		settings.hdrMode = static_cast<int>(ini.GetLongValue(section, "iHDRMode", settings.hdrMode));
		settings.peakLuminance = ClampFloat(
			static_cast<float>(ini.GetDoubleValue(section, "fPeakLuminance", settings.peakLuminance)),
			80.0f, 10000.0f, "fPeakLuminance");
		settings.paperWhiteLuminance = ClampFloat(
			static_cast<float>(ini.GetDoubleValue(section, "fPaperWhiteLuminance", settings.paperWhiteLuminance)),
			20.0f, 2000.0f, "fPaperWhiteLuminance");
		settings.scRGBReferenceLuminance = ClampFloat(
			static_cast<float>(ini.GetDoubleValue(section, "fScRGBReferenceLuminance", settings.scRGBReferenceLuminance)),
			20.0f, 1000.0f, "fScRGBReferenceLuminance");
		// calibrationActive is NOT persisted — it is a runtime-only UI mode
	}

	logger::info("[Feature::HDR] Settings (mode={}, peak={}, paperWhite={}, scRGBRef={})",
		settings.hdrMode, settings.peakLuminance, settings.paperWhiteLuminance,
		settings.scRGBReferenceLuminance);
}

void FeatureHDR::SaveSettings()
{
	SyncCalibrationStateFromRuntime(settings);

	constexpr const char* section = "Settings";

	CSimpleIniA ini;
	ini.SetUnicode();

	std::error_code ec;
	if (std::filesystem::exists(GetSettingsPath(), ec)) {
		ini.LoadFile(GetSettingsPath().string().c_str());
	}

	ini.SetLongValue(section, "iHDRMode", settings.hdrMode);
	ini.SetDoubleValue(section, "fPeakLuminance", settings.peakLuminance);
	ini.SetDoubleValue(section, "fPaperWhiteLuminance", settings.paperWhiteLuminance);
	ini.SetDoubleValue(section, "fScRGBReferenceLuminance", settings.scRGBReferenceLuminance);

	// calibrationActive is runtime-only — not persisted

	std::filesystem::create_directories(GetSettingsPath().parent_path(), ec);
	ini.SaveFile(GetSettingsPath().string().c_str());

	PushSettingsToRuntime(settings);
	logger::info("[Feature::HDR] Settings saved");
}

void FeatureHDR::RestoreDefaultSettings()
{
	settings = {};
	SaveSettings();
}

void FeatureHDR::DrawSettings()
{
	SyncCalibrationStateFromRuntime(settings);

	if (ImGui::CollapsingHeader("HDR Display")) {
		int changed = 0;

		const char* modes[] = { "Disabled", "scRGB (HDR)", "HDR10 (HDR)" };
		if (ImGui::Combo("Mode", &settings.hdrMode, modes, IM_ARRAYSIZE(modes))) {
			changed = 1;
		}

		if (settings.calibrationActive) {
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Calibration Mode");
			if (settings.hdrMode > 0) {
				changed |= ImGui::SliderFloat("Peak Luminance (nits)", &settings.peakLuminance, 80.0f, 10000.0f, "%.0f") ? 1 : 0;
				changed |= ImGui::SliderFloat("Paper White (nits)", &settings.paperWhiteLuminance, 20.0f, 1000.0f, "%.0f") ? 1 : 0;
				if (settings.hdrMode == 1) {
					changed |= ImGui::SliderFloat("scRGB Reference (nits)", &settings.scRGBReferenceLuminance, 10.0f, 500.0f, "%.0f") ? 1 : 0;
				}
			}
			ImGui::Separator();
			if (ImGui::Button("End Calibration")) {
				settings.calibrationActive = false;
				PushSettingsToRuntime(settings);
				SaveSettings();
			}
		} else {
			if (ImGui::Button("Start Calibration")) {
				bool hdrWasDisabled = (settings.hdrMode == 0);
				if (hdrWasDisabled) settings.hdrMode = 2;
				if (settings.peakLuminance < 400.0f) settings.peakLuminance = 1000.0f;
				settings.calibrationActive = true;
				PushSettingsToRuntime(settings);
				SaveSettings();
				if (hdrWasDisabled) {
					auto* swapChain = DX12SwapChain::GetSingleton();
					if (swapChain->d3d12Device) {
						swapChain->EnsureColorSpaceResources();
					}
				}
			}
		}

		if (changed && settings.calibrationActive) {
			PushSettingsToRuntime(settings);
		}
	}
}

std::vector<FeatureConstraints::Constraint> FeatureHDR::GetActiveConstraints()
{
	std::vector<FeatureConstraints::Constraint> constraints;

	if (!settings.hdrMode) return constraints;

	// HDR active: recommend disabling Frame Limit for proper HDR timing
	constraints.push_back({
		.targetSetting = { "FrameGeneration", "bFrameLimitMode" },
		.forcedValue = false,
		.reason = "HDR requires frame pacing from DXGI, not software frame limit",
		.recommendDisableAtBoot = false
	});

	return constraints;
}
