#pragma once

#include "Core/Feature.h"

struct ShaderDump : Feature
{
	[[nodiscard]] std::string GetName() override { return "ShaderDB Dump"; }
	[[nodiscard]] std::string GetShortName() override { return "ShaderDump"; }
	[[nodiscard]] bool IsCore() const override { return true; }
	[[nodiscard]] std::string_view GetCategory() const override { return FeatureCategories::kTools; }
	[[nodiscard]] std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override;

	void LoadSettings() override;
	void SaveSettings() override;
	void Load() override;

	bool dumpAllShaders = false;
};
