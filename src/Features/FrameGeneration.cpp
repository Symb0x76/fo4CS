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
		settings.frameGenerationBackend = static_cast<int>(ini.GetLongValue("Settings", "iFrameGenerationBackend", Upscaling::kFrameGenerationBackendDLSS));
	}

	// Sync to shared Upscaling singleton
	upscaling->settings.frameGenerationMode = settings.frameGenerationMode;
	upscaling->settings.frameLimitMode = settings.frameLimitMode;
	upscaling->settings.frameGenerationBackend = settings.frameGenerationBackend;
	upscaling->ApplyRuntimeFallbacks();
	settings.frameGenerationBackend = upscaling->settings.frameGenerationBackend;

	logger::info("[Feature::FrameGeneration] Settings (enabled={}, limiter={}, backend={})",
		settings.frameGenerationMode, settings.frameLimitMode, settings.frameGenerationBackend);
}

void FeatureFrameGeneration::SaveSettings()
{
	if (upscaling) {
		upscaling->settings.frameGenerationMode = settings.frameGenerationMode;
		upscaling->settings.frameLimitMode = settings.frameLimitMode;
		upscaling->settings.frameGenerationBackend = settings.frameGenerationBackend;
		upscaling->ApplyRuntimeFallbacks();
		settings.frameGenerationBackend = upscaling->settings.frameGenerationBackend;
	}

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

		if (upscaling) {
			upscaling->settings.frameGenerationBackend = settings.frameGenerationBackend;
			upscaling->ApplyRuntimeFallbacks();
			settings.frameGenerationBackend = upscaling->settings.frameGenerationBackend;
			if (const char* reason = upscaling->GetDLSSUnavailableReason()) {
				ImGui::TextWrapped("%s", reason);
			}
		}

		const char* backends[] = { "NVIDIA DLSS-G", "AMD FSR FG" };
		int backendIndex = settings.frameGenerationBackend == Upscaling::kFrameGenerationBackendFSR ? 1 : 0;
		if (ImGui::Combo("Backend", &backendIndex, backends, IM_ARRAYSIZE(backends))) {
			settings.frameGenerationBackend = backendIndex == 0 ? Upscaling::kFrameGenerationBackendDLSS : Upscaling::kFrameGenerationBackendFSR;
			changed = 1;
		}

		if (changed && upscaling) {
			upscaling->settings.frameGenerationMode = settings.frameGenerationMode;
			upscaling->settings.frameLimitMode = settings.frameLimitMode;
			upscaling->settings.frameGenerationBackend = settings.frameGenerationBackend;
			upscaling->ApplyRuntimeFallbacks();
			settings.frameGenerationBackend = upscaling->settings.frameGenerationBackend;
		}
	}
}
