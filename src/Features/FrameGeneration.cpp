#include "Features/FrameGeneration.h"
#include "Core/CommunityShaders.h"

#include "Upscaler.h"

#include <SimpleIni.h>
#include <imgui.h>
void FeatureFrameGeneration::Load()
{
	upscaling = Upscaling::GetSingleton();
	LoadSettings();

	version = "1.0.0";
	loaded = true;
	logger::info("[Feature::FrameGeneration] Loaded");
}

void FeatureFrameGeneration::PostPostLoad()
{
	if (!loaded || !upscaling) return;

	// Frame generation resources are created lazily by CheckResources()
	// during frame processing when render targets are available.
	// Calling CreateFrameGenerationResources here crashes on null main.texture.

	logger::info("[Feature::FrameGeneration] PostPostLoad complete");
}

void FeatureFrameGeneration::SetupResources()
{
	if (!loaded || !upscaling) return;

	if (auto* device = CommunityShaders::Runtime::GetSingleton()->GetDevice()) {
		upscaling->CreateFrameGenerationResources();
	}
}

void FeatureFrameGeneration::Prepass()
{
	if (!loaded || !upscaling) return;

	if (upscaling->setupBuffers) {
		upscaling->CopyBuffersToSharedResources();
	}
}

void FeatureFrameGeneration::Reset()
{
	// FrameGeneration resources are managed by Upscaling singleton
}

void FeatureFrameGeneration::LoadSettings()
{
	upscaling = Upscaling::GetSingleton();

	CSimpleIniA ini;
	ini.SetUnicode();

	std::error_code ec;
	if (std::filesystem::exists(GetSettingsPath(), ec)) {
		ini.LoadFile(GetSettingsPath().string().c_str());

		settings.frameGenerationMode = ini.GetBoolValue("Settings", "bFrameGenerationMode", settings.frameGenerationMode);
		settings.frameLimitMode = ini.GetBoolValue("Settings", "bFrameLimitMode", settings.frameLimitMode);
		settings.frameGenerationBackend = static_cast<int>(ini.GetLongValue("Settings", "iFrameGenerationBackend", settings.frameGenerationBackend));
	}

	// Sync to shared Upscaling singleton
	upscaling->settings.frameGenerationMode = settings.frameGenerationMode;
	upscaling->settings.frameLimitMode = settings.frameLimitMode;
	upscaling->settings.frameGenerationBackend = settings.frameGenerationBackend;

	logger::info("[Feature::FrameGeneration] Settings (enabled={}, limiter={}, backend={})",
		settings.frameGenerationMode, settings.frameLimitMode, settings.frameGenerationBackend);
}

void FeatureFrameGeneration::SaveSettings()
{
	CSimpleIniA ini;
	ini.SetUnicode();

	std::error_code ec;
	if (std::filesystem::exists(GetSettingsPath(), ec)) {
		ini.LoadFile(GetSettingsPath().string().c_str());
	}

	ini.SetBoolValue("Settings", "bFrameGenerationMode", settings.frameGenerationMode);
	ini.SetBoolValue("Settings", "bFrameLimitMode", settings.frameLimitMode);
	ini.SetLongValue("Settings", "iFrameGenerationBackend", settings.frameGenerationBackend);

	std::filesystem::create_directories(GetSettingsPath().parent_path(), ec);
	ini.SaveFile(GetSettingsPath().string().c_str());

	if (upscaling) {
		upscaling->settings.frameGenerationMode = settings.frameGenerationMode;
		upscaling->settings.frameLimitMode = settings.frameLimitMode;
		upscaling->settings.frameGenerationBackend = settings.frameGenerationBackend;
	}
}

void FeatureFrameGeneration::RestoreDefaultSettings()
{
	settings = {};
	SaveSettings();
}

void FeatureFrameGeneration::DrawSettings()
{
	if (ImGui::CollapsingHeader("Frame Generation")) {
		int changed = 0;

		changed |= ImGui::Checkbox("Enabled", &settings.frameGenerationMode) ? 1 : 0;
		ImGui::SameLine();
		changed |= ImGui::Checkbox("Frame Limit", &settings.frameLimitMode) ? 1 : 0;

		const char* backends[] = { "Auto", "NVIDIA DLSS-G", "AMD FSR FG" };
		changed |= ImGui::Combo("Backend", &settings.frameGenerationBackend, backends, IM_ARRAYSIZE(backends)) ? 1 : 0;

		if (changed && upscaling) {
			upscaling->settings.frameGenerationMode = settings.frameGenerationMode;
			upscaling->settings.frameLimitMode = settings.frameLimitMode;
			upscaling->settings.frameGenerationBackend = settings.frameGenerationBackend;
		}
	}
}
