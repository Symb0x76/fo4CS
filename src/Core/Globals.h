#pragma once

#include "Core/Feature.h"

#include <memory>
#include <span>
#include <vector>

struct LightLimitFix;
struct ShaderDump;

// Global Feature instances — each Feature is declared and defined here (Skyrim CS pattern)
namespace globals::features
{
	extern ::LightLimitFix lightLimitFix;
	extern ::ShaderDump shaderDump;
}

// Lifecycle iterators (called from Runtime)
namespace CommunityShaders
{
	std::vector<Feature*>& GetFeatureList();
	void LoadFeatures();
	void DataLoaded();
	void PostPostLoad();
	void SetupResources();
	void ResetFeatures();
	void DrawFeatureSettings();
}
