#pragma once

#include "Core/Feature.h"

class Upscaling;

struct FeatureUpscaling : Feature
{
	[[nodiscard]] std::string GetName() override { return "Upscaling"; }
	[[nodiscard]] std::string GetShortName() override { return "Upscaling"; }
	[[nodiscard]] std::string_view GetCategory() const override { return FeatureCategories::kRendering; }
	[[nodiscard]] std::string_view GetShaderDefineName() override { return "UPSCALING"; }
	[[nodiscard]] bool IsCore() const override { return true; }

	struct Settings
	{
		int upscaleMethodPreference = 2;
		int qualityMode = 1;
		int dlssPreset = 0;
	} settings;

	Upscaling* upscaling = nullptr;

	void Load() override;
	void PostPostLoad() override;
	void SetupResources() override;
	void Prepass() override;
	void Reset() override;

	void LoadSettings() override;
	void SaveSettings() override;
	void RestoreDefaultSettings() override;
	void DrawSettings() override;

	[[nodiscard]] std::filesystem::path GetSettingsPath() override
	{
		return std::filesystem::path("Data\\F4SE\\Plugins\\Upscaler") / "Upscaler.ini";
	}
};
