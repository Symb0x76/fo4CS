#include "Core/Globals.h"

#include "Features/ShaderDumpFeature.h"

namespace CommunityShaders
{
	std::vector<std::unique_ptr<Feature>>& GetFeatureList()
	{
		static std::vector<std::unique_ptr<Feature>> features;
		if (features.empty()) {
			features.push_back(std::make_unique<Features::ShaderDumpFeature>());
		}
		return features;
	}

	std::span<std::unique_ptr<Feature>> GetFeatures()
	{
		return GetFeatureList();
	}

	void LoadFeatures()
	{
		for (auto& feature : GetFeatureList()) {
			try {
				feature->LoadSettings();
				feature->Load();
				feature->loaded = true;
				logger::info("[CommunityShaders] Loaded feature {}", feature->GetName());
			} catch (const std::exception& e) {
				feature->loaded = false;
				feature->failedLoadedMessage = e.what();
				logger::error("[CommunityShaders] Failed to load feature {}: {}", feature->GetName(), e.what());
			}
		}
	}

	void DataLoaded()
	{
		for (auto& feature : GetFeatureList()) {
			if (feature->loaded) {
				feature->DataLoaded();
			}
		}
	}

	void PostPostLoad()
	{
		for (auto& feature : GetFeatureList()) {
			if (feature->loaded) {
				feature->PostPostLoad();
			}
		}
	}

	void SetupResources()
	{
		for (auto& feature : GetFeatureList()) {
			if (feature->loaded) {
				feature->SetupResources();
			}
		}
	}

	void ResetFeatures()
	{
		for (auto& feature : GetFeatureList()) {
			if (feature->loaded) {
				feature->Reset();
			}
		}
	}

	void DrawFeatureSettings()
	{
		for (auto& feature : GetFeatureList()) {
			if (feature->loaded) {
				feature->DrawSettings();
			} else if (feature->DrawFailLoadMessage()) {
				feature->DrawUnloadedUI();
			}
		}
	}
}
