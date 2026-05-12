#pragma once

#include <string_view>

namespace FeatureCategories
{
	inline constexpr std::string_view kCore = "Core";
	inline constexpr std::string_view kLighting = "Lighting";
	inline constexpr std::string_view kMaterials = "Materials";
	inline constexpr std::string_view kPostProcessing = "Post Processing";
	inline constexpr std::string_view kTerrain = "Terrain";
	inline constexpr std::string_view kTools = "Tools";
	inline constexpr std::string_view kOther = "Other";
	inline constexpr std::string_view kRendering = "Rendering";
	inline constexpr std::string_view kLatency = "Latency";
	inline constexpr std::string_view kOutput = "Output";
	inline constexpr std::string_view kDebug = "Debug";
}
