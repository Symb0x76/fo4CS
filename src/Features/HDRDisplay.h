#pragma once

#include "Core/Feature.h"

struct FeatureHDR : Feature
{
	[[nodiscard]] std::string GetName() override { return "HDR Display"; }
	[[nodiscard]] std::string GetShortName() override { return "HDR"; }
	[[nodiscard]] std::string_view GetCategory() const override { return FeatureCategories::kOutput; }
	[[nodiscard]] std::string_view GetShaderDefineName() override { return "HDR"; }
	[[nodiscard]] bool IsCore() const override { return true; }
	[[nodiscard]] bool SupportsVR() override { return false; }

	struct Settings
	{
		int hdrMode = 0;
		float peakLuminance = 1000.0f;
		float paperWhiteLuminance = 200.0f;
		float scRGBReferenceLuminance = 80.0f;
		bool calibrationActive = false;
	} settings;

	void Load() override;
	void PostPostLoad() override;
	void SetupResources() override;
	void Prepass() override;
	void Reset() override;

	void LoadSettings() override;
	void SaveSettings() override;
	void RestoreDefaultSettings() override;
	void DrawSettings() override;

	[[nodiscard]] std::vector<FeatureConstraints::Constraint> GetActiveConstraints() override;

	[[nodiscard]] std::filesystem::path GetSettingsPath() override
	{
		return std::filesystem::path("Data\\F4SE\\Plugins\\HDR") / "HDR.ini";
	}
};
