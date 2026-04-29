#pragma once

#include "Core/Feature.h"

namespace CommunityShaders::Features
{
	class ShaderDumpFeature final : public Feature
	{
	public:
		[[nodiscard]] std::string GetName() override { return "ShaderDB Dump"; }
		[[nodiscard]] std::string GetShortName() override { return "ShaderDump"; }
		[[nodiscard]] bool IsCore() const override { return true; }
		[[nodiscard]] std::string_view GetCategory() const override { return FeatureCategories::kTools; }
		[[nodiscard]] std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override;

		void LoadSettings() override;
		void SaveSettings() override;
		void Load() override;

	private:
		bool dumpAllShaders = false;
	};
}
