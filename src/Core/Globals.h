#pragma once

#include "Core/Feature.h"

#include <memory>
#include <span>
#include <vector>

namespace CommunityShaders
{
	std::vector<std::unique_ptr<Feature>>& GetFeatureList();
	std::span<std::unique_ptr<Feature>> GetFeatures();
	void LoadFeatures();
	void DataLoaded();
	void PostPostLoad();
	void SetupResources();
	void ResetFeatures();
	void DrawFeatureSettings();
}
