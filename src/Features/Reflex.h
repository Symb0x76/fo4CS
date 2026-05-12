#pragma once

#include "Core/Feature.h"

class Upscaling;

struct FeatureReflex : Feature
{
	[[nodiscard]] std::string GetName() override { return "Reflex"; }
	[[nodiscard]] std::string GetShortName() override { return "Reflex"; }
	[[nodiscard]] std::string_view GetCategory() const override { return FeatureCategories::kLatency; }
	[[nodiscard]] std::string_view GetShaderDefineName() override { return "REFLEX"; }
	[[nodiscard]] bool IsCore() const override { return true; }

	struct Settings
	{
		int reflexMode = 1;
		bool reflexSleepMode = true;
	} settings;

	Upscaling* upscaling = nullptr;

	void Load() override;
	void PostPostLoad() override;
	void Prepass() override;
	void Reset() override;

	void LoadSettings() override;
	void SaveSettings() override;
	void RestoreDefaultSettings() override;
	void DrawSettings() override;

	[[nodiscard]] std::filesystem::path GetSettingsPath() override
	{
		return std::filesystem::path("Data\\F4SE\\Plugins\\Reflex") / "Reflex.ini";
	}
};
