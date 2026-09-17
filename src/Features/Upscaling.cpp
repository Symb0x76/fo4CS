#include "Features/Upscaling.h"
#include "Core/CommunityShaders.h"

#include "DX11Hooks.h"
#include "FidelityFX.h"
#include "Streamline.h"
#include "Upscaler.h"

#include <SimpleIni.h>
#include <imgui.h>

namespace
{
	int ClampInt(int value, int minValue, int maxValue, const char* name)
	{
		if (value < minValue || value > maxValue) {
			logger::warn("[Upscaling] {}={} out of range, clamping to [{}, {}]", name, value, minValue, maxValue);
			return std::clamp(value, minValue, maxValue);
		}
		return value;
	}

}

void FeatureUpscaling::Load()
{
	upscaling = Upscaling::GetSingleton();
	upscaling->pluginMode = Upscaling::PluginMode::kUpscaler;

	LoadSettings();
	DX11Hooks::Install();


	version = "1.0.0";
	loaded = true;
	logger::info("[Feature::Upscaling] Loaded");
}

void FeatureUpscaling::PostPostLoad()
{
	if (!loaded || !upscaling) return;

	upscaling->PostPostLoad();
	logger::info("[Feature::Upscaling] PostPostLoad complete");
}

void FeatureUpscaling::SetupResources()
{
	if (!loaded || !upscaling) return;

	auto* device = CommunityShaders::Runtime::GetSingleton()->GetDevice();
	if (!device) return;

	if (upscaling->CreateUpscalingResources()) {
		logger::info("[Feature::Upscaling] Resources created");
	} else {
		logger::debug("[Feature::Upscaling] Resources deferred until render targets are available");
	}
	// Frame generation resources are created by FeatureFrameGeneration::PostPostLoad()
	// when render targets are available (not yet during device creation).
}

void FeatureUpscaling::Prepass()
{
	if (!loaded || !upscaling) return;

	upscaling->UpdateUpscaling();
}

void FeatureUpscaling::Reset()
{
	if (!loaded || !upscaling) return;

	// DX12SwapChain::Present already drives Upscaling::Reset() once per present
	// (DX12SwapChain.cpp, in the "reset-shared-resources" step), after frameIndex
	// advances, so the slot it blanks is the one the next frame captures into.
	// That is the correct point.
	//
	// Running it here as well is not merely redundant, it is destructive. This
	// dispatch reaches us from ResetFeatures(), which Runtime::OnFrame() calls and
	// which D3D11PresentationBackend::Present() drives as its very first act
	// (PresentationBackend.cpp, AdvanceFeatureCore) -- so it lands after the
	// HUDLess capture has run and set hudLessFrameValid, and before
	// FidelityFX::Present() consumes those shared textures.
	//
	// Upscaling::Reset() does not merely flip a flag: it ClearRenderTargetViews
	// the HUDLess, depth and motion-vector shared surfaces to black and zeroes
	// hudLessFrameValid/hudLessFrameIDs (Upscaler.cpp). So on this branch the
	// symptom is not a skip -- FidelityFX's canUseFrameGen only tests pointers,
	// so generation still runs, on blanked inputs -- plus the UI colour/alpha
	// composite bails on !hudLessFrameValid (Upscaler.cpp) and never builds.
	// Let the present path own the per-frame reset whenever it is running one.
	//
	// This must mirror frameGenerationBackendAvailable in DX12SwapChain::Present
	// *exactly*, not just test the user's backend selection: the two owners have
	// to partition the work with no gap. If the selected backend failed to come
	// up -- featureDLSSG or featureFrameGen null -- the present path declines to
	// reset, so if we also declined nothing would ever clear the shared HUDLess,
	// depth and motion-vector surfaces. In that case there is no generation to
	// corrupt either, so falling through to the reset below is both safe and
	// necessary.
	const bool frameGenerationBackendAvailable =
		(upscaling->UsesDLSSFrameGeneration() && Streamline::GetSingleton()->featureDLSSG) ||
		(upscaling->UsesFSRFrameGeneration() && FidelityFX::GetSingleton()->featureFrameGen);
	if (frameGenerationBackendAvailable) {
		return;
	}

	upscaling->Reset();
}

void FeatureUpscaling::LoadSettings()
{
	upscaling = Upscaling::GetSingleton();

	// Load FrameGen/Upscaler INI settings into the shared singleton.
	// Must happen before DX11Hooks::Install() so ShouldLoadFidelityFX()
	// sees the correct frameGenerationBackend value.
	upscaling->LoadSettings();

	// Sync back to Feature-specific settings struct
	settings.upscaleMethodPreference = upscaling->settings.upscaleMethodPreference;
	settings.qualityMode = upscaling->settings.qualityMode;
	settings.dlssPreset = upscaling->settings.dlssPreset;

	logger::info("[Feature::Upscaling] Settings loaded (method={}, quality={}, preset={})",
		settings.upscaleMethodPreference, settings.qualityMode, settings.dlssPreset);
}

void FeatureUpscaling::SaveSettings()
{
	if (upscaling) {
		upscaling->settings.upscaleMethodPreference = settings.upscaleMethodPreference;
		upscaling->settings.qualityMode = settings.qualityMode;
		upscaling->settings.dlssPreset = settings.dlssPreset;
		upscaling->ApplyRuntimeFallbacks();
		settings.upscaleMethodPreference = upscaling->settings.upscaleMethodPreference;
	}

	constexpr const char* section = "Settings";

	const auto path = GetSettingsPath();
	CSimpleIniA ini;
	OpenSettingsIni(ini, path);

	ini.SetLongValue(section, "iUpscaleMethodPreference", settings.upscaleMethodPreference);
	ini.SetLongValue(section, "iQualityMode", settings.qualityMode);
	ini.SetLongValue(section, "iDLSSPreset", settings.dlssPreset);

	SaveSettingsIni(ini, path);

	// Sync back to shared singleton
	if (upscaling) {
		upscaling->settings.upscaleMethodPreference = settings.upscaleMethodPreference;
		upscaling->settings.qualityMode = settings.qualityMode;
		upscaling->settings.dlssPreset = settings.dlssPreset;
	}

	logger::info("[Feature::Upscaling] Settings saved");
}

void FeatureUpscaling::RestoreDefaultSettings()
{
	settings = {};
	SaveSettings();
}

void FeatureUpscaling::DrawSettings()
{
	if (ImGui::CollapsingHeader("Upscaling")) {
		int changed = 0;

		if (upscaling) {
			upscaling->settings.upscaleMethodPreference = settings.upscaleMethodPreference;
			upscaling->ApplyRuntimeFallbacks();
			settings.upscaleMethodPreference = upscaling->settings.upscaleMethodPreference;
			if (const char* reason = upscaling->GetDLSSUnavailableReason()) {
				ImGui::TextWrapped("%s", reason);
			}
		}

		const char* methods[] = { "Disabled", "FSR", "DLSS" };
		if (ImGui::Combo("Method", &settings.upscaleMethodPreference, methods, IM_ARRAYSIZE(methods))) {
			changed = 1;
		}

		const char* qualityModes[] = { "Native AA", "Quality", "Balanced", "Performance", "Ultra Performance" };
		if (ImGui::Combo("Quality", &settings.qualityMode, qualityModes, IM_ARRAYSIZE(qualityModes))) {
			changed = 1;
		}

		const int validPresets[] = { 0, 10, 11, 12, 13 };
		const char* presetNames[] = { "Default", "Preset J", "Preset K", "Preset L", "Preset M" };
		int presetIdx = 0;
		for (int i = 0; i < IM_ARRAYSIZE(validPresets); ++i) {
			if (settings.dlssPreset == validPresets[i]) { presetIdx = i; break; }
		}
		if (ImGui::Combo("DLSS Preset", &presetIdx, presetNames, IM_ARRAYSIZE(presetNames))) {
			settings.dlssPreset = validPresets[presetIdx];
			changed = 1;
		}

		if (changed && upscaling) {
			upscaling->settings.upscaleMethodPreference = settings.upscaleMethodPreference;
			upscaling->settings.qualityMode = settings.qualityMode;
			upscaling->settings.dlssPreset = settings.dlssPreset;
			upscaling->ApplyRuntimeFallbacks();
			settings.upscaleMethodPreference = upscaling->settings.upscaleMethodPreference;
		}
	}
}
